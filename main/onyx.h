#pragma once
#include <stdint.h>
#include <stdbool.h>

#define ONYX_OSC_PORT    9000       /* default ESP32 listen port; overridden by NVS  */
#define ONYX_SERVER_PORT 8000       /* default ONYX console OSC port; overridden by NVS */
#define ONYX_FADER_MAX  255.0f      /* ONYX native fader range (DMX, 0–255)    */
#define ONYX_NVS_NS       "osc_cfg"   /* NVS namespace for OSC settings          */
#define ONYX_MDNS_HOSTNAME "onyxosc"  /* default mDNS hostname → onyxosc.local   */

/* 20 fader strips: bases 4200–4290 (faders 1–10) then 4600–4690 (faders 11–20) */
#define ONYX_NUM_FADERS 20

typedef struct {
    int fader;   /* /Mx/fader/XXXX */
    int button;  /* /Mx/button/XXXX — label, color, text color */
    int led1;    /* button LED: on/off, color, blink */
    int led2;    /* button LED: on/off, color, blink */
    int led3;    /* button LED: on/off, color only   */
} onyx_fader_cfg_t;

extern const onyx_fader_cfg_t onyx_faders[ONYX_NUM_FADERS];

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

/* OSC message log — ring buffer of last ONYX_LOG_SIZE messages */
#define ONYX_LOG_SIZE 128

typedef struct {
    uint32_t seq;
    char     addr[64];
    char     types[8];
    char     val[32];   /* stringified first argument */
} onyx_log_entry_t;

/* Returns up to max_out entries with seq > since; entries are in arrival order. */
int onyx_get_osc_log(uint32_t since, onyx_log_entry_t *out, int max_out);

/* A random value generated at each boot — changes when device resets. */
uint32_t onyx_get_boot_epoch(void);

/* Push an arbitrary event into the log (e.g. outgoing button presses). */
void onyx_log_event(const char *addr, char type, const char *val);

/* NVS-backed settings */
uint16_t onyx_load_port(void);
uint16_t onyx_load_server_port(void);
void     onyx_load_onyx_ip(char *buf, size_t len);
void     onyx_load_hostname(char *buf, size_t len);
void     onyx_save_settings(uint16_t listen_port, uint16_t server_port,
                             const char *onyx_ip, const char *hostname);

/* Send an OSC button press to the ONYX console (val=1 press, val=0 release) */
void onyx_send_button_press(int id, int val);

/* Send an OSC fader value to the ONYX console (value in ONYX native range 0–255) */
void onyx_send_fader(int id, float value);

/* Request a full state dump from ONYX */
void onyx_send_sync(void);
