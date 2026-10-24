#include "power.h"
#include "epd_drv.h"
#include "settings.h"
#include "wifi_lan.h"
#include "online_ota.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "power";

#define AUTO_WAKE_WINDOW_S 180   /* 定时唤醒后等人的窗口（秒） */

/* 电池分压 470k:100k => Vbat = Vpin * (470k+100k)/100k = Vpin * 5.7。 */
#define BAT_DIV_NUM        570
#define BAT_DIV_DEN        100
#define BAT_ADC_MAX        4095
#define BAT_FULLSCALE_MV   3100   /* 12dB 满量程，按板实测标定 */
/* 未连接判定：单节锂电物理范围 + 连续有效次数（见 power_battery_mv）。 */
#define BAT_MV_MIN         2400
#define BAT_MV_MAX         4600

static adc_oneshot_unit_handle_t s_adc;
static adc_channel_t s_bat_channel;
static TickType_t s_last_activity;
static bool s_auto_wake = false;
static bool s_lan_light_sleep = false;
static esp_pm_lock_handle_t s_panel_lock;
static SemaphoreHandle_t s_busy_lock;
static unsigned s_busy_count;

void power_busy(bool busy)
{
    xSemaphoreTake(s_busy_lock, portMAX_DELAY);
    if (busy) ++s_busy_count;
    else {
        configASSERT(s_busy_count > 0);
        --s_busy_count;
    }
    s_last_activity = xTaskGetTickCount();
    xSemaphoreGive(s_busy_lock);
}

void power_log_heap(const char *operation)
{
    ESP_LOGI(TAG, "%s: internal heap free=%u largest=%u minimum=%u", operation,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

void power_panel_busy(bool busy)
{
    if (busy) power_busy(true);
    ESP_ERROR_CHECK(busy ? esp_pm_lock_acquire(s_panel_lock) :
                          esp_pm_lock_release(s_panel_lock));
    if (!busy) power_busy(false);
}

void power_set_lan_light_sleep(bool enabled)
{
    esp_pm_config_t config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .light_sleep_enable = enabled,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&config));
    if (enabled != s_lan_light_sleep) {
        s_lan_light_sleep = enabled;
        ESP_LOGI(TAG, "LAN automatic light sleep %s", enabled ? "enabled" : "disabled");
    }
}

void power_init(void)
{
    s_busy_lock = xSemaphoreCreateMutex();
    configASSERT(s_busy_lock);
    /* Keep AP/provisioning awake; the LAN worker enables automatic sleep in STA mode. */
    power_set_lan_light_sleep(false);
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "panel", &s_panel_lock));
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << EPD_PIN_WAKE_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    /* Resolve the ADC1 channel from the target-specific battery pin. */
    adc_unit_t unit;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(EPD_PIN_BAT_ADC, &unit, &s_bat_channel));
    ESP_ERROR_CHECK(unit == ADC_UNIT_1 ? ESP_OK : ESP_ERR_INVALID_ARG);
    adc_oneshot_unit_init_cfg_t u = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&u, &s_adc));
    adc_oneshot_chan_cfg_t c = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_bat_channel, &c));

    power_activity();
}

void power_activity(void)
{
    xSemaphoreTake(s_busy_lock, portMAX_DELAY);
    s_last_activity = xTaskGetTickCount();
    xSemaphoreGive(s_busy_lock);
}

void power_set_auto_wake(bool v) { s_auto_wake = v; }
bool power_auto_wake(void) { return s_auto_wake; }

uint32_t power_wakeup_causes(void)
{
    return esp_sleep_get_wakeup_causes();
}

static bool should_sleep_locked(void)
{
    if (s_busy_count) return false;
    /* LAN access must remain available while connected or provisioning. */
    if (wifi_lan_keep_awake() || online_ota_busy()) return false;
    const epaper_settings_t *s = settings_get();
    uint32_t limit = s->sleep_s;

    if (s_auto_wake) {
        /* 定时醒来：用短窗口等人，除非用户设了更短的空闲超时。 */
        if (limit == 0 || limit > AUTO_WAKE_WINDOW_S) limit = AUTO_WAKE_WINDOW_S;
    }
    if (limit == 0) return false;

    TickType_t idle = xTaskGetTickCount() - s_last_activity;
    return (uint64_t)idle * portTICK_PERIOD_MS >= (uint64_t)limit * 1000;
}

bool power_should_sleep(void)
{
    xSemaphoreTake(s_busy_lock, portMAX_DELAY);
    bool sleep = should_sleep_locked();
    xSemaphoreGive(s_busy_lock);
    return sleep;
}

void power_enter_deep_sleep(void)
{
    /* Recheck atomically with operation admission; new work waits for reboot. */
    xSemaphoreTake(s_busy_lock, portMAX_DELAY);
    if (!should_sleep_locked()) {
        xSemaphoreGive(s_busy_lock);
        return;
    }
    esp_wifi_stop();
    epd_panel_deep_sleep();

    /* 唤醒键：GPIO5 上拉、按钮接 GND，低电平唤醒（已验证路径）。 */
    gpio_pullup_en((gpio_num_t)EPD_PIN_WAKE_BTN);
    gpio_pulldown_dis((gpio_num_t)EPD_PIN_WAKE_BTN);
    gpio_set_direction((gpio_num_t)EPD_PIN_WAKE_BTN, GPIO_MODE_INPUT);
#if CONFIG_IDF_TARGET_ESP32C3
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << EPD_PIN_WAKE_BTN, ESP_GPIO_WAKEUP_GPIO_LOW));
#else
    ESP_ERROR_CHECK(rtc_gpio_pullup_en((gpio_num_t)EPD_PIN_WAKE_BTN));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis((gpio_num_t)EPD_PIN_WAKE_BTN));
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(
        1ULL << EPD_PIN_WAKE_BTN, ESP_EXT1_WAKEUP_ANY_LOW));
#endif

    uint32_t wake_s = settings_get()->wake_s;
    if (wake_s > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)wake_s * 1000000ULL);
    }

    ESP_LOGI(TAG, "deep sleep (btn GPIO%d low, timer %lus)",
             EPD_PIN_WAKE_BTN, (unsigned long)wake_s);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}

int power_battery_mv(void)
{
    /* 电池未连接检测：ADC 引脚悬空时读数靠杂散耦合乱漂（实测 3.2/3.3/17.6V 交替，
       17.6V 即漂到接近满量程的换算值）。单次采样无法区分，用跨请求状态机：
       读数必须落在单节锂电物理范围内，且连续 3 次 /api/info 轮询都有效才显示；
       任一次超范围立即清零 → 页面显示「未连接」。真电池接上后轮询 2~3 次即出电压。 */
    static int s_valid = 0;

    int raw = 0;
    if (adc_oneshot_read(s_adc, s_bat_channel, &raw) != ESP_OK) { s_valid = 0; return -1; }
    int mv = (int)((int64_t)raw * BAT_FULLSCALE_MV * BAT_DIV_NUM / BAT_ADC_MAX / BAT_DIV_DEN);

    if (mv < BAT_MV_MIN || mv > BAT_MV_MAX) { s_valid = 0; return -1; }
    if (++s_valid < 3) return -1;
    return (int)((int64_t)mv * settings_get()->bat_cal_ppm / 1000000);
}
