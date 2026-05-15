#include "onyx.h"
#include "osc.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

static const char *TAG = "onyx";

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
} s_leds[16];
static int s_n_leds;

static int led_find_or_alloc(int id) {
    for (int i = 0; i < s_n_leds; i++)
        if (s_leds[i].id == id) return i;
    if (s_n_leds < 16) { s_leds[s_n_leds].id = id; return s_n_leds++; }
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

void onyx_save_settings(uint16_t listen_port, uint16_t server_port, const char *onyx_ip) {
    nvs_handle_t nvs;
    if (nvs_open(ONYX_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_u16(nvs, "port",     listen_port);
    nvs_set_u16(nvs, "srv_port", server_port);
    if (onyx_ip) nvs_set_str(nvs, "onyx_ip", onyx_ip);
    nvs_commit(nvs);
    nvs_close(nvs);
    ESP_LOGI(TAG, "settings saved — listen %u  server %u  ONYX IP: %s",
             listen_port, server_port, onyx_ip ? onyx_ip : "");
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
            if (cs[0]) cache_button_color(id, cs);

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

/* ---------- UDP listener ------------------------------------------------- */

static void onyx_task(void *arg) {
    uint16_t port = (uint16_t)(uintptr_t)arg;

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port        = htons(port),
    };
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "socket() failed"); vTaskDelete(NULL); return; }
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed on port %u", port);
        close(sock); vTaskDelete(NULL); return;
    }
    ESP_LOGI(TAG, "listening for OSC on UDP port %u", port);

    uint8_t buf[1024];
    while (1) {
        int len = recv(sock, buf, sizeof(buf), 0);
        if (len <= 0) continue;

        if (osc_is_bundle(buf, len)) {
            osc_bundle_iter_t it;
            osc_bundle_init(buf, len, &it);
            const uint8_t *mbuf;
            int mlen;
            while (osc_bundle_next(&it, &mbuf, &mlen)) {
                osc_msg_t msg;
                if (!osc_parse(mbuf, mlen, &msg)) continue;
                ESP_LOGD(TAG, "OSC(bundle) %s  types=%s", msg.address, msg.types);
                dispatch(msg.address, &msg);
            }
        } else {
            osc_msg_t msg;
            if (!osc_parse(buf, len, &msg)) { ESP_LOGW(TAG, "invalid OSC packet"); continue; }
            ESP_LOGD(TAG, "OSC %s  types=%s", msg.address, msg.types);
            dispatch(msg.address, &msg);
        }
    }

    close(sock);
    vTaskDelete(NULL);
}

/* ---------- public API --------------------------------------------------- */

void onyx_start(onyx_fader_cb_t on_fader,
                onyx_text_cb_t  on_button_text,
                onyx_color_cb_t on_fader_color) {
    s_fader_cb = on_fader;
    s_text_cb  = on_button_text;
    s_color_cb = on_fader_color;
    uint16_t port = onyx_load_port();
    ESP_LOGI(TAG, "starting OSC listener on port %u", port);
    xTaskCreate(onyx_task, "onyx", 4096, (void *)(uintptr_t)port, 5, NULL);
}
