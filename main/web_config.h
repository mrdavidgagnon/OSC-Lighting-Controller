#pragma once
#include "esp_netif.h"

typedef void (*web_config_forget_cb_t)(void);
void web_config_start(esp_netif_t *netif, web_config_forget_cb_t on_forget);
