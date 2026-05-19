#pragma once

/* GPIO pins for the I2C bus scan — change to match your wiring */
#define I2C_SCAN_SDA   5
#define I2C_SCAN_SCL  23

/* Scan all 7-bit addresses and log which ones respond.
   The bus is released after scanning so normal I2C drivers can reinitialise it. */
void i2c_scan_bus(void);
