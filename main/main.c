#include "wifi_manager.h"
#include "captive_portal.h"
#include "onyx.h"
#include "i2c_bus.h"
#include "mcp23017.h"
#include "ads1115.h"
#include "esp_log.h"

static const char *TAG = "main";

static void on_fader(int id, float value) {
    ESP_LOGD(TAG, "fader %d = %.3f", id, value);
}

static void on_button_text(int id, const char *text) {
    ESP_LOGD(TAG, "button %d text = \"%s\"", id, text);
}

static void on_fader_color(int id, const char *color) {
    ESP_LOGD(TAG, "fader %d color = %s", id, color);
}

void app_main(void) {
    wifi_manager_init();

    char ssid[33], password[65];
    if (wifi_manager_load_credentials(ssid, sizeof(ssid), password, sizeof(password))) {
        wifi_manager_on_connected(onyx_send_sync);
        wifi_manager_start_sta(ssid, password);
        onyx_start(on_fader, on_button_text, on_fader_color);
        i2c_bus_init();
        mcp23017_start();
        ads1115_start();
    } else {
        wifi_manager_start_ap();
        captive_portal_start();
    }
}
