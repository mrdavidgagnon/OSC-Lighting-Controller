#include "ads1115.h"
#include "i2c_bus.h"
#include "onyx.h"
#include "oled_display.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "ads1115";

#define REG_CONV   0x00
#define REG_CFG    0x01
#define CFG_LOW    0x83   /* DR=128SPS, comparator disabled */
#define ADS_NVS_NS "ads_cal"

static const int s_fader_id[4] = {4203, 4213, 4223, 4233};
static i2c_master_dev_handle_t s_dev;

/* ---------- calibration state -------------------------------------------- */

static volatile int s_last_raw[4];  /* updated each conversion, read by cal functions */
static int  s_cal_min[4];
static int  s_cal_max[4];
static bool s_cal_valid;

/* EMA smoothing applied to raw ADC before calibration mapping.
   α=0.3 → ~87 ms time constant at 32 effective SPS per channel. */
#define EMA_ALPHA 0.3f
static float s_ema[4];
static bool  s_ema_init[4];

static void cal_load(void) {
    nvs_handle_t nvs;
    if (nvs_open(ADS_NVS_NS, NVS_READONLY, &nvs) != ESP_OK) return;
    uint8_t valid = 0;
    nvs_get_u8(nvs, "valid", &valid);
    if (valid) {
        char key[8];
        for (int i = 0; i < 4; i++) {
            uint16_t lo = 0, hi = 32767;
            snprintf(key, sizeof(key), "lo_%d", i);
            nvs_get_u16(nvs, key, &lo);
            snprintf(key, sizeof(key), "hi_%d", i);
            nvs_get_u16(nvs, key, &hi);
            s_cal_min[i] = lo;
            s_cal_max[i] = hi;
        }
        s_cal_valid = true;
        ESP_LOGI(TAG, "calibration loaded: lo=%d/%d/%d/%d  hi=%d/%d/%d/%d",
                 s_cal_min[0], s_cal_min[1], s_cal_min[2], s_cal_min[3],
                 s_cal_max[0], s_cal_max[1], s_cal_max[2], s_cal_max[3]);
    }
    nvs_close(nvs);
}

static void cal_save(void) {
    nvs_handle_t nvs;
    if (nvs_open(ADS_NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) return;
    char key[8];
    for (int i = 0; i < 4; i++) {
        snprintf(key, sizeof(key), "lo_%d", i);
        nvs_set_u16(nvs, key, (uint16_t)s_cal_min[i]);
        snprintf(key, sizeof(key), "hi_%d", i);
        nvs_set_u16(nvs, key, (uint16_t)s_cal_max[i]);
    }
    nvs_set_u8(nvs, "valid", 1);
    nvs_commit(nvs);
    nvs_close(nvs);
}

/* Map a smoothed (float) raw reading to a float 0–255 output.
   Falls back to linear when uncalibrated. 2% dead zone at each end. */
static float apply_cal_f(int ch, float raw) {
    if (!s_cal_valid)
        return raw * 255.0f / 32767.0f;

    float mn    = (float)s_cal_min[ch];
    float mx    = (float)s_cal_max[ch];
    float range = mx - mn;
    if (range < 10.0f) return 0.0f;

    float dead = range * 0.02f;
    if (raw <= mn + dead) return 0.0f;
    if (raw >= mx - dead) return 255.0f;

    float adj = raw - (mn + dead);
    return adj * 255.0f / (range - 2.0f * dead);
}

/* ---------- I2C helpers -------------------------------------------------- */

static esp_err_t start_conversion(int ch) {
    uint8_t cfg_hi = 0x80 | ((uint8_t)(4 + ch) << 4) | 0x03;
    uint8_t buf[3] = {REG_CFG, cfg_hi, CFG_LOW};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 50);
}

static esp_err_t read_conversion(int16_t *out) {
    uint8_t ptr = REG_CONV;
    uint8_t data[2];
    esp_err_t ret = i2c_master_transmit_receive(s_dev, &ptr, 1, data, 2, 50);
    if (ret == ESP_OK)
        *out = (int16_t)((data[0] << 8) | data[1]);
    return ret;
}

static esp_err_t wait_ready(void) {
    uint8_t ptr = REG_CFG;
    uint8_t cfg[2];
    for (int i = 0; i < 20; i++) {
        vTaskDelay(1);
        if (i2c_master_transmit_receive(s_dev, &ptr, 1, cfg, 2, 50) == ESP_OK
                && (cfg[0] & 0x80))
            return ESP_OK;
    }
    return ESP_ERR_TIMEOUT;
}

/* ---------- polling task ------------------------------------------------- */

static void ads_task(void *arg) {
    float last_sent_f[4] = {-999.0f, -999.0f, -999.0f, -999.0f};
    int   last_disp       = -1;

    while (1) {
        for (int ch = 0; ch < 4; ch++) {
            if (start_conversion(ch) != ESP_OK) continue;
            if (wait_ready()          != ESP_OK) continue;

            int16_t raw;
            if (read_conversion(&raw) != ESP_OK) continue;

            if (raw < 0) raw = 0;
            s_last_raw[ch] = raw;

            if (!s_ema_init[ch]) {
                s_ema[ch]      = (float)raw;
                s_ema_init[ch] = true;
            } else {
                s_ema[ch] += EMA_ALPHA * ((float)raw - s_ema[ch]);
            }

            float out_f = apply_cal_f(ch, s_ema[ch]);

            /* Update OLED immediately for ch0 — no waiting for OSC round-trip. */
            if (ch == 0) {
                int disp_val = (int)(out_f + 0.5f);
                if (disp_val < 0)   disp_val = 0;
                if (disp_val > 255) disp_val = 255;
                if (disp_val != last_disp) {
                    oled_display_set_local_fader(disp_val);
                    last_disp = disp_val;
                }
            }

            if (fabsf(out_f - last_sent_f[ch]) >= 1.0f) {
                last_sent_f[ch] = out_f;
                int val = (int)(out_f + 0.5f);
                if (val < 0)   val = 0;
                if (val > 255) val = 255;
                onyx_send_fader(s_fader_id[ch], (float)val);
                char addr[32], val_str[16];
                snprintf(addr,    sizeof(addr),    "/Mx/fader/%d", s_fader_id[ch]);
                snprintf(val_str, sizeof(val_str), "%d", val);
                onyx_log_event(addr, 'f', val_str);
                ESP_LOGI(TAG, "ch%d fader %d = %d (raw=%d)", ch, s_fader_id[ch], val, (int)raw);
            }
        }
    }
}

/* ---------- calibration API ---------------------------------------------- */

esp_err_t ads1115_cal_set_low(int raw_out[4]) {
    for (int i = 0; i < 4; i++) {
        s_cal_min[i] = s_last_raw[i];
        if (raw_out) raw_out[i] = s_last_raw[i];
    }
    ESP_LOGI(TAG, "cal low captured: %d %d %d %d",
             s_cal_min[0], s_cal_min[1], s_cal_min[2], s_cal_min[3]);
    return ESP_OK;
}

esp_err_t ads1115_cal_set_high(int raw_out[4]) {
    for (int i = 0; i < 4; i++) {
        s_cal_max[i] = s_last_raw[i];
        if (raw_out) raw_out[i] = s_last_raw[i];
    }
    s_cal_valid = true;
    cal_save();
    ESP_LOGI(TAG, "cal high captured: %d %d %d %d — saved",
             s_cal_max[0], s_cal_max[1], s_cal_max[2], s_cal_max[3]);
    return ESP_OK;
}

/* ---------- startup ------------------------------------------------------- */

esp_err_t ads1115_start(void) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ADS_I2C_ADDR,
        .scl_speed_hz    = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(ret));
        return ret;
    }
    cal_load();
    ESP_LOGI(TAG, "ADS1115 at 0x%02X ready", ADS_I2C_ADDR);
    xTaskCreate(ads_task, "ads1115", 2048, NULL, 5, NULL);
    return ESP_OK;
}
