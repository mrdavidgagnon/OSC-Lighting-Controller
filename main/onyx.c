#include "onyx.h"
#include "osc.h"
#include "diag.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

static const char *TAG = "onyx";

/* ---------- persistent send socket --------------------------------------- */

static int                s_send_sock = -1;
static struct sockaddr_in s_send_dest;

/* ---------- boot epoch --------------------------------------------------- */

static uint32_t s_boot_epoch;

uint32_t onyx_get_boot_epoch(void) { return s_boot_epoch; }

/* ---------- callbacks ---------------------------------------------------- */

static onyx_fader_cb_t s_fader_cb;
static onyx_text_cb_t  s_text_cb;
static onyx_color_cb_t s_color_cb;

/* ---------- state cache -------------------------------------------------- */

#define MAX_TRACKED 32

static struct {
    int   id;
    float value;
    char  color[16];
    bool  valid;
} s_faders[MAX_TRACKED];
static int s_n_faders;

static struct {
    int  id;
    char text[64];
    char color[16];
    char text_color[16];
    bool valid;
} s_buttons[MAX_TRACKED];
static int s_n_buttons;

static void cache_fader_value(int id, float v) {
    for (int i = 0; i < s_n_faders; i++) {
        if (s_faders[i].id != id) continue;
        s_faders[i].value = v;
        s_faders[i].valid = true;
        return;
    }
    if (s_n_faders < MAX_TRACKED) {
        s_faders[s_n_faders].id    = id;
        s_faders[s_n_faders].value = v;
        s_faders[s_n_faders].valid = true;
        s_faders[s_n_faders].color[0] = '\0';
        s_n_faders++;
    }
}

static void cache_fader_color(int id, const char *color) {
    for (int i = 0; i < s_n_faders; i++) {
        if (s_faders[i].id != id) continue;
        strlcpy(s_faders[i].color, color, sizeof(s_faders[i].color));
        return;
    }
    if (s_n_faders < MAX_TRACKED) {
        s_faders[s_n_faders].id = id;
        s_faders[s_n_faders].color[0] = '\0';
        strlcpy(s_faders[s_n_faders].color, color, sizeof(s_faders[s_n_faders].color));
        s_n_faders++;
    }
}

static void cache_button_text(int id, const char *text) {
    for (int i = 0; i < s_n_buttons; i++) {
        if (s_buttons[i].id != id) continue;
        strlcpy(s_buttons[i].text, text, sizeof(s_buttons[i].text));
        s_buttons[i].valid = true;
        return;
    }
    if (s_n_buttons < MAX_TRACKED) {
        s_buttons[s_n_buttons].id    = id;
        s_buttons[s_n_buttons].valid = true;
        strlcpy(s_buttons[s_n_buttons].text, text, sizeof(s_buttons[s_n_buttons].text));
        s_n_buttons++;
    }
}

bool onyx_get_fader(int id, float *value, char *color, size_t color_len) {
    for (int i = 0; i < s_n_faders; i++) {
        if (s_faders[i].id != id || !s_faders[i].valid) continue;
        if (value) *value = s_faders[i].value;
        if (color && color_len) strlcpy(color, s_faders[i].color, color_len);
        return true;
    }
    return false;
}

bool onyx_get_button_text(int id, char *text, size_t text_len) {
    for (int i = 0; i < s_n_buttons; i++) {
        if (s_buttons[i].id != id || !s_buttons[i].valid) continue;
        if (text && text_len) strlcpy(text, s_buttons[i].text, text_len);
        return true;
    }
    return false;
}

static void cache_button_color(int id, const char *color) {
    for (int i = 0; i < s_n_buttons; i++) {
        if (s_buttons[i].id != id) continue;
        strlcpy(s_buttons[i].color, color, sizeof(s_buttons[i].color));
        return;
    }
    if (s_n_buttons < MAX_TRACKED) {
        s_buttons[s_n_buttons].id = id;
        strlcpy(s_buttons[s_n_buttons].color, color, sizeof(s_buttons[s_n_buttons].color));
        s_n_buttons++;
    }
}

static void cache_button_text_color(int id, const char *color) {
    for (int i = 0; i < s_n_buttons; i++) {
        if (s_buttons[i].id != id) continue;
        strlcpy(s_buttons[i].text_color, color, sizeof(s_buttons[i].text_color));
        return;
    }
    if (s_n_buttons < MAX_TRACKED) {
        s_buttons[s_n_buttons].id = id;
        strlcpy(s_buttons[s_n_buttons].text_color, color, sizeof(s_buttons[s_n_buttons].text_color));
        s_n_buttons++;
    }
}

bool onyx_get_button_color(int id, char *color, size_t color_len) {
    for (int i = 0; i < s_n_buttons; i++) {
        if (s_buttons[i].id != id || !s_buttons[i].valid) continue;
        if (color && color_len) strlcpy(color, s_buttons[i].color, color_len);
        return true;
    }
    return false;
}

bool onyx_get_button_text_color(int id, char *color, size_t color_len) {
    for (int i = 0; i < s_n_buttons; i++) {
        if (s_buttons[i].id != id || !s_buttons[i].valid) continue;
        if (color && color_len) strlcpy(color, s_buttons[i].text_color, color_len);
        return true;
    }
    return false;
}

/* ---------- button LED state cache --------------------------------------- */

static struct {
    int  id;
    bool on;
    char color[16];
    bool blink;
    bool valid;
} s_leds[64];
static int s_n_leds;

static int led_find_or_alloc(int id) {
    for (int i = 0; i < s_n_leds; i++)
        if (s_leds[i].id == id) return i;
    if (s_n_leds < 64) { s_leds[s_n_leds].id = id; return s_n_leds++; }
    return -1;
}

static void cache_led_on(int id, bool on) {
    int i = led_find_or_alloc(id);
    if (i < 0) return;
    s_leds[i].on    = on;
    s_leds[i].valid = true;
}

static void cache_led_color(int id, const char *color) {
    int i = led_find_or_alloc(id);
    if (i < 0) return;
    strlcpy(s_leds[i].color, color, sizeof(s_leds[i].color));
    s_leds[i].valid = true;
}

static void cache_led_blink(int id, bool blink) {
    int i = led_find_or_alloc(id);
    if (i < 0) return;
    s_leds[i].blink = blink;
    s_leds[i].valid = true;
}

bool onyx_get_button_led(int id, onyx_button_led_t *out) {
    for (int i = 0; i < s_n_leds; i++) {
        if (s_leds[i].id != id) continue;
        out->on    = s_leds[i].on;
        out->blink = s_leds[i].blink;
        out->valid = s_leds[i].valid;
        strlcpy(out->color, s_leds[i].color, sizeof(out->color));
        return s_leds[i].valid;
    }
    *out = (onyx_button_led_t){};
    return false;
}

/* ---------- OSC message ring buffer -------------------------------------- */

static onyx_log_entry_t  s_log[ONYX_LOG_SIZE];
static uint32_t          s_log_next;   /* total messages written (not masked) */
static SemaphoreHandle_t s_log_mutex;

static void osc_log_push(const char *addr, const osc_msg_t *msg) {
    if (!s_log_mutex) return;
    if (xSemaphoreTake(s_log_mutex, 0) != pdTRUE) return;
    onyx_log_entry_t *e = &s_log[s_log_next % ONYX_LOG_SIZE];
    e->seq = ++s_log_next;
    strlcpy(e->addr,  addr,       sizeof(e->addr));
    strlcpy(e->types, msg->types, sizeof(e->types));
    e->val[0] = '\0';
    if      (msg->types[0] == 'f')
        snprintf(e->val, sizeof(e->val), "%.2f", (double)osc_float(msg, 0));
    else if (msg->types[0] == 'i' || msg->types[0] == 'r')
        snprintf(e->val, sizeof(e->val), "%" PRId32, osc_int(msg, 0));
    else if (msg->types[0] == 's' || msg->types[0] == 'S') {
        const char *s = osc_string(msg, 0);
        if (s) strlcpy(e->val, s, sizeof(e->val));
    }
    xSemaphoreGive(s_log_mutex);
}

int onyx_get_osc_log(uint32_t since, onyx_log_entry_t *out, int max_out) {
    if (!s_log_mutex) return 0;
    xSemaphoreTake(s_log_mutex, portMAX_DELAY);
    uint32_t total  = s_log_next;
    uint32_t oldest = (total > ONYX_LOG_SIZE) ? total - ONYX_LOG_SIZE : 0;
    int count = 0;
    for (uint32_t i = oldest; i < total && count < max_out; i++) {
        onyx_log_entry_t *e = &s_log[i % ONYX_LOG_SIZE];
        if (e->seq > since)
            out[count++] = *e;
    }
    xSemaphoreGive(s_log_mutex);
    return count;
}

void onyx_log_event(const char *addr, char type, const char *val) {
    if (!s_log_mutex) return;
    if (xSemaphoreTake(s_log_mutex, 0) != pdTRUE) return;
    onyx_log_entry_t *e = &s_log[s_log_next % ONYX_LOG_SIZE];
    e->seq     = ++s_log_next;
    e->types[0] = type;
    e->types[1] = '\0';
    strlcpy(e->addr, addr,       sizeof(e->addr));
    strlcpy(e->val,  val ? val : "", sizeof(e->val));
    xSemaphoreGive(s_log_mutex);
}

/* ---------- NVS settings ------------------------------------------------- */

uint16_t onyx_load_port(void) {
    nvs_handle_t nvs;
    uint16_t port = ONYX_OSC_PORT;
    if (nvs_open(ONYX_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u16(nvs, "port", &port);
        nvs_close(nvs);
    }
    return port;
}

uint16_t onyx_load_server_port(void) {
    nvs_handle_t nvs;
    uint16_t port = ONYX_SERVER_PORT;
    if (nvs_open(ONYX_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u16(nvs, "srv_port", &port);
        nvs_close(nvs);
    }
    return port;
}

void onyx_load_onyx_ip(char *buf, size_t len) {
    if (!buf || !len) return;
    buf[0] = '\0';
    nvs_handle_t nvs;
    if (nvs_open(ONYX_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_str(nvs, "onyx_ip", buf, &len);
        nvs_close(nvs);
    }
}

void onyx_load_hostname(char *buf, size_t len) {
    if (!buf || !len) return;
    strlcpy(buf, ONYX_MDNS_HOSTNAME, len);
    nvs_handle_t nvs;
    if (nvs_open(ONYX_NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_str(nvs, "hostname", buf, &len);
        nvs_close(nvs);
    }
}

void onyx_save_settings(uint16_t listen_port, uint16_t server_port,
                         const char *onyx_ip, const char *hostname) {
    nvs_handle_t nvs;
    if (nvs_open(ONYX_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_u16(nvs, "port",     listen_port);
    nvs_set_u16(nvs, "srv_port", server_port);
    if (onyx_ip)  nvs_set_str(nvs, "onyx_ip",  onyx_ip);
    if (hostname) nvs_set_str(nvs, "hostname", hostname);
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "settings saved — listen %u  server %u  ONYX IP: %s  hostname: %s.local",
             listen_port, server_port, onyx_ip ? onyx_ip : "", hostname ? hostname : "");
}

/* ---------- address dispatch --------------------------------------------- */

static bool addr_match(const char *addr, const char *prefix, int *id, const char **rest) {
    size_t plen = strlen(prefix);
    if (strncmp(addr, prefix, plen) != 0) return false;
    char *end;
    long v = strtol(addr + plen, &end, 10);
    if (end == addr + plen) return false;
    *id   = (int)v;
    *rest = end;
    return true;
}

static void dispatch(const char *addr, osc_msg_t *msg) {
    int id;
    const char *rest;

    if (addr_match(addr, "/Mx/fader/", &id, &rest)) {
        if (rest[0] == '\0' && msg->types[0] == 'f') {
            float v = osc_float(msg, 0);
            cache_fader_value(id, v / ONYX_FADER_MAX);   /* store as 0.0–1.0 */
            if (s_fader_cb) s_fader_cb(id, v);           /* raw value to callback */

        } else if (strcmp(rest, "/color") == 0) {
            char cs[16] = "";
            if (msg->types[0] == 's' || msg->types[0] == 'S') {
                const char *s = osc_string(msg, 0);
                if (s) strlcpy(cs, s, sizeof(cs));
            } else if (msg->types[0] == 'r' || msg->types[0] == 'i') {
                uint32_t rgba = (uint32_t)osc_int(msg, 0);
                snprintf(cs, sizeof(cs), "#%06" PRIx32, (rgba >> 8) & 0xFFFFFFu);
            }
            if (cs[0]) {
                cache_fader_color(id, cs);
                if (s_color_cb) s_color_cb(id, cs);
            }
        }

    } else if (addr_match(addr, "/Mx/button/", &id, &rest)) {
        if (strcmp(rest, "/text") == 0) {
            if (msg->types[0] == 's' || msg->types[0] == 'S') {
                const char *text = osc_string(msg, 0);
                if (text) {
                    cache_button_text(id, text);
                    if (s_text_cb) s_text_cb(id, text);
                }
            }
        } else if (strcmp(rest, "/color") == 0) {
            char cs[16] = "";
            if (msg->types[0] == 's' || msg->types[0] == 'S') {
                const char *s = osc_string(msg, 0);
                if (s) strlcpy(cs, s, sizeof(cs));
            } else if (msg->types[0] == 'r' || msg->types[0] == 'i') {
                uint32_t rgba = (uint32_t)osc_int(msg, 0);
                snprintf(cs, sizeof(cs), "#%06" PRIx32, (rgba >> 8) & 0xFFFFFFu);
            }
            if (cs[0]) {
                cache_button_color(id, cs);
                if (s_color_cb) s_color_cb(id, cs);
            }

        } else if (strcmp(rest, "/text/color") == 0) {
            char cs[16] = "";
            if (msg->types[0] == 's' || msg->types[0] == 'S') {
                const char *s = osc_string(msg, 0);
                if (s) strlcpy(cs, s, sizeof(cs));
            } else if (msg->types[0] == 'r' || msg->types[0] == 'i') {
                uint32_t rgba = (uint32_t)osc_int(msg, 0);
                snprintf(cs, sizeof(cs), "#%06" PRIx32, (rgba >> 8) & 0xFFFFFFu);
            }
            if (cs[0]) cache_button_text_color(id, cs);

        } else if (strcmp(rest, "/led") == 0) {
            if (msg->types[0] == 'i')
                cache_led_on(id, osc_int(msg, 0) != 0);

        } else if (strcmp(rest, "/led/color") == 0) {
            char cs[16] = "";
            if (msg->types[0] == 's' || msg->types[0] == 'S') {
                const char *s = osc_string(msg, 0);
                if (s) strlcpy(cs, s, sizeof(cs));
            } else if (msg->types[0] == 'r' || msg->types[0] == 'i') {
                uint32_t rgba = (uint32_t)osc_int(msg, 0);
                snprintf(cs, sizeof(cs), "#%06" PRIx32, (rgba >> 8) & 0xFFFFFFu);
            }
            if (cs[0]) cache_led_color(id, cs);

        } else if (strcmp(rest, "/led/blink") == 0) {
            if (msg->types[0] == 'i')
                cache_led_blink(id, osc_int(msg, 0) != 0);
        }
    }
}

/* ---------- UDP receiver + inline dispatch -------------------------------- */

/* Single task: recv() blocks until a packet arrives, then parses and
   dispatches it inline.  No intermediate queue or copy — the LwIP socket
   buffer (SO_RCVBUF) absorbs bursts while the task is processing. */
static void onyx_osc_task(void *arg) {
    uint16_t port = (uint16_t)(uintptr_t)arg;

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(port),
    };
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "rx: socket() failed"); vTaskDelete(NULL); return; }

    int rcvbuf = 32768;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "rx: bind() failed on port %u", port);
        close(sock); vTaskDelete(NULL); return;
    }
    ESP_LOGI(TAG, "listening for OSC on UDP port %u", port);

    static uint8_t buf[1024];
    while (1) {
        int len = recv(sock, buf, sizeof(buf), 0);
        if (len <= 0) continue;
        diag_osc_pkt();

        if (osc_is_bundle(buf, len)) {
            osc_bundle_iter_t it;
            osc_bundle_init(buf, len, &it);
            const uint8_t *mbuf;
            int mlen;
            while (osc_bundle_next(&it, &mbuf, &mlen)) {
                osc_msg_t msg;
                if (!osc_parse(mbuf, mlen, &msg)) continue;
                ESP_LOGD(TAG, "OSC(bundle) %s  types=%s", msg.address, msg.types);
                osc_log_push(msg.address, &msg);
                dispatch(msg.address, &msg);
            }
        } else {
            osc_msg_t msg;
            if (!osc_parse(buf, len, &msg)) {
                ESP_LOGW(TAG, "invalid OSC packet");
                continue;
            }
            ESP_LOGD(TAG, "OSC %s  types=%s", msg.address, msg.types);
            osc_log_push(msg.address, &msg);
            dispatch(msg.address, &msg);
        }
    }

    close(sock);
    vTaskDelete(NULL);
}

/* ---------- OSC send ----------------------------------------------------- */

void onyx_send_fader(int id, float value) {
    if (s_send_sock < 0) { ESP_LOGW(TAG, "fader: no send socket"); return; }

    char addr[32];
    snprintf(addr, sizeof(addr), "/Mx/fader/%d", id);

    uint8_t buf[64] = {};
    int pos = 0;

    int alen = (int)strlen(addr) + 1;
    memcpy(buf, addr, alen);
    pos = (alen + 3) & ~3;

    buf[pos++] = ','; buf[pos++] = 'f'; buf[pos++] = '\0';
    while (pos & 3) buf[pos++] = '\0';

    uint32_t raw;
    memcpy(&raw, &value, sizeof(raw));
    buf[pos++] = (uint8_t)(raw >> 24);
    buf[pos++] = (uint8_t)(raw >> 16);
    buf[pos++] = (uint8_t)(raw >>  8);
    buf[pos++] = (uint8_t) raw;

    sendto(s_send_sock, buf, pos, 0, (struct sockaddr *)&s_send_dest, sizeof(s_send_dest));
    ESP_LOGD(TAG, "OSC /Mx/fader/%d = %.2f", id, (double)value);
}

void onyx_send_sync(void) {
    if (s_send_sock < 0) { ESP_LOGW(TAG, "sync: no send socket"); return; }

    const char *addr = "/Mx/configuration/deviceSpace/refresh";
    uint8_t buf[64] = {};
    int pos = 0;

    int alen = (int)strlen(addr) + 1;
    memcpy(buf, addr, alen);
    pos = (alen + 3) & ~3;

    buf[pos++] = ','; buf[pos++] = 'i'; buf[pos++] = '\0';
    while (pos & 3) buf[pos++] = '\0';

    buf[pos++] = 0; buf[pos++] = 0; buf[pos++] = 0; buf[pos++] = 1; /* int32 = 1 */

    sendto(s_send_sock, buf, pos, 0, (struct sockaddr *)&s_send_dest, sizeof(s_send_dest));
    ESP_LOGI(TAG, "sync sent");
}

void onyx_send_button_press(int id, int val) {
    if (s_send_sock < 0) { ESP_LOGW(TAG, "press: no send socket"); return; }

    char addr[32];
    snprintf(addr, sizeof(addr), "/Mx/button/%d", id);

    uint8_t buf[64] = {};
    int pos = 0;

    int alen = (int)strlen(addr) + 1;
    memcpy(buf, addr, alen);
    pos = (alen + 3) & ~3;

    buf[pos++] = ','; buf[pos++] = 'i'; buf[pos++] = '\0';
    while (pos & 3) buf[pos++] = '\0';

    uint32_t v = (uint32_t)val;
    buf[pos++] = (uint8_t)(v >> 24);
    buf[pos++] = (uint8_t)(v >> 16);
    buf[pos++] = (uint8_t)(v >>  8);
    buf[pos++] = (uint8_t) v;

    sendto(s_send_sock, buf, pos, 0, (struct sockaddr *)&s_send_dest, sizeof(s_send_dest));
    ESP_LOGD(TAG, "OSC /Mx/button/%d = %d", id, val);
}

/* ---------- fader config table ------------------------------------------- */

const onyx_fader_cfg_t onyx_faders[ONYX_NUM_FADERS] = {
    /* bases 420X, X=0..9 */
    {4203, 4204, 4201, 4202, 4205},
    {4213, 4214, 4211, 4212, 4215},
    {4223, 4224, 4221, 4222, 4225},
    {4233, 4234, 4231, 4232, 4235},
    {4243, 4244, 4241, 4242, 4245},
    {4253, 4254, 4251, 4252, 4255},
    {4263, 4264, 4261, 4262, 4265},
    {4273, 4274, 4271, 4272, 4275},
    {4283, 4284, 4281, 4282, 4285},
    {4293, 4294, 4291, 4292, 4295},
    /* bases 460X, X=0..9 */
    {4603, 4604, 4601, 4602, 4605},
    {4613, 4614, 4611, 4612, 4615},
    {4623, 4624, 4621, 4622, 4625},
    {4633, 4634, 4631, 4632, 4635},
    {4643, 4644, 4641, 4642, 4645},
    {4653, 4654, 4651, 4652, 4655},
    {4663, 4664, 4661, 4662, 4665},
    {4673, 4674, 4671, 4672, 4675},
    {4683, 4684, 4681, 4682, 4685},
    {4693, 4694, 4691, 4692, 4695},
};

/* ---------- public API --------------------------------------------------- */

void onyx_start(onyx_fader_cb_t on_fader,
                onyx_text_cb_t  on_button_text,
                onyx_color_cb_t on_fader_color) {
    s_fader_cb   = on_fader;
    s_text_cb    = on_button_text;
    s_color_cb   = on_fader_color;
    s_boot_epoch = esp_random();
    s_log_mutex  = xSemaphoreCreateMutex();

    /* Build one persistent UDP send socket so fader/button sends skip NVS. */
    char onyx_ip[32] = {};
    onyx_load_onyx_ip(onyx_ip, sizeof(onyx_ip));
    uint16_t srv_port = onyx_load_server_port();
    if (onyx_ip[0]) {
        s_send_dest.sin_family = AF_INET;
        s_send_dest.sin_port   = htons(srv_port);
        if (inet_pton(AF_INET, onyx_ip, &s_send_dest.sin_addr) > 0) {
            s_send_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (s_send_sock < 0)
                ESP_LOGE(TAG, "send socket() failed");
            else
                ESP_LOGI(TAG, "send socket ready → %s:%u", onyx_ip, srv_port);
        }
    }

    uint16_t port = onyx_load_port();
    ESP_LOGI(TAG, "starting OSC listener on port %u", port);
    xTaskCreate(onyx_osc_task, "onyx_osc", 4096, (void *)(uintptr_t)port, 6, NULL);
}
