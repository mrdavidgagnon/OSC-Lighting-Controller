#pragma once
#include <stdbool.h>
#include <stddef.h>

#define WIFI_AP_SSID       "OSC-Controller"
#define WIFI_AP_CHANNEL    6
#define WIFI_AP_MAX_CONN   4
#define WIFI_AP_IP         "192.168.4.1"

#define LED_GPIO           2

#define WIFI_NVS_NAMESPACE "wifi_cfg"
#define WIFI_NVS_KEY_SSID  "ssid"
#define WIFI_NVS_KEY_PASS  "pass"

void wifi_manager_init(void);
bool wifi_manager_load_credentials(char *ssid, size_t ssid_len,
                                   char *password, size_t pass_len);
void wifi_manager_save_credentials(const char *ssid, const char *password);
void wifi_manager_clear_credentials(void);
void wifi_manager_start_ap(void);
void wifi_manager_start_sta(const char *ssid, const char *password);
