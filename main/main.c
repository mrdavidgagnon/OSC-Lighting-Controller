#include "wifi_manager.h"
#include "captive_portal.h"
#include "onyx.h"
#include "i2c_bus.h"
#include "mcp23017.h"
#include "ads1115.h"
#include "oled_display.h"
#include "i2c_scan.h"
#include "esp_log.h"
#include "mdns.h"

static const char *TAG = "main";

static void on_fader(int id, float value) {
    ESP_LOGI(TAG, "fader %d = %.3f", id, value);
}

static void on_button_text(int id, const char *text) {
    ESP_LOGD(TAG, "button %d text = \"%s\"", id, text);
    if (id == onyx_faders[0].button)
        oled_display_set_fader_name(text);
}

static void on_fader_color(int id, const char *color) {
    ESP_LOGD(TAG, "fader %d color = %s", id, color);
}

void app_main(void) {
    oled_display_init();
    i2c_scan_bus();
    wifi_manager_init();

    char ssid[33], password[65];
    if (wifi_manager_load_credentials(ssid, sizeof(ssid), password, sizeof(password))) {
        wifi_manager_on_connected(onyx_send_sync);
        char hostname[64];
        onyx_load_hostname(hostname, sizeof(hostname));
        mdns_init();
        mdns_hostname_set(hostname);
        mdns_instance_name_set("OSC Lighting Controller");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
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
