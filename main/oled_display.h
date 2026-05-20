#pragma once

/* GPIO pins — change these to match your wiring */
#define OLED_PIN_SCK   6
#define OLED_PIN_MOSI  7
#define OLED_PIN_CS   10
#define OLED_PIN_DC    9
#define OLED_PIN_RST   8

void oled_display_init(void);
void oled_display_set_fader_name(const char *name);
void oled_display_set_local_fader(int val);  /* 0-255, updates immediately from ADC */
