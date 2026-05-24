#include "mcp23017.h"
#include "i2c_bus.h"
#include "onyx.h"
#include "diag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mcp23017";

/* MCP23017 register addresses (IOCON.BANK = 0, the power-on default) */
#define REG_IODIRA  0x00
#define REG_IODIRB  0x01
#define REG_GPPUA   0x0C
#define REG_GPPUB   0x0D
#define REG_GPIOA   0x12
#define REG_GPIOB   0x13

/* OSC button IDs for pins 0-15 (active-low; 0 = unused).
   Sequential: fader 1 btn1/2/3, fader 2 btn1/2/3, ... fader 5 btn1/2/3, unused. */
static const int s_pin_id[16] = {
    4201, 4202, 4205,   /* fader 1 */
    4211, 4212, 4215,   /* fader 2 */
    4221, 4222, 4225,   /* fader 3 */
    4231, 4232, 4235,   /* fader 4 */
    4241, 4242, 4245,   /* fader 5 */
    0,                  /* GPB7 — unused */
};

static i2c_master_dev_handle_t s_dev;

static esp_err_t write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    esp_err_t ret = i2c_master_transmit(s_dev, buf, sizeof(buf), 10);
    diag_i2c_count(ret == ESP_OK);
    return ret;
}

static esp_err_t read_reg(uint8_t reg, uint8_t *out) {
    esp_err_t ret = i2c_master_transmit_receive(s_dev, &reg, 1, out, 1, 10);
    diag_i2c_count(ret == ESP_OK);
    return ret;
}

/* Returns 16-bit GPIO state: bits 0-7 = GPIOA, bits 8-15 = GPIOB.
   A low bit means the button is pressed (active-low with pull-ups). */
static uint16_t read_all(void) {
    uint8_t a = 0xFF, b = 0xFF;
    read_reg(REG_GPIOA, &a);
    read_reg(REG_GPIOB, &b);
    return (uint16_t)a | ((uint16_t)b << 8);
}

static void mcp_task(void *arg) {
    uint16_t stable = read_all();
    uint16_t prev   = stable;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(20));
        uint16_t cur = read_all();

        /* Require two consecutive identical readings before acting (debounce). */
        uint16_t settled = ~(cur ^ prev);           /* bits unchanged since last poll */
        uint16_t changed = (cur ^ stable) & settled; /* newly-settled bits that differ from stable */

        for (int i = 0; i < 16; i++) {
            if (!(changed & (1u << i))) continue;
            if (!s_pin_id[i]) continue;
            int pressed = !(cur & (1u << i));       /* active-low: 0 = pressed */
            int val = pressed ? 1 : 0;
            onyx_send_button_press(s_pin_id[i], val);
            char addr[32], val_str[4];
            snprintf(addr,    sizeof(addr),    "/Mx/button/%d", s_pin_id[i]);
            snprintf(val_str, sizeof(val_str), "%d", val);
            onyx_log_event(addr, 'i', val_str);
            ESP_LOGI(TAG, "pin %d id=%d %s", i, s_pin_id[i], pressed ? "press" : "release");
        }

        stable = (stable & ~changed) | (cur & changed);
        prev   = cur;
    }
}

esp_err_t mcp23017_start(void) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = MCP_I2C_ADDR,
        .scl_speed_hz    = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle(), &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* All 16 pins: inputs with internal pull-ups */
    if ((ret = write_reg(REG_IODIRA, 0xFF)) != ESP_OK) goto fail;
    if ((ret = write_reg(REG_IODIRB, 0xFF)) != ESP_OK) goto fail;
    if ((ret = write_reg(REG_GPPUA,  0xFF)) != ESP_OK) goto fail;
    if ((ret = write_reg(REG_GPPUB,  0xFF)) != ESP_OK) goto fail;

    ESP_LOGI(TAG, "MCP23017 at 0x%02X ready", MCP_I2C_ADDR);

    xTaskCreate(mcp_task, "mcp23017", 2048, NULL, 5, NULL);
    return ESP_OK;

fail:
    ESP_LOGE(TAG, "MCP23017 register init failed: %s", esp_err_to_name(ret));
    return ret;
}
