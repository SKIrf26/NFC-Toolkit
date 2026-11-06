/**
 * ui.cpp - LVGL 9 touch UI for the NFC Toolkit on the LilyGO T-Watch Ultra.
 *
 * Every app screen is built fresh when opened and torn down when left; leaving a
 * screen always stops the NFC engine so the RF field is never left running.
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include <WiFi.h>
#include "ui.h"
#include "nfc_engine.h"
#include "ndef_builder.h"
#include "storage.h"

/* ------------------------------------------------------------------------- */
/* Look & feel                                                               */
/* ------------------------------------------------------------------------- */
#define COL_BG      lv_color_hex(0x0B0F14)
#define COL_CARD    lv_color_hex(0x161C24)
#define COL_TEXT    lv_color_hex(0xE8EDF2)
#define COL_DIM     lv_color_hex(0x8A96A3)
#define COL_OK      lv_color_hex(0x2ECC71)
#define COL_BAD     lv_color_hex(0xE74C3C)
#define COL_WARN    lv_color_hex(0xF5B041)

static const uint32_t kAccent[] = {0x3498DB, 0x27AE60, 0x16A085, 0x8E44AD, 0xE67E22, 0xF1C40F, 0xE74C3C, 0x7F8C8D};

#define FONT_TITLE (&lv_font_montserrat_24)
#define FONT_BIG   (&lv_font_montserrat_28)
#define FONT_BODY  (&lv_font_montserrat_18)
#define FONT_SMALL (&lv_font_montserrat_16)
#define FONT_MONO  (&lv_font_montserrat_14)

static const int kHeaderH = 52;

/* Safe area for the T-Watch Ultra's 2.06" AMOLED (410x502, heavily rounded corners).
 * Values follow LilyGO's factory firmware (~30 px top offset, ~32 px side inset) so nothing
 * lands under a corner. Everything below is laid out inside this rectangle. */
static const int kSafeX = 32;       // left/right inset for content
static const int kSafeTop = 16;     // header top
static const int kSafeBottom = 30;  // bottom inset
static const int kHeaderInset = 34; // header pill left/right inset
static const int kGap = 6;

static int32_t contentTop() { return kSafeTop + kHeaderH + kGap; }
static int32_t contentH() ;
static int32_t contentW() ;
static const size_t kMaxLabelText = 12000; // keep LVGL labels responsive

static int32_t W = 410, H = 502;
static int32_t contentH() { return H - contentTop() - kSafeBottom; }
static int32_t contentW() { return W - 2 * kSafeX; }
static int32_t kbHeight() { return H * 40 / 100; }

/* ------------------------------------------------------------------------- */
/* Screen management                                                         */
/* ------------------------------------------------------------------------- */
static lv_obj_t *g_scr = nullptr;
static lv_obj_t *g_content = nullptr;
static lv_obj_t *g_kb = nullptr;
static lv_timer_t *g_timer = nullptr;
static void (*g_cleanup)() = nullptr;

static void buildHome();
static lv_obj_t *g_panel = nullptr, *g_panelTitle = nullptr, *g_panelBody = nullptr, *g_panelBtn = nullptr,
                *g_panelSpin = nullptr;
static void (*g_panelOnClose)() = nullptr;

static void haptic()
{
    instance.vibrator();
}

static void leaveScreen()
{
    if (g_timer) { lv_timer_delete(g_timer); g_timer = nullptr; }
    engineStop();
    engineSetProgressHook(nullptr);
    if (g_cleanup) { g_cleanup(); g_cleanup = nullptr; }
    g_content = nullptr;
    g_kb = nullptr;
    g_panel = g_panelTitle = g_panelBody = g_panelBtn = g_panelSpin = nullptr;
    g_panelOnClose = nullptr;
}

static lv_obj_t *newScreen()
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_text_color(scr, COL_TEXT, 0);
    lv_obj_set_style_text_font(scr, FONT_BODY, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

static void showScreen(lv_obj_t *scr)
{
    lv_obj_t *old = g_scr;
    g_scr = scr;
    lv_screen_load(scr);
    if (old) lv_obj_delete_async(old);
}

static void backCb(lv_event_t *e)
{
    (void)e;
    leaveScreen();
    buildHome();
}

static lv_obj_t *label(lv_obj_t *parent, const char *txt, const lv_font_t *font = FONT_BODY,
                       lv_color_t color = COL_TEXT, bool wrap = true)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    if (wrap) {
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    }
    lv_label_set_text(l, txt);
    return l;
}

static void setLabelText(lv_obj_t *l, const String &s)
{
    if (!l) return;
    if (s.length() > kMaxLabelText) {
        String t = s.substring(0, kMaxLabelText);
        t += "\n... (truncated on screen - save to keep everything)";
        lv_label_set_text(l, t.c_str());
    } else {
        lv_label_set_text(l, s.c_str());
    }
}

static lv_obj_t *button(lv_obj_t *parent, const char *txt, uint32_t color, lv_event_cb_t cb, void *ud = nullptr,
                        int32_t w = LV_PCT(100))
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_width(b, w);
    lv_obj_set_height(b, 56);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_obj_set_style_pad_hor(b, 8, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, FONT_BODY, 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return b;
}

static void buttonSetText(lv_obj_t *b, const char *txt)
{
    if (b) lv_label_set_text(lv_obj_get_child(b, 0), txt);
}

static lv_obj_t *card(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_width(c, lv_pct(100));
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, COL_CARD, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 14, 0);
    lv_obj_set_style_pad_all(c, 12, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 6, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static lv_obj_t *row(lv_obj_t *parent)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_pad_column(r, 8, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

/* Screen with a header (back + title) and a scrolling content column. */
static lv_obj_t *appScreen(const char *title, uint32_t accent)
{
    lv_obj_t *scr = newScreen();

    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, W - 2 * kHeaderInset, kHeaderH);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, kSafeTop);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_button_create(hdr);
    lv_obj_set_size(back, 64, kHeaderH - 10);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(back, lv_color_black(), 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, backCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *bl = lv_label_create(back);
    lv_obj_set_style_text_font(bl, FONT_TITLE, 0);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_center(bl);

    lv_obj_t *t = lv_label_create(hdr);
    lv_obj_set_style_text_font(t, FONT_TITLE, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_set_width(t, W - 2 * kHeaderInset - 64 - 24);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(t, title);
    lv_obj_align(t, LV_ALIGN_CENTER, 22, 0);

    g_content = lv_obj_create(scr);
    lv_obj_set_size(g_content, contentW(), contentH());
    lv_obj_align(g_content, LV_ALIGN_TOP_MID, 0, contentTop());
    lv_obj_set_style_bg_opa(g_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_content, 0, 0);
    lv_obj_set_style_radius(g_content, 0, 0);
    lv_obj_set_style_pad_all(g_content, 2, 0);
    lv_obj_set_style_pad_bottom(g_content, 16, 0);
    lv_obj_set_style_width(g_content, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_row(g_content, 10, 0);
    lv_obj_set_flex_flow(g_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_content, LV_DIR_VER);
    return scr;
}

/* ------------------------------------------------------------------------- */
/* On-screen keyboard shared by text inputs                                  */
/* ------------------------------------------------------------------------- */
static void kbHide()
{
    if (!g_kb) return;
    lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(g_kb, nullptr);
    if (g_content) lv_obj_set_height(g_content, contentH());
}

static void kbEventCb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) kbHide();
}

static void taEventCb(lv_event_t *e)
{
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (!g_kb) return;
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        int32_t kbH = kbHeight();
        lv_keyboard_set_textarea(g_kb, ta);
        lv_obj_remove_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
        if (g_content) {
            lv_obj_set_height(g_content, H - contentTop() - kbH - kSafeBottom - kGap);
            lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
        }
    } else if (code == LV_EVENT_DEFOCUSED) {
        kbHide();
    }
}

static void ensureKeyboard(lv_obj_t *scr)
{
    g_kb = lv_keyboard_create(scr);
    /* Inset from the bottom corners so the outer keys stay visible. */
    lv_obj_set_size(g_kb, W - 2 * (kSafeX - 4), kbHeight());
    lv_obj_align(g_kb, LV_ALIGN_BOTTOM_MID, 0, -kSafeBottom);
    lv_obj_set_style_radius(g_kb, 18, 0);
    lv_obj_set_style_pad_all(g_kb, 4, 0);
    lv_obj_set_style_text_font(g_kb, FONT_SMALL, 0);
    lv_obj_add_flag(g_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_kb, kbEventCb, LV_EVENT_ALL, nullptr);
}

static lv_obj_t *textField(lv_obj_t *parent, const char *caption, const char *placeholder, const char *initial = "",
                           bool password = false)
{
    label(parent, caption, FONT_SMALL, COL_DIM);
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, lv_pct(100));
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_text(ta, initial ? initial : "");
    lv_textarea_set_password_mode(ta, password);
    lv_obj_set_style_text_font(ta, FONT_BODY, 0);
    lv_obj_add_event_cb(ta, taEventCb, LV_EVENT_ALL, nullptr);
    return ta;
}

static lv_obj_t *dropdown(lv_obj_t *parent, const char *caption, const char *options, lv_event_cb_t cb)
{
    label(parent, caption, FONT_SMALL, COL_DIM);
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_obj_set_width(dd, lv_pct(100));
    lv_dropdown_set_options(dd, options);
    lv_obj_set_style_text_font(dd, FONT_BODY, 0);
    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list) lv_obj_set_style_text_font(list, FONT_BODY, 0);
    if (cb) lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return dd;
}

/* ------------------------------------------------------------------------- */
/* Modal "hold tag" panel used by Write / Copy                               */
/* ------------------------------------------------------------------------- */
static void panelCloseCb(lv_event_t *e)
{
    (void)e;
    engineStop();
    if (g_panel) lv_obj_delete_async(g_panel);
    g_panel = g_panelTitle = g_panelBody = g_panelBtn = g_panelSpin = nullptr;
    if (g_panelOnClose) g_panelOnClose();
}

static void panelOpen(const char *title, const char *body, uint32_t accent)
{
    kbHide();
    g_panel = lv_obj_create(g_scr);
    lv_obj_set_size(g_panel, W, H);
    lv_obj_center(g_panel);
    lv_obj_set_style_bg_color(g_panel, COL_BG, 0);
    lv_obj_set_style_bg_opa(g_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_panel, 0, 0);
    lv_obj_set_style_radius(g_panel, 0, 0);
    lv_obj_set_style_pad_left(g_panel, kSafeX + 4, 0);
    lv_obj_set_style_pad_right(g_panel, kSafeX + 4, 0);
    lv_obj_set_style_pad_top(g_panel, kSafeTop + 24, 0);
    lv_obj_set_style_pad_bottom(g_panel, kSafeBottom + 16, 0);
    lv_obj_set_style_pad_row(g_panel, 12, 0);
    lv_obj_set_flex_flow(g_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_panelSpin = lv_spinner_create(g_panel);
    lv_obj_set_size(g_panelSpin, 70, 70);
    lv_obj_set_style_arc_color(g_panelSpin, lv_color_hex(accent), LV_PART_INDICATOR);

    g_panelTitle = label(g_panel, title, FONT_BIG, COL_TEXT);
    lv_obj_set_style_text_align(g_panelTitle, LV_TEXT_ALIGN_CENTER, 0);
    g_panelBody = label(g_panel, body, FONT_SMALL, COL_DIM);
    g_panelBtn = button(g_panel, "Cancel", 0x34495E, panelCloseCb);
}

static void panelUpdate(EngineEventKind kind, const String &title, const String &body)
{
    if (!g_panel) return;
    lv_label_set_text(g_panelTitle, title.c_str());
    setLabelText(g_panelBody, body);
    if (kind != ENG_EV_STATUS) {
        if (g_panelSpin) { lv_obj_delete(g_panelSpin); g_panelSpin = nullptr; }
        lv_obj_set_style_text_color(g_panelTitle, kind == ENG_EV_SUCCESS ? COL_OK : COL_BAD, 0);
        lv_obj_set_style_text_color(g_panelBody, COL_TEXT, 0);
        buttonSetText(g_panelBtn, "Done");
        lv_obj_set_style_bg_color(g_panelBtn, kind == ENG_EV_SUCCESS ? COL_OK : COL_BAD, 0);
        haptic();
    }
}

static void panelProgress(const String &msg)
{
    if (g_panelBody) {
        lv_label_set_text(g_panelBody, msg.c_str());
        lv_refr_now(NULL);
    }
}

/* ------------------------------------------------------------------------- */
/* Shared state between screens                                              */
/* ------------------------------------------------------------------------- */
static int g_emuPrefillKind = -1; // LilyGoNfcEmulationKind or -1
static String g_emuPrefillA, g_emuPrefillB;

/* ========================================================================= */
/* SCAN                                                                      */
/* ========================================================================= */
static lv_obj_t *sc_status, *sc_info, *sc_sw, *sc_btnEmu, *sc_btnQr, *sc_btnWifi, *sc_qr;
static LilyGoNfcReaderResult sc_last;
static bool sc_pendingHistory = false;
static String sc_qrText, sc_wifiSsid, sc_wifiPass;
static uint32_t sc_wifiStart = 0;

static String formatResult(const LilyGoNfcReaderResult &r)
{
    String s;
    s += "UID: " + String(r.uidText[0] ? r.uidText : "--") + "\n";
    s += "Card: " + String(r.cardType[0] ? r.cardType : "--") + "\n";
    s += String(r.technology) + " / " + r.interfaceName + " / " + r.tagType + "\n";
    if (r.cardInfo[0] && strcmp(r.cardInfo, "--") != 0) s += String(r.cardInfo) + "\n";
    if (r.mifareClassic) s += "MIFARE Classic: sectors are Crypto1-protected (UID only).\n";
    if (r.ndefAttempted) {
        char b[96];
        snprintf(b, sizeof(b), "NDEF: %s  v%u.%u  %lu/%lu B\n", r.ndefStateText[0] ? r.ndefStateText : "--",
                 r.ndefMajorVersion, r.ndefMinorVersion, (unsigned long)r.ndefMessageLen,
                 (unsigned long)r.ndefAreaLen);
        s += b;
    }
    if (r.lastError != ST_ERR_NONE && r.errorText[0]) s += "Error: " + String(r.errorText) + "\n";
    for (uint8_t i = 0; i < r.recordCount; i++) {
        const LilyGoNfcRecord &rec = r.records[i];
        s += "\n#" + String(rec.index) + " " + (rec.title[0] ? rec.title : lilygoNfcRecordKindName(rec.kind)) + "\n";
        if (rec.kind == LILYGO_NFC_RECORD_WIFI) {
            s += "SSID: " + String(rec.wifiSsid) + "\nPassword: " + String(rec.wifiPassword) + "\n";
        } else if (rec.value[0]) {
            s += String(rec.value) + "\n";
        }
        if (rec.detail[0]) s += String(rec.detail) + "\n";
    }
    return s;
}

static String resultSummary(const LilyGoNfcReaderResult &r)
{
    for (uint8_t i = 0; i < r.recordCount; i++) {
        const LilyGoNfcRecord &rec = r.records[i];
        if (rec.kind == LILYGO_NFC_RECORD_WIFI) return "Wi-Fi " + String(rec.wifiSsid);
        if (rec.value[0]) return String(lilygoNfcRecordKindName(rec.kind)) + ": " + rec.value;
    }
    return r.ndefStateText[0] ? String("NDEF ") + r.ndefStateText : String("UID only");
}

static void scanUpdateActions()
{
    sc_qrText = "";
    sc_wifiSsid = sc_wifiPass = "";
    g_emuPrefillKind = -1;
    for (uint8_t i = 0; i < sc_last.recordCount; i++) {
        const LilyGoNfcRecord &rec = sc_last.records[i];
        if (rec.kind == LILYGO_NFC_RECORD_URI && g_emuPrefillKind < 0) {
            g_emuPrefillKind = LILYGO_NFC_EMULATION_URL;
            g_emuPrefillA = rec.value;
            sc_qrText = rec.value;
        } else if (rec.kind == LILYGO_NFC_RECORD_TEXT && g_emuPrefillKind < 0) {
            g_emuPrefillKind = LILYGO_NFC_EMULATION_TEXT;
            g_emuPrefillA = rec.value;
            if (!sc_qrText.length()) sc_qrText = rec.value;
        } else if (rec.kind == LILYGO_NFC_RECORD_WIFI) {
            sc_wifiSsid = rec.wifiSsid;
            sc_wifiPass = rec.wifiPassword;
            if (g_emuPrefillKind < 0) {
                g_emuPrefillKind = LILYGO_NFC_EMULATION_WIFI;
                g_emuPrefillA = rec.wifiSsid;
                g_emuPrefillB = rec.wifiPassword;
            }
        }
    }
    if (!sc_qrText.length()) sc_qrText = sc_last.uidText;
    if (sc_btnEmu) {
        if (g_emuPrefillKind >= 0) lv_obj_remove_state(sc_btnEmu, LV_STATE_DISABLED);
        else lv_obj_add_state(sc_btnEmu, LV_STATE_DISABLED);
    }
    if (sc_btnQr) lv_obj_remove_state(sc_btnQr, LV_STATE_DISABLED);
    if (sc_btnWifi) {
        if (sc_wifiSsid.length()) lv_obj_remove_flag(sc_btnWifi, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(sc_btnWifi, LV_OBJ_FLAG_HIDDEN);
    }
}

static void scanSaveHistory()
{
    historyAdd(sc_last.uidText, sc_last.cardType, resultSummary(sc_last));
    sc_pendingHistory = false;
}

static void onScanEvent(LilyGoNfcEvent ev, const LilyGoNfcReaderResult &r, void *ud)
{
    (void)ud;
    if (!sc_status) return;
    memcpy(&sc_last, &r, sizeof(sc_last));
    switch (ev) {
    case LILYGO_NFC_EVENT_STARTED:
        lv_label_set_text(sc_status, "Hold a tag to the watch");
        lv_obj_set_style_text_color(sc_status, COL_TEXT, 0);
        break;
    case LILYGO_NFC_EVENT_START_FAILED:
        lv_label_set_text(sc_status, "NFC start failed");
        lv_obj_set_style_text_color(sc_status, COL_BAD, 0);
        break;
    case LILYGO_NFC_EVENT_CARD_DETECTED:
        haptic();
        lv_label_set_text(sc_status, LV_SYMBOL_OK " Tag detected");
        lv_obj_set_style_text_color(sc_status, COL_OK, 0);
        setLabelText(sc_info, formatResult(r));
        sc_pendingHistory = true;
        scanUpdateActions();
        break;
    case LILYGO_NFC_EVENT_NDEF_READ:
    case LILYGO_NFC_EVENT_NDEF_UNSUPPORTED:
    case LILYGO_NFC_EVENT_NDEF_ERROR:
        setLabelText(sc_info, formatResult(r));
        scanUpdateActions();
        scanSaveHistory();
        break;
    case LILYGO_NFC_EVENT_CARD_RELEASED:
        if (sc_pendingHistory) scanSaveHistory();
        lv_label_set_text(sc_status, "Tag removed - ready");
        lv_obj_set_style_text_color(sc_status, COL_DIM, 0);
        break;
    default:
        break;
    }
}

static void scanRestart()
{
    bool lp = sc_sw && lv_obj_has_state(sc_sw, LV_STATE_CHECKED);
    if (!engineStartScan(lp, onScanEvent)) {
        lv_label_set_text(sc_status, "NFC start failed");
        lv_obj_set_style_text_color(sc_status, COL_BAD, 0);
    }
}

static void scanSwCb(lv_event_t *e) { (void)e; scanRestart(); }

static void buildEmulate();
static void scanEmuCb(lv_event_t *e) { (void)e; leaveScreen(); buildEmulate(); }

static void qrCloseCb(lv_event_t *e)
{
    (void)e;
    if (sc_qr) { lv_obj_delete_async(sc_qr); sc_qr = nullptr; }
}

static void scanQrCb(lv_event_t *e)
{
    (void)e;
    if (!sc_qrText.length() || sc_qr) return;
    sc_qr = lv_obj_create(g_scr);
    lv_obj_set_size(sc_qr, W, H);
    lv_obj_center(sc_qr);
    lv_obj_set_style_bg_color(sc_qr, lv_color_white(), 0);
    lv_obj_set_style_radius(sc_qr, 0, 0);
    lv_obj_set_flex_flow(sc_qr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sc_qr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(sc_qr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sc_qr, qrCloseCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *qr = lv_qrcode_create(sc_qr);
    int32_t sz = (W < H ? W : H) - 80;
    lv_qrcode_set_size(qr, sz);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, sc_qrText.c_str(), sc_qrText.length());
    lv_obj_t *l = label(sc_qr, "Tap to close", FONT_SMALL, lv_color_hex(0x444444), false);
    (void)l;
}

static void scanWifiTimer(lv_timer_t *t)
{
    (void)t;
    if (!sc_status || !sc_wifiStart) return;
    wl_status_t st = WiFi.status();
    if (st == WL_CONNECTED) {
        String s = LV_SYMBOL_WIFI " Connected  " + WiFi.localIP().toString();
        lv_label_set_text(sc_status, s.c_str());
        lv_obj_set_style_text_color(sc_status, COL_OK, 0);
        sc_wifiStart = 0;
    } else if (millis() - sc_wifiStart > 20000) {
        lv_label_set_text(sc_status, "Wi-Fi connect failed");
        lv_obj_set_style_text_color(sc_status, COL_BAD, 0);
        WiFi.disconnect(true);
        sc_wifiStart = 0;
    }
}

static void scanWifiCb(lv_event_t *e)
{
    (void)e;
    if (!sc_wifiSsid.length()) return;
    WiFi.mode(WIFI_STA);
    WiFi.begin(sc_wifiSsid.c_str(), sc_wifiPass.c_str());
    sc_wifiStart = millis();
    lv_label_set_text(sc_status, "Joining Wi-Fi...");
    lv_obj_set_style_text_color(sc_status, COL_WARN, 0);
}

static void scanCleanup()
{
    sc_status = sc_info = sc_sw = sc_btnEmu = sc_btnQr = sc_btnWifi = sc_qr = nullptr;
    sc_wifiStart = 0;
}

static void buildScan()
{
    lv_obj_t *scr = appScreen("Scan", kAccent[0]);
    g_cleanup = scanCleanup;

    sc_status = label(g_content, "Starting...", FONT_BIG, COL_TEXT);
    lv_obj_t *r = row(g_content);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *l = label(r, "Low-power wake-up", FONT_SMALL, COL_DIM, false);
    (void)l;
    sc_sw = lv_switch_create(r);
    lv_obj_add_event_cb(sc_sw, scanSwCb, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *c = card(g_content);
    sc_info = label(c, "Supports NFC-A/B/F/V and ST25TB:\nNTAG, Ultralight, MIFARE (UID), DESFire, "
                       "ISO15693/ICODE/ST25DV, FeliCa, ST25TB, phones.", FONT_SMALL, COL_DIM);

    sc_btnEmu = button(g_content, LV_SYMBOL_UPLOAD " Emulate this tag", kAccent[4], scanEmuCb);
    lv_obj_add_state(sc_btnEmu, LV_STATE_DISABLED);
    sc_btnQr = button(g_content, "Show as QR code", 0x34495E, scanQrCb);
    lv_obj_add_state(sc_btnQr, LV_STATE_DISABLED);
    sc_btnWifi = button(g_content, LV_SYMBOL_WIFI " Join this Wi-Fi", kAccent[1], scanWifiCb);
    lv_obj_add_flag(sc_btnWifi, LV_OBJ_FLAG_HIDDEN);

    g_timer = lv_timer_create(scanWifiTimer, 500, nullptr);
    showScreen(scr);
    scanRestart();
}

/* ========================================================================= */
/* WRITE                                                                     */
/* ========================================================================= */
enum WriteType { WT_URL, WT_TEXT, WT_WIFI, WT_TEL, WT_SMS, WT_EMAIL, WT_VCARD, WT_GEO, WT_APP, WT_ERASE, WT_FORMAT };
static lv_obj_t *wr_type, *wr_fields, *wr_f[3], *wr_auth;
static uint8_t wr_msg[1024];

static String urlEncode(const char *s)
{
    String o;
    for (; *s; s++) {
        char c = *s;
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '@' || c == '+') o += c;
        else { char b[4]; snprintf(b, sizeof(b), "%%%02X", (uint8_t)c); o += b; }
    }
    return o;
}

static const char *taText(int i) { return wr_f[i] ? lv_textarea_get_text(wr_f[i]) : ""; }

static void writeRebuildFields()
{
    lv_obj_clean(wr_fields);
    wr_f[0] = wr_f[1] = wr_f[2] = nullptr;
    wr_auth = nullptr;
    switch ((WriteType)lv_dropdown_get_selected(wr_type)) {
    case WT_URL: wr_f[0] = textField(wr_fields, "Web address", "https://example.com"); break;
    case WT_TEXT: wr_f[0] = textField(wr_fields, "Text", "Hello from my watch"); break;
    case WT_WIFI:
        wr_f[0] = textField(wr_fields, "Network name (SSID)", "MyWiFi");
        wr_f[1] = textField(wr_fields, "Password", "password", "", false);
        wr_auth = dropdown(wr_fields, "Security", "WPA2-Personal\nWPA/WPA2 mixed\nOpen (no password)", nullptr);
        break;
    case WT_TEL: wr_f[0] = textField(wr_fields, "Phone number", "+15551234567"); break;
    case WT_SMS:
        wr_f[0] = textField(wr_fields, "Phone number", "+15551234567");
        wr_f[1] = textField(wr_fields, "Message", "Hi!");
        break;
    case WT_EMAIL:
        wr_f[0] = textField(wr_fields, "Email address", "me@example.com");
        wr_f[1] = textField(wr_fields, "Subject (optional)", "");
        wr_f[2] = textField(wr_fields, "Body (optional)", "");
        break;
    case WT_VCARD:
        wr_f[0] = textField(wr_fields, "Full name", "Jane Doe");
        wr_f[1] = textField(wr_fields, "Phone", "+15551234567");
        wr_f[2] = textField(wr_fields, "Email", "jane@example.com");
        break;
    case WT_GEO:
        wr_f[0] = textField(wr_fields, "Latitude", "41.8781");
        wr_f[1] = textField(wr_fields, "Longitude", "-87.6298");
        break;
    case WT_APP: wr_f[0] = textField(wr_fields, "Android package", "com.android.chrome"); break;
    case WT_ERASE:
        label(wr_fields, "Replaces the tag's content with an empty NDEF record. The tag stays writable.", FONT_SMALL, COL_DIM);
        break;
    case WT_FORMAT:
        label(wr_fields, "NFC-Forum formats a blank Type 2 / Type 5 tag (writes the Capability Container) "
                         "and leaves an empty NDEF message.", FONT_SMALL, COL_DIM);
        break;
    }
}

static void writeTypeCb(lv_event_t *e) { (void)e; kbHide(); writeRebuildFields(); }

static void onWriteEvent(EngineEventKind k, const String &title, const String &body) { panelUpdate(k, title, body); }

static void writeGoCb(lv_event_t *e)
{
    (void)e;
    WriteType t = (WriteType)lv_dropdown_get_selected(wr_type);
    if (t == WT_ERASE || t == WT_FORMAT) {
        panelOpen("Hold tag to watch", t == WT_ERASE ? "Erasing..." : "Formatting...", kAccent[1]);
        engineSetProgressHook(panelProgress);
        engineStartWrite(t == WT_ERASE ? WRITE_ERASE : WRITE_FORMAT, nullptr, 0, onWriteEvent);
        return;
    }

    NdefBuilder nb(wr_msg, sizeof(wr_msg));
    nb.reset();
    bool ok = true;
    String a = taText(0), b = taText(1), c = taText(2);
    if (t != WT_WIFI && !a.length()) ok = false;
    switch (t) {
    case WT_URL:
        if (a.indexOf(':') < 0) a = "https://" + a;
        ok = ok && nb.addUri(a.c_str());
        break;
    case WT_TEXT: ok = ok && nb.addText(a.c_str()); break;
    case WT_WIFI: {
        uint16_t sel = wr_auth ? lv_dropdown_get_selected(wr_auth) : 0;
        uint16_t auth = sel == 0 ? WSC_AUTH_WPA2_PSK : sel == 1 ? WSC_AUTH_WPA_WPA2 : WSC_AUTH_OPEN;
        uint16_t enc = sel == 0 ? WSC_ENC_AES : sel == 1 ? WSC_ENC_AES_TKIP : WSC_ENC_NONE;
        ok = a.length() && nb.addWifi(a.c_str(), sel == 2 ? "" : b.c_str(), auth, enc);
        break;
    }
    case WT_TEL: ok = ok && nb.addUri(("tel:" + a).c_str()); break;
    case WT_SMS: {
        String u = "sms:" + a;
        if (b.length()) u += "?body=" + urlEncode(b.c_str());
        ok = ok && nb.addUri(u.c_str());
        break;
    }
    case WT_EMAIL: {
        String u = "mailto:" + a;
        String q;
        if (b.length()) q += "subject=" + urlEncode(b.c_str());
        if (c.length()) q += String(q.length() ? "&" : "") + "body=" + urlEncode(c.c_str());
        if (q.length()) u += "?" + q;
        ok = ok && nb.addUri(u.c_str());
        break;
    }
    case WT_VCARD: {
        String v = "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:" + a + "\r\nN:;" + a + ";;;\r\n";
        if (b.length()) v += "TEL:" + b + "\r\n";
        if (c.length()) v += "EMAIL:" + c + "\r\n";
        v += "END:VCARD\r\n";
        ok = ok && nb.addMime("text/vcard", (const uint8_t *)v.c_str(), v.length());
        break;
    }
    case WT_GEO: ok = ok && b.length() && nb.addUri(("geo:" + a + "," + b).c_str()); break;
    case WT_APP: ok = ok && nb.addAndroidApp(a.c_str()); break;
    default: ok = false; break;
    }
    size_t len = ok ? nb.finish() : 0;
    if (!len) {
        panelOpen("Check the fields", nb.overflow() ? "Message too long." : "Please fill in the required fields.", kAccent[1]);
        panelUpdate(ENG_EV_FAIL, "Check the fields", nb.overflow() ? "Message too long." : "Please fill in the required fields.");
        return;
    }
    String body = String(len) + " byte NDEF message:\n" + ndefDescribeMessage(wr_msg, len) + "\nHold a tag to the watch...";
    panelOpen("Hold tag to watch", body.c_str(), kAccent[1]);
    engineSetProgressHook(panelProgress);
    engineStartWrite(WRITE_MESSAGE, wr_msg, len, onWriteEvent);
}

static void writeCleanup() { wr_type = wr_fields = wr_auth = nullptr; wr_f[0] = wr_f[1] = wr_f[2] = nullptr; }

static void buildWrite()
{
    lv_obj_t *scr = appScreen("Write", kAccent[1]);
    g_cleanup = writeCleanup;
    wr_type = dropdown(g_content, "What to write",
                       "Web link (URL)\nText\nWi-Fi network\nPhone call\nSMS\nEmail\nContact (vCard)\n"
                       "Map location\nLaunch Android app\nErase tag\nFormat blank tag",
                       writeTypeCb);
    wr_fields = lv_obj_create(g_content);
    lv_obj_set_size(wr_fields, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wr_fields, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wr_fields, 0, 0);
    lv_obj_set_style_pad_all(wr_fields, 0, 0);
    lv_obj_set_style_pad_row(wr_fields, 6, 0);
    lv_obj_set_flex_flow(wr_fields, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(wr_fields, LV_OBJ_FLAG_SCROLLABLE);
    writeRebuildFields();
    button(g_content, LV_SYMBOL_EDIT " Write to tag", kAccent[1], writeGoCb);
    ensureKeyboard(scr);
    showScreen(scr);
}

/* ========================================================================= */
/* COPY                                                                      */
/* ========================================================================= */
static lv_obj_t *cp_summary, *cp_btnWrite;

static void copyRefresh()
{
    if (!cp_summary) return;
    if (engineHasCopyBuffer()) {
        String s = "Captured " + String(engineCopyLength()) + " bytes:\n" + engineCopySummary();
        setLabelText(cp_summary, s);
        lv_obj_remove_state(cp_btnWrite, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(cp_summary, "Nothing captured yet.");
        lv_obj_add_state(cp_btnWrite, LV_STATE_DISABLED);
    }
}

static void onCopyEvent(EngineEventKind k, const String &title, const String &body)
{
    panelUpdate(k, title, body);
    if (k != ENG_EV_STATUS) copyRefresh();
}

static void copyReadCb(lv_event_t *e)
{
    (void)e;
    panelOpen("Hold SOURCE tag", "Its NDEF message will be captured into memory.", kAccent[2]);
    engineSetProgressHook(panelProgress);
    engineStartCopyRead(onCopyEvent);
}

static void copyWriteCb(lv_event_t *e)
{
    (void)e;
    panelOpen("Hold TARGET tag", "Present a different, writable tag.", kAccent[2]);
    engineSetProgressHook(panelProgress);
    engineStartCopyWrite(onCopyEvent);
}

static void copyCleanup() { cp_summary = cp_btnWrite = nullptr; }

static void buildCopy()
{
    lv_obj_t *scr = appScreen("Copy tag", kAccent[2]);
    g_cleanup = copyCleanup;
    g_panelOnClose = copyRefresh;
    label(g_content, "Clones the NDEF content (links, text, Wi-Fi, contacts...) from one tag to another. "
                     "Works across tag types, e.g. NTAG -> ISO15693. UIDs are not cloned.", FONT_SMALL, COL_DIM);
    button(g_content, "1. Read source tag", kAccent[2], copyReadCb);
    lv_obj_t *c = card(g_content);
    cp_summary = label(c, "", FONT_SMALL, COL_TEXT);
    cp_btnWrite = button(g_content, "2. Write to new tag", kAccent[1], copyWriteCb);
    copyRefresh();
    showScreen(scr);
}

/* ========================================================================= */
/* DUMP                                                                      */
/* ========================================================================= */
static lv_obj_t *dp_status, *dp_text, *dp_btnSave;

static void dumpProgress(const String &msg)
{
    if (dp_status) {
        lv_label_set_text(dp_status, msg.c_str());
        lv_refr_now(NULL);
    }
}

static void onDumpEvent(EngineEventKind k, const String &title, const String &body)
{
    if (!dp_status) return;
    if (k == ENG_EV_STATUS) { lv_label_set_text(dp_status, title.c_str()); return; }
    haptic();
    lv_label_set_text(dp_status, title.c_str());
    lv_obj_set_style_text_color(dp_status, k == ENG_EV_SUCCESS ? COL_OK : COL_BAD, 0);
    setLabelText(dp_text, body);
    if (k == ENG_EV_SUCCESS) {
        lv_obj_remove_state(dp_btnSave, LV_STATE_DISABLED);
        historyAdd(engineLastUid(), engineLastType(), "memory dump");
    }
}

static void dumpStart()
{
    lv_label_set_text(dp_status, "Hold a tag to the watch");
    lv_obj_set_style_text_color(dp_status, COL_TEXT, 0);
    lv_obj_add_state(dp_btnSave, LV_STATE_DISABLED);
    engineSetProgressHook(dumpProgress);
    engineStartDump(onDumpEvent);
}

static void dumpAgainCb(lv_event_t *e) { (void)e; dumpStart(); }

static void dumpSaveCb(lv_event_t *e)
{
    (void)e;
    size_t binLen = 0;
    const uint8_t *bin = engineLastDumpBin(&binLen);
    String p1 = storageSaveText("dump", engineLastUid(), engineLastDumpText());
    String p2 = binLen ? storageSaveBin("dump", engineLastUid(), bin, binLen) : String("");
    String msg = p1.length() ? ("Saved to " + String(storageName()) + ":\n" + p1 + (p2.length() ? "\n" + p2 : ""))
                             : String("Save failed - no storage available");
    lv_label_set_text(dp_status, msg.c_str());
    lv_obj_set_style_text_color(dp_status, p1.length() ? COL_OK : COL_BAD, 0);
}

static void dumpCleanup() { dp_status = dp_text = dp_btnSave = nullptr; }

static void buildDump()
{
    lv_obj_t *scr = appScreen("Dump", kAccent[3]);
    g_cleanup = dumpCleanup;
    dp_status = label(g_content, "", FONT_BODY, COL_TEXT);
    lv_obj_t *r = row(g_content);
    lv_obj_t *b1 = button(r, LV_SYMBOL_REFRESH " Again", kAccent[3], dumpAgainCb, nullptr, 0);
    dp_btnSave = button(r, LV_SYMBOL_SAVE " Save", 0x34495E, dumpSaveCb, nullptr, 0);
    lv_obj_set_flex_grow(b1, 1);
    lv_obj_set_flex_grow(dp_btnSave, 1);
    lv_obj_t *c = card(g_content);
    dp_text = label(c, "Reads full memory of NTAG/Ultralight (+ originality signature), ISO15693, ST25TB, "
                       "FeliCa Lite blocks and ISO-DEP NDEF files.", FONT_MONO, COL_DIM);
    showScreen(scr);
    dumpStart();
}

/* ========================================================================= */
/* EMULATE                                                                   */
/* ========================================================================= */
static lv_obj_t *em_kind, *em_tech, *em_fields, *em_a, *em_b, *em_btn, *em_status;
static String em_strA, em_strB, em_name;

static void emRebuild()
{
    lv_obj_clean(em_fields);
    em_a = em_b = nullptr;
    int k = lv_dropdown_get_selected(em_kind);
    String pa = (g_emuPrefillKind == k) ? g_emuPrefillA : String("");
    String pb = (g_emuPrefillKind == k) ? g_emuPrefillB : String("");
    if (k == LILYGO_NFC_EMULATION_URL) {
        em_a = textField(em_fields, "URL", "https://example.com", pa.length() ? pa.c_str() : "https://lilygo.cc");
    } else if (k == LILYGO_NFC_EMULATION_TEXT) {
        em_a = textField(em_fields, "Text", "Hello", pa.length() ? pa.c_str() : "Hello from T-Watch Ultra");
    } else {
        em_a = textField(em_fields, "SSID", "MyWiFi", pa.c_str());
        em_b = textField(em_fields, "Password (empty = open)", "", pb.c_str());
    }
}

static void emKindCb(lv_event_t *e) { (void)e; kbHide(); emRebuild(); }

static void emTimer(lv_timer_t *t)
{
    (void)t;
    if (!em_status) return;
    if (engineMode() != ENG_EMULATE) return;
    const LilyGoNfcEmulationStatus &s = LilyGoNfc.emulationStatus();
    char b[320];
    snprintf(b, sizeof(b), "%s%s\n%s\nPayload: %s\nNDEF file: %lu B\nReader: %s %s\nReads: %lu   Exchanges: %lu%s%s",
             s.readerActive ? LV_SYMBOL_OK " Reader connected" : "Waiting for a phone/reader...",
             "", lilygoNfcEmulationTechName(s.tech), s.payloadPreview, (unsigned long)s.ndefFileLen,
             s.activeTechnology[0] ? s.activeTechnology : "--", s.activeInterface[0] ? s.activeInterface : "",
             (unsigned long)s.readCount, (unsigned long)s.exchangeCount,
             s.lastError != ST_ERR_NONE && s.errorText[0] ? "\nLast error: " : "",
             s.lastError != ST_ERR_NONE ? s.errorText : "");
    lv_label_set_text(em_status, b);
    lv_obj_set_style_text_color(em_status, s.readerActive ? COL_OK : COL_TEXT, 0);
}

static void onEmuEvent(LilyGoNfcEvent ev, const LilyGoNfcEmulationStatus &s, void *ud)
{
    (void)s; (void)ud;
    if (ev == LILYGO_NFC_EVENT_EMULATION_ACTIVATED) haptic();
}

static void emStartStopCb(lv_event_t *e)
{
    (void)e;
    kbHide();
    if (engineMode() == ENG_EMULATE) {
        engineStop();
        buttonSetText(em_btn, LV_SYMBOL_PLAY " Start emulating");
        lv_obj_set_style_bg_color(em_btn, lv_color_hex(kAccent[4]), 0);
        lv_label_set_text(em_status, "Stopped.");
        return;
    }
    LilyGoNfcEmulationConfig cfg = {};
    cfg.kind = (LilyGoNfcEmulationKind)lv_dropdown_get_selected(em_kind);
    uint16_t ts = lv_dropdown_get_selected(em_tech);
    cfg.tech = ts == 0 ? LILYGO_NFC_EMULATION_TECH_NFCA_T4T : ts == 1 ? LILYGO_NFC_EMULATION_TECH_NFCF_T3T
                                                                     : LILYGO_NFC_EMULATION_TECH_MIXED;
    em_strA = em_a ? lv_textarea_get_text(em_a) : "";
    em_strB = em_b ? lv_textarea_get_text(em_b) : "";
    em_name = String("Watch ") + lilygoNfcEmulationKindName(cfg.kind);
    cfg.templateName = em_name.c_str();
    if (cfg.kind == LILYGO_NFC_EMULATION_URL) cfg.url = em_strA.c_str();
    else if (cfg.kind == LILYGO_NFC_EMULATION_TEXT) cfg.text = em_strA.c_str();
    else {
        cfg.wifiSsid = em_strA.c_str();
        cfg.wifiPassword = em_strB.c_str();
        if (!em_strB.length()) { cfg.wifiAuthenticationType = WSC_AUTH_OPEN; cfg.wifiEncryptionType = WSC_ENC_NONE; }
    }
    cfg.callback = onEmuEvent;
    if (engineStartEmulation(cfg)) {
        buttonSetText(em_btn, LV_SYMBOL_STOP " Stop");
        lv_obj_set_style_bg_color(em_btn, COL_BAD, 0);
        lv_label_set_text(em_status, "Emulating - hold a phone to the watch.");
    } else {
        lv_label_set_text(em_status, "Could not start emulation.");
    }
}

static void emCleanup() { em_kind = em_tech = em_fields = em_a = em_b = em_btn = em_status = nullptr; }

static void buildEmulate()
{
    lv_obj_t *scr = appScreen("Emulate", kAccent[4]);
    g_cleanup = emCleanup;
    em_kind = dropdown(g_content, "Act as a tag containing", "Web link (URL)\nText\nWi-Fi network", emKindCb);
    if (g_emuPrefillKind >= 0) lv_dropdown_set_selected(em_kind, g_emuPrefillKind);
    em_tech = dropdown(g_content, "Technology",
                       "NFC-A Type 4 (iPhone + Android)\nNFC-F Type 3 (FeliCa)\nMixed A + F", nullptr);
    em_fields = lv_obj_create(g_content);
    lv_obj_set_size(em_fields, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(em_fields, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(em_fields, 0, 0);
    lv_obj_set_style_pad_all(em_fields, 0, 0);
    lv_obj_set_style_pad_row(em_fields, 6, 0);
    lv_obj_set_flex_flow(em_fields, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(em_fields, LV_OBJ_FLAG_SCROLLABLE);
    emRebuild();
    em_btn = button(g_content, LV_SYMBOL_PLAY " Start emulating", kAccent[4], emStartStopCb);
    lv_obj_t *c = card(g_content);
    em_status = label(c, "The watch becomes an NFC tag that phones can read.", FONT_SMALL, COL_TEXT);
    ensureKeyboard(scr);
    g_timer = lv_timer_create(emTimer, 300, nullptr);
    showScreen(scr);
}

/* ========================================================================= */
/* FIELD DETECT                                                              */
/* ========================================================================= */
static lv_obj_t *fd_circle, *fd_label, *fd_count;
static bool fd_last = false;
static uint32_t fd_onSince = 0, fd_totalMs = 0;

static void fieldTimer(lv_timer_t *t)
{
    (void)t;
    if (!fd_circle) return;
    bool p = engineFieldPresent();
    if (p != fd_last) {
        fd_last = p;
        if (p) { haptic(); fd_onSince = millis(); }
        else fd_totalMs += millis() - fd_onSince;
        lv_obj_set_style_bg_color(fd_circle, p ? COL_OK : lv_color_hex(0x2C3E50), 0);
        lv_label_set_text(fd_label, p ? "RF FIELD DETECTED" : "No field");
        lv_obj_set_style_text_color(fd_label, p ? COL_OK : COL_DIM, 0);
    }
    char b[96];
    uint32_t tot = fd_totalMs + (p ? millis() - fd_onSince : 0);
    snprintf(b, sizeof(b), "Field events: %lu\nTotal time in field: %.1f s", (unsigned long)engineFieldEvents(),
             tot / 1000.0f);
    lv_label_set_text(fd_count, b);
}

static void fieldCleanup() { fd_circle = fd_label = fd_count = nullptr; }

static void buildField()
{
    lv_obj_t *scr = appScreen("Field detect", kAccent[5]);
    g_cleanup = fieldCleanup;
    lv_obj_set_flex_align(g_content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    fd_circle = lv_obj_create(g_content);
    int32_t d = (W < H ? W : H) / 2;
    lv_obj_set_size(fd_circle, d, d);
    lv_obj_set_style_radius(fd_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(fd_circle, lv_color_hex(0x2C3E50), 0);
    lv_obj_set_style_border_width(fd_circle, 0, 0);
    lv_obj_remove_flag(fd_circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *ic = lv_label_create(fd_circle);
    lv_obj_set_style_text_font(ic, &lv_font_montserrat_48, 0);
    lv_label_set_text(ic, LV_SYMBOL_WIFI);
    lv_obj_center(ic);
    fd_label = label(g_content, "No field", FONT_BIG, COL_DIM);
    lv_obj_set_style_text_align(fd_label, LV_TEXT_ALIGN_CENTER, 0);
    fd_count = label(g_content, "", FONT_BODY, COL_TEXT);
    lv_obj_set_style_text_align(fd_count, LV_TEXT_ALIGN_CENTER, 0);
    label(g_content, "Passive detector (watch's own field is OFF). Lights up near phones in NFC mode, "
                     "payment terminals, door/transit readers and other NFC readers.", FONT_SMALL, COL_DIM);
    fd_last = false;
    fd_totalMs = 0;
    engineStartField();
    g_timer = lv_timer_create(fieldTimer, 40, nullptr);
    showScreen(scr);
}

/* ========================================================================= */
/* RF LAB (diagnostics)                                                      */
/* ========================================================================= */
static lv_obj_t *rf_chip, *rf_volt, *rf_sw, *rf_amp, *rf_ampBar, *rf_ph, *rf_phBar, *rf_chart, *rf_prox;
static lv_chart_series_t *rf_sA, *rf_sP;
static int rf_baseAmp = -1, rf_basePh = -1;
static DiagData rf_last;

static void rfTimer(lv_timer_t *t)
{
    (void)t;
    if (!rf_chip) return;
    bool want = lv_obj_has_state(rf_sw, LV_STATE_CHECKED);
    DiagData d;
    if (!engineDiagMeasure(d, want)) return;
    rf_last = d;
    char b[200];
    snprintf(b, sizeof(b), "Chip: %s  rev %u  (ID 0x%02X)\nExternal field: %s", d.isB ? "ST25R3916B" : "ST25R3916",
             d.icRev, d.icIdentity, d.extField ? "YES" : "no");
    lv_label_set_text(rf_chip, b);
    snprintf(b, sizeof(b), "VDD %u mV   VDD_RF %u mV\nVDD_A %u mV   VDD_D %u mV\nVDD_AM %u mV%s%s",
             d.vdd_mV, d.vddRf_mV, d.vddA_mV, d.vddD_mV, d.vddAm_mV,
             d.capSupported ? "\nCap sensor: " : "\nCap sensor: n/a on 3916B",
             d.capSupported ? (String(d.capacitance) + (d.fieldOn ? " (last, field off)" : "")).c_str() : "");
    lv_label_set_text(rf_volt, b);

    if (want && d.fieldError) {
        lv_obj_remove_state(rf_sw, LV_STATE_CHECKED);
        lv_label_set_text(rf_prox, "Field refused: external field present (collision avoidance).");
    }
    snprintf(b, sizeof(b), "Amplitude: %u  (~%u mV)", d.amplitude, (unsigned)(d.amplitude * 13));
    lv_label_set_text(rf_amp, b);
    lv_bar_set_value(rf_ampBar, d.amplitude, LV_ANIM_OFF);
    snprintf(b, sizeof(b), "Phase: %u  (~%d deg)", d.phase, (int)(d.phase * 180 / 255));
    lv_label_set_text(rf_ph, b);
    lv_bar_set_value(rf_phBar, d.phase, LV_ANIM_OFF);

    if (d.fieldOn) {
        lv_chart_set_next_value(rf_chart, rf_sA, d.amplitude);
        lv_chart_set_next_value(rf_chart, rf_sP, d.phase);
        if (rf_baseAmp >= 0) {
            int da = (int)d.amplitude - rf_baseAmp, dp = (int)d.phase - rf_basePh;
            int score = abs(da) * 2 + abs(dp);
            snprintf(b, sizeof(b), "Detuning vs baseline: dA %+d  dP %+d\n%s", da, dp,
                     score > 12 ? "Something is on the antenna (tag/metal/hand)" : score > 4 ? "Slight change" : "Clear");
            lv_label_set_text(rf_prox, b);
            lv_obj_set_style_text_color(rf_prox, score > 12 ? COL_WARN : COL_TEXT, 0);
        }
    }
}

static void rfBaseCb(lv_event_t *e)
{
    (void)e;
    if (!rf_last.fieldOn) {
        lv_label_set_text(rf_prox, "Turn the RF carrier on first.");
        return;
    }
    rf_baseAmp = rf_last.amplitude;
    rf_basePh = rf_last.phase;
    lv_label_set_text(rf_prox, "Baseline stored. Bring a tag close to see the antenna detune.");
}

static void rfCleanup()
{
    rf_chip = rf_volt = rf_sw = rf_amp = rf_ampBar = rf_ph = rf_phBar = rf_chart = rf_prox = nullptr;
    rf_sA = rf_sP = nullptr;
}

static lv_obj_t *mkBar(lv_obj_t *parent, uint32_t color)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_size(b, lv_pct(100), 16);
    lv_bar_set_range(b, 0, 255);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), LV_PART_INDICATOR);
    return b;
}

static void buildRfLab()
{
    lv_obj_t *scr = appScreen("RF Lab", kAccent[6]);
    g_cleanup = rfCleanup;
    rf_baseAmp = rf_basePh = -1;
    memset(&rf_last, 0, sizeof(rf_last));

    lv_obj_t *c1 = card(g_content);
    rf_chip = label(c1, "...", FONT_SMALL);
    rf_volt = label(c1, "", FONT_SMALL, COL_DIM);

    lv_obj_t *r = row(g_content);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(r, "RF carrier (13.56 MHz)", FONT_SMALL, COL_TEXT, false);
    rf_sw = lv_switch_create(r);

    lv_obj_t *c2 = card(g_content);
    rf_amp = label(c2, "Amplitude: -", FONT_SMALL);
    rf_ampBar = mkBar(c2, 0x3498DB);
    rf_ph = label(c2, "Phase: -", FONT_SMALL);
    rf_phBar = mkBar(c2, 0xE67E22);

    rf_chart = lv_chart_create(g_content);
    lv_obj_set_size(rf_chart, lv_pct(100), 150);
    lv_chart_set_type(rf_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(rf_chart, 80);
    lv_chart_set_axis_range(rf_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 255);
    lv_obj_set_style_bg_color(rf_chart, COL_CARD, 0);
    lv_obj_set_style_border_width(rf_chart, 0, 0);
    lv_obj_set_style_size(rf_chart, 0, 0, LV_PART_INDICATOR);
    rf_sA = lv_chart_add_series(rf_chart, lv_color_hex(0x3498DB), LV_CHART_AXIS_PRIMARY_Y);
    rf_sP = lv_chart_add_series(rf_chart, lv_color_hex(0xE67E22), LV_CHART_AXIS_PRIMARY_Y);

    button(g_content, "Set baseline (no tag)", 0x34495E, rfBaseCb);
    rf_prox = label(g_content, "Turn on the carrier, set a baseline, then bring a tag close: the ST25R3916 "
                               "measures how the antenna detunes - the same principle as its wake-up mode.",
                    FONT_SMALL, COL_DIM);

    engineStartDiag();
    g_timer = lv_timer_create(rfTimer, 200, nullptr);
    showScreen(scr);
}

/* ========================================================================= */
/* HISTORY                                                                   */
/* ========================================================================= */
static lv_obj_t *hi_list, *hi_clear;
static bool hi_armed = false;

static void histFill()
{
    lv_obj_clean(hi_list);
    std::vector<HistoryEntry> v = historyLoad(60);
    if (v.empty()) {
        label(hi_list, "No scans yet.", FONT_BODY, COL_DIM);
        return;
    }
    for (auto &e : v) {
        lv_obj_t *c = card(hi_list);
        label(c, (e.when + "  " + e.type).c_str(), FONT_SMALL, COL_DIM);
        label(c, e.uid.c_str(), FONT_BODY, COL_TEXT);
        if (e.summary.length()) label(c, e.summary.c_str(), FONT_SMALL, COL_TEXT);
    }
}

static void histClearCb(lv_event_t *e)
{
    (void)e;
    if (!hi_armed) {
        hi_armed = true;
        buttonSetText(hi_clear, "Tap again to clear all");
        lv_obj_set_style_bg_color(hi_clear, COL_BAD, 0);
        return;
    }
    hi_armed = false;
    historyClear();
    buttonSetText(hi_clear, LV_SYMBOL_TRASH " Clear history");
    lv_obj_set_style_bg_color(hi_clear, lv_color_hex(0x34495E), 0);
    histFill();
}

static void histCleanup() { hi_list = hi_clear = nullptr; hi_armed = false; }

static void buildHistory()
{
    lv_obj_t *scr = appScreen("History", kAccent[7]);
    g_cleanup = histCleanup;
    String s = String("Stored on: ") + storageName() + "  (/nfc/)";
    label(g_content, s.c_str(), FONT_SMALL, COL_DIM);
    hi_clear = button(g_content, LV_SYMBOL_TRASH " Clear history", 0x34495E, histClearCb);
    hi_list = lv_obj_create(g_content);
    lv_obj_set_size(hi_list, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hi_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hi_list, 0, 0);
    lv_obj_set_style_pad_all(hi_list, 0, 0);
    lv_obj_set_style_pad_row(hi_list, 8, 0);
    lv_obj_set_flex_flow(hi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(hi_list, LV_OBJ_FLAG_SCROLLABLE);
    histFill();
    showScreen(scr);
}

/* ========================================================================= */
/* HOME                                                                      */
/* ========================================================================= */
struct Tile { const char *icon; const char *name; void (*open)(); };
static const Tile kTiles[] = {
    {LV_SYMBOL_EYE_OPEN, "Scan", buildScan},
    {LV_SYMBOL_EDIT, "Write", buildWrite},
    {LV_SYMBOL_COPY, "Copy", buildCopy},
    {LV_SYMBOL_LIST, "Dump", buildDump},
    {LV_SYMBOL_UPLOAD, "Emulate", buildEmulate},
    {LV_SYMBOL_WIFI, "Field", buildField},
    {LV_SYMBOL_SETTINGS, "RF Lab", buildRfLab},
    {LV_SYMBOL_DIRECTORY, "History", buildHistory},
};
static lv_obj_t *hm_batt = nullptr;

static void tileCb(lv_event_t *e)
{
    const Tile *t = (const Tile *)lv_event_get_user_data(e);
    leaveScreen();
    if (t == &kTiles[4]) g_emuPrefillKind = -1; // fresh emulate from home
    t->open();
}

static void homeTimer(lv_timer_t *t)
{
    (void)t;
    if (!hm_batt) return;
    char b[48];
    snprintf(b, sizeof(b), "%s %d%%", LV_SYMBOL_BATTERY_FULL, (int)instance.getBatteryPercent());
    lv_label_set_text(hm_batt, b);
}

static void homeCleanup() { hm_batt = nullptr; }

static void buildHome()
{
    lv_obj_t *scr = newScreen();
    g_cleanup = homeCleanup;

    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, FONT_TITLE, 0);
    lv_label_set_text(title, "NFC Toolkit");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, kSafeX + 18, kSafeTop + 12);
    hm_batt = lv_label_create(scr);
    lv_obj_set_style_text_font(hm_batt, FONT_SMALL, 0);
    lv_obj_set_style_text_color(hm_batt, COL_DIM, 0);
    lv_obj_align(hm_batt, LV_ALIGN_TOP_RIGHT, -(kSafeX + 18), kSafeTop + 18);

    lv_obj_t *grid = lv_obj_create(scr);
    const int32_t gridTop = kSafeTop + 52;
    lv_obj_set_size(grid, contentW(), H - gridTop - kSafeBottom);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, gridTop);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_style_pad_column(grid, 10, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);

    int32_t tw = (contentW() - 10) / 2;
    int32_t th = (H - gridTop - kSafeBottom - 3 * 10) / 4;
    if (th < 72) th = 72;
    for (size_t i = 0; i < sizeof(kTiles) / sizeof(kTiles[0]); i++) {
        lv_obj_t *b = lv_button_create(grid);
        lv_obj_set_size(b, tw, th);
        lv_obj_set_style_bg_color(b, lv_color_hex(kAccent[i]), 0);
        lv_obj_set_style_radius(b, 18, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(b, tileCb, LV_EVENT_CLICKED, (void *)&kTiles[i]);
        lv_obj_t *ic = lv_label_create(b);
        lv_obj_set_style_text_font(ic, FONT_BIG, 0);
        lv_label_set_text(ic, kTiles[i].icon);
        lv_obj_t *nm = lv_label_create(b);
        lv_obj_set_style_text_font(nm, FONT_BODY, 0);
        lv_label_set_text(nm, kTiles[i].name);
    }
    homeTimer(nullptr);
    g_timer = lv_timer_create(homeTimer, 10000, nullptr);
    showScreen(scr);
}

/* ------------------------------------------------------------------------- */
void uiBegin()
{
    W = lv_display_get_horizontal_resolution(NULL);
    H = lv_display_get_vertical_resolution(NULL);
    buildHome();
}

void uiLoop()
{
    /* BOOT button acts as "back to home" from any app screen. */
    static bool lastBoot = true;
    bool boot = digitalRead(0);
    if (lastBoot && !boot && g_scr && g_content) {
        leaveScreen();
        buildHome();
    }
    lastBoot = boot;
}
