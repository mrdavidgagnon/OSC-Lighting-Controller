#include "wifi_manager.h"
#include "captive_portal.h"
#include "onyx.h"
#include "esp_log.h"

static const char *TAG = "main";

static void on_fader(int id, float value) {
    ESP_LOGI(TAG, "fader %d = %.3f", id, value);
}

static void on_button_text(int id, const char *text) {
    ESP_LOGI(TAG, "button %d text = \"%s\"", id, text);
}

static void on_fader_color(int id, const char *color) {
    ESP_LOGI(TAG, "fader %d color = %s", id, color);
}

void app_main(void) {
    wifi_manager_init();

    char ssid[33], password[65];
    if (wifi_manager_load_credentials(ssid, sizeof(ssid), password, sizeof(password))) {
        wifi_manager_start_sta(ssid, password);
        onyx_start(on_fader, on_button_text, on_fader_color);
    } else {
        wifi_manager_start_ap();
        captive_portal_start();
    }
}
