#include "oled_display.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdint.h>

static const char *TAG = "oled";

#define TFT_HOST  SPI2_HOST
#define C_BLACK   0x0000u
#define C_WHITE   0xFFFFu

static spi_device_handle_t s_spi;
static bool     s_ok      = false;
static uint16_t s_bg      = C_BLACK;
static char     s_name[16] = "";

/* ---------- 5×7 font, ASCII 32–126 ------------------------------------ */
/* Each entry: 5 column bytes, bit-0 = top pixel row */
static const uint8_t FONT[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* '\'' */
    {0x00,0x1C,0x22,0x41,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')' */
    {0x14,0x08,0x3E,0x08,0x14}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* '\\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 'f' */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 't' */
    {0x3C,0x40,0x40,0x40,0x3C}, /* 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* '}' */
    {0x10,0x08,0x08,0x10,0x08}, /* '~' */
};

/* ---------- SPI helpers ------------------------------------------------ */

static void IRAM_ATTR spi_pre_cb(spi_transaction_t *t)
{
    gpio_set_level(OLED_PIN_DC, (int)(uintptr_t)t->user);
}

static void tft_send(bool is_data, const uint8_t *buf, size_t len)
{
    if (!s_ok || len == 0) return;
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = buf,
        .user      = (void *)(uintptr_t)(is_data ? 1 : 0),
    };
    esp_err_t err = spi_device_transmit(s_spi, &t);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "SPI tx: %s", esp_err_to_name(err));
}

static void tft_cmd(uint8_t cmd)  { tft_send(false, &cmd, 1); }
static void tft_data1(uint8_t d)  { tft_send(true,  &d,   1); }

/* ---------- Display primitives ---------------------------------------- */

static void tft_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t caset[4] = {
        (uint8_t)((x0 + TFT_COL_OFFSET) >> 8), (uint8_t)((x0 + TFT_COL_OFFSET) & 0xFF),
        (uint8_t)((x1 + TFT_COL_OFFSET) >> 8), (uint8_t)((x1 + TFT_COL_OFFSET) & 0xFF),
    };
    uint8_t raset[4] = {
        (uint8_t)((y0 + TFT_ROW_OFFSET) >> 8), (uint8_t)((y0 + TFT_ROW_OFFSET) & 0xFF),
        (uint8_t)((y1 + TFT_ROW_OFFSET) >> 8), (uint8_t)((y1 + TFT_ROW_OFFSET) & 0xFF),
    };
    tft_cmd(0x2A); tft_send(true, caset, 4);  /* CASET */
    tft_cmd(0x2B); tft_send(true, raset, 4);  /* RASET */
    tft_cmd(0x2C);                             /* RAMWR */
}

/* Static buffer for fill and char-row sends (DMA-safe, DRAM).
 * Must hold the widest possible row: TFT_W pixels × 2 bytes. */
static uint8_t s_row_buf[TFT_W * 2];

static void tft_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (!s_ok || w == 0 || h == 0) return;
    tft_set_window(x, y, x + w - 1, y + h - 1);
    uint8_t hi = color >> 8, lo = color & 0xFF;
    int chunk = (w <= TFT_W) ? (int)w : TFT_W;
    for (int i = 0; i < chunk * 2; i += 2) { s_row_buf[i] = hi; s_row_buf[i+1] = lo; }
    for (uint16_t row = 0; row < h; row++) {
        int rem = (int)w;
        while (rem > 0) {
            int n = (rem < chunk) ? rem : chunk;
            tft_send(true, s_row_buf, (size_t)(n * 2));
            rem -= n;
        }
    }
}

/* Draw a character at N× scale. Sets a full (5N)×(7N) window and streams
 * pixel data one row at a time using s_row_buf (max 110 bytes, N≤11). */
static void tft_draw_char_scaled(uint16_t x, uint16_t y, char c, int N,
                                  uint16_t fg, uint16_t bg)
{
    if (c < 32 || c > 126) c = '?';
    const uint8_t *g = FONT[(uint8_t)c - 32];
    int w = 5 * N, h = 7 * N;
    tft_set_window(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
    for (int r = 0; r < h; r++) {
        int glyph_row = r / N;
        for (int ci = 0; ci < w; ci++) {
            uint16_t color = (g[ci / N] & (1 << glyph_row)) ? fg : bg;
            s_row_buf[ci * 2]     = (uint8_t)(color >> 8);
            s_row_buf[ci * 2 + 1] = (uint8_t)(color & 0xFF);
        }
        tft_send(true, s_row_buf, (size_t)(w * 2));
    }
}

/* Pick the largest integer scale N such that:
 *   all L chars fit within TFT_W  (5*L*N + L-1 ≤ TFT_W)
 *   the char height fits TFT_H    (7*N ≤ TFT_H) */
static int pick_scale(int len)
{
    if (len <= 0) return 1;
    for (int n = TFT_H / 7; n >= 1; n--) {
        if (5 * len * n + (len - 1) <= TFT_W)
            return n;
    }
    return 1;
}

/* Parse "#rrggbb" or "rrggbb" → RGB565. Accounts for BGR MADCTL bit. */
static uint16_t parse_color(const char *hex)
{
    if (!hex) return C_BLACK;
    const char *p = (*hex == '#') ? hex + 1 : hex;
    if (strlen(p) < 6) return C_BLACK;
    uint8_t r = 0, g = 0, b = 0;
    for (int i = 0; i < 6; i++) {
        char c = p[i];
        uint8_t nib;
        if      (c >= '0' && c <= '9') nib = (uint8_t)(c - '0');
        else if (c >= 'a' && c <= 'f') nib = (uint8_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') nib = (uint8_t)(c - 'A' + 10);
        else return C_BLACK;
        if      (i < 2) r = (uint8_t)((r << 4) | nib);
        else if (i < 4) g = (uint8_t)((g << 4) | nib);
        else            b = (uint8_t)((b << 4) | nib);
    }
#if (TFT_MADCTL) & 0x08
    /* BGR mode: bits[15:11]=B, bits[10:5]=G, bits[4:0]=R */
    return (uint16_t)(((uint16_t)(b >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (r >> 3));
#else
    return (uint16_t)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (b >> 3));
#endif
}

/* ---------- Internal redraw ------------------------------------------- */

static void redraw(void)
{
    if (!s_ok) return;
    tft_fill_rect(0, 0, TFT_W, TFT_H, s_bg);

    int len = (int)strlen(s_name);
    if (len == 0) return;

    int scale   = pick_scale(len);
    int text_w  = 5 * len * scale + (len - 1);
    int text_h  = 7 * scale;
    uint16_t x  = (uint16_t)((TFT_W - text_w) / 2);
    uint16_t y  = (uint16_t)((TFT_H - text_h) / 2);

    for (int i = 0; i < len; i++) {
        tft_draw_char_scaled(x, y, s_name[i], scale, C_WHITE, s_bg);
        x = (uint16_t)(x + 5 * scale + 1);
    }
}

/* ---------- Public API ------------------------------------------------- */

void oled_display_init(void)
{
    ESP_LOGI(TAG, "ST7735S landscape init: SCK=GPIO%d MOSI=GPIO%d CS=GPIO%d DC=GPIO%d RST=GPIO%d",
             OLED_PIN_SCK, OLED_PIN_MOSI, OLED_PIN_CS, OLED_PIN_DC, OLED_PIN_RST);

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << OLED_PIN_DC) | (1ULL << OLED_PIN_RST),
        .mode         = GPIO_MODE_OUTPUT,
    };
    if (gpio_config(&io) != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed");
        return;
    }

    gpio_set_level(OLED_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(OLED_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    spi_bus_config_t buscfg = {
        .mosi_io_num     = OLED_PIN_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = OLED_PIN_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = TFT_W * TFT_H * 2,
    };
    if (spi_bus_initialize(TFT_HOST, &buscfg, SPI_DMA_CH_AUTO) != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed");
        return;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num   = OLED_PIN_CS,
        .queue_size     = 1,
        .pre_cb         = spi_pre_cb,
    };
    if (spi_bus_add_device(TFT_HOST, &devcfg, &s_spi) != ESP_OK) {
        ESP_LOGE(TAG, "SPI add device failed");
        spi_bus_free(TFT_HOST);
        return;
    }

    s_ok = true;

    tft_cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150)); /* SWRESET */
    tft_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120)); /* SLPOUT  */

    tft_cmd(0xB1); tft_data1(0x05); tft_data1(0x3C); tft_data1(0x3C);  /* FRMCTR1 */
    tft_cmd(0xB2); tft_data1(0x05); tft_data1(0x3C); tft_data1(0x3C);  /* FRMCTR2 */
    tft_cmd(0xB3);
        tft_data1(0x05); tft_data1(0x3C); tft_data1(0x3C);             /* FRMCTR3 */
        tft_data1(0x05); tft_data1(0x3C); tft_data1(0x3C);
    tft_cmd(0xB4); tft_data1(0x03);                                      /* INVCTR  */
    tft_cmd(0xC0); tft_data1(0x28); tft_data1(0x08); tft_data1(0x04);   /* PWCTR1  */
    tft_cmd(0xC1); tft_data1(0xC0);                                      /* PWCTR2  */
    tft_cmd(0xC2); tft_data1(0x0D); tft_data1(0x00);                    /* PWCTR3  */
    tft_cmd(0xC3); tft_data1(0x8D); tft_data1(0x6A);                    /* PWCTR4  */
    tft_cmd(0xC4); tft_data1(0x8D); tft_data1(0xEE);                    /* PWCTR5  */
    tft_cmd(0xC5); tft_data1(0x0E);                                      /* VMCTR1  */
    tft_cmd(0x36); tft_data1(TFT_MADCTL);                                 /* MADCTL  */
    tft_cmd(0x3A); tft_data1(0x05);                                       /* COLMOD 16-bit */

    static const uint8_t gpos[16] = {
        0x10,0x0E,0x02,0x03,0x0E,0x07,0x02,0x07,
        0x0A,0x12,0x27,0x37,0x00,0x0D,0x0E,0x10};
    tft_cmd(0xE0); tft_send(true, gpos, 16);                             /* GMCTRP1 */
    static const uint8_t gneg[16] = {
        0x10,0x0E,0x03,0x03,0x0F,0x06,0x02,0x08,
        0x0A,0x13,0x26,0x36,0x00,0x0D,0x0E,0x10};
    tft_cmd(0xE1); tft_send(true, gneg, 16);                             /* GMCTRN1 */

    tft_cmd(0x13); vTaskDelay(pdMS_TO_TICKS(10));                        /* NORON   */
    tft_cmd(0x29); vTaskDelay(pdMS_TO_TICKS(100));                       /* DISPON  */

    /* Show startup state — replaced by first OSC name/color update */
    strncpy(s_name, "OSC", sizeof(s_name) - 1);
    s_name[sizeof(s_name) - 1] = '\0';
    redraw();
    ESP_LOGI(TAG, "ST7735S %dx%d landscape OK", TFT_W, TFT_H);
}

void oled_display_set_fader_name(const char *name)
{
    strncpy(s_name, name ? name : "", sizeof(s_name) - 1);
    s_name[sizeof(s_name) - 1] = '\0';
    ESP_LOGI(TAG, "name -> \"%s\"", s_name);
    redraw();
}

void oled_display_set_channel_color(const char *hex_color)
{
    s_bg = parse_color(hex_color);
    ESP_LOGI(TAG, "color -> %s (0x%04X)", hex_color ? hex_color : "null", s_bg);
    redraw();
}
