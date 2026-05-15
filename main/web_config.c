#include "web_config.h"
#include "onyx.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "web_cfg";
static esp_netif_t *s_netif;
static web_config_forget_cb_t s_forget_cb;

static void json_esc(const char *src, char *dst, size_t dst_len) {
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 2 < dst_len; si++) {
        char c = src[si];
        if (c == '"' || c == '\\') {
            if (di + 3 > dst_len) break;
            dst[di++] = '\\';
        }
        dst[di++] = c;
    }
    dst[di] = '\0';
}

/* ---------- form helpers ------------------------------------------------- */

static void url_decode(char *str) {
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '+') { *dst++ = ' '; src++; }
        else if (*src == '%' && src[1] && src[2]) {
            char hex[3] = {src[1], src[2], '\0'};
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else { *dst++ = *src++; }
    }
    *dst = '\0';
}

static void parse_field(const char *body, const char *key, char *out, size_t out_len) {
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

/* ---------- HTML --------------------------------------------------------- */

/* HEAD: doctype → <div id="p-net" class="panel active"> */
static const char HTML_A[] =
    "<!DOCTYPE html><html lang=\"en\"><head>"
    "<meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>OSC Controller</title>"
    "<style>"
    "*{box-sizing:border-box}"
    "body{font-family:system-ui,sans-serif;margin:0;padding:1rem;background:#1a1a2e;"
    "color:#eee;min-height:100vh;display:flex;align-items:center;justify-content:center}"
    ".card{background:#16213e;border-radius:8px;padding:2rem;width:100%;max-width:420px;"
    "box-shadow:0 4px 20px rgba(0,0,0,.4)}"
    ".tabs{display:flex;gap:.5rem;margin-bottom:1.5rem}"
    ".tab{flex:1;padding:.5rem;background:#0f3460;border:none;border-radius:4px;"
    "color:#aaa;cursor:pointer;font-size:.85rem}"
    ".tab.active{background:#e94560;color:#fff}"
    ".panel{display:none}.panel.active{display:block}"
    "table{width:100%;border-collapse:collapse;margin-bottom:1.5rem}"
    "td{padding:.5rem .25rem;border-bottom:1px solid #0f3460;font-size:.9rem}"
    "td:first-child{color:#aaa;width:40%}td:last-child{font-family:monospace}"
    ".row{display:flex;justify-content:space-between;padding:.45rem 0;"
    "border-bottom:1px solid #0f3460;font-size:.9rem;margin-bottom:.1rem}"
    ".lbl{color:#aaa}"
    ".track{background:#0f3460;border-radius:4px;height:14px;margin:.85rem 0}"
    ".fill{background:#e94560;border-radius:4px;height:100%;width:0%;"
    "transition:width .25s ease}"
    ".swatch{width:100%;height:40px;border-radius:4px;background:#222;"
    "margin-top:.5rem;border:1px solid #0f3460;transition:background .25s}"
    "label{display:block;margin-bottom:1rem}"
    "label>span{display:block;margin-bottom:.3rem;font-size:.85rem;color:#ccc}"
    "input{width:100%;padding:.6rem .8rem;border:1px solid #0f3460;border-radius:4px;"
    "background:#0f3460;color:#eee;font-size:1rem}"
    "input:focus{outline:none;border-color:#e94560}"
    "button{width:100%;padding:.75rem;border:none;border-radius:4px;"
    "font-size:1rem;cursor:pointer;margin-top:.5rem}"
    ".primary{background:#e94560;color:#fff}.primary:hover{background:#c73652}"
    ".danger{background:#c0392b;color:#fff}.danger:hover{background:#a93226}"
    ".hint{color:#888;font-size:.8rem;margin:.6rem 0 1rem;line-height:1.5}"
    ".hint b{color:#ccc}"
    ".leds{margin-top:.85rem;border-top:1px solid #0f3460;padding-top:.75rem}"
    ".leds-hd{font-size:.78rem;color:#555;text-transform:uppercase;"
    "letter-spacing:.06em;margin-bottom:.5rem}"
    ".led-row{display:flex;align-items:center;justify-content:space-between;"
    "padding:.3rem 0;font-size:.9rem}"
    ".led-right{display:flex;align-items:center;gap:.5rem}"
    ".led-dot{width:20px;height:20px;border-radius:4px;background:#111;"
    "border:1px solid #0f3460;transition:background .2s,box-shadow .2s}"
    ".blk{font-size:.7rem;padding:.1rem .35rem;border-radius:3px;"
    "background:#e94560;color:#fff;display:none}"
    ".blk.on{display:inline}"
    ".col-sw{flex:1;height:20px;border-radius:3px;background:#111;"
    "border:1px solid #0f3460;transition:background .2s}"
    "</style></head><body><div class=\"card\">"
    "<div class=\"tabs\">"
    "<button class=\"tab active\" onclick=\"show('net',this)\">Network</button>"
    "<button class=\"tab\" onclick=\"show('fdr',this)\">Fader</button>"
    "<button class=\"tab\" onclick=\"show('cfg',this)\">Settings</button>"
    "</div>"
    "<div id=\"p-net\" class=\"panel active\">";

/* MID: end network panel → full fader panel → open settings panel */
static const char HTML_B[] =
    "</div>"
    "<div id=\"p-fdr\" class=\"panel\">"
    "<div class=\"row\"><span class=\"lbl\">Name</span><span id=\"f-name\">\xe2\x80\x94</span></div>"
    "<div class=\"row\"><span class=\"lbl\">Value</span><span id=\"f-val\">\xe2\x80\x94</span></div>"
    "<div class=\"track\"><div class=\"fill\" id=\"f-bar\"></div></div>"
    "<div class=\"swatch\" id=\"f-sw\"></div>"
    "<div class=\"leds\">"
    "<div class=\"leds-hd\">Button LEDs</div>"
    "<div class=\"led-row\"><span class=\"lbl\">Button 1</span>"
    "<div class=\"led-right\"><div class=\"led-dot\" id=\"l1-dot\"></div>"
    "<span class=\"blk\" id=\"l1-blk\">BLINK</span></div></div>"
    "<div class=\"led-row\"><span class=\"lbl\">Button 2</span>"
    "<div class=\"led-right\"><div class=\"led-dot\" id=\"l2-dot\"></div>"
    "<span class=\"blk\" id=\"l2-blk\">BLINK</span></div></div>"
    "<div class=\"led-row\"><span class=\"lbl\">Button 3</span>"
    "<div class=\"led-right\"><div class=\"led-dot\" id=\"l3-dot\"></div>"
    "</div></div>"
    "</div>"
    "<div class=\"leds\">"
    "<div class=\"leds-hd\">Button Colors</div>"
    "<div class=\"led-row\"><span class=\"lbl\">Button</span>"
    "<div class=\"led-right\" style=\"flex:1;margin-left:.5rem\">"
    "<div class=\"col-sw\" id=\"b-col\"></div></div></div>"
    "<div class=\"led-row\"><span class=\"lbl\">Text</span>"
    "<div class=\"led-right\" style=\"flex:1;margin-left:.5rem\">"
    "<div class=\"col-sw\" id=\"b-tcol\"></div></div></div>"
    "</div>"
    "</div>"
    "<div id=\"p-cfg\" class=\"panel\">";

/* FOOT: end settings panel → card → JS → body/html */
static const char HTML_C[] =
    "</div></div>"
    "<script>"
    "var T=null;"
    "function setLed(n,on,col,blink){"
      "var d=document.getElementById('l'+n+'-dot');"
      "if(d)d.style.background=on?(col||'#fff'):'#111';"
      "var b=document.getElementById('l'+n+'-blk');"
      "if(b)b.className='blk'+(blink?' on':'');"
    "}"
    "function show(id,btn){"
      "document.querySelectorAll('.panel').forEach(function(p){p.classList.remove('active')});"
      "document.querySelectorAll('.tab').forEach(function(t){t.classList.remove('active')});"
      "document.getElementById('p-'+id).classList.add('active');"
      "btn.classList.add('active');"
      "if(id==='fdr'){poll();T=setInterval(poll,300);}"
      "else if(T){clearInterval(T);T=null;}"
    "}"
    "function poll(){"
      "fetch('/api/fader').then(function(r){return r.json();}).then(function(d){"
        "document.getElementById('f-name').textContent=d.name||'\xe2\x80\x94';"
        "document.getElementById('f-val').textContent=d.valid"
          "?(d.value*100).toFixed(1)+'%':'\xe2\x80\x94';"
        "document.getElementById('f-bar').style.width=d.valid?(d.value*100)+'%':'0%';"
        "document.getElementById('f-sw').style.background=d.color||'#222';"
        "setLed(1,d.b1_on,d.b1_col,d.b1_blk);"
        "setLed(2,d.b2_on,d.b2_col,d.b2_blk);"
        "setLed(3,d.b3_on,d.b3_col,false);"
        "var bc=document.getElementById('b-col');"
        "if(bc)bc.style.background=d.b4_col||'#111';"
        "var btc=document.getElementById('b-tcol');"
        "if(btc)btc.style.background=d.b4_tcol||'#111';"
      "}).catch(function(){});}"
    "</script>"
    "</body></html>";

static const char HTML_FORGOTTEN[] =
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\"><title>Forgotten</title>"
    "<style>body{font-family:system-ui,sans-serif;display:flex;align-items:center;"
    "justify-content:center;min-height:100vh;margin:0;background:#1a1a2e;color:#eee}"
    ".c{text-align:center;padding:2rem}h1{color:#e94560}p{color:#aaa}</style>"
    "</head><body><div class=\"c\"><h1>Network Forgotten</h1>"
    "<p>Restarting in AP mode\xe2\x80\xa6</p>"
    "<p>Reconnect to <b>OSC-Controller</b>.</p></div></body></html>";

static const char HTML_SAVED[] =
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\"><title>Saved</title>"
    "<style>body{font-family:system-ui,sans-serif;display:flex;align-items:center;"
    "justify-content:center;min-height:100vh;margin:0;background:#1a1a2e;color:#eee}"
    ".c{text-align:center;padding:2rem}h1{color:#4caf50}p{color:#aaa}</style>"
    "</head><body><div class=\"c\"><h1>&#x2713; Settings Saved</h1>"
    "<p>Restarting\xe2\x80\xa6</p></div></body></html>";

/* ---------- deferred restart --------------------------------------------- */

static void restart_task(void *arg) { vTaskDelay(pdMS_TO_TICKS(500)); esp_restart(); }
static void forget_task(void *arg)  {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (s_forget_cb) s_forget_cb();
    vTaskDelete(NULL);
}

/* ---------- handlers ----------------------------------------------------- */

static esp_err_t handler_root(httpd_req_t *req) {
    /* Network info */
    esp_netif_ip_info_t ip = {};
    esp_netif_get_ip_info(s_netif, &ip);
    esp_netif_dns_info_t dns1 = {}, dns2 = {};
    esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_MAIN,   &dns1);
    esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_BACKUP, &dns2);
    wifi_ap_record_t ap = {};
    esp_wifi_sta_get_ap_info(&ap);

    char ip_s[16], nm_s[16], gw_s[16], d1_s[16], d2_s[16];
    snprintf(ip_s, sizeof(ip_s), IPSTR, IP2STR(&ip.ip));
    snprintf(nm_s, sizeof(nm_s), IPSTR, IP2STR(&ip.netmask));
    snprintf(gw_s, sizeof(gw_s), IPSTR, IP2STR(&ip.gw));
    snprintf(d1_s, sizeof(d1_s), IPSTR, IP2STR(&dns1.ip.u_addr.ip4));
    snprintf(d2_s, sizeof(d2_s), IPSTR, IP2STR(&dns2.ip.u_addr.ip4));

    /* OSC settings */
    uint16_t port        = onyx_load_port();
    uint16_t server_port = onyx_load_server_port();
    char onyx_ip[32] = {};
    onyx_load_onyx_ip(onyx_ip, sizeof(onyx_ip));

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, HTML_A);

    /* Dynamic: network panel */
    char net[512];
    snprintf(net, sizeof(net),
        "<table>"
        "<tr><td>SSID</td><td>%s</td></tr>"
        "<tr><td>IP</td><td>%s</td></tr>"
        "<tr><td>Subnet</td><td>%s</td></tr>"
        "<tr><td>Gateway</td><td>%s</td></tr>"
        "<tr><td>DNS 1</td><td>%s</td></tr>"
        "<tr><td>DNS 2</td><td>%s</td></tr>"
        "<tr><td>Signal</td><td>%d dBm</td></tr>"
        "</table>"
        "<form method=\"POST\" action=\"/forget\">"
        "<button class=\"danger\" type=\"submit\">Forget Network</button>"
        "</form>",
        (char *)ap.ssid, ip_s, nm_s, gw_s, d1_s, d2_s, ap.rssi);
    httpd_resp_sendstr_chunk(req, net);

    httpd_resp_sendstr_chunk(req, HTML_B);

    /* Dynamic: settings panel */
    char cfg[800];
    snprintf(cfg, sizeof(cfg),
        "<form method=\"POST\" action=\"/settings\">"
        "<label><span>ESP32 Listen Port</span>"
        "<input type=\"number\" name=\"port\" value=\"%u\" min=\"1\" max=\"65535\"></label>"
        "<label><span>ONYX Console IP</span>"
        "<input type=\"text\" name=\"onyx_ip\" value=\"%s\""
        " placeholder=\"e.g. 192.168.1.100\"></label>"
        "<label><span>ONYX Server Port</span>"
        "<input type=\"number\" name=\"srv_port\" value=\"%u\" min=\"1\" max=\"65535\"></label>"
        "<p class=\"hint\">"
        "ONYX \xe2\x86\x92 ESP32: set OSC output destination to <b>%s : %u</b><br>"
        "ESP32 \xe2\x86\x92 ONYX: sends to <b>%s : %u</b>"
        "</p>"
        "<button class=\"primary\" type=\"submit\">Save &amp; Restart</button>"
        "</form>",
        port, onyx_ip, server_port, ip_s, port, onyx_ip[0] ? onyx_ip : "ONYX-IP", server_port);
    httpd_resp_sendstr_chunk(req, cfg);

    httpd_resp_sendstr_chunk(req, HTML_C);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t handler_api_fader(httpd_req_t *req) {
    float value = 0.0f;
    char  color[16] = "";
    char  name[64]  = "";

    bool valid = onyx_get_fader(ONYX_FADER_ID, &value, color, sizeof(color));
    onyx_get_button_text(ONYX_BUTTON_ID, name, sizeof(name));

    onyx_button_led_t led1 = {}, led2 = {}, led3 = {};
    onyx_get_button_led(ONYX_BTN_LED_1, &led1);
    onyx_get_button_led(ONYX_BTN_LED_2, &led2);
    onyx_get_button_led(ONYX_BTN_LED_3, &led3);

    char btn_col[16]  = "";
    char btn_tcol[16] = "";
    onyx_get_button_color(ONYX_BUTTON_ID, btn_col, sizeof(btn_col));
    onyx_get_button_text_color(ONYX_BUTTON_ID, btn_tcol, sizeof(btn_tcol));

    char safe_name[130] = "";
    json_esc(name, safe_name, sizeof(safe_name));

    char json[430];
    snprintf(json, sizeof(json),
        "{\"valid\":%s,\"value\":%.4f,\"name\":\"%s\",\"color\":\"%s\","
        "\"b1_on\":%d,\"b1_col\":\"%s\",\"b1_blk\":%d,"
        "\"b2_on\":%d,\"b2_col\":\"%s\",\"b2_blk\":%d,"
        "\"b3_on\":%d,\"b3_col\":\"%s\","
        "\"b4_col\":\"%s\",\"b4_tcol\":\"%s\"}",
        valid ? "true" : "false", (double)value, safe_name,
        color[0] ? color : "#222222",
        led1.on, led1.color[0] ? led1.color : "#111111", led1.blink,
        led2.on, led2.color[0] ? led2.color : "#111111", led2.blink,
        led3.on, led3.color[0] ? led3.color : "#111111",
        btn_col[0]  ? btn_col  : "#111111",
        btn_tcol[0] ? btn_tcol : "#111111");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_settings(httpd_req_t *req) {
    char body[128] = {};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty body");
        return ESP_FAIL;
    }
    body[len] = '\0';

    char port_str[8] = {}, srv_port_str[8] = {}, onyx_ip[32] = {};
    parse_field(body, "port",     port_str,     sizeof(port_str));
    parse_field(body, "srv_port", srv_port_str, sizeof(srv_port_str));
    parse_field(body, "onyx_ip",  onyx_ip,      sizeof(onyx_ip));

    uint16_t port     = (uint16_t)atoi(port_str);
    uint16_t srv_port = (uint16_t)atoi(srv_port_str);
    if (port     == 0) port     = ONYX_OSC_PORT;
    if (srv_port == 0) srv_port = ONYX_SERVER_PORT;

    onyx_save_settings(port, srv_port, onyx_ip);

    httpd_resp_set_type(req, "text/html");
    esp_err_t ret = httpd_resp_send(req, HTML_SAVED, HTTPD_RESP_USE_STRLEN);
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    return ret;
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

    httpd_config_t config  = HTTPD_DEFAULT_CONFIG();
    config.stack_size      = 8192;
    config.max_uri_handlers = 8;

    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTP server");
        return;
    }

    static const httpd_uri_t routes[] = {
        { .uri = "/",           .method = HTTP_GET,  .handler = handler_root       },
        { .uri = "/api/fader",  .method = HTTP_GET,  .handler = handler_api_fader  },
        { .uri = "/settings",   .method = HTTP_POST, .handler = handler_settings   },
        { .uri = "/forget",     .method = HTTP_POST, .handler = handler_forget     },
    };
    for (int i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++)
        httpd_register_uri_handler(server, &routes[i]);

    ESP_LOGI(TAG, "web config server started");
}
