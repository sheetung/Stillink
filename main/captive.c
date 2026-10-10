#include "captive.h"

#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <errno.h>
#include <stdbool.h>

static const char *TAG = "captive";

#define DNS_PORT 53
#define DNS_BUF  512
#define APPLE_SUCCESS_WINDOW_MS 60000

static volatile bool s_apple_success_pending;
static TickType_t s_apple_success_deadline;

static void captive_arm_apple_success(void)
{
    s_apple_success_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(APPLE_SUCCESS_WINDOW_MS);
    s_apple_success_pending = true;
}

static bool captive_apple_success_active(void)
{
    if (!s_apple_success_pending) return false;
    TickType_t now = xTaskGetTickCount();
    if ((int32_t)(s_apple_success_deadline - now) < 0) {
        s_apple_success_pending = false;
        return false;
    }
    return true;
}

void captive_apple_station_disconnected(void)
{
    /* Success 只服务于当前这次连接。用户主动断开后立即恢复门户，下一次连接
       无需等待超时或重启 ESP32；60 秒窗口只作为漏掉断开事件时的兜底。 */
    s_apple_success_pending = false;
}

static esp_err_t captive_apple_success(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req,
        "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
}

static esp_err_t captive_apple_done_page(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
        "<title>地址已复制</title><style>*{box-sizing:border-box}body{margin:0;min-height:100vh;"
        "display:flex;align-items:center;justify-content:center;padding:24px;background:#f5f5f7;"
        "color:#1d1d1f;font-family:-apple-system,BlinkMacSystemFont,\"PingFang SC\",sans-serif}"
        ".card{width:100%;max-width:430px;padding:30px 24px;border-radius:20px;background:#fff;"
        "box-shadow:0 10px 36px #00000014;text-align:center}.ok{width:60px;height:60px;margin:0 auto 18px;"
        "display:grid;place-items:center;border-radius:50%;background:#e8f7ed;color:#16833d;"
        "font-size:32px;font-weight:700}h1{font-size:23px;margin:0 0 12px}p{font-size:16px;"
        "line-height:1.65;color:#555;margin:8px 0}.next{margin-top:18px;padding:14px;border-radius:12px;"
        "background:#fff7df;color:#684f18;font-size:15px}</style></head><body><main class=\"card\">"
        "<div class=\"ok\">&#10003;</div><h1>地址已复制</h1>"
        "<p>请点击右上角的“完成”关闭此页面。</p>"
        "<p class=\"next\">然后打开系统自带的 Safari，在地址栏粘贴并访问 <b>192.168.4.1</b></p>"
        "</main></body></html>");
}

static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        return;
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "bind :%d failed: %d", DNS_PORT, errno);
        close(sock);
        return;
    }
    ESP_LOGI(TAG, "DNS hijack on :%d -> 192.168.4.1", DNS_PORT);

    uint8_t q[DNS_BUF], r[DNS_BUF];

    for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int n = recvfrom(sock, q, sizeof(q), 0, (struct sockaddr *)&from, &fl);
        if (n < 12) continue; /* 至少一个完整 DNS 头 */

        /* Apple 的解析器可能在一个 DNS 包里携带多个问题；旧实现仅接受 QDCOUNT=1，
           整包丢弃后 iPhone 就不会继续请求 /hotspot-detect.html。 */
        if ((q[2] & 0xF8) != 0) continue; /* QR=0 且 OPCODE=QUERY */
        uint16_t qdcount = ((uint16_t)q[4] << 8) | q[5];
        if (qdcount == 0 || qdcount > 8) continue;

        uint16_t name_off[8], qtype[8], qclass[8];
        int qend = 12;
        bool packet_valid = true;
        for (uint16_t qi = 0; qi < qdcount; qi++) {
            name_off[qi] = (uint16_t)qend;
            bool name_valid = false;
            while (qend < n) {
                uint8_t len = q[qend];
                if (len == 0) { qend++; name_valid = true; break; }
                if ((len & 0xC0) == 0xC0) {
                    if (qend + 1 >= n) break;
                    qend += 2;
                    name_valid = true;
                    break;
                }
                if (len > 63 || qend + 1 + len > n) break;
                qend += 1 + len;
            }
            if (!name_valid || qend + 4 > n) { packet_valid = false; break; }
            qtype[qi] = ((uint16_t)q[qend] << 8) | q[qend + 1];
            qclass[qi] = ((uint16_t)q[qend + 2] << 8) | q[qend + 3];
            qend += 4;
        }
        if (!packet_valid) continue;

        memset(r, 0, sizeof(r));
        r[0] = q[0]; r[1] = q[1];             /* 事务 ID 原样回 */
        r[2] = 0x84 | (q[2] & 0x01);          /* QR=1、AA=1，保留 RD */
        r[3] = 0x00;                          /* NOERROR */
        r[4] = (uint8_t)(qdcount >> 8);
        r[5] = (uint8_t)qdcount;
        memcpy(r + 12, q + 12, (size_t)(qend - 12));

        /* 每个 A/IN 问题返回 192.168.4.1；AAAA、HTTPS 等保留为 NOERROR 空答案，
           避免类型不匹配，同时不能用 NXDOMAIN（会连带否定同名 A 记录）。 */
        int a = qend;
        uint16_t ancount = 0;
        for (uint16_t qi = 0; qi < qdcount; qi++) {
            if (qtype[qi] != 1 || qclass[qi] != 1) continue;
            if (a + 16 > DNS_BUF) { packet_valid = false; break; }
            uint16_t ptr = (uint16_t)(0xC000u | name_off[qi]);
            r[a++] = (uint8_t)(ptr >> 8); r[a++] = (uint8_t)ptr;
            r[a++] = 0x00; r[a++] = 0x01;     /* TYPE  = A   */
            r[a++] = 0x00; r[a++] = 0x01;     /* CLASS = IN  */
            r[a++] = 0x00; r[a++] = 0x00; r[a++] = 0x00; r[a++] = 0x3C; /* TTL 60 */
            r[a++] = 0x00; r[a++] = 0x04;     /* RDLEN = 4  */
            r[a++] = 192; r[a++] = 168; r[a++] = 4; r[a++] = 1;
            ancount++;
        }
        if (!packet_valid) continue;
        r[6] = (uint8_t)(ancount >> 8);
        r[7] = (uint8_t)ancount;

        sendto(sock, r, a, 0, (struct sockaddr *)&from, fl);
    }
}

void captive_dns_start(void)
{
    xTaskCreate(dns_task, "captive_dns", 3072, NULL, 5, NULL);
}

esp_err_t captive_redirect(httpd_req_t *req)
{
    /* 303 + 非空正文可让 iOS Captive Network Assistant 识别。Location 必须
       使用绝对 URL；Vivo 的 captive WebView 会把相对地址解析成 file:///。 */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/?moink_captive=1");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req,
        "<!doctype html><a href=\"http://192.168.4.1/?moink_captive=1\">Open MoInk</a>");
}

esp_err_t captive_apple_landing(httpd_req_t *req)
{
    /* 用户复制地址后显示友好完成页；同时开启成功窗口，让 Apple 后台复探获得
       严格的标准 Success。热点保持连接，供 Safari 访问。 */
    char query[64], done[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
        httpd_query_key_value(query, "moink_done", done, sizeof(done)) == ESP_OK &&
        strcmp(done, "1") == 0) {
        captive_arm_apple_success();
        return captive_apple_done_page(req);
    }

    if (captive_apple_success_active()) return captive_apple_success(req);

    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req,
        "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">"
        "<title>使用 Safari 打开 MoInk</title><style>"
        "*{box-sizing:border-box}body{margin:0;min-height:100vh;display:flex;align-items:center;"
        "justify-content:center;padding:24px;background:#f5f5f7;color:#1d1d1f;font-family:-apple-system,"
        "BlinkMacSystemFont,\"PingFang SC\",sans-serif}.card{width:100%;max-width:430px;background:#fff;"
        "border-radius:20px;padding:28px 22px;box-shadow:0 10px 36px #00000014;text-align:center}"
        ".logo{font-size:40px;margin-bottom:10px}h1{font-size:23px;margin:0 0 12px}p{font-size:15px;"
        "line-height:1.65;color:#555;margin:8px 0}.addr{display:block;margin:18px 0 12px;padding:13px;"
        "border-radius:12px;background:#f2f3f5;color:#1d1d1f;font:600 18px ui-monospace,monospace}"
        "button{width:100%;border:0;border-radius:12px;padding:14px;background:#1677ff;color:#fff;"
        "font-size:17px;font-weight:700}.steps{margin-top:18px;padding:14px;text-align:left;border-radius:12px;"
        "background:#fff7df;color:#684f18;font-size:14px;line-height:1.65}.ok{color:#16833d;font-weight:700}"
        "</style></head><body><main class=\"card\"><div class=\"logo\">&#128241;</div>"
        "<h1>请使用 Safari 打开配置页</h1><p>墨印 · MoInk</p>"
        "<span class=\"addr\" id=\"addr\">192.168.4.1</span>"
        "<button id=\"copy\" type=\"button\">复制地址并关闭此页</button>"
        "<p id=\"state\"></p><div class=\"steps\"><b>接下来：</b><br>"
        "1. 点击上方按钮复制地址<br>2. 打开系统自带的 Safari<br>3. 在地址栏粘贴并访问</div>"
        "</main><script>function finish(){document.getElementById('state').className='ok';"
        "document.getElementById('state').textContent='已复制，请打开 Safari 并粘贴访问';"
        "setTimeout(function(){location.replace('/hotspot-detect.html?moink_done=1');},700)}"
        "function fallback(){var t=document.createElement('textarea');t.value='192.168.4.1';"
        "t.style.position='fixed';t.style.opacity='0';document.body.appendChild(t);t.focus();t.select();"
        "t.setSelectionRange(0,t.value.length);var ok=false;try{ok=document.execCommand('copy')}catch(e){}"
        "document.body.removeChild(t);if(ok)finish();else{var a=document.getElementById('addr');"
        "document.getElementById('state').textContent='复制失败，请长按上方地址复制';window.getSelection().selectAllChildren(a)}}"
        "document.getElementById('copy').onclick=function(){if(navigator.clipboard&&window.isSecureContext)"
        "navigator.clipboard.writeText('192.168.4.1').then(finish,fallback);else fallback()};</script>"
        "</body></html>");
}
