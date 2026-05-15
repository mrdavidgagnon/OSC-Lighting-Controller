#include "web_config.h"
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "web_cfg";
static esp_netif_t *s_netif;
static web_config_forget_cb_t s_forget_cb;

/* ---------- HTML --------------------------------------------------------- */

static const char HTML_HEAD[] =
    "<!DOCTYPE html><html lang=\"en\"><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>OSC Controller \xe2\x80\x94 Network</title>"
    "<style>"
    "*{box-sizing:border-box}"
    "body{font-family:system-ui,sans-serif;margin:0;padding:1rem;background:#1a1a2e;"
    "color:#eee;min-height:100vh;display:flex;align-items:center;justify-content:center}"
    ".card{background:#16213e;border-radius:8px;padding:2rem;width:100%;max-width:420px;"
    "box-shadow:0 4px 20px rgba(0,0,0,.4)}"
    "h1{margin:0 0 1.5rem;font-size:1.4rem;color:#e94560}"
    "table{width:100%;border-collapse:collapse;margin-bottom:1.5rem}"
    "td{padding:.5rem .25rem;border-bottom:1px solid #0f3460;font-size:.9rem}"
    "td:first-child{color:#aaa;width:40%}"
    "td:last-child{font-family:monospace}"
    "button{width:100%;padding:.75rem;border:none;border-radius:4px;"
    "font-size:1rem;cursor:pointer}"
    ".danger{background:#c0392b;color:#fff}"
    ".danger:hover{background:#a93226}"
    "</style></head><body><div class=\"card\"><h1>Network</h1>";

static const char HTML_TAIL[] =
    "<form method=\"POST\" action=\"/forget\">"
    "<button class=\"danger\" type=\"submit\">Forget Network</button>"
    "</form></div></body></html>";

static const char HTML_FORGOTTEN[] =
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<title>Forgotten</title>"
    "<style>body{font-family:system-ui,sans-serif;display:flex;align-items:center;"
    "justify-content:center;min-height:100vh;margin:0;background:#1a1a2e;color:#eee}"
    ".card{text-align:center;padding:2rem}"
    "h1{color:#e94560}p{color:#aaa}</style>"
    "</head><body><div class=\"card\">"
    "<h1>Network Forgotten</h1>"
    "<p>Restarting in AP mode\xe2\x80\xa6</p>"
    "<p>Reconnect to the <b>OSC-Controller</b> network.</p>"
    "</div></body></html>";

/* ---------- handlers ----------------------------------------------------- */

static esp_err_t handler_network(httpd_req_t *req) {
    esp_netif_ip_info_t ip_info = {};
    esp_netif_get_ip_info(s_netif, &ip_info);

    esp_netif_dns_info_t dns1 = {}, dns2 = {};
    esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_MAIN,   &dns1);
    esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_BACKUP, &dns2);

    wifi_ap_record_t ap = {};
    esp_wifi_sta_get_ap_info(&ap);

    char ip_s[16], nm_s[16], gw_s[16], d1_s[16], d2_s[16];
    snprintf(ip_s, sizeof(ip_s), IPSTR, IP2STR(&ip_info.ip));
    snprintf(nm_s, sizeof(nm_s), IPSTR, IP2STR(&ip_info.netmask));
    snprintf(gw_s, sizeof(gw_s), IPSTR, IP2STR(&ip_info.gw));
    snprintf(d1_s, sizeof(d1_s), IPSTR, IP2STR(&dns1.ip.u_addr.ip4));
    snprintf(d2_s, sizeof(d2_s), IPSTR, IP2STR(&dns2.ip.u_addr.ip4));

    char rows[512];
    snprintf(rows, sizeof(rows),
        "<table>"
        "<tr><td>SSID</td><td>%s</td></tr>"
        "<tr><td>IP</td><td>%s</td></tr>"
        "<tr><td>Subnet</td><td>%s</td></tr>"
        "<tr><td>Gateway</td><td>%s</td></tr>"
        "<tr><td>DNS 1</td><td>%s</td></tr>"
        "<tr><td>DNS 2</td><td>%s</td></tr>"
        "<tr><td>Signal</td><td>%d dBm</td></tr>"
        "</table>",
        (char *)ap.ssid, ip_s, nm_s, gw_s, d1_s, d2_s, ap.rssi);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, HTML_HEAD);
    httpd_resp_sendstr_chunk(req, rows);
    httpd_resp_sendstr_chunk(req, HTML_TAIL);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static void forget_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (s_forget_cb) s_forget_cb();
    vTaskDelete(NULL);
}

static esp_err_t handler_forget(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    esp_err_t ret = httpd_resp_send(req, HTML_FORGOTTEN, HTTPD_RESP_USE_STRLEN);
    xTaskCreate(forget_task, "forget", 2048, NULL, 5, NULL);
    return ret;
}

/* ---------- public API --------------------------------------------------- */

void web_config_start(esp_netif_t *netif, web_config_forget_cb_t on_forget) {
    s_netif     = netif;
    s_forget_cb = on_forget;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTP server");
        return;
    }

    static const httpd_uri_t routes[] = {
        { .uri = "/",       .method = HTTP_GET,  .handler = handler_network },
        { .uri = "/forget", .method = HTTP_POST, .handler = handler_forget  },
    };
    for (int i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }

    ESP_LOGI(TAG, "web config server started");
}
