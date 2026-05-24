#pragma once
#include <stdint.h>

/* Lightweight per-subsystem counters.  Call from any task; reads are approximate
   (single-core ESP32-C5, no atomics needed for monitoring purposes). */

void diag_i2c_count(int ok);   /* ok=1 success, 0=error */
void diag_spi_count(int ok);
void diag_osc_pkt(void);

typedef struct {
    uint32_t i2c_ok,   i2c_err;
    uint32_t spi_ok,   spi_err;
    uint32_t osc_pkts;
    int32_t  wifi_rssi;
    uint8_t  wifi_ch;
    uint32_t heap_free, heap_min;
    uint32_t uptime_s;
    uint32_t cpu_mhz;
} diag_snapshot_t;

void diag_snapshot(diag_snapshot_t *out);
