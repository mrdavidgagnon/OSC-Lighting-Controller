#include "i2c_scan.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "i2c_scan";

void i2c_scan_bus(void)
{
    ESP_LOGI(TAG, "Scanning I2C bus: SDA=GPIO%d  SCL=GPIO%d", I2C_SCAN_SDA, I2C_SCAN_SCL);

    i2c_master_bus_config_t cfg = {
        .i2c_port            = -1,          /* auto-select port */
        .sda_io_num          = I2C_SCAN_SDA,
        .scl_io_num          = I2C_SCAN_SCL,
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_new_master_bus(&cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Bus init failed: %s — check SDA=GPIO%d SCL=GPIO%d",
                 esp_err_to_name(err), I2C_SCAN_SDA, I2C_SCAN_SCL);
        return;
    }

    int found = 0;
    for (uint8_t addr = 0x03; addr < 0x78; addr++) {
        if (i2c_master_probe(bus, addr, 10) == ESP_OK) {
            ESP_LOGI(TAG, "  Found device at 0x%02X", addr);
            found++;
        }
    }

    if (found == 0)
        ESP_LOGW(TAG, "  No I2C devices found — verify SDA/SCL wiring and pull-ups");
    else
        ESP_LOGI(TAG, "  Scan complete: %d device(s) found", found);

    i2c_del_master_bus(bus);
}
