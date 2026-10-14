#include "service_access.h"
#include "auth.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "nvs.h"
#include "power.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char base[192], secret[65];
static SemaphoreHandle_t credential_lock;
void service_access_init(void) {
  credential_lock = xSemaphoreCreateMutex();
  configASSERT(credential_lock);
  nvs_handle_t n;
  if (nvs_open("service", NVS_READONLY, &n) == ESP_OK) {
    size_t a = sizeof(base), b = sizeof(secret);
    nvs_get_str(n, "url", base, &a);
    nvs_get_str(n, "key", secret, &b);
    nvs_close(n);
  }
}
void service_access_reset(void) {
  nvs_handle_t n;
  if (nvs_open("service", NVS_READWRITE, &n) == ESP_OK) {
    nvs_erase_all(n);
    nvs_commit(n);
    nvs_close(n);
  }
  xSemaphoreTake(credential_lock, portMAX_DELAY);
  base[0] = secret[0] = 0;
  xSemaphoreGive(credential_lock);
}
void service_access_attach(esp_http_client_handle_t c, const char *url) {
  xSemaphoreTake(credential_lock, portMAX_DELAY);
  size_t len = strlen(base);
  if (secret[0] && len && !strncmp(url, base, len) &&
      (url[len] == '/' || url[len] == 0)) {
    char h[80];
    snprintf(h, sizeof(h), "Bearer %s", secret);
    esp_http_client_set_header(c, "Authorization", h);
  }
  xSemaphoreGive(credential_lock);
}
static esp_err_t reply(httpd_req_t *r, const char *s, const char *body) {
  httpd_resp_set_type(r, "application/json");
  httpd_resp_set_status(r, s);
  httpd_resp_set_hdr(r, "Access-Control-Allow-Origin", "*");
  return httpd_resp_sendstr(r, body);
}
static cJSON *read_body(httpd_req_t *r) {
  if (!r->content_len || r->content_len > 512)
    return NULL;
  char b[513];
  int used = 0;
  while (used < r->content_len) {
    int n = httpd_req_recv(r, b + used, r->content_len - used);
    if (n <= 0)
      return NULL;
    used += n;
  }
  b[used] = 0;
  return cJSON_Parse(b);
}
static bool valid_base(const char *s) {
  if (!s[0])
    return true;
  const char *p = !strncmp(s, "http://", 7)    ? s + 7
                  : !strncmp(s, "https://", 8) ? s + 8
                                               : NULL;
  if (!p || !*p || strlen(s) >= sizeof(base))
    return false;
  for (; *p; p++)
    if (*p <= 32 || strchr("@?#\\", *p))
      return false;
  return true;
}
static esp_err_t get_config(httpd_req_t *r) {
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "service_url", base);
  cJSON_AddBoolToObject(j, "key_set", secret[0]);
  char *s = cJSON_PrintUnformatted(j);
  cJSON_Delete(j);
  if (!s)
    return ESP_ERR_NO_MEM;
  esp_err_t e = reply(r, "200 OK", s);
  free(s);
  return e;
}
static esp_err_t config(httpd_req_t *r) {
  cJSON *j = read_body(r),
        *u = cJSON_GetObjectItemCaseSensitive(j, "service_url"),
        *k = cJSON_GetObjectItemCaseSensitive(j, "key");
  if (!cJSON_IsString(u) || !valid_base(u->valuestring) ||
      (k && (!cJSON_IsString(k) || strlen(k->valuestring) > 64))) {
    cJSON_Delete(j);
    return reply(r, "400 Bad Request",
                 "{\"error\":\"Invalid service address or key\"}");
  }
  char next[192], next_key[65];
  strlcpy(next, u->valuestring, sizeof(next));
  size_t len = strlen(next);
  while (len && next[len - 1] == '/')
    next[--len] = 0;
  strlcpy(next_key,
          k                     ? k->valuestring
          : !strcmp(next, base) ? secret
                                : "",
          sizeof(next_key));
  if (!next[0])
    next_key[0] = 0;
  for (const char *p = next_key; *p; p++)
    if (*p < 33 || *p > 126) {
      cJSON_Delete(j);
      return reply(r, "400 Bad Request", "{\"error\":\"Invalid service key\"}");
    }
  cJSON_Delete(j);
  if (next[0]) {
    char url[220], h[80];
    snprintf(url, sizeof(url), "%s/health", next);
    esp_http_client_config_t o = {.url = url,
                                  .timeout_ms = 8000,
                                  .crt_bundle_attach = esp_crt_bundle_attach,
                                  .disable_auto_redirect = true};
    esp_http_client_handle_t c = esp_http_client_init(&o);
    if (!c)
      return ESP_ERR_NO_MEM;
    snprintf(h, sizeof(h), "Bearer %s", next_key);
    esp_http_client_set_header(c, "Authorization", h);
    esp_err_t e = esp_http_client_open(c, 0);
    int status = 0;
    char health[2048];
    int used = 0;
    if (e == ESP_OK && esp_http_client_fetch_headers(c) >= 0) {
      status = esp_http_client_get_status_code(c);
      int got;
      while (used < sizeof(health) - 1 &&
             (got = esp_http_client_read(c, health + used,
                                         sizeof(health) - 1 - used)) > 0)
        used += got;
    }
    health[used] = 0;
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    if (status != 200)
      return reply(
          r, "400 Bad Request",
          "{\"error\":\"Service connection or key verification failed\"}");
    if (!strcmp(r->uri, "/api/service/check"))
      return reply(r, "200 OK", health);
  }
  nvs_handle_t n;
  esp_err_t e = nvs_open("service", NVS_READWRITE, &n);
  if (e == ESP_OK) {
    e = nvs_set_str(n, "url", next);
    if (e == ESP_OK)
      e = nvs_set_str(n, "key", next_key);
    if (e == ESP_OK)
      e = nvs_commit(n);
    nvs_close(n);
  }
  if (e != ESP_OK)
    return reply(r, "500 Internal Server Error",
                 "{\"error\":\"Cannot save service\"}");
  xSemaphoreTake(credential_lock, portMAX_DELAY);
  strlcpy(base, next, sizeof(base));
  strlcpy(secret, next_key, sizeof(secret));
  xSemaphoreGive(credential_lock);
  return get_config(r);
}
static esp_err_t proxy(httpd_req_t *r) {
  char query[768], path[512];
  if (!base[0])
    return reply(r, "409 Conflict", "{\"error\":\"Configure service first\"}");
  if (httpd_req_get_url_query_str(r, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "path", path, sizeof(path)) != ESP_OK)
    return reply(r, "400 Bad Request", "{}");
  char *src = path, *dst = path;
  while (*src) {
    if (*src == '%' && src[1] && src[2]) {
      char hex[3] = {src[1], src[2], 0};
      *dst++ = (char)strtol(hex, NULL, 16);
      src += 3;
    } else
      *dst++ = *src++;
  }
  *dst = 0;
  if (strcmp(path, "/health") && strncmp(path, "/v1/", 4))
    return reply(r, "400 Bad Request", "{\"error\":\"Invalid service path\"}");
  if (strchr(path, '\r') || strchr(path, '\n') || strchr(path, '\\') ||
      strstr(path, ".."))
    return reply(r, "400 Bad Request", "{}");
  if (r->content_len > 120016)
    return reply(r, "413 Payload Too Large", "{}");
  char url[720];
  snprintf(url, sizeof(url), "%s%s", base, path);
  esp_http_client_config_t o = {.url = url,
                                .timeout_ms = 15000,
                                .crt_bundle_attach = esp_crt_bundle_attach,
                                .disable_auto_redirect = true,
                                .buffer_size = 1024};
  esp_http_client_handle_t c = esp_http_client_init(&o);
  if (!c)
    return ESP_ERR_NO_MEM;
  service_access_attach(c, url);
  esp_http_client_set_method(c, r->method == HTTP_POST     ? HTTP_METHOD_POST
                                : r->method == HTTP_DELETE ? HTTP_METHOD_DELETE
                                                           : HTTP_METHOD_GET);
  char type[80];
  if (httpd_req_get_hdr_value_str(r, "Content-Type", type, sizeof(type)) ==
      ESP_OK)
    esp_http_client_set_header(c, "Content-Type", type);
  esp_err_t err = esp_http_client_open(c, r->content_len);
  char buffer[1024];
  size_t got = 0;
  while (err == ESP_OK && got < r->content_len) {
    int n = httpd_req_recv(r, buffer,
                           r->content_len - got > sizeof(buffer)
                               ? sizeof(buffer)
                               : r->content_len - got);
    if (n <= 0) {
      err = ESP_FAIL;
      break;
    }
    int sent = 0;
    while (sent < n) {
      int w = esp_http_client_write(c, buffer + sent, n - sent);
      if (w <= 0) {
        err = ESP_FAIL;
        break;
      }
      sent += w;
    }
    got += n;
    power_activity();
  }
  if (err != ESP_OK || esp_http_client_fetch_headers(c) < 0) {
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return reply(r, "502 Bad Gateway", "{\"error\":\"Service unavailable\"}");
  }
  int status = esp_http_client_get_status_code(c);
  snprintf(type, sizeof(type), "%d Service response", status);
  httpd_resp_set_status(r, type);
  httpd_resp_set_hdr(r, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(r, "Cache-Control", "no-store");
  httpd_resp_set_type(r,
                      strstr(path, ".png") ? "image/png" : "application/json");
  int n;
  while ((n = esp_http_client_read(c, buffer, sizeof(buffer))) > 0) {
    if (httpd_resp_send_chunk(r, buffer, n) != ESP_OK) {
      err = ESP_FAIL;
      break;
    }
    power_activity();
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  if (err == ESP_OK && n == 0)
    return httpd_resp_send_chunk(r, NULL, 0);
  return ESP_FAIL;
}
void service_access_register(httpd_handle_t s) {
  httpd_uri_t routes[] = {
      {.uri = "/api/service/check", .method = HTTP_POST, .handler = config},
      {.uri = "/api/service", .method = HTTP_GET, .handler = get_config},
      {.uri = "/api/service", .method = HTTP_POST, .handler = config},
      {.uri = "/api/service/proxy", .method = HTTP_GET, .handler = proxy},
      {.uri = "/api/service/proxy", .method = HTTP_POST, .handler = proxy},
      {.uri = "/api/service/proxy", .method = HTTP_DELETE, .handler = proxy}};
  for (size_t i = 0; i < sizeof(routes) / sizeof(*routes); i++)
    ESP_ERROR_CHECK(auth_register_uri_handler(s, &routes[i]));
}
