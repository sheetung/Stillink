#ifndef EPAPER_SETTINGS_H
#define EPAPER_SETTINGS_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/*
 * 持久化设置（NVS 命名空间 "epaper"）。页面所有画面参数（风格 / 强度 / 文字 /
 * 旋转…）都留在手机 localStorage，固件只存会影响「传输与电源」的这几项。
 *
 * ★ R1.2.0（FB-015）起：每次固件升级都强制重置设置
 * （settings_reset_for_upgrade()），保留热点名、密码与电池校准。旧值不参与兼容，
 * 因此这里不再有任何「历史值归一」逻辑；越界值一律回落到默认档。
 */

#define SETT_SSID_MAX 33
#define SETT_PASS_MAX 65

/* 休眠时长档位（秒）。0 = 不休眠。 */
#define SETT_SLEEP_OFF    0
#define SETT_SLEEP_1MIN   60
#define SETT_SLEEP_3MIN   180
#define SETT_SLEEP_5MIN   300

/* 定时自动唤醒档位（秒）。0 = 关闭。 */
#define SETT_WAKE_OFF     0
#define SETT_WAKE_1H      3600
#define SETT_WAKE_12H     43200
#define SETT_WAKE_1D      86400

/* WiFi 发射功率档位。高 = 18dBm；中 = 10dBm（默认）；低 = 8.5dBm（缺陷批次救急档）。 */
#define SETT_WIFI_PWR_HIGH  0
#define SETT_WIFI_PWR_MID   1
#define SETT_WIFI_PWR_LOW   2

typedef struct {
    uint8_t  panel;     /* EPD_PANEL_A0 / EPD_PANEL_A1 */
    uint8_t  hflip;     /* 0/1 水平翻转（仅 A1 的 NATIVE800 对照档生效） */
    uint8_t  a1_mode;   /* A1 驱动模式 1..2（见 epd_drv.h EPD_A1_MODE_*；其他屏忽略） */
    uint8_t  wifi_pwr;  /* SETT_WIFI_PWR_HIGH / MID / LOW */
    uint32_t sleep_s;   /* 空闲后深睡；0 = 不休眠 */
    uint32_t wake_s;    /* 定时自动唤醒间隔；0 = 关闭 */
    uint32_t bat_cal_ppm; /* Battery gain, 1000000 = 1.0; preserved across OTA. */
    char     ap_ssid[SETT_SSID_MAX];  /* 空 = 用默认 Stillink-XXXX */
    char     ap_pass[SETT_PASS_MAX];  /* 空 = 热点开放 */
} epaper_settings_t;

void settings_init(void);
const epaper_settings_t *settings_get(void);

/* Validate the complete configuration, write changed fields and commit once.
 * Runtime settings are updated only after a successful commit. */
esp_err_t settings_save(const epaper_settings_t *next);

/* 恢复出厂：擦除整个 NVS 命名空间（含热点凭据与页面热更标记）；调用方负责重启。 */
void settings_factory_reset(void);

/*
 * 固件升级强制重置：擦除除热点名/密码及电池校准以外的设备设置键。
 * 页面热更标记（web_len / web_crc / page_ver）由 ota_web_clear() 负责清除。
 * 调用方在固件校验通过、确定要切槽重启之后调用。
 */
void settings_reset_for_upgrade(void);

#endif /* EPAPER_SETTINGS_H */
