#include "wifi_manager.h"
#include "web_config.h"
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wifi_mgr";
static esp_netif_t *s_sta_netif = NULL;

static void do_forget(void) {
    wifi_manager_clear_credentials();
    esp_restart();
}

/* ---------- LED blink ---------------------------------------------------- */
/* GPIO4 on the C5 DevKit v1 is a WS2812 RGB LED that requires the RMT
   peripheral — simple gpio_set_level does not work.  Stubs until a
   led_strip driver is added. */

static void led_start(void) { }
static void led_stop(void)  { }

void wifi_manager_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
}

bool wifi_manager_load_credentials(char *ssid, size_t ssid_len,
                                   char *password, size_t pass_len) {
    nvs_handle_t nvs;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return false;
    bool ok = (nvs_get_str(nvs, WIFI_NVS_KEY_SSID, ssid, &ssid_len) == ESP_OK) &&
              (nvs_get_str(nvs, WIFI_NVS_KEY_PASS, password, &pass_len) == ESP_OK) &&
              (ssid[0] != '\0');
    nvs_close(nvs);
    return ok;
}

void wifi_manager_clear_credentials(void) {
    nvs_handle_t nvs;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "credentials cleared");
}

void wifi_manager_save_credentials(const char *ssid, const char *password) {
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(nvs_set_str(nvs, WIFI_NVS_KEY_SSID, ssid));
    ESP_ERROR_CHECK(nvs_set_str(nvs, WIFI_NVS_KEY_PASS, password));
    ESP_ERROR_CHECK(nvs_commit(nvs));
    nvs_close(nvs);
    ESP_LOGI(TAG, "credentials saved for SSID: %s", ssid);
}

#define STA_MAX_RETRIES 5

static int                s_retry_count = 0;
static wifi_connected_cb_t s_connected_cb;

void wifi_manager_on_connected(wifi_connected_cb_t cb) { s_connected_cb = cb; }

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        s_retry_count = 0;
        led_start();
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = data;
        bool auth_fail = (ev->reason == WIFI_REASON_AUTH_FAIL ||
                          ev->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                          ev->reason == WIFI_REASON_NO_AP_FOUND);
        if (auth_fail || ++s_retry_count > STA_MAX_RETRIES) {
            ESP_LOGW(TAG, "cannot connect (reason %d) — clearing credentials and rebooting to AP", ev->reason);
            wifi_manager_clear_credentials();
            esp_restart();
        }
        ESP_LOGI(TAG, "disconnected — retrying (%d/%d)", s_retry_count, STA_MAX_RETRIES);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        led_stop();
        ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        if (s_connected_cb) s_connected_cb();
    }
}

void wifi_manager_start_ap(void) {
    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    wifi_config_t cfg = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = "",
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "AP started — SSID: %s  IP: %s", WIFI_AP_SSID, WIFI_AP_IP);
    led_start();
}

void wifi_manager_start_sta(const char *ssid, const char *password) {
    s_sta_netif = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t cfg = {};
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, password, sizeof(cfg.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "STA connecting to: %s", ssid);
    web_config_start(s_sta_netif, do_forget);
}
