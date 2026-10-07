/*
 * NovaOS — кастомная прошивка для ESP32-2432S028R (CYD)
 * Свайп для разблокировки, проводник SD, часы, калькулятор, заметки.
 */

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <SD.h>

// ==================== КОНФИГ ====================
#define SCREEN_W    240
#define SCREEN_H    320
#define TOUCH_CS    33
#define TOUCH_IRQ   36
#define SD_CS       5
#define PIN_LOCK    "1234" // Пароль по умолчанию (можно убрать)

// ==================== ГЛОБАЛЬНЫЕ ОБЪЕКТЫ ====================
TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[SCREEN_W * 20];
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

// Экраны
static lv_obj_t *scr_lock;
static lv_obj_t *scr_home;
static lv_obj_t *scr_app;

// ==================== ПРОТОТИПЫ ====================
void show_lock_screen();
void show_home_screen();
lv_obj_t* create_app_screen(const char* title);
void app_clock(lv_event_t* e);
void app_calc(lv_event_t* e);
void app_notes(lv_event_t* e);
void app_file_manager(lv_event_t* e);

// ==================== ДРАЙВЕР ДИСПЛЕЯ ====================
void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)&color_p->full, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

// ==================== ДРАЙВЕР ТАЧА ====================
void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    static int16_t last_x = 0, last_y = 0;
    uint16_t rawX, rawY;
    if (tft.getTouchRaw(&rawX, &rawY)) {
        int16_t x = map(rawX, 200, 3700, 0, SCREEN_W);
        int16_t y = map(rawY, 240, 3800, 0, SCREEN_H);
        x = constrain(x, 0, SCREEN_W - 1);
        y = constrain(y, 0, SCREEN_H - 1);
        last_x = x; last_y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

// ==================== ЭКРАН БЛОКИРОВКИ ====================
static lv_obj_t *lbl_lock_time;
static lv_obj_t *lbl_lock_date;
static lv_obj_t *lock_indicator;

static void update_lock_clock(lv_timer_t *t) {
    if (!lbl_lock_time) return;
    unsigned long sec = millis() / 1000;
    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%02lu:%02lu",
             (sec / 3600) % 24, (sec / 60) % 60);
    lv_label_set_text(lbl_lock_time, time_buf);
    lv_label_set_text(lbl_lock_date, "Mon, Oct 7");
}

static void lock_gesture_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    
    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        if (dir == LV_DIR_TOP) {
            // Свайп вверх — разблокировка
            show_home_screen();
        }
    }
}

void show_lock_screen() {
    scr_lock = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_lock, lv_color_hex(0x0A0E14), 0);
    
    // Часы
    lbl_lock_time = lv_label_create(scr_lock);
    lv_label_set_text(lbl_lock_time, "00:00");
    lv_obj_set_style_text_color(lbl_lock_time, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_lock_time, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl_lock_time, LV_ALIGN_CENTER, 0, -40);
    
    // Дата
    lbl_lock_date = lv_label_create(scr_lock);
    lv_label_set_text(lbl_lock_date, "Mon, Oct 7");
    lv_obj_set_style_text_color(lbl_lock_date, lv_color_hex(0x8899AA), 0);
    lv_obj_set_style_text_font(lbl_lock_date, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_lock_date, LV_ALIGN_CENTER, 0, 0);
    
    // Индикатор свайпа
    lock_indicator = lv_label_create(scr_lock);
    lv_label_set_text(lock_indicator, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(lock_indicator, lv_color_hex(0x4A9F), 0);
lv_obj_set_style_text_font(lock_indicator, &lv_font_montserrat_28, 0);
    lv_obj_align(lock_indicator, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_anim_time(lock_indicator, 1500, 0);
    lv_obj_set_style_anim_repeat_count(lock_indicator, LV_ANIM_REPEAT_INFINITE, 0);
    lv_obj_set_style_anim_playback_time(lock_indicator, 1000, 0);
    lv_obj_set_style_anim_playback_delay(lock_indicator, 0, 0);
    lv_obj_set_style_translate_y(lock_indicator, -10, 0);
    
    // Обработчик жестов на весь экран
    lv_obj_add_event_cb(scr_lock, lock_gesture_cb, LV_EVENT_GESTURE, NULL);
    
    update_lock_clock(NULL);
    lv_timer_create(update_lock_clock, 1000, NULL);
    
    lv_scr_load(scr_lock);
}

// ==================== ДОМАШНИЙ ЭКРАН ====================
static void icon_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        int id = (int)(intptr_t)lv_event_get_user_data(e);
        if      (id == 0) app_file_manager(e);
        else if (id == 1) app_clock(e);
        else if (id == 2) app_calc(e);
        else if (id == 3) app_notes(e);
    }
}

static lv_obj_t* make_icon(lv_obj_t *parent, const char *sym, const char *name, uint32_t color, int id) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 70, 90);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn = lv_btn_create(cont);
    lv_obj_set_size(btn, 60, 60);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_shadow_width(btn, 8, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
    lv_obj_add_event_cb(btn, icon_cb, LV_EVENT_CLICKED, (void*)(intptr_t)id);

    lv_obj_t *sym_lbl = lv_label_create(btn);
    lv_label_set_text(sym_lbl, sym);
    lv_obj_set_style_text_font(sym_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sym_lbl, lv_color_white(), 0);
    lv_obj_center(sym_lbl);

    lv_obj_t *name_lbl = lv_label_create(cont);
    lv_label_set_text(name_lbl, name);
    lv_obj_set_style_text_color(name_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(name_lbl, LV_ALIGN_BOTTOM_MID, 0, 0);
    return cont;
}

void show_home_screen() {
    scr_home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_home, lv_color_hex(0x121A25), 0);

    // Статус-бар
    lv_obj_t *status_bar = lv_obj_create(scr_home);
    lv_obj_set_size(status_bar, SCREEN_W, 26);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x1A2435), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 4, 0);

    lv_obj_t *time_lbl = lv_label_create(status_bar);
    lv_label_set_text(time_lbl, "12:00");
    lv_obj_set_style_text_color(time_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(time_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t *wifi_lbl = lv_label_create(status_bar);
    lv_label_set_text(wifi_lbl, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(wifi_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(wifi_lbl, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *batt_lbl = lv_label_create(status_bar);
    lv_label_set_text(batt_lbl, "100%");
    lv_obj_set_style_text_color(batt_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(batt_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(batt_lbl, LV_ALIGN_RIGHT_MID, -4, 0);

    // Сетка иконок
    lv_obj_t *grid = lv_obj_create(scr_home);
    lv_obj_set_size(grid, 220, 260);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -10);
lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_layout(grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(grid, 10, 0);

    make_icon(grid, LV_SYMBOL_DIRECTORY, "Файлы",   0x4A6FDC, 0);
    make_icon(grid, LV_SYMBOL_BELL,      "Часы",    0x2E8B57, 1);
    make_icon(grid, LV_SYMBOL_EDIT,      "Кальк.",  0xC0392B, 2);
    make_icon(grid, LV_SYMBOL_FILE,      "Заметки", 0x8E44AD, 3);

    lv_scr_load(scr_home);
}

// ==================== ФРЕЙМВОРК ПРИЛОЖЕНИЙ ====================
static void back_to_home(lv_event_t *e) { show_home_screen(); }

lv_obj_t* create_app_screen(const char* title) {
    scr_app = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_app, lv_color_hex(0x121A25), 0);

    lv_obj_t *bar = lv_obj_create(scr_app);
    lv_obj_set_size(bar, SCREEN_W, 36);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1A2435), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 4, 0);

    lv_obj_t *back = lv_btn_create(bar);
    lv_obj_set_size(back, 40, 28);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x2A3A55), 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_add_event_cb(back, back_to_home, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_center(bl);

    lv_obj_t *t = lv_label_create(bar);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);

    lv_scr_load(scr_app);
    return scr_app;
}

// ==================== ПРИЛОЖЕНИЕ: ПРОВОДНИК ====================
static lv_obj_t *file_list;

static void sd_refresh() {
    if (!file_list) return;
    lv_obj_clean(file_list);
    
    File root = SD.open("/");
    if (!root) {
        lv_obj_t *item = lv_list_add_text(file_list, "SD не найдена");
        lv_obj_set_style_text_color(item, lv_color_hex(0xC0392B), 0);
        return;
    }
    
    while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        
        char line[64];
        if (entry.isDirectory()) {
            snprintf(line, sizeof(line), "%s %s", LV_SYMBOL_DIRECTORY, entry.name());
        } else {
            snprintf(line, sizeof(line), "%s %s (%u B)", LV_SYMBOL_FILE, entry.name(), entry.size());
        }
        lv_list_add_text(file_list, line);
        entry.close();
    }
    root.close();
}

static void refresh_cb(lv_event_t *e) { sd_refresh(); }

void app_file_manager(lv_event_t *e) {
    create_app_screen("Проводник");
    
    lv_obj_t *refresh_btn = lv_btn_create(scr_app);
    lv_obj_set_size(refresh_btn, 80, 30);
    lv_obj_align(refresh_btn, LV_ALIGN_TOP_RIGHT, -8, 42);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(0x4A6FDC), 0);
    lv_obj_set_style_radius(refresh_btn, 8, 0);
    lv_obj_add_event_cb(refresh_btn, refresh_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(refresh_btn);
    lv_label_set_text(rl, "Обновить");
    lv_obj_set_style_text_color(rl, lv_color_white(), 0);
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_12, 0);
    lv_obj_center(rl);
    
    file_list = lv_list_create(scr_app);
    lv_obj_set_size(file_list, SCREEN_W - 16, SCREEN_H - 90);
    lv_obj_align(file_list, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(file_list, lv_color_hex(0x1A2435), 0);
    lv_obj_set_style_border_width(file_list, 0, 0);
    lv_obj_set_style_radius(file_list, 8, 0);
    
    sd_refresh();
}

// ==================== ПРИЛОЖЕНИЕ: ЧАСЫ ====================
static lv_obj_t *clock_label;

static void clock_tick(lv_timer_t *t) {
    if (!clock_label) return;
    unsigned long sec = millis() / 1000;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
             (sec / 3600) % 24, (sec / 60) % 60, sec % 60);
    lv_label_set_text(clock_label, buf);
}

void app_clock(lv_event_t *e) {
    create_app_screen("Часы");
    clock_label = lv_label_create(scr_app);
    lv_obj_set_style_text_color(clock_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_28, 0);
    lv_obj_align(clock_label, LV_ALIGN_CENTER, 0, 0);
    clock_tick(NULL);
    lv_timer_create(clock_tick, 1000, NULL);
}

// ==================== ПРИЛОЖЕНИЕ: КАЛЬКУЛЯТОР ====================
static lv_obj_t *calc_display;
static double calc_a = 0, calc_b = 0;
static char calc_op = 0;
static bool calc_new = true;

static void calc_update(const char *s) { lv_label_set_text(calc_display, s); }

static void calc_btn_cb(lv_event_t *e) {
    const char *txt = lv_label_get_text(lv_obj_get_child(lv_event_get_target(e), 0));
    char buf[32];
    if (strcmp(txt, "C") == 0) {
        calc_a = calc_b = 0; calc_op = 0; calc_new = true;
        calc_update("0"); return;
    }
    if (strcmp(txt, "=") == 0) {
        if (calc_op) {
            double r = 0;
            switch (calc_op) {
                case '+': r = calc_a + calc_b; break;
                case '-': r = calc_a - calc_b; break;
                case '*': r = calc_a * calc_b; break;
                case '/': r = calc_b != 0 ? calc_a / calc_b : 0; break;
            }
            snprintf(buf, sizeof(buf), "%g", r);
            calc_update(buf);
            calc_a = r; calc_op = 0; calc_new = true;
        }
        return;
    }
    if (strchr("+-*/", txt[0]) && strlen(txt) == 1) {
        calc_a = atof(lv_label_get_text(calc_display));
        calc_op = txt[0];
        calc_new = true;
        return;
    }
    const char *cur = lv_label_get_text(calc_display);
    if (calc_new || strcmp(cur, "0") == 0) {
        calc_update(txt);
        calc_new = false;
    } else {
        snprintf(buf, sizeof(buf), "%s%s", cur, txt);
        calc_update(buf);
    }
}

void app_calc(lv_event_t *e) {
    create_app_screen("Калькулятор");

    calc_display = lv_label_create(scr_app);
    lv_label_set_text(calc_display, "0");
    lv_obj_set_style_text_color(calc_display, lv_color_white(), 0);
    lv_obj_set_style_text_font(calc_display, &lv_font_montserrat_28, 0);
    lv_obj_align(calc_display, LV_ALIGN_TOP_RIGHT, -10, 45);

    lv_obj_t *pad = lv_obj_create(scr_app);
    lv_obj_set_size(pad, 220, 220);
    lv_obj_align(pad, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pad, 0, 0);
    lv_obj_set_layout(pad, LV_LAYOUT_GRID);
    static lv_coord_t col[] = {50, 50, 50, 50, LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row[] = {50, 50, 50, 50, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(pad, col, row);
    lv_obj_set_style_pad_gap(pad, 4, 0);

    const char *keys[16] = {"7","8","9","/","4","5","6","*","1","2","3","-","C","0","=","+"};
    for (int i = 0; i < 16; i++) {
        lv_obj_t *btn = lv_btn_create(pad);
        lv_obj_set_size(btn, 48, 48);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A3A55), 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, keys[i]);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, calc_btn_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_CENTER, i % 4, 1,
                                  LV_GRID_ALIGN_CENTER, i / 4, 1);
    }
}

// ==================== ПРИЛОЖЕНИЕ: ЗАМЕТКИ ====================
void app_notes(lv_event_t *e) {
    create_app_screen("Заметки");
    
    lv_obj_t *ta = lv_textarea_create(scr_app);
lv_obj_set_size(ta, SCREEN_W - 20, 120);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 45);
    lv_textarea_set_placeholder_text(ta, "Пишите здесь...");
    lv_textarea_set_one_line(ta, false);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x1A2435), 0);
    lv_obj_set_style_text_color(ta, lv_color_white(), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x4A6FDC), 0);
    
    lv_obj_t *kb = lv_keyboard_create(scr_app);
    lv_obj_set_size(kb, SCREEN_W, 140);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, ta);
}

// ==================== SETUP / LOOP ====================
void setup() {
    Serial.begin(115200);
    
    // Инициализация дисплея
    tft.init();
    tft.setRotation(0);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    // Инициализация SD
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    if (!SD.begin(SD_CS)) {
        Serial.println("SD card mount failed!");
    }
    
    // Инициализация LVGL
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, SCREEN_W * 20);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = SCREEN_W;
    disp_drv.ver_res = SCREEN_H;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);

    // Запуск системы
    show_lock_screen();
}

void loop() {
    lv_timer_handler();
    delay(5);
}
