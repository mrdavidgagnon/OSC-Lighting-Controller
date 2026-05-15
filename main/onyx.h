#pragma once
#include <stdint.h>
#include <stdbool.h>

#define ONYX_OSC_PORT    9000       /* default ESP32 listen port; overridden by NVS  */
#define ONYX_SERVER_PORT 8000       /* default ONYX console OSC port; overridden by NVS */
#define ONYX_FADER_MAX  255.0f      /* ONYX native fader range (DMX, 0–255)    */
#define ONYX_NVS_NS     "osc_cfg"   /* NVS namespace for OSC settings          */

/* ONYX object IDs to monitor — adjust to match your patch */
#define ONYX_FADER_ID   4203
#define ONYX_BUTTON_ID  4204

/* Button LED IDs associated with the fader */
#define ONYX_BTN_LED_1  4201   /* on/off, color, blink */
#define ONYX_BTN_LED_2  4202   /* on/off, color, blink */
#define ONYX_BTN_LED_3  4205   /* on/off, color only   */

typedef struct {
    bool on;
    char color[16];
    bool blink;
    bool valid;
} onyx_button_led_t;

bool onyx_get_button_led(int id, onyx_button_led_t *out);

typedef void (*onyx_fader_cb_t)(int id, float value);
typedef void (*onyx_text_cb_t)(int id, const char *text);
typedef void (*onyx_color_cb_t)(int id, const char *color);

/* Start the OSC listener — port is loaded from NVS, defaulting to ONYX_OSC_PORT */
void onyx_start(onyx_fader_cb_t on_fader,
                onyx_text_cb_t  on_button_text,
                onyx_color_cb_t on_fader_color);

/* Query cached state (returns false if no data has arrived yet) */
bool onyx_get_fader(int id, float *value, char *color, size_t color_len);
bool onyx_get_button_text(int id, char *text, size_t text_len);
bool onyx_get_button_color(int id, char *color, size_t color_len);
bool onyx_get_button_text_color(int id, char *color, size_t color_len);

/* NVS-backed settings */
uint16_t onyx_load_port(void);
uint16_t onyx_load_server_port(void);
void     onyx_load_onyx_ip(char *buf, size_t len);
void     onyx_save_settings(uint16_t listen_port, uint16_t server_port, const char *onyx_ip);
