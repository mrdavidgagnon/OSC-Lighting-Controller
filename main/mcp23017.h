#pragma once
#include "esp_err.h"

#define MCP_I2C_ADDR 0x20

/* Initialize I2C + MCP23017, then start the button polling task. */
esp_err_t mcp23017_start(void);
