#include "i2c_bus.h"
#include "esp_log.h"

static const char              *TAG = "i2c_bus";
static i2c_master_bus_handle_t  s_bus;

esp_err_t i2c_bus_init(void) {
    i2c_master_bus_config_t cfg = {
        .i2c_port                   = I2C_NUM_0,
        .sda_io_num                 = I2C_BUS_SDA_PIN,
        .scl_io_num                 = I2C_BUS_SCL_PIN,
        .clk_source                 = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt          = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&cfg, &s_bus);
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(ret));
    else
        ESP_LOGI(TAG, "ready (SDA=%d SCL=%d)", I2C_BUS_SDA_PIN, I2C_BUS_SCL_PIN);
    return ret;
}

i2c_master_bus_handle_t i2c_bus_handle(void) { return s_bus; }
