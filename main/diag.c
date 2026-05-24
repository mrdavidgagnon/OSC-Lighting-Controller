#include "diag.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

static volatile uint32_t s_i2c_ok, s_i2c_err;
static volatile uint32_t s_spi_ok, s_spi_err;
static volatile uint32_t s_osc_pkts;

void diag_i2c_count(int ok) { if (ok) s_i2c_ok++; else s_i2c_err++; }
void diag_spi_count(int ok) { if (ok) s_spi_ok++; else s_spi_err++; }
void diag_osc_pkt(void)     { s_osc_pkts++; }

void diag_snapshot(diag_snapshot_t *out) {
    out->i2c_ok   = s_i2c_ok;
    out->i2c_err  = s_i2c_err;
    out->spi_ok   = s_spi_ok;
    out->spi_err  = s_spi_err;
    out->osc_pkts = s_osc_pkts;
    out->heap_free = esp_get_free_heap_size();
    out->heap_min  = esp_get_minimum_free_heap_size();
    out->uptime_s  = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    out->cpu_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;

    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        out->wifi_rssi = ap.rssi;
        out->wifi_ch   = ap.primary;
    } else {
        out->wifi_rssi = 0;
        out->wifi_ch   = 0;
    }
}
