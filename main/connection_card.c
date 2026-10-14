#include "connection_card.h"
#include "auth.h"
#include "card_font.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "frame.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "qrcodegen.h"
#include <stdio.h>
#include <string.h>
static TaskHandle_t task;
static volatile bool holding;
static void pixel(uint8_t *fb, int width, int height, int x, int y) {
  if (x < 0 || y < 0 || x >= width || y >= height)
    return;
  /* Portrait UI -> clockwise rotation -> 180-degree frame packing. */
  int pos = (width - 1 - x) * height + y;
  fb[pos / 4] &= ~(3 << (6 - 2 * (pos % 4)));
}
static void text(uint8_t *fb, int w, int h, int x, int y, const char *s,
                 int scale) {
  for (; *s; s++, x += 8 * scale) {
    unsigned ch = (unsigned char)*s;
    if (ch < 32 || ch > 126)
      ch = '?';
    for (int yy = 0; yy < 16; yy++)
      for (int xx = 0; xx < 8; xx++)
        if (card_font[ch - 32][yy] & (1 << (7 - xx)))
          for (int dy = 0; dy < scale; dy++)
            for (int dx = 0; dx < scale; dx++)
              pixel(fb, w, h, x + xx * scale + dx, y + yy * scale + dy);
  }
}
static void draw(uint8_t *fb, uint16_t w, uint16_t h, void *context) {
  uint16_t swap = w;
  w = h;
  h = swap;
  if (context) {
    text(fb, w, h, 70, 150, "FACTORY RESET", 4);
    text(fb, w, h, 70, 240, "Release the button", 3);
    text(fb, w, h, 70, 320, "Reconnect to Stillink hotspot", 2);
    return;
  }
  char address[64] = "http://192.168.4.1/", key[65];
  esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  esp_netif_ip_info_t ip;
  wifi_ap_record_t ap;
  bool lan = sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr &&
             esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
  if (lan)
    snprintf(address, sizeof(address), "http://" IPSTR "/", IP2STR(&ip.ip));
  auth_key_copy(key, sizeof(key));
  text(fb, w, h, 42, 35, "Stillink", 4);
  text(fb, w, h, 42, 115,
       lan ? "Wi-Fi connected" : "Connect to Stillink hotspot", 2);
  if (lan) {
    char ssid[33];
    memcpy(ssid, ap.ssid, 32);
    ssid[32] = 0;
    text(fb, w, h, 42, 162, ssid, 2);
  }
  text(fb, w, h, 42, 220, address, 2);
  text(fb, w, h, 42, 292, "LOGIN KEY", 2);
  int split = 22;
  for (size_t i = 0; i < strlen(key); i += split) {
    char line[23];
    strlcpy(line, key + i, sizeof(line));
    text(fb, w, h, 42, 338 + (int)(i / split) * 38, line, 2);
  }
  text(fb, w, h, 42, h - 62, "Hold wake button 5s to reset", 2);
  text(fb, w, h, 42, h - 30, "QR contains address only", 1);
  uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(4)],
      temp[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
  if (qrcodegen_encodeText(address, temp, qr, qrcodegen_Ecc_MEDIUM, 1, 4,
                           qrcodegen_Mask_AUTO, true)) {
    int size = qrcodegen_getSize(qr), scale = 6, x0 = (w - size * scale) / 2,
        y0 = 470;
    for (int y = 0; y < size; y++)
      for (int x = 0; x < size; x++)
        if (qrcodegen_getModule(qr, x, y))
          for (int dy = 0; dy < scale; dy++)
            for (int dx = 0; dx < scale; dx++)
              pixel(fb, w, h, x0 + x * scale + dx, y0 + y * scale + dy);
  }
}
static void worker(void *arg) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(500));
    ulTaskNotifyTake(pdTRUE, 0);
    while (frame_render_card(draw, NULL) != 0)
      vTaskDelay(pdMS_TO_TICKS(500));
  }
}
void connection_card_init(void) {
  configASSERT(xTaskCreate(worker, "connection_card", 4096, NULL, 4, &task) ==
               pdPASS);
}
void connection_card_request(void) {
  holding = true;
  if (task)
    xTaskNotifyGive(task);
}
bool connection_card_holding(void) { return holding; }
void connection_card_release(void) { holding = false; }
void connection_card_reset_notice(void) {
  holding = true;
  frame_render_card(draw, (void *)1);
}
