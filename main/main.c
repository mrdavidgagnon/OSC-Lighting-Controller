#include "wifi_manager.h"
#include "captive_portal.h"

void app_main(void) {
    wifi_manager_init();

    char ssid[33], password[65];
    if (wifi_manager_load_credentials(ssid, sizeof(ssid), password, sizeof(password))) {
        wifi_manager_start_sta(ssid, password);
    } else {
        wifi_manager_start_ap();
        captive_portal_start();
    }
}
