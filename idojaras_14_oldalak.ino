// Oldalváltás, Napló oldal, automatikus visszatérés a főoldalra
// (Az idojaras.ino része: az Arduino IDE és a PlatformIO a fájlokat ábécésorrendben, egy programmá fűzi össze.)

// Az aktuális oldal megjelenítése; a főoldalon a felső sáv bal oldali órája rejtve van (ott a nagy óra látszik)
static void show_page(int p) {
  if (fs_idx >= 0) fs_close();
  bl_panel_close();
  settings_close();
  current_page = p;
  page_since = millis();

  lv_obj_set_hidden(page_dash, p != PAGE_DASH);
  lv_obj_set_hidden(page_hist, p != PAGE_HIST);
  lv_obj_set_hidden(page_hist2, p != PAGE_HIST2);
  lv_obj_set_hidden(page_fc, p != PAGE_FC);
  lv_obj_set_hidden(page_log, p != PAGE_LOG);
  lv_obj_set_hidden(page_details, p != PAGE_DETAILS);
  en_page_show(p == PAGE_ENERGY);

  // a fejléc bal oldalán az oldal neve (a főoldalon üres); a grafikonoldalakon mellette a Nap | Hét | Hónap kapcsoló
  static const char *PAGE_NAME[PAGE_COUNT] = {"", tr("Energia", "Energy"), tr("Előrejelzés", "Forecast"), tr("Kint", "Outdoor"), tr("Bent", "Indoor"), tr("Részletes adatok", "Details"), tr("Napló", "Log")};
  lv_label_set_text(lbl_info, PAGE_NAME[p]);
  const bool hist_page = (p == PAGE_HIST || p == PAGE_HIST2);
  if (ch_toggle) lv_obj_set_hidden(ch_toggle, !hist_page);
  update_status_bar();
  if (p == PAGE_HIST || p == PAGE_HIST2) hist_update_charts();
  if (p == PAGE_FC) fc_update_ui();
}

static void log_refresh() {
  static char text[LOG_LINES * (LOG_LEN + 1) + 1];
  static uint32_t last_ver = 0xFFFFFFFF, last_ms = 0;
  if (log_version == last_ver || millis() - last_ms < 1000) return;
  last_ms = millis(); last_ver = log_version;
  char *p = text;
  portENTER_CRITICAL(&log_mux);
  for (int i = 1; i <= LOG_LINES; i++) {             // régebbitől az újabbig; az utolsó az éppen írt sor
    const char *l = log_lines[(log_head + i) % LOG_LINES];
    size_t n = strlen(l);
    memcpy(p, l, n); p += n; *p++ = '\n';
  }
  portEXIT_CRITICAL(&log_mux);
  *p = 0;
  lv_label_set_text_static(lbl_log, text);
}

static void page_task() {
  if (current_page == PAGE_LOG) log_refresh();
  en_page_task();
  uint32_t idle_ms = millis() - page_since;

  if (PAGE_ROTATE) {
    uint32_t limit = (uint32_t)(current_page == PAGE_DASH ? DASH_SECONDS : DETAILS_SECONDS) * 1000UL;
    if (idle_ms >= limit) show_page(page_step(current_page, 1));
    return;
  }

  // Érintéses használatnál a főoldal az "alap": a többi oldal nyugalom után visszaugrik
  if (current_page != PAGE_DASH && current_page != PAGE_ENERGY && DETAILS_RETURN_SECONDS > 0 &&   // az energia oldal marad
      idle_ms >= (uint32_t)DETAILS_RETURN_SECONDS * 1000UL) {
    show_page(PAGE_DASH);
    return;
  }

  // a grafikonok a látható oldalon félpercenként frissülnek
  static uint32_t last_chart = 0;
  if ((current_page == PAGE_HIST || current_page == PAGE_HIST2 || current_page == PAGE_FC) && millis() - last_chart >= 30000UL) {
    last_chart = millis();
    if (current_page == PAGE_FC) fc_update_ui(); else hist_update_charts();
  }
}
