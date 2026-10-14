#include "auth.h"
#include "bootloader_random.h"
#include "cJSON.h"
#include "connection_card.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "power.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SESSION_COUNT 6
static char key[65];
static portMUX_TYPE key_lock = portMUX_INITIALIZER_UNLOCKED;
static bool first;
static struct {
  char token[33];
  int64_t until;
} sessions[SESSION_COUNT];
static int failures;
static int64_t blocked_until;
static struct {
  esp_err_t (*handler)(httpd_req_t *);
  void *ctx;
} handlers[48];
static size_t count;
static void random_hex(char *out, int bytes) {
  unsigned char data[32];
  esp_fill_random(data, bytes);
  for (int i = 0; i < bytes; i++)
    sprintf(out + i * 2, "%02x", data[i]);
}
static bool equal(const char *a, const char *b) {
  size_t x = strlen(a), y = strlen(b);
  unsigned diff = x ^ y;
  for (size_t i = 0; i < x; i++)
    diff |= (unsigned char)a[i] ^ (i < y ? (unsigned char)b[i] : 0);
  return diff == 0;
}
void auth_init(void) {
  nvs_handle_t nvs;
  ESP_ERROR_CHECK(nvs_open("auth", NVS_READWRITE, &nvs));
  size_t size = sizeof(key);
  first = nvs_get_str(nvs, "key", key, &size) != ESP_OK;
  if (first) {
    bootloader_random_enable();
    random_hex(key, 8);
    bootloader_random_disable();
    ESP_ERROR_CHECK(nvs_set_str(nvs, "key", key));
    ESP_ERROR_CHECK(nvs_commit(nvs));
  }
  nvs_close(nvs);
}
void auth_factory_reset(void) {
  nvs_handle_t n;
  if (nvs_open("auth", NVS_READWRITE, &n) == ESP_OK) {
    nvs_erase_all(n);
    nvs_commit(n);
    nvs_close(n);
  }
  memset(sessions, 0, sizeof(sessions));
}
bool auth_first_boot(void) { return first; }
void auth_key_copy(char *out, size_t size) {
  portENTER_CRITICAL(&key_lock);
  strlcpy(out, key, size);
  portEXIT_CRITICAL(&key_lock);
}
static esp_err_t json(httpd_req_t *r, const char *status, const char *body) {
  httpd_resp_set_status(r, status);
  httpd_resp_set_type(r, "application/json");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  httpd_resp_set_hdr(r, "Access-Control-Allow-Origin", "*");
  return httpd_resp_sendstr(r, body);
}
static bool authorized(httpd_req_t *r) {
  char header[128];
  if (httpd_req_get_hdr_value_str(r, "Authorization", header, sizeof(header)) !=
          ESP_OK ||
      strncmp(header, "Bearer ", 7))
    return false;
  const char *token = header + 7;
  if (equal(token, key))
    return true;
  int64_t now = esp_timer_get_time();
  for (int i = 0; i < SESSION_COUNT; i++)
    if (sessions[i].until > now && equal(token, sessions[i].token))
      return true;
  return false;
}
static esp_err_t guard(httpd_req_t *r) {
  if (!authorized(r)) {
    httpd_resp_set_hdr(r, "Connection", "close");
    json(r, "401 Unauthorized", "{\"error\":\"Authentication required\"}");
    return ESP_FAIL;
  }
  power_activity();
  size_t i = (size_t)r->user_ctx;
  r->user_ctx = handlers[i].ctx;
  return handlers[i].handler(r);
}
esp_err_t auth_register_uri_handler(httpd_handle_t server,
                                    const httpd_uri_t *route) {
  if (strncmp(route->uri, "/api/", 5) || route->method == HTTP_OPTIONS)
    return httpd_register_uri_handler(server, route);
  if (count >= 48)
    return ESP_ERR_NO_MEM;
  httpd_uri_t copy = *route;
  handlers[count].handler = route->handler;
  handlers[count].ctx = route->user_ctx;
  copy.handler = guard;
  copy.user_ctx = (void *)count++;
  return httpd_register_uri_handler(server, &copy);
}
static cJSON *read_json(httpd_req_t *r) {
  if (!r->content_len || r->content_len > 256)
    return NULL;
  char body[257];
  int got = 0;
  while (got < r->content_len) {
    int n = httpd_req_recv(r, body + got, r->content_len - got);
    if (n <= 0)
      return NULL;
    got += n;
  }
  body[got] = 0;
  return cJSON_Parse(body);
}
static esp_err_t login(httpd_req_t *r) {
  int64_t now = esp_timer_get_time();
  if (now < blocked_until)
    return json(r, "429 Too Many Requests",
                "{\"error\":\"Try again in 30 seconds\"}");
  cJSON *j = read_json(r), *v = cJSON_GetObjectItemCaseSensitive(j, "key");
  bool valid = cJSON_IsString(v) && equal(v->valuestring, key);
  cJSON_Delete(j);
  if (!valid) {
    if (++failures >= 5) {
      blocked_until = now + 30000000;
      failures = 0;
    }
    return json(r, "401 Unauthorized", "{\"error\":\"Invalid key\"}");
  }
  failures = 0;
  connection_card_release();
  int slot = 0;
  for (int i = 0; i < SESSION_COUNT; i++)
    if (sessions[i].until < sessions[slot].until)
      slot = i;
  random_hex(sessions[slot].token, 16);
  sessions[slot].until = now + 86400000000LL;
  char body[120];
  snprintf(body, sizeof(body), "{\"token\":\"%s\",\"expires_in\":86400}",
           sessions[slot].token);
  power_activity();
  return json(r, "200 OK", body);
}
static esp_err_t change_key(httpd_req_t *r) {
  cJSON *j = read_json(r), *v = cJSON_GetObjectItemCaseSensitive(j, "key");
  bool valid = cJSON_IsString(v) && strlen(v->valuestring) >= 12 &&
               strlen(v->valuestring) <= 64;
  if (valid)
    for (const char *p = v->valuestring; *p; p++)
      if (*p < 33 || *p > 126)
        valid = false;
  if (!valid) {
    cJSON_Delete(j);
    return json(r, "400 Bad Request",
                "{\"error\":\"Key must contain 12-64 printable ASCII "
                "characters without spaces\"}");
  }
  nvs_handle_t n;
  esp_err_t err = nvs_open("auth", NVS_READWRITE, &n);
  if (err == ESP_OK) {
    err = nvs_set_str(n, "key", v->valuestring);
    if (err == ESP_OK)
      err = nvs_commit(n);
    nvs_close(n);
  }
  if (err == ESP_OK) {
    portENTER_CRITICAL(&key_lock);
    strlcpy(key, v->valuestring, sizeof(key));
    portEXIT_CRITICAL(&key_lock);
    memset(sessions, 0, sizeof(sessions));
  }
  cJSON_Delete(j);
  return json(r, err == ESP_OK ? "200 OK" : "500 Internal Server Error",
              err == ESP_OK ? "{\"saved\":true}"
                            : "{\"error\":\"Cannot save key\"}");
}
static esp_err_t status(httpd_req_t *r) {
  return json(r, "200 OK", "{\"authenticated\":true}");
}
static esp_err_t show_card(httpd_req_t *r) {
  connection_card_request();
  return json(r, "202 Accepted", "{\"requested\":true}");
}
static esp_err_t logout(httpd_req_t *r) {
  char h[128];
  if (httpd_req_get_hdr_value_str(r, "Authorization", h, sizeof(h)) == ESP_OK)
    for (int i = 0; i < SESSION_COUNT; i++)
      if (!strcmp(h + 7, sessions[i].token))
        sessions[i].until = 0;
  return json(r, "200 OK", "{\"logged_out\":true}");
}
void auth_register(httpd_handle_t server) {
  httpd_uri_t l = {
      .uri = "/api/auth/login", .method = HTTP_POST, .handler = login};
  ESP_ERROR_CHECK(httpd_register_uri_handler(server, &l));
  httpd_uri_t routes[] = {
      {.uri = "/api/auth/status", .method = HTTP_GET, .handler = status},
      {.uri = "/api/auth/key", .method = HTTP_POST, .handler = change_key},
      {.uri = "/api/auth/logout", .method = HTTP_POST, .handler = logout},
      {.uri = "/api/connection/show",
       .method = HTTP_POST,
       .handler = show_card}};
  for (size_t i = 0; i < sizeof(routes) / sizeof(*routes); i++)
    ESP_ERROR_CHECK(auth_register_uri_handler(server, &routes[i]));
}
