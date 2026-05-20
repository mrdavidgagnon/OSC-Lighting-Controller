#include "oled_display.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdint.h>

static const char *TAG = "oled";

#define OLED_HOST   SPI2_HOST
#define OLED_W      128
#define OLED_PAGES    8

static spi_device_handle_t s_spi;

/* ---------- 5x7 font, ASCII 32–126 -------------------------------------- */
/* Each entry: 5 column bytes, LSB = top pixel row */
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

/* ---------- SSD1306 low-level ------------------------------------------- */

static void IRAM_ATTR spi_pre_cb(spi_transaction_t *t)
{
    gpio_set_level(OLED_PIN_DC, (int)(uintptr_t)t->user);
}

static bool s_ok = false;

static void oled_send(bool is_data, const uint8_t *buf, size_t len)
{
    if (!s_ok) return;
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = buf,
        .user      = (void *)(uintptr_t)(is_data ? 1 : 0),
    };
    esp_err_t err = spi_device_transmit(s_spi, &t);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "SPI tx failed: %s", esp_err_to_name(err));
}

static void oled_cmd(uint8_t cmd)
{
    oled_send(false, &cmd, 1);
}

static void oled_set_pos(uint8_t page, uint8_t col)
{
    oled_cmd(0xB0 | (page & 0x07));
    oled_cmd(0x00 | (col & 0x0F));
    oled_cmd(0x10 | ((col >> 4) & 0x07));
}

static void oled_fill_page(uint8_t page, uint8_t fill)
{
    static uint8_t row[OLED_W];
    memset(row, fill, OLED_W);
    oled_set_pos(page, 0);
    oled_send(true, row, OLED_W);
}

static void oled_draw_string(uint8_t page, const char *s)
{
    uint8_t col = 0;
    while (*s && col + 6 <= OLED_W) {
        uint8_t buf[6];
        char c = *s++;
        if (c < 32 || c > 126) c = '?';
        memcpy(buf, FONT[(uint8_t)c - 32], 5);
        buf[5] = 0x00;
        oled_set_pos(page, col);
        oled_send(true, buf, 6);
        col += 6;
    }
}

/* Expand each bit in a nibble to two bits: 0b0101 → 0b00110011 */
static const uint8_t EXPAND[16] = {
    0x00, 0x03, 0x0C, 0x0F,
    0x30, 0x33, 0x3C, 0x3F,
    0xC0, 0xC3, 0xCC, 0xCF,
    0xF0, 0xF3, 0xFC, 0xFF,
};

/* 2x scaled character: 10px wide × 16px tall (2 pages).
   Each original column is doubled; each row-bit is doubled across two pages. */
static void oled_draw_char_2x(uint8_t page, uint8_t col, char c)
{
    if (c < 32 || c > 126) c = '?';
    const uint8_t *g = FONT[(uint8_t)c - 32];
    uint8_t p0[10], p1[10];
    for (int i = 0; i < 5; i++) {
        uint8_t lo = EXPAND[g[i] & 0x0F];       /* lower 4 rows → page 0 */
        uint8_t hi = EXPAND[(g[i] >> 4) & 0x0F]; /* upper 4 rows → page 1 */
        p0[i * 2] = p0[i * 2 + 1] = lo;
        p1[i * 2] = p1[i * 2 + 1] = hi;
    }
    oled_set_pos(page,     col); oled_send(true, p0, 10);
    oled_set_pos(page + 1, col); oled_send(true, p1, 10);
}

static void oled_draw_string_2x(uint8_t page, const char *s)
{
    uint8_t col = 0;
    while (*s && col + 10 <= OLED_W)
        oled_draw_char_2x(page, col, *s++), col += 10;
}

/* ---------- public API --------------------------------------------------- */

void oled_display_init(void)
{
    ESP_LOGI(TAG, "SPI OLED init: SCK=GPIO%d MOSI=GPIO%d CS=GPIO%d DC=GPIO%d RST=GPIO%d",
             OLED_PIN_SCK, OLED_PIN_MOSI, OLED_PIN_CS, OLED_PIN_DC, OLED_PIN_RST);

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << OLED_PIN_DC) | (1ULL << OLED_PIN_RST),
        .mode         = GPIO_MODE_OUTPUT,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s — check DC=GPIO%d RST=GPIO%d",
                 esp_err_to_name(err), OLED_PIN_DC, OLED_PIN_RST);
        return;
    }

    gpio_set_level(OLED_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(OLED_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    spi_bus_config_t buscfg = {
        .mosi_io_num     = OLED_PIN_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = OLED_PIN_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = OLED_W * OLED_PAGES,
    };
    err = spi_bus_initialize(OLED_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s — check SCK=GPIO%d MOSI=GPIO%d",
                 esp_err_to_name(err), OLED_PIN_SCK, OLED_PIN_MOSI);
        return;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 8 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num   = OLED_PIN_CS,
        .queue_size     = 1,
        .pre_cb         = spi_pre_cb,
    };
    err = spi_bus_add_device(OLED_HOST, &devcfg, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI add device failed: %s — check CS=GPIO%d",
                 esp_err_to_name(err), OLED_PIN_CS);
        spi_bus_free(OLED_HOST);
        return;
    }

    s_ok = true;

    static const uint8_t init_cmds[] = {
        0xAE,       /* display off           */
        0xD5, 0x80, /* clock div             */
        0xA8, 0x3F, /* multiplex 1/64        */
        0xD3, 0x00, /* display offset        */
        0x40,       /* start line 0          */
        0x8D, 0x14, /* charge pump on        */
        0x20, 0x00, /* horizontal addr mode  */
        0xA1,       /* seg remap             */
        0xC8,       /* com scan dec          */
        0xDA, 0x12, /* com pins config       */
        0x81, 0xCF, /* contrast              */
        0xD9, 0xF1, /* pre-charge period     */
        0xDB, 0x40, /* vcomh deselect level  */
        0xA4,       /* display from RAM      */
        0xA6,       /* normal (not inverted) */
        0xAF,       /* display on            */
    };
    for (size_t i = 0; i < sizeof(init_cmds); i++)
        oled_cmd(init_cmds[i]);

    for (int p = 0; p < OLED_PAGES; p++)
        oled_fill_page(p, 0x00);

    oled_draw_string(0, "FADER 1");
    oled_fill_page(1, 0x08); /* thin separator line */

    ESP_LOGI(TAG, "SSD1306 initialized OK");
}

void oled_display_set_fader_name(const char *name)
{
    oled_fill_page(2, 0x00);
    oled_fill_page(3, 0x00);

    if (!name || !*name) return;

    char buf[13];
    strncpy(buf, name, 12);
    buf[12] = '\0';

    oled_draw_string_2x(2, buf);
    ESP_LOGI(TAG, "name -> \"%s\"", name);
}

/* Draw a vertical fader track in pages 4-7 (bottom 32 px).
   A 2-pixel-thick horizontal line marks the current position.
   val=255 → line near top; val=0 → line near bottom. */
void oled_display_set_local_fader(int val)
{
    if (!s_ok) return;
    if (val < 0)   val = 0;
    if (val > 255) val = 255;

    /* Map 0-255 to row 1-29 within the 32-row zone (rows 0 and 31 are border).
       2-px line, so start row 1 (val=255) … 28 (val=0). */
    int line_row = 1 + (27 * (255 - val) / 255);

    uint8_t buf[OLED_W];
    for (int p = 0; p < 4; p++) {
        /* Build the column byte for non-border columns this page. */
        uint8_t col_byte = 0;
        for (int bit = 0; bit < 8; bit++) {
            int row = p * 8 + bit;
            if (row == 0 || row == 31)                   col_byte |= (1 << bit); /* h-border */
            if (row == line_row || row == line_row + 1)  col_byte |= (1 << bit); /* marker   */
        }

        for (int x = 0; x < OLED_W; x++)
            buf[x] = (x == 0 || x == OLED_W - 1) ? 0xFF : col_byte;

        oled_set_pos(4 + p, 0);
        oled_send(true, buf, OLED_W);
    }
}
