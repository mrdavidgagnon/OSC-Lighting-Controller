#pragma once

/* GPIO pins — change these to match your wiring */
#define OLED_PIN_SCK   6
#define OLED_PIN_MOSI  7
#define OLED_PIN_CS   10
#define OLED_PIN_DC    9
#define OLED_PIN_RST   8

/* ST7735S display geometry — landscape 160×80.
 * Offsets swap vs portrait because MV bit exchanges the axes.
 * Adjust if image appears shifted on your specific module. */
#define TFT_W           160
#define TFT_H           80
#define TFT_COL_OFFSET  1    /* physical row start (x-axis in landscape) */
#define TFT_ROW_OFFSET  26   /* physical col start (y-axis in landscape) */

/* MADCTL: MX=1(0x40) MV=1(0x20) BGR=1(0x08) → 90° CW landscape + BGR colour order.
 * Change BGR bit (0x08) to 0x00 if red and blue appear swapped. */
#define TFT_MADCTL      0x68

void oled_display_init(void);
void oled_display_set_fader_name(const char *name);         /* channel name from OSC */
void oled_display_set_channel_color(const char *hex_color); /* "#rrggbb" from OSC    */
