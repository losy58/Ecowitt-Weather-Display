// Guition JC8012P4A1C (ESP32-P4): kijelző (JD9365, MIPI-DSI), érintés (GSL3680, I2C), háttérvilágítás (PWM)
// A panel- és érintés-meghajtó a gyártó Arduino-példájából való (esp_lcd_jd9365.c, esp_lcd_gsl3680.c, esp_lcd_touch.c,
// gsl_point_id.c); a lábkiosztás a gyártó kapcsolási rajzából.
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_i2c.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include <math.h>
#include "esp32-hal-ledc.h"
#include "esp_lcd_jd9365.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gsl3680.h"
#include "kijelzo_p4.h"

#define PANEL_W 800              // a panel természetes (álló) mérete
#define PANEL_H 1280
#define PIN_LCD_RST   27
#define PIN_LCD_PWM   23         // háttérvilágítás (MP3202 EN), magas = világít
#define PIN_TP_SDA    7          // az érintés I2C-buszán van a hangkodek (ES8311) és az óra (RX8025T) is
#define PIN_TP_SCL    8
#define PIN_TP_RST    22
#define PIN_TP_INT    21
#define DSI_PHY_LDO_CHAN 3       // LDO_VO3 -> VDD_MIPI_DPHY, 2,5 V
#define DSI_PHY_LDO_MV   2500
#define BL_FREQ_HZ    20000      // a gyártó BSP-jével azonos PWM
#define BL_BITS       10

// A fekvő kép elforgatásának iránya: 0 = a kijelző teteje a panel jobb oldala, 1 = fordítva (180 fokkal elforgatva).
// A 0-val a kép fejjel lefelé állt (2026.10.14.); ha a lapot fordítva szereled fel, ezt kell átírni (az érintés is követi).
#define ROT_FLIP 1

static esp_lcd_panel_handle_t s_panel = NULL;
static int s_ui_w = 0, s_ui_h = 0, s_ox = 0, s_oy = 0;   // a felület mérete és helye a fekvő (1280x800) képen
static uint16_t *s_rot = NULL;                          // a forgatás puffere
static size_t s_rot_px = 0;
static esp_lcd_touch_handle_t s_tp = NULL;
static SemaphoreHandle_t s_draw_done = NULL;            // a panel a képpufferbe DMA2D-vel, a háttérben másol
static i2c_master_bus_handle_t s_i2c = NULL;            // az érintés I2C-busza (rajta az óra is)
static i2c_master_dev_handle_t s_rtc = NULL;            // RX8025T valós idejű óra

static void logmsg(void (*log)(const char *), const char *msg) { if (log) log(msg); }

static bool IRAM_ATTR on_trans_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *ctx) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(s_draw_done, &woken);
  return woken == pdTRUE;
}

// Rajzolás a panelre: előbb megvárja az előző másolás végét (különben a meghajtó eldobja: "previous draw operation is
// not finished"), a mostanit a háttérben hagyja futni. A puffer csak a következő draw()/draw_wait() után írható újra.
static void draw(int x0, int y0, int x1, int y1, const void *buf) {
  xSemaphoreTake(s_draw_done, pdMS_TO_TICKS(200));
  if (esp_lcd_panel_draw_bitmap(s_panel, x0, y0, x1, y1, buf) != ESP_OK) xSemaphoreGive(s_draw_done);
}
static void draw_wait(void) {
  xSemaphoreTake(s_draw_done, pdMS_TO_TICKS(200));
  xSemaphoreGive(s_draw_done);
}

bool p4_display_init(int ui_w, int ui_h, void (*log)(const char *msg)) {
  s_ui_w = ui_w; s_ui_h = ui_h;
  s_ox = (PANEL_H - ui_w) / 2;
  s_oy = (PANEL_W - ui_h) / 2;

  // a háttérvilágítás kikapcsolva, amíg a panel el nem indul
  ledcAttach(PIN_LCD_PWM, BL_FREQ_HZ, BL_BITS);
  ledcWrite(PIN_LCD_PWM, 0);

  esp_ldo_channel_handle_t ldo = NULL;
  esp_ldo_channel_config_t ldo_cfg = { .chan_id = DSI_PHY_LDO_CHAN, .voltage_mv = DSI_PHY_LDO_MV };
  if (esp_ldo_acquire_channel(&ldo_cfg, &ldo) != ESP_OK) { logmsg(log, "Kijelző: a MIPI-DSI tápja nem indult"); return false; }

  esp_lcd_dsi_bus_handle_t bus = NULL;
  esp_lcd_dsi_bus_config_t bus_cfg = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
  if (esp_lcd_new_dsi_bus(&bus_cfg, &bus) != ESP_OK) { logmsg(log, "Kijelző: DSI-busz hiba"); return false; }

  esp_lcd_panel_io_handle_t io = NULL;
  esp_lcd_dbi_io_config_t dbi_cfg = JD9365_PANEL_IO_DBI_CONFIG();
  if (esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, &io) != ESP_OK) { logmsg(log, "Kijelző: DBI hiba"); return false; }

  esp_lcd_dpi_panel_config_t dpi_cfg = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
  jd9365_vendor_config_t vendor_cfg = { .mipi_config = { .dsi_bus = bus, .dpi_config = &dpi_cfg } };
  esp_lcd_panel_dev_config_t panel_cfg = {
    .reset_gpio_num = PIN_LCD_RST,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = 16,
    .vendor_config = &vendor_cfg,
  };
  if (esp_lcd_new_panel_jd9365(io, &panel_cfg, &s_panel) != ESP_OK ||
      esp_lcd_panel_reset(s_panel) != ESP_OK || esp_lcd_panel_init(s_panel) != ESP_OK) {
    logmsg(log, "Kijelző: a JD9365 panel nem indult");
    return false;
  }
  esp_lcd_panel_mirror(s_panel, true, true);   // mint a gyártó példájában

  s_draw_done = xSemaphoreCreateBinary();
  xSemaphoreGive(s_draw_done);
  esp_lcd_dpi_panel_event_callbacks_t cbs = { .on_color_trans_done = on_trans_done };
  esp_lcd_dpi_panel_register_event_callbacks(s_panel, &cbs, NULL);

  // a teljes kép feketére (a felület körüli keret is)
  const int lines = 40;
  uint16_t *black = heap_caps_calloc((size_t)PANEL_W * lines, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (black) {
    for (int y = 0; y < PANEL_H; y += lines) draw(0, y, PANEL_W, y + lines, black);
    draw_wait();
    heap_caps_free(black);
  }
  // induláskor P4_BL_START % (a backlight_task() innen húzza fel fokozatosan): azonnali 100 %-nál a gép USB-portjáról
  // táplálva leesett a tápfeszültség (brownout, újraindulás)
  p4_backlight(P4_BL_START);
  logmsg(log, "Kijelző kész (JD9365, 800x1280, MIPI-DSI)");
  return true;
}

void p4_display_flush(int x1, int y1, int x2, int y2, const uint16_t *px) {
  if (!s_panel) return;
  const int w = x2 - x1 + 1, h = y2 - y1 + 1;
  const size_t n = (size_t)w * h;
  draw_wait();                                           // a forgatás puffere csak az előző másolás után írható
  if (n > s_rot_px) {
    heap_caps_free(s_rot);
    // kis puffer a belső RAM-ba, a nagy (P4_LVGL_LINES soros) a PSRAM-ba: a belső RAM a WiFi-nek és a TLS-nek kell
    if (n * 2 <= 32 * 1024) s_rot = heap_caps_malloc(n * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_rot) s_rot = heap_caps_aligned_alloc(64, n * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_rot_px = s_rot ? n : 0;
    if (!s_rot) return;
  }
  // a felület (x, y) pontja a fekvő képen (x + ox, y + oy); ebből 90 fokos forgatással a panel (px, py) pontja
#if ROT_FLIP == 0
  // px = 799 - fy, py = fx  ->  a cél: h széles, w magas téglalap
  for (int y = 0; y < h; y++) {
    const uint16_t *src = px + (size_t)y * w;
    uint16_t *dst = s_rot + (h - 1 - y);
    for (int x = 0; x < w; x++) dst[(size_t)x * h] = src[x];
  }
  const int px0 = PANEL_W - 1 - (y2 + s_oy), py0 = x1 + s_ox;
#else
  // px = fy, py = 1279 - fx
  for (int y = 0; y < h; y++) {
    const uint16_t *src = px + (size_t)y * w;
    uint16_t *dst = s_rot + y;
    for (int x = 0; x < w; x++) dst[(size_t)(w - 1 - x) * h] = src[x];
  }
  const int px0 = y1 + s_oy, py0 = PANEL_H - 1 - (x2 + s_ox);
#endif
  draw(px0, py0, px0 + h, py0 + w, s_rot);
}

void p4_backlight(int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  ledcWrite(PIN_LCD_PWM, (uint32_t)pct * ((1u << BL_BITS) - 1) / 100u);
}

bool p4_touch_init(void (*log)(const char *msg)) {
  i2c_master_bus_handle_t bus = NULL;
  i2c_master_bus_config_t bus_cfg = {
    .i2c_port = 1,                    // mint a gyári programban (BSP: I2C 1, belső felhúzás és zavarszűrő nélkül)
    .sda_io_num = PIN_TP_SDA,
    .scl_io_num = PIN_TP_SCL,
    .clk_source = I2C_CLK_SRC_DEFAULT,
  };
  if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) { logmsg(log, "Érintés: I2C-busz hiba"); return false; }
  s_i2c = bus;

  esp_lcd_panel_io_handle_t io = NULL;
  esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GSL3680_CONFIG();
  io_cfg.scl_speed_hz = 400000;
  if (esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io) != ESP_OK) { logmsg(log, "Érintés: I2C-eszköz hiba"); return false; }

  esp_lcd_touch_config_t tp_cfg = {
    .x_max = PANEL_W,
    .y_max = PANEL_H,
    .rst_gpio_num = PIN_TP_RST,
    .int_gpio_num = PIN_TP_INT,
    .levels = { .reset = 0, .interrupt = 0 },
    .flags = { .swap_xy = 0, .mirror_x = 1, .mirror_y = 1 },   // mint a gyártó példájában
  };
  if (esp_lcd_touch_new_i2c_gsl3680(io, &tp_cfg, &s_tp) != ESP_OK) {
    s_tp = NULL;
    logmsg(log, "Érintővezérlő nem található (GSL3680)");
    return false;
  }
  logmsg(log, "Érintővezérlő: GSL3680");
  return true;
}

i2c_master_bus_handle_t p4_i2c_bus(void) { return s_i2c; }   // a kamera (kamera_p4.c) is ezen a buszon van

bool p4_touch_read(int *x, int *y) {
  if (!s_tp) return false;
  uint16_t tx = 0, ty = 0, strength = 0;
  uint8_t cnt = 0;
  esp_lcd_touch_read_data(s_tp);
  if (!esp_lcd_touch_get_coordinates(s_tp, &tx, &ty, &strength, &cnt, 1) || cnt == 0) return false;
  // a panel koordinátái (a gyártó példája szerint az x tükrözve), majd vissza a fekvő képre és a felületre
  const int px = PANEL_W - 1 - tx, py = ty;
#if ROT_FLIP == 0
  const int fx = py, fy = PANEL_W - 1 - px;
#else
  const int fx = PANEL_H - 1 - py, fy = px;
#endif
  const int ux = fx - s_ox, uy = fy - s_oy;
  if (ux < 0 || uy < 0 || ux >= s_ui_w || uy >= s_ui_h) return false;   // a fekete kereten
  *x = ux; *y = uy;
  return true;
}


// ---------------------------------------------------------------------------
// RX8025T valós idejű óra (I2C 0x32, az érintés buszán; saját CR1220 gombeleme van). UTC-t tárolunk benne.
// Regiszterek: 0x00..0x06 mp, perc, óra, a hét napja (bit), nap, hónap, év (BCD); 0x0E jelzők (bit1 VLF: az óra
// tápja kimaradt, az idő érvénytelen).
// ---------------------------------------------------------------------------
#define RTC_ADDR 0x32
#define RTC_FLAG 0x0E
#define RTC_VLF  0x02

static bool rtc_dev(void) {
  if (s_rtc) return true;
  if (!s_i2c) return false;
  i2c_device_config_t cfg = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = RTC_ADDR, .scl_speed_hz = 400000 };
  return i2c_master_bus_add_device(s_i2c, &cfg, &s_rtc) == ESP_OK;
}

static int bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t bin2bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

bool p4_rtc_read(int *y, int *mo, int *d, int *h, int *mi, int *s) {
  if (!rtc_dev()) return false;
  uint8_t reg = 0, b[15];
  if (i2c_master_transmit_receive(s_rtc, &reg, 1, b, sizeof(b), 50) != ESP_OK) return false;
  if (b[RTC_FLAG] & RTC_VLF) return false;   // a gombelem lemerült / az óra még nem volt beállítva
  *s = bcd2bin(b[0] & 0x7F); *mi = bcd2bin(b[1] & 0x7F); *h = bcd2bin(b[2] & 0x3F);
  *d = bcd2bin(b[4] & 0x3F); *mo = bcd2bin(b[5] & 0x1F); *y = 2000 + bcd2bin(b[6]);
  return *s < 60 && *mi < 60 && *h < 24 && *d >= 1 && *d <= 31 && *mo >= 1 && *mo <= 12;
}

bool p4_rtc_write(int y, int mo, int d, int h, int mi, int s, int wday) {
  if (!rtc_dev() || y < 2000 || y > 2099) return false;
  uint8_t b[8] = { 0x00, bin2bcd(s), bin2bcd(mi), bin2bcd(h), (uint8_t)(1u << (wday & 7)), bin2bcd(d), bin2bcd(mo), bin2bcd(y - 2000) };
  if (i2c_master_transmit(s_rtc, b, sizeof(b), 50) != ESP_OK) return false;
  uint8_t f[2] = { RTC_FLAG, 0x00 };   // a VLF (és a többi jelző) törlése: az idő mostantól érvényes
  return i2c_master_transmit(s_rtc, f, sizeof(f), 50) == ESP_OK;
}

// ---------------------------------------------------------------------------
// Hang: ES8311 kodek (I2C 0x18, az érintés buszán), I2S (MCLK GPIO13, BCLK GPIO12, LRCK GPIO10, adat GPIO9),
// NS4150 erősítő (PA_CTRL GPIO20, magas = be), hangszóró a CN3-on. A kodek I2S-szolgaként, 16 kHz, 16 bit.
// A hangjelzést háttérfeladat játssza le (a főciklus nem vár rá); az erősítő csak lejátszás alatt van bekapcsolva.
// ---------------------------------------------------------------------------
#define ES8311_ADDR 0x18
#define PIN_I2S_MCLK 13
#define PIN_I2S_BCLK 12
#define PIN_I2S_WS   10
#define PIN_I2S_DOUT 9
#define PIN_PA_CTRL  20
#define AUD_RATE     16000

static i2c_master_dev_handle_t s_es = NULL;
static i2s_chan_handle_t s_tx = NULL;
static TaskHandle_t s_beep_task = NULL;
static volatile float s_amp = 0.35f;                // a hangjelzés erőssége (0..1)

static bool es_write(uint8_t reg, uint8_t val) {
  uint8_t b[2] = { reg, val };
  return i2c_master_transmit(s_es, b, 2, 50) == ESP_OK;
}

// egy hang (Hz, ms) halk be- és kiúsztatással (kattanás nélkül), sztereóban ugyanaz
static void beep_tone(float hz, int ms, float amp) {
  static int16_t buf[256 * 2];
  const int n = AUD_RATE * ms / 1000, fade = AUD_RATE / 200;   // 5 ms úsztatás
  float ph = 0.0f;
  const float step = 2.0f * (float)M_PI * hz / AUD_RATE;
  for (int i = 0; i < n; ) {
    int k = 0;
    for (; k < 256 && i < n; k++, i++) {
      float e = 1.0f;
      if (i < fade) e = (float)i / fade;
      else if (n - i < fade) e = (float)(n - i) / fade;
      const int16_t v = hz > 0 ? (int16_t)(sinf(ph) * amp * e * 32767.0f) : 0;
      ph += step;
      if (ph > 2.0f * (float)M_PI) ph -= 2.0f * (float)M_PI;
      buf[2 * k] = buf[2 * k + 1] = v;
    }
    size_t w;
    i2s_channel_write(s_tx, buf, (size_t)k * 4, &w, 200);
  }
}

static void beep_worker(void *arg) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    gpio_set_level(PIN_PA_CTRL, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    // "ding-ding-dong": két rövid magas és egy hosszabb mélyebb hang
    beep_tone(1047, 140, s_amp); beep_tone(0, 90, 0);
    beep_tone(1047, 140, s_amp); beep_tone(0, 90, 0);
    beep_tone(784, 320, s_amp);
    beep_tone(0, 120, 0);   // csend, hogy az utolsó hang végét ne vágja el az erősítő kikapcsolása
    gpio_set_level(PIN_PA_CTRL, 0);
  }
}

bool p4_audio_init(void (*log)(const char *msg)) {
  if (!s_i2c) return false;
  gpio_config_t pa = { .pin_bit_mask = 1ULL << PIN_PA_CTRL, .mode = GPIO_MODE_OUTPUT };
  gpio_config(&pa);
  gpio_set_level(PIN_PA_CTRL, 0);

  // I2S előbb: a kodeknek MCLK kell a beállításhoz
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.auto_clear = true;
  if (i2s_new_channel(&cc, &s_tx, NULL) != ESP_OK) { logmsg(log, "Hang: I2S hiba"); return false; }
  i2s_std_config_t sc = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUD_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = { .mclk = PIN_I2S_MCLK, .bclk = PIN_I2S_BCLK, .ws = PIN_I2S_WS, .dout = PIN_I2S_DOUT, .din = I2S_GPIO_UNUSED },
  };
  sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;   // 4,096 MHz
  if (i2s_channel_init_std_mode(s_tx, &sc) != ESP_OK || i2s_channel_enable(s_tx) != ESP_OK) {
    logmsg(log, "Hang: I2S hiba"); return false;
  }

  i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = ES8311_ADDR, .scl_speed_hz = 400000 };
  if (i2c_master_bus_add_device(s_i2c, &dc, &s_es) != ESP_OK) { logmsg(log, "Hang: I2C hiba"); return false; }
  // ES8311: szolga mód, MCLK a lábról, 16 kHz (MCLK/256), 16 bites I2S, csak a DAC (lejátszás)
  static const uint8_t INIT[][2] = {
    {0x45, 0x00}, {0x01, 0x30}, {0x02, 0x00}, {0x03, 0x10}, {0x04, 0x10}, {0x05, 0x00}, {0x06, 0x03},
    {0x07, 0x00}, {0x08, 0xFF}, {0x0B, 0x00}, {0x0C, 0x00}, {0x10, 0x1F}, {0x11, 0x7F}, {0x00, 0x80},
    {0x01, 0x3F}, {0x09, 0x0C}, {0x0A, 0x0C}, {0x0D, 0x01}, {0x0E, 0x02}, {0x12, 0x00}, {0x13, 0x10},
    {0x1C, 0x6A}, {0x37, 0x08}, {0x32, 0xBF}, {0x31, 0x00},
  };
  for (size_t i = 0; i < sizeof(INIT) / sizeof(INIT[0]); i++) {
    if (!es_write(INIT[i][0], INIT[i][1])) { logmsg(log, "Hang: az ES8311 nem válaszol"); return false; }
  }
  if (xTaskCreatePinnedToCoreWithCaps(beep_worker, "hang", 4096, NULL, 2, &s_beep_task, 1,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    s_beep_task = NULL; logmsg(log, "Hang: a háttérfeladat nem indult"); return false;
  }
  logmsg(log, "Hang: ES8311 kész");
  return true;
}

void p4_beep(float amp) {
  if (!s_beep_task) return;
  s_amp = amp < 0.0f ? 0.0f : amp > 1.0f ? 1.0f : amp;
  xTaskNotifyGive(s_beep_task);
}
