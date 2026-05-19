#pragma once
#include "esp_err.h"

#define ADS_I2C_ADDR 0x48

/* Start the ADS1115 polling task.  Reads AIN0-AIN3 and drives faders 1-4. */
esp_err_t ads1115_start(void);

/* Calibration: two-step procedure.
   1. Move all faders to lowest position → call ads1115_cal_set_low().
   2. Move all faders to highest position → call ads1115_cal_set_high().
   High-step saves both endpoints to NVS and activates calibration immediately.
   raw_out[4] receives the captured ADC readings (pass NULL to ignore). */
esp_err_t ads1115_cal_set_low(int raw_out[4]);
esp_err_t ads1115_cal_set_high(int raw_out[4]);
