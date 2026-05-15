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
    ".card{background:#16213e;border-radius:8px;padding:2rem;width:100%;max-width:960px;"
    "box-shadow:0 4px 20px rgba(0,0,0,.4)}"
    ".tabs{display:flex;gap:.5rem;margin-bottom:1.5rem}"
    ".tab{flex:1;padding:.5rem;background:#0f3460;border:none;border-radius:4px;"
    "color:#aaa;cursor:pointer;font-size:.85rem}"
    ".tab.active{background:#e94560;color:#fff}"
    ".panel{display:none}.panel.active{display:block}"
    "table{width:100%;border-collapse:collapse;margin-bottom:1.5rem}"
    "td{padding:.5rem .25rem;border-bottom:1px solid #0f3460;font-size:.9rem}"
    "td:first-child{color:#aaa;width:40%}td:last-child{font-family:monospace}"
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
    "@keyframes blink{0%,100%{opacity:1}50%{opacity:.12}}"
    ".strip-row{display:flex;gap:.5rem;overflow-x:auto;-webkit-overflow-scrolling:touch;"
    "padding:.25rem 0 .5rem}"
    ".strip{width:80px;flex-shrink:0;border-radius:6px;background:#222;"
    "display:flex;flex-direction:column;align-items:stretch;"
    "padding:.5rem .4rem;gap:.3rem;"
    "box-shadow:inset 0 0 0 1px rgba(255,255,255,.08),0 2px 8px rgba(0,0,0,.5);"
    "min-height:320px}"
    ".strip-lbl{font-size:.78rem;font-weight:600;text-align:center;color:#fff;"
    "padding:.25rem 0;border-bottom:1px solid rgba(255,255,255,.12);"
    "margin-bottom:.05rem;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}"
    ".s-btn{display:flex;align-items:center;gap:.35rem;"
    "background:rgba(0,0,0,.3);border:1px solid rgba(255,255,255,.1);"
    "border-radius:3px;padding:.3rem .4rem;margin-top:0;"
    "cursor:pointer;color:rgba(255,255,255,.85);font-size:.72rem;"
    "width:100%;text-align:left;-webkit-user-select:none;user-select:none}"
    ".s-btn:active{background:rgba(255,255,255,.15)}"
    ".s-led{width:9px;height:9px;border-radius:50%;flex-shrink:0;"
    "background:#111;border:1px solid rgba(255,255,255,.15);"
    "transition:background .15s,box-shadow .15s}"
    ".s-led.blinking{animation:blink .8s step-start infinite}"
    ".fdr-wrap{flex:1;display:flex;gap:.35rem;align-items:stretch;min-height:140px}"
    ".fdr-track{flex:1;background:rgba(0,0,0,.4);border-radius:3px;"
    "position:relative;overflow:hidden;cursor:ns-resize;touch-action:none}"
    ".fdr-fill{position:absolute;bottom:0;left:0;right:0;"
    "background:rgba(255,255,255,.35);border-radius:3px;"
    "height:0%;transition:height .2s ease}"
    "</style></head><body><div class=\"card\">"
    "<div class=\"tabs\">"
    "<button class=\"tab active\" onclick=\"show('net',this)\">Network</button>"
    "<button class=\"tab\" onclick=\"show('mon',this)\">Monitor</button>"
    "<button class=\"tab\" onclick=\"show('cfg',this)\">Settings</button>"
    "</div>"
    "<div id=\"p-net\" class=\"panel active\">";

/* MID: end network panel → monitor panel → open settings panel */
static const char HTML_B[] =
    "</div>"
    "<div id=\"p-mon\" class=\"panel\">"
    "<div class=\"strip-row\" id=\"strip-row\"></div>"
    "</div>"
    "<div id=\"p-cfg\" class=\"panel\">";

/* FOOT: end settings panel → card → JS → body/html */
static const char HTML_C[] =
    "</div></div>"
    "<script>"
    "var T=null;"
    "var FDRS=["
      "{fi:4203,bi:4204,l1:4201,l2:4202,l3:4205},"
      "{fi:4213,bi:4214,l1:4211,l2:4212,l3:4215},"
      "{fi:4223,bi:4224,l1:4221,l2:4222,l3:4225},"
      "{fi:4233,bi:4234,l1:4231,l2:4232,l3:4235},"
      "{fi:4243,bi:4244,l1:4241,l2:4242,l3:4245},"
      "{fi:4253,bi:4254,l1:4251,l2:4252,l3:4255},"
      "{fi:4263,bi:4264,l1:4261,l2:4262,l3:4265},"
      "{fi:4273,bi:4274,l1:4271,l2:4272,l3:4275},"
      "{fi:4283,bi:4284,l1:4281,l2:4282,l3:4285},"
      "{fi:4293,bi:4294,l1:4291,l2:4292,l3:4295},"
      "{fi:4603,bi:4604,l1:4601,l2:4602,l3:4605},"
      "{fi:4613,bi:4614,l1:4611,l2:4612,l3:4615},"
      "{fi:4623,bi:4624,l1:4621,l2:4622,l3:4625},"
      "{fi:4633,bi:4634,l1:4631,l2:4632,l3:4635},"
      "{fi:4643,bi:4644,l1:4641,l2:4642,l3:4645},"
      "{fi:4653,bi:4654,l1:4651,l2:4652,l3:4655},"
      "{fi:4663,bi:4664,l1:4661,l2:4662,l3:4665},"
      "{fi:4673,bi:4674,l1:4671,l2:4672,l3:4675},"
      "{fi:4683,bi:4684,l1:4681,l2:4682,l3:4685},"
      "{fi:4693,bi:4694,l1:4691,l2:4692,l3:4695}"
    "];"
    "var sts=FDRS.map(function(){return{dragging:false,lastSend:0,val:0,oscLast:-1};});"
    "var row=document.getElementById('strip-row');"
    "var TPL="
      "'<div class=\"strip\" id=\"ss-_\">"
      "<div class=\"strip-lbl\" id=\"sl-_\">\xe2\x80\x94</div>"
      "<button class=\"s-btn\" id=\"sb1-_\" type=\"button\">"
      "<div class=\"s-led\" id=\"led1-_\"></div><span>1</span></button>"
      "<button class=\"s-btn\" id=\"sb2-_\" type=\"button\">"
      "<div class=\"s-led\" id=\"led2-_\"></div><span>2</span></button>"
      "<div class=\"fdr-wrap\">"
      "<div class=\"fdr-track\" id=\"ft-_\">"
      "<div class=\"fdr-fill\" id=\"ff-_\"></div></div></div>"
      "<button class=\"s-btn\" id=\"sb3-_\" type=\"button\">"
      "<div class=\"s-led\" id=\"led3-_\"></div><span>3</span></button>"
      "</div>';"
    "FDRS.forEach(function(f,i){"
      "row.innerHTML+=TPL.replace(/_/g,i);"
    "});"
    "function setLed(el,on,col,blk){"
      "if(!el)return;"
      "el.style.background=on?(col||'#fff'):'#111';"
      "el.style.boxShadow=on?'0 0 5px '+(col||'#fff'):'none';"
      "el.className='s-led'+(blk?' blinking':'');}"
    "function show(id,btn){"
      "document.querySelectorAll('.panel').forEach(function(p){p.classList.remove('active');});"
      "document.querySelectorAll('.tab').forEach(function(t){t.classList.remove('active');});"
      "document.getElementById('p-'+id).classList.add('active');"
      "btn.classList.add('active');"
      "if(id==='mon'){poll();T=setInterval(poll,300);}"
      "else if(T){clearInterval(T);T=null;}"
    "}"
    "function poll(){"
      "fetch('/api/fader').then(function(r){return r.json();})"
      ".then(function(arr){"
        "arr.forEach(function(d,i){"
          "var st=sts[i];"
          "var ss=document.getElementById('ss-'+i);"
          "if(ss)ss.style.background=d.bc||'#222';"
          "var lbl=document.getElementById('sl-'+i);"
          "if(lbl){lbl.textContent=d.n||'\xe2\x80\x94';lbl.style.color=d.tc||'#fff';}"
          "if(!st.dragging&&d.ok&&d.v!==st.oscLast){"
            "st.oscLast=d.v;"
            "var ff=document.getElementById('ff-'+i);"
            "if(ff)ff.style.height=(d.v*100).toFixed(1)+'%';}"
          "setLed(document.getElementById('led1-'+i),d.b1,d.b1c,d.b1b);"
          "setLed(document.getElementById('led2-'+i),d.b2,d.b2c,d.b2b);"
          "setLed(document.getElementById('led3-'+i),d.b3,d.b3c,false);"
        "});"
      "}).catch(function(){});}"
    "function postPress(id,val){"
      "fetch('/api/press',{method:'POST',"
      "body:'id='+id+'&val='+val,"
      "headers:{'Content-Type':'application/x-www-form-urlencoded'}"
      "}).catch(function(){});}"
    "function setupFader(track,fill,st,faderId){"
      "function getT(e){"
        "var r=track.getBoundingClientRect();"
        "if(!r.height)return 0;"
        "var cy=e.touches?e.touches[0].clientY:e.clientY,"
        "t=1-(cy-r.top)/r.height;"
        "return Math.max(0,Math.min(1,t));}"
      "function upd(t){fill.style.height=(t*100).toFixed(1)+'%';}"
      "function snd(val){"
        "var now=Date.now();"
        "if(now-st.lastSend<50)return;"
        "st.lastSend=now;"
        "fetch('/api/fader-set',{method:'POST',"
        "body:'id='+faderId+'&val='+val.toFixed(2),"
        "headers:{'Content-Type':'application/x-www-form-urlencoded'}"
        "}).catch(function(){});}"
      "function dn(e){e.preventDefault();st.dragging=true;st.val=getT(e);upd(st.val);snd(st.val*255);}"
      "function mv(e){if(!st.dragging)return;e.preventDefault();st.val=getT(e);upd(st.val);snd(st.val*255);}"
      "function up(){if(st.dragging){st.lastSend=0;snd(st.val*255);}st.dragging=false;}"
      "track.addEventListener('mousedown',dn);"
      "window.addEventListener('mousemove',mv);"
      "window.addEventListener('mouseup',up);"
      "track.addEventListener('touchstart',dn,{passive:false});"
      "window.addEventListener('touchmove',mv,{passive:false});"
      "window.addEventListener('touchend',up);"
      "window.addEventListener('touchcancel',up);}"
    "function setupBtn(el,id){"
      "function dn(e){e.preventDefault();postPress(id,1);}"
      "function up(e){e.preventDefault();postPress(id,0);}"
      "el.addEventListener('mousedown',dn);"
      "el.addEventListener('mouseup',up);"
      "el.addEventListener('mouseleave',up);"
      "el.addEventListener('touchstart',dn,{passive:false});"
      "el.addEventListener('touchend',up,{passive:false});"
      "el.addEventListener('touchcancel',up,{passive:false});}"
    "FDRS.forEach(function(f,i){"
      "setupBtn(document.getElementById('sb1-'+i),f.l1);"
      "setupBtn(document.getElementById('sb2-'+i),f.l2);"
      "setupBtn(document.getElementById('sb3-'+i),f.l3);"
      "setupFader("
        "document.getElementById('ft-'+i),"
        "document.getElementById('ff-'+i),"
        "sts[i],f.fi);"
    "});"
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
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "[");
    for (int i = 0; i < ONYX_NUM_FADERS; i++) {
        const onyx_fader_cfg_t *f = &onyx_faders[i];
        float value = 0.0f;
        char  name[64]     = "";
        char  btn_col[16]  = "";
        char  btn_tcol[16] = "";
        onyx_button_led_t led1 = {}, led2 = {}, led3 = {};

        bool valid = onyx_get_fader(f->fader, &value, NULL, 0);
        onyx_get_button_text(f->button, name, sizeof(name));
        onyx_get_button_color(f->button, btn_col, sizeof(btn_col));
        onyx_get_button_text_color(f->button, btn_tcol, sizeof(btn_tcol));
        onyx_get_button_led(f->led1, &led1);
        onyx_get_button_led(f->led2, &led2);
        onyx_get_button_led(f->led3, &led3);

        char safe_name[130] = "";
        json_esc(name, safe_name, sizeof(safe_name));

        char json[380];
        snprintf(json, sizeof(json),
            "%s{\"ok\":%s,\"v\":%.4f,\"n\":\"%s\","
            "\"bc\":\"%s\",\"tc\":\"%s\","
            "\"b1\":%d,\"b1c\":\"%s\",\"b1b\":%d,"
            "\"b2\":%d,\"b2c\":\"%s\",\"b2b\":%d,"
            "\"b3\":%d,\"b3c\":\"%s\"}",
            i > 0 ? "," : "",
            valid ? "true" : "false", (double)value, safe_name,
            btn_col[0]  ? btn_col  : "#222",
            btn_tcol[0] ? btn_tcol : "#fff",
            led1.on, led1.color[0] ? led1.color : "#111", led1.blink,
            led2.on, led2.color[0] ? led2.color : "#111", led2.blink,
            led3.on, led3.color[0] ? led3.color : "#111");
        httpd_resp_sendstr_chunk(req, json);
    }
    httpd_resp_sendstr_chunk(req, "]");
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t handler_api_fader_set(httpd_req_t *req) {
    char body[48] = {};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len > 0) {
        body[len] = '\0';
        char id_str[10] = {}, val_str[12] = {};
        parse_field(body, "id",  id_str,  sizeof(id_str));
        parse_field(body, "val", val_str, sizeof(val_str));
        int id = atoi(id_str);
        if (id > 0) onyx_send_fader(id, (float)atof(val_str));
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t handler_api_press(httpd_req_t *req) {
    char body[48] = {};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len > 0) {
        body[len] = '\0';
        char id_str[10] = {}, val_str[4] = {};
        parse_field(body, "id",  id_str,  sizeof(id_str));
        parse_field(body, "val", val_str, sizeof(val_str));
        int id  = atoi(id_str);
        int val = atoi(val_str);
        if (id > 0) onyx_send_button_press(id, val);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
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
        { .uri = "/api/fader",     .method = HTTP_GET,  .handler = handler_api_fader     },
        { .uri = "/api/fader-set", .method = HTTP_POST, .handler = handler_api_fader_set },
        { .uri = "/api/press",     .method = HTTP_POST, .handler = handler_api_press     },
        { .uri = "/settings",   .method = HTTP_POST, .handler = handler_settings   },
        { .uri = "/forget",     .method = HTTP_POST, .handler = handler_forget     },
    };
    for (int i = 0; i < (int)(sizeof(routes) / sizeof(routes[0])); i++)
        httpd_register_uri_handler(server, &routes[i]);

    ESP_LOGI(TAG, "web config server started");
}
