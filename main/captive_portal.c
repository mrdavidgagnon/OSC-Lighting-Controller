#include "captive_portal.h"
#include "wifi_manager.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

static const char *TAG = "captive";

#define PORTAL_URL "http://" WIFI_AP_IP "/"

static const char HTML_INDEX[] =
    "<!DOCTYPE html><html lang=\"en\"><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>OSC Controller \xe2\x80\x94 WiFi Setup</title>"
    "<style>"
    "*{box-sizing:border-box}"
    "body{font-family:system-ui,sans-serif;margin:0;padding:1rem;background:#1a1a2e;"
    "color:#eee;min-height:100vh;display:flex;align-items:center;justify-content:center}"
    ".card{background:#16213e;border-radius:8px;padding:2rem;width:100%;max-width:380px;"
    "box-shadow:0 4px 20px rgba(0,0,0,.4)}"
    "h1{margin:0 0 .5rem;font-size:1.4rem;color:#e94560}"
    "p{margin:0 0 1.5rem;color:#aaa;font-size:.9rem}"
    "label{display:block;margin-bottom:1rem}"
    "span{display:block;margin-bottom:.3rem;font-size:.85rem;color:#ccc}"
    "input{width:100%;padding:.6rem .8rem;border:1px solid #0f3460;border-radius:4px;"
    "background:#0f3460;color:#eee;font-size:1rem}"
    "input:focus{outline:none;border-color:#e94560}"
    "button{width:100%;padding:.75rem;background:#e94560;color:#fff;border:none;"
    "border-radius:4px;font-size:1rem;cursor:pointer;margin-top:.5rem}"
    "button:hover{background:#c73652}"
    "</style></head><body>"
    "<div class=\"card\">"
    "<h1>OSC Controller</h1>"
    "<p>Connect to your WiFi network to get started.</p>"
    "<form method=\"POST\" action=\"/save\">"
    "<label><span>Network Name (SSID)</span>"
    "<input type=\"text\" name=\"ssid\" required maxlength=\"32\""
    " autocomplete=\"off\" autocorrect=\"off\" spellcheck=\"false\"></label>"
    "<label><span>Password</span>"
    "<input type=\"password\" name=\"password\" maxlength=\"64\"></label>"
    "<button type=\"submit\">Connect</button>"
    "</form></div></body></html>";

static const char HTML_SAVED[] =
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<title>Connecting\xe2\x80\xa6</title>"
    "<style>body{font-family:system-ui,sans-serif;display:flex;align-items:center;"
    "justify-content:center;min-height:100vh;margin:0;background:#1a1a2e;color:#eee}"
    ".card{text-align:center;padding:2rem}h1{color:#4caf50}p{color:#aaa}</style>"
    "</head><body><div class=\"card\">"
    "<h1>&#x2713; Credentials Saved</h1>"
    "<p>Restarting and connecting to your network\xe2\x80\xa6</p>"
    "<p>You may close this page.</p>"
    "</div></body></html>";

/* ---------- form helpers ------------------------------------------------- */

static void url_decode(char *str) {
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], '\0'};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void parse_form_field(const char *body, const char *key,
                             char *out, size_t out_len) {
    char search[40];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) { out[0] = '\0'; return; }
    p += strlen(search);
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    url_decode(out);
}

/* ---------- HTTP handlers ------------------------------------------------ */

static esp_err_t handler_index(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, HTML_INDEX, HTTPD_RESP_USE_STRLEN);
}

static void restart_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

static esp_err_t handler_save(httpd_req_t *req) {
    char body[256] = {};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }
    body[len] = '\0';

    char ssid[33] = {}, password[65] = {};
    parse_form_field(body, "ssid", ssid, sizeof(ssid));
    parse_form_field(body, "password", password, sizeof(password));

    if (ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    wifi_manager_save_credentials(ssid, password);

    httpd_resp_set_type(req, "text/html");
    esp_err_t ret = httpd_resp_send(req, HTML_SAVED, HTTPD_RESP_USE_STRLEN);
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    return ret;
}

static esp_err_t handler_redirect(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", PORTAL_URL);
    return httpd_resp_send(req, NULL, 0);
}

/* Catch-all for any URI not explicitly registered */
static esp_err_t handler_404(httpd_req_t *req, httpd_err_code_t err) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", PORTAL_URL);
    return httpd_resp_send(req, NULL, 0);
}

/* ---------- DNS server --------------------------------------------------- */
/*
 * Spoof every DNS query with an A record pointing to WIFI_AP_IP so that
 * OS captive-portal detectors and browsers are redirected to the config page
 * regardless of the hostname they look up.
 */
static void dns_server_task(void *arg) {
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(53),
    };
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket() failed");
        vTaskDelete(NULL);
        return;
    }
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind() failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS server listening on :53");

    uint8_t buf[512];
    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);

    while (1) {
        int len = recvfrom(sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&client, &client_len);
        if (len < 12) continue;

        /* Rewrite header flags in-place: QR=1 AA=1 RA=1, preserve RD */
        uint16_t flags_in  = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t flags_out = 0x8400u | (flags_in & 0x0100u);
        buf[2]  = (uint8_t)(flags_out >> 8);
        buf[3]  = (uint8_t)(flags_out & 0xFF);
        buf[6]  = 0; buf[7]  = 1;  /* ANCOUNT = 1 */
        buf[8]  = 0; buf[9]  = 0;  /* NSCOUNT = 0 */
        buf[10] = 0; buf[11] = 0;  /* ARCOUNT = 0 */

        /* Walk past the QNAME to find where the question section ends */
        int pos = 12;
        while (pos < len && buf[pos] != 0) {
            if ((buf[pos] & 0xC0) == 0xC0) { pos += 2; break; }
            pos += buf[pos] + 1;
        }
        if (pos < len && buf[pos] == 0) pos++;  /* skip null label */
        if (pos + 4 > len) continue;            /* malformed — drop */
        pos += 4;                               /* skip QTYPE + QCLASS */

        /* Append answer record */
        if (pos + 16 > (int)sizeof(buf)) continue;
        buf[pos++] = 0xC0; buf[pos++] = 0x0C;  /* name: pointer to byte 12 */
        buf[pos++] = 0x00; buf[pos++] = 0x01;  /* type A                   */
        buf[pos++] = 0x00; buf[pos++] = 0x01;  /* class IN                 */
        buf[pos++] = 0x00; buf[pos++] = 0x00;
        buf[pos++] = 0x00; buf[pos++] = 0x3C;  /* TTL 60 s                 */
        buf[pos++] = 0x00; buf[pos++] = 0x04;  /* RDLENGTH 4               */
        buf[pos++] = 192;  buf[pos++] = 168;
        buf[pos++] = 4;    buf[pos++] = 1;      /* 192.168.4.1              */

        sendto(sock, buf, pos, 0, (struct sockaddr *)&client, client_len);
    }

    close(sock);
    vTaskDelete(NULL);
}

/* ---------- public API --------------------------------------------------- */

void captive_portal_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    static const httpd_uri_t routes[] = {
        { .uri = "/",                    .method = HTTP_GET,  .handler = handler_index    },
        { .uri = "/save",                .method = HTTP_POST, .handler = handler_save     },
        /* OS captive-portal detection endpoints */
        { .uri = "/generate_204",        .method = HTTP_GET,  .handler = handler_redirect },
        { .uri = "/hotspot-detect.html", .method = HTTP_GET,  .handler = handler_redirect },
        { .uri = "/canonical.html",      .method = HTTP_GET,  .handler = handler_redirect },
        { .uri = "/ncsi.txt",            .method = HTTP_GET,  .handler = handler_redirect },
        { .uri = "/connecttest.txt",     .method = HTTP_GET,  .handler = handler_redirect },
        { .uri = "/redirect",            .method = HTTP_GET,  .handler = handler_redirect },
    };
    for (int i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, handler_404);

    xTaskCreate(dns_server_task, "dns_srv", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "captive portal started — http://" WIFI_AP_IP);
}
