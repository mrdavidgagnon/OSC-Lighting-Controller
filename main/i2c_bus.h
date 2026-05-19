#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"

#define I2C_BUS_SDA_PIN 6
#define I2C_BUS_SCL_PIN 7

esp_err_t               i2c_bus_init(void);
i2c_master_bus_handle_t i2c_bus_handle(void);
