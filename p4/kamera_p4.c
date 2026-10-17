// Guition JC8012P4A1C (ESP32-P4): a kamera (OV02C10, 2 MP, MIPI-CSI) mint fény- és mozgásérzékelő
// A kamera az érintés I2C-buszán (0x36) vezérelhető; a kép a CSI-vezérlőn és az ISP-n át RAW8-ként (színszűrős,
// színkeverés nélküli nyers kép) a PSRAM-ba kerül. A kamerát a saját kódunk állítja be a gyártó regiszterlistájával
// (ov02c10_1080p_1lane.h), nem az esp_video: annak a keretrendszerben nincs OV02C10-meghajtója, és nekünk színes kép
// nem kell, csak a világosság.
// A kép nem hagyja el a lapot és nem mentődik: egy háttérfeladat fél másodpercenként egy képből 64x36-os rácsot számol,
// ebből a világosságot és a mozgást (az előző rácshoz képest megváltozott részek arányát). Az expozíciót és az
// erősítést maga állítja, így a világosság / (expozíció * erősítés) arányos a szoba megvilágításával.
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "esp_log.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_ctlr_csi.h"
#include "driver/isp.h"
#include "kijelzo_p4.h"

typedef struct { uint16_t reg; uint8_t val; } ov02c10_reginfo_t;
#define OV02C10_REG_SLEEP_MODE 0x0100
#define OV02C10_REG_END        0xffff
#include "ov02c10_1080p_1lane.h"

#define CAM_ADDR      0x36
#define CAM_PID       0x5602     // a 0x300a-0x300b azonosító
#define CAM_W         1920
#define CAM_H         1080
#define CAM_LANE_MBPS 405        // mint a gyári demóban
#define CAM_VTS       8730       // képmagasság sorokban: a gyári 1164 (30 kép/mp) helyett kb. 4 kép/mp; kevesebb
                                 // PSRAM-forgalom (a kijelző is onnan olvas), és sötétben hosszabb expozíció
#define CAM_TLINE_MS  0.028148f  // egy sor ideje
#define CAM_EXP_MIN   8          // expozíció (0x3501-0x3502), sorokban
#define CAM_EXP_MAX   (CAM_VTS - 16)
#define CAM_GAIN_MIN  0x0100     // analóg erősítés (0x3508-0x3509): 0x100 = 1x
#define CAM_GAIN_MAX  0x0F80     // 15,5x
#define CAM_BLACK     16         // feketeszint a RAW8 képen (a kamera a 10 bites képen 64-re állítja)
#define CAM_TARGET    64         // a kívánt átlagos világosság a feketeszint fölött (0..239)
#define CAM_SAT       225        // ennél világosabb átlag: a kép telítődik, az expozíció erősen csökken
#define CAM_PERIOD_MS 500
#define LIGHT_EMA     0.15f      // a simított fényérték (logaritmikus mozgóátlag) súlya: kb. 3 mp alatt követi a változást
#define GRID_W        64         // a mérőrács: 30x30 képpontos cellák, mindegyik közepén egy 4x4-es folt átlaga
#define GRID_H        36

static i2c_master_dev_handle_t s_cam_i2c = NULL;
static esp_cam_ctlr_handle_t s_csi = NULL;
static isp_proc_handle_t s_isp = NULL;
static uint8_t *s_buf = NULL;
static size_t s_buf_len = 0;
static SemaphoreHandle_t s_frame = NULL;
static uint8_t s_grid[GRID_W * GRID_H], s_prev[GRID_W * GRID_H];
static int s_exp = 2000, s_gain = CAM_GAIN_MIN;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static p4_cam_stat_t s_st;        // a legutóbbi mérés (s_mux alatt)
static int64_t s_motion_us = 0;   // az utolsó mozgás ideje (0: még nem volt)
static volatile int s_pause = 0;  // >0: a képfolyam áll (flash-írás alatt; p4_cam_pause)

i2c_master_bus_handle_t p4_i2c_bus(void);   // kijelzo_p4.c: az érintés I2C-busza

static void logmsg(void (*log)(const char *), const char *msg) { if (log) log(msg); }

// Az indítás alatt az ESP-IDF hibaüzenetei (E (...)) közül az első: ebből látszik a kezelőoldalon, mi nem tetszett
// a meghajtónak (a soros napló csak USB-n látszana, és a port megnyitása újraindítja a lapot)
static vprintf_like_t s_old_vprintf = NULL;
static char s_idf_err[96];
static int cam_vprintf(const char *fmt, va_list ap) {
  va_list ap2;
  va_copy(ap2, ap);
  if (!s_idf_err[0] && (fmt[0] == 'E' || (fmt[0] == '\033' && strstr(fmt, "E (")))) vsnprintf(s_idf_err, sizeof(s_idf_err), fmt, ap2);
  va_end(ap2);
  return s_old_vprintf ? s_old_vprintf(fmt, ap) : 0;
}

// hiba az indításkor: a Naplóba és a kezelőoldalra (s_st.err); e: az ESP-IDF hibakódja (0: nincs)
static bool fail(void (*log)(const char *), const char *msg, esp_err_t e) {
  if (s_old_vprintf) { esp_log_set_vprintf(s_old_vprintf); s_old_vprintf = NULL; }
  for (char *c = s_idf_err; *c; c++) if (*c == '\n' || *c == '\033') *c = ' ';
  if (e) snprintf(s_st.err, sizeof(s_st.err), "%s (0x%x) %s", msg, (unsigned)e, s_idf_err);
  else snprintf(s_st.err, sizeof(s_st.err), "%s", msg);
  logmsg(log, s_st.err);
  return false;
}

static bool cam_wr(uint16_t reg, uint8_t val) {
  uint8_t b[3] = { (uint8_t)(reg >> 8), (uint8_t)reg, val };
  return i2c_master_transmit(s_cam_i2c, b, 3, 50) == ESP_OK;
}

static bool cam_rd(uint16_t reg, uint8_t *val) {
  uint8_t a[2] = { (uint8_t)(reg >> 8), (uint8_t)reg };
  return i2c_master_transmit_receive(s_cam_i2c, a, 2, val, 1, 50) == ESP_OK;
}

static bool cam_set_exp_gain(int exp, int gain) {
  bool ok = cam_wr(0x3501, (uint8_t)(exp >> 8)) && cam_wr(0x3502, (uint8_t)exp) &&
            cam_wr(0x3508, (uint8_t)(gain >> 8)) && cam_wr(0x3509, (uint8_t)gain);
  if (ok) { s_exp = exp; s_gain = gain; }
  return ok;
}

// Minden kép a saját pufferünkbe kerül (a meghajtó tartalékpuffere ki van kapcsolva, lásd p4_cam_init()):
// a következő kép helye (megszakításból, a kép végén és indításkor)
static bool IRAM_ATTR on_new_trans(esp_cam_ctlr_handle_t h, esp_cam_ctlr_trans_t *t, void *ud) {
  t->buffer = s_buf;
  t->buflen = s_buf_len;
  return false;
}

// a kép vége (megszakításból): szól a feladatnak
static bool IRAM_ATTR on_frame(esp_cam_ctlr_handle_t h, esp_cam_ctlr_trans_t *t, void *ud) {
  BaseType_t w = pdFALSE;
  xSemaphoreGiveFromISR(s_frame, &w);
  return w == pdTRUE;
}

// a rács számolása; visszaadja az átlagos világosságot (0..255)
static int grid_calc(void) {
  const int cw = CAM_W / GRID_W, ch = CAM_H / GRID_H;
  uint32_t total = 0;
  for (int gy = 0; gy < GRID_H; gy++) {
    for (int gx = 0; gx < GRID_W; gx++) {
      const int x0 = (gx * cw + cw / 2 - 2) & ~1, y0 = (gy * ch + ch / 2 - 2) & ~1;   // páros: mind a 4 színszűrő benne van
      uint32_t s = 0;
      for (int y = 0; y < 4; y++) {
        const uint8_t *p = s_buf + (size_t)(y0 + y) * CAM_W + x0;
        s += p[0] + p[1] + p[2] + p[3];
      }
      s_grid[gy * GRID_W + gx] = (uint8_t)(s >> 4);
      total += s >> 4;
    }
  }
  return (int)(total / (GRID_W * GRID_H));
}

// a megváltozott cellák aránya (%); a zaj miatt a küszöb a világossággal nő
static float grid_motion(void) {
  int n = 0;
  for (int i = 0; i < GRID_W * GRID_H; i++) {
    const int d = abs((int)s_grid[i] - (int)s_prev[i]);
    if (d > 6 + s_grid[i] / 8) n++;
  }
  return n * 100.0f / (GRID_W * GRID_H);
}

// Saját expozíció-szabályozás: a kívánt fénymennyiség (expozíció * erősítés) a világosság arányában;
// előbb az expozíciós idő nő (kevesebb zaj), csak utána az erősítés. Csillapítva: egy lépésben csak a hiányzó arány
// gyökével (a fele útig, logaritmikusan) és legfeljebb 4-szeres változással, hogy egy elhaladó kéz vagy a villany
// felkapcsolása után ne lengjen ide-oda. true: változott (a következő kép még a régivel készült)
static bool cam_ae(int m) {
  const float cur = (float)s_exp * s_gain;
  float r = m >= CAM_SAT - CAM_BLACK ? 0.25f : (float)CAM_TARGET / (m < 1 ? 1 : m);
  if (r > 0.75f && r < 1.33f) return false;   // elég közel van: nem állít
  r = sqrtf(r);
  if (r > 4.0f) r = 4.0f;
  if (r < 0.25f) r = 0.25f;
  const float want = cur * r;
  int exp = (int)(want / CAM_GAIN_MIN), gain = CAM_GAIN_MIN;
  if (exp > CAM_EXP_MAX) {
    exp = CAM_EXP_MAX;
    gain = (int)(want / exp);
    if (gain > CAM_GAIN_MAX) gain = CAM_GAIN_MAX;
  }
  if (exp < CAM_EXP_MIN) exp = CAM_EXP_MIN;
  if (exp == s_exp && gain == s_gain) return false;
  cam_set_exp_gain(exp, gain);
  return true;
}

static void cam_worker(void *arg) {
  bool prev_ok = false;    // van összehasonlítható előző rács (azonos beállítással készült)
  int skip = 1;            // ennyi képet eldob a beállítás változása után
  float light_log = 0.0f;  // a simított fényérték logaritmusa
  int light_n = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(CAM_PERIOD_MS));
    if (s_pause) { prev_ok = false; skip = 1; continue; }
    // a következő kép vége: utána kb. 215 ms-ig (a képek közti szünetben) nem ír a DMA a pufferbe
    xSemaphoreTake(s_frame, 0);
    if (xSemaphoreTake(s_frame, pdMS_TO_TICKS(2000)) != pdTRUE) {
      if (s_pause) continue;   // közben megállt a képfolyam: nem hiba
      portENTER_CRITICAL(&s_mux);
      s_st.errors++;
      portEXIT_CRITICAL(&s_mux);
      continue;
    }
    const int raw = grid_calc();
    if (skip > 0) { skip--; prev_ok = false; continue; }
    const int m = raw > CAM_BLACK ? raw - CAM_BLACK : 0;
    const float exp_ms = s_exp * CAM_TLINE_MS, gain = s_gain / 256.0f;
    // fényérték; teljes sötétben (a világosság 0) is legyen értelmes, kicsi szám
    const float light = (m > 0 ? m : 0.5f) / (exp_ms * gain);
    light_log = light_n ? light_log + LIGHT_EMA * (logf(light) - light_log) : logf(light);
    if (light_n < 1000) light_n++;
    float motion = -1.0f;
    if (prev_ok) motion = grid_motion();
    portENTER_CRITICAL(&s_mux);
    if (motion >= 1.0f) s_motion_us = esp_timer_get_time();
    s_st.frames++;
    s_st.mean = m;
    s_st.exp_ms = exp_ms;
    s_st.gain = gain;
    s_st.light = light;
    s_st.light_avg = light_n >= 6 ? expf(light_log) : 0.0f;   // az első 3 mp-ben még nincs
    if (motion >= 0.0f) s_st.motion = motion;
    portEXIT_CRITICAL(&s_mux);
    memcpy(s_prev, s_grid, sizeof(s_grid));
    prev_ok = true;
    if (cam_ae(m)) { skip = 1; prev_ok = false; }
  }
}

bool p4_cam_init(void (*log)(const char *msg)) {
  i2c_master_bus_handle_t bus = p4_i2c_bus();
  if (!bus) return fail(log, "Kamera: nincs I2C-busz", 0);
  esp_err_t e;
  s_old_vprintf = esp_log_set_vprintf(cam_vprintf);
  i2c_device_config_t dc = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = CAM_ADDR, .scl_speed_hz = 100000 };
  if ((e = i2c_master_bus_add_device(bus, &dc, &s_cam_i2c)) != ESP_OK) return fail(log, "Kamera: I2C hiba", e);
  uint8_t hi = 0, lo = 0;
  if (!cam_rd(0x300a, &hi) || !cam_rd(0x300b, &lo)) return fail(log, "Kamera: nincs (vagy nem válaszol az I2C 0x36-on)", 0);
  if (((hi << 8) | lo) != CAM_PID) {
    char m[64];
    snprintf(m, sizeof(m), "Kamera: ismeretlen azonosító 0x%02x%02x", hi, lo);
    return fail(log, m, 0);
  }
  // beállítás: a gyári lista (az elején szoftveres újraindítás, utána várni kell), a képmagasság és a kezdő expozíció
  for (const ov02c10_reginfo_t *r = ov02c10_mipi_1lane_24Minput_1920x1080_raw10_30fps; r->reg != OV02C10_REG_END; r++) {
    if (!cam_wr(r->reg, r->val)) return fail(log, "Kamera: beállítási hiba", 0);
    if (r->reg == 0x0103) vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (!cam_wr(0x380e, CAM_VTS >> 8) || !cam_wr(0x380f, CAM_VTS & 0xFF) || !cam_set_exp_gain(s_exp, s_gain))
    return fail(log, "Kamera: beállítási hiba", 0);

  // CSI (a MIPI-tápot a kijelző már bekapcsolta: közös a DSI-vel)
  esp_cam_ctlr_csi_config_t cc = {
    .ctlr_id = 0,
    .clk_src = MIPI_CSI_PHY_CLK_SRC_DEFAULT,
    .h_res = CAM_W,
    .v_res = CAM_H,
    .data_lane_num = 1,
    .lane_bit_rate_mbps = CAM_LANE_MBPS,
    .input_data_color_type = CAM_CTLR_COLOR_RAW10,
    .output_data_color_type = CAM_CTLR_COLOR_RAW8,
    .queue_items = 1,
    // A meghajtó saját tartalékpuffere ki: azt a kimeneti (8 bites) mérethez foglalja (2,07 MB), a DMA viszont a
    // bemeneti (10 bites) méretet (2,59 MB) írja bele; a túlírás felülírta a szomszédos PSRAM-ot (2026.10.17. este:
    // az akkumérés ADC-adatai sérültek, összeomlás). Minden kép a saját, 10 bites méretű pufferünkbe megy.
    .bk_buffer_dis = 1,
  };
  if ((e = esp_cam_new_csi_ctlr(&cc, &s_csi)) != ESP_OK) return fail(log, "Kamera: CSI hiba", e);

  // ISP: a 10 bites nyers képből 8 bites (színkeverés nélkül); mint az esp_video-ban, a CSI-vezérlő után
  esp_isp_processor_cfg_t ic = {
    .clk_src = ISP_CLK_SRC_DEFAULT,
    .clk_hz = 240 * 1000 * 1000,   // a kamera pixelórajele 81 MHz: ennél gyorsabb kell (80 MHz-en nem jött kép)
    .input_data_source = ISP_INPUT_DATA_SOURCE_CSI,
    .input_data_color_type = ISP_COLOR_RAW10,
    .output_data_color_type = ISP_COLOR_RAW8,
    .has_line_start_packet = false,
    .has_line_end_packet = false,
    .h_res = CAM_W,
    .v_res = CAM_H,
    .bayer_order = COLOR_RAW_ELEMENT_ORDER_GBRG,
  };
  if ((e = esp_isp_new_processor(&ic, &s_isp)) != ESP_OK) return fail(log, "Kamera: ISP hiba", e);
  if ((e = esp_isp_enable(s_isp)) != ESP_OK) return fail(log, "Kamera: ISP indítási hiba", e);
  s_frame = xSemaphoreCreateBinary();
  s_buf_len = CAM_W * CAM_H * 10 / 8;   // a 10 bites méret: a DMA ennyit ír (lásd fent)
  s_buf = esp_cam_ctlr_alloc_buffer(s_csi, s_buf_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
  if (!s_frame || !s_buf) return fail(log, "Kamera: nincs memória", 0);
  esp_cam_ctlr_evt_cbs_t cbs = { .on_get_new_trans = on_new_trans, .on_trans_finished = on_frame };
  if ((e = esp_cam_ctlr_register_event_callbacks(s_csi, &cbs, NULL)) != ESP_OK) return fail(log, "Kamera: CSI-callback hiba", e);
  if ((e = esp_cam_ctlr_enable(s_csi)) != ESP_OK) return fail(log, "Kamera: CSI engedélyezési hiba", e);
  if ((e = esp_cam_ctlr_start(s_csi)) != ESP_OK) return fail(log, "Kamera: CSI indítási hiba", e);
  if (!cam_wr(OV02C10_REG_SLEEP_MODE, 0x01)) return fail(log, "Kamera: nem indul a képfolyam", 0);

  if (xTaskCreatePinnedToCoreWithCaps(cam_worker, "kamera", 4096, NULL, 1, NULL, 0,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS)
    return fail(log, "Kamera: a háttérfeladat nem indult", 0);
  esp_log_set_vprintf(s_old_vprintf);
  s_old_vprintf = NULL;
  s_st.ok = true;
  logmsg(log, "Kamera: OV02C10 kész (fény- és mozgásmérés)");
  return true;
}

// A képfolyam megállítása flash-írás (OTA, előzménymentés, beállítások) idejére, egymásba ágyazható. Előbb a CSI-vezérlő
// áll le (azonnal: nincs több DMA és megszakítás), utána a kamera; folytatáskor fordítva, mint induláskor: a vezérlő
// friss DMA-val indul, és a kamera egy új kép elejétől küld. (Ha csak a kamera állt le, a DMA egy félkész képen ragadt,
// és utána nem jött több kép.)
void p4_cam_pause(bool on) {
  if (!s_st.ok) return;
  if (on) {
    if (s_pause++ == 0) {
      esp_cam_ctlr_stop(s_csi);
      cam_wr(OV02C10_REG_SLEEP_MODE, 0x00);
    }
  } else if (s_pause > 0 && --s_pause == 0) {
    esp_cam_ctlr_start(s_csi);
    cam_wr(OV02C10_REG_SLEEP_MODE, 0x01);
  }
}

void p4_cam_get(p4_cam_stat_t *st) {
  portENTER_CRITICAL(&s_mux);
  *st = s_st;
  const int64_t mu = s_motion_us;
  portEXIT_CRITICAL(&s_mux);
  st->motion_age_s = mu ? (uint32_t)((esp_timer_get_time() - mu) / 1000000) : UINT32_MAX;
}
