// Desktop simulator: real LVGL + real ui.cpp, hardware stubbed. Renders each screen to raw RGB565.
#include "../../NFC_Toolkit/ui.cpp"
#include <map>

static uint32_t g_ms = 1000;
uint32_t millis() { return g_ms; }
SimInstance instance; SimNfc LilyGoNfc; SimWiFi WiFi;
const char *lilygoNfcRecordKindName(LilyGoNfcRecordKind k){ switch(k){case LILYGO_NFC_RECORD_URI:return "URI";case LILYGO_NFC_RECORD_TEXT:return "Text";case LILYGO_NFC_RECORD_WIFI:return "Wi-Fi";default:return "Record";} }
const char *lilygoNfcEmulationKindName(LilyGoNfcEmulationKind k){ return k==0?"URL":k==1?"Text":"Wi-Fi"; }
const char *lilygoNfcEmulationTechName(LilyGoNfcEmulationTech t){ return t==LILYGO_NFC_EMULATION_TECH_NFCA_T4T?"NFC-A Type 4 Tag":t==LILYGO_NFC_EMULATION_TECH_NFCF_T3T?"NFC-F Type 3 Tag":"NFC-A + NFC-F"; }

/* ---- engine stubs ---- */
static EngineMode s_mode = ENG_IDLE; static bool s_field=false; static bool s_copy=false;
bool engineBegin(){return true;} void engineLoop(){} void engineStop(){ s_mode=ENG_IDLE; }
EngineMode engineMode(){return s_mode;} const char *engineModeName(EngineMode){return "";}
bool engineStartScan(bool, LilyGoNfcEventCallback){ s_mode=ENG_SCAN; return true;}
bool engineStartEmulation(const LilyGoNfcEmulationConfig&){ s_mode=ENG_EMULATE; return true;}
static const char *kDump =
"UID: 04:A2:3B:6A:91:5C:80\nTech: NFC-A (ISO14443A)\nATQA: 00 44  SAK: 00\nType: MIFARE Ultralight / NTAG (Type 2)\nInterface: RF frame\n\n"
"GET_VERSION: 00 04 04 02 01 00 11 03\nModel: NTAG215\nPages: 135 (540 bytes)\nCC: E1 v1.0  data area 496 B  access 00\n\n"
"P000: 04 A2 3B 15 |..;.|\nP001: 6A 91 5C 80 |j.\\.|\nP002: 27 48 00 00 |'H..|\nP003: E1 10 3E 00 |..>.|\n"
"P004: 03 0F D1 01 |....|\nP005: 0B 55 04 6C |.U.l|\nP006: 69 6C 79 67 |ilyg|\nP007: 6F 2E 63 63 |o.cc|\nP008: FE 00 00 00 |....|\n"
"P009: 00 00 00 00 |....|\n...\n\nNDEF:\n1. URI: https://lilygo.cc\n\nOriginality signature (ECC):\n"
"1F 9C 2A 44 0B 7E 31 D2 88 05 6C 4A 19 E0 77 3B\nC4 51 02 9A 6E 13 F8 2D 40 B7 9E 65 0C 21 DA 58\n";
bool engineStartDump(EngineCallback cb){ cb(ENG_EV_SUCCESS,"Dump: 04:A2:3B:6A:91:5C:80",kDump); return true;}
bool engineStartWrite(WriteOp, const uint8_t*m, size_t n, EngineCallback cb){ String b="UID: 04:A2:3B:6A:91:5C:80\nTech: NFC-A (ISO14443A)\nNDEF v1.0  area 496 B  used 15 B  read/write\nVerified OK.\n\nContent:\n"; if(m) b+=ndefDescribeMessage(m,n); cb(ENG_EV_SUCCESS,"Write complete",b); return true;}
bool engineStartCopyRead(EngineCallback cb){ s_copy=true; cb(ENG_EV_SUCCESS,"Source captured","Captured 15 bytes"); return true;}
bool engineStartCopyWrite(EngineCallback){return true;}
bool engineHasCopyBuffer(){return s_copy;} size_t engineCopyLength(){return 15;}
String engineCopySummary(){ return "1. URI: https://lilygo.cc\n"; }
bool engineStartField(){ s_mode=ENG_FIELD; return true;} bool engineFieldPresent(){return s_field;} uint32_t engineFieldEvents(){return 3;}
bool engineStartDiag(){ s_mode=ENG_DIAG; return true;}
static int s_tick=0;
bool engineDiagMeasure(DiagData &d, bool f){ d.icIdentity=0x2A; d.isB=false; d.icRev=2; d.vdd_mV=3310; d.vddRf_mV=3120; d.vddA_mV=3290; d.vddD_mV=1810; d.vddAm_mV=3280;
  s_tick++; d.fieldOn=f; d.amplitude=f?(uint8_t)(118 + (s_tick>40? 14*(s_tick%7>3):0)):2; d.phase=f?(uint8_t)(92+(s_tick>40?8:0)):0; d.capacitance=57; d.capSupported=true; d.extField=false; return true;}
static String s_dt=kDump, s_uid="04:A2:3B:6A:91:5C:80", s_type="NFC-A";
const String &engineLastDumpText(){return s_dt;} const uint8_t *engineLastDumpBin(size_t*l){*l=0;return nullptr;}
const String &engineLastUid(){return s_uid;} const String &engineLastType(){return s_type;}
void engineSetProgressHook(void (*)(const String&)){}
/* ---- storage stubs ---- */
bool storageBegin(){return true;} const char *storageName(){return "SD card";} fs::FS *storageFs(){return nullptr;}
void historyAdd(const String&,const String&,const String&){}
std::vector<HistoryEntry> historyLoad(size_t){ return {
 {"2026-11-06 13:41","04:A2:3B:6A:91:5C:80","NTAG215","URI: https://lilygo.cc"},
 {"2026-11-06 13:38","E0:02:24:00:1A:6B:33:C1","ISO15693 (ST25DV)","Text: Shelf B-12"},
 {"2026-11-06 13:30","01:2E:4F:9A:11:07:22:6D","FeliCa","UID only"},
 {"2026-11-06 12:55","8A:3C:51:E2","MIFARE Classic 1K","UID only"} }; }
void historyClear(){}
String storageSaveText(const String&,const String&,const String&){return "/nfc/dump.txt";}
String storageSaveBin(const String&,const String&,const uint8_t*,size_t){return "";}
String nowString(bool){return "2026-11-06 13:45";}

/* ---- display ---- */
static uint16_t fb[410*502];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px){
  int w=lv_area_get_width(a); uint16_t*p=(uint16_t*)px;
  for(int y=a->y1;y<=a->y2;y++) for(int x=a->x1;x<=a->x2;x++) fb[y*410+x]=*p++;
  lv_display_flush_ready(d);}
static void pump(int n=15){ for(int i=0;i<n;i++){ g_ms+=30; lv_timer_handler(); } }
static void snap(const char*name){ pump(); lv_obj_invalidate(lv_screen_active()); lv_refr_now(NULL);
  char p[128]; snprintf(p,128,"shots/%s.raw",name); FILE*f=fopen(p,"wb"); fwrite(fb,2,410*502,f); fclose(f); }
static void open(void(*b)()){ leaveScreen(); b(); pump(); }

int main(){
  lv_init(); lv_tick_set_cb(millis);
  lv_display_t *d=lv_display_create(410,502);
  static uint16_t buf[410*502]; lv_display_set_buffers(d,buf,NULL,sizeof(buf),LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(d,flush);
  uiBegin(); pump(); snap("01_home");
  open(buildScan); snap("02_scan_idle");
  LilyGoNfcReaderResult r; memset(&r,0,sizeof r);
  strcpy(r.uidText,"04:A2:3B:6A:91:5C:80"); strcpy(r.cardType,"NTAG215"); strcpy(r.technology,"NFC-A"); strcpy(r.interfaceName,"RF"); strcpy(r.tagType,"T2T");
  r.ndefAttempted=true; strcpy(r.ndefStateText,"READ/WRITE"); r.ndefMajorVersion=1; r.ndefMessageLen=15; r.ndefAreaLen=496; r.recordCount=1;
  r.records[0].index=1; r.records[0].kind=LILYGO_NFC_RECORD_URI; strcpy(r.records[0].title,"URI"); strcpy(r.records[0].value,"https://lilygo.cc");
  onScanEvent(LILYGO_NFC_EVENT_CARD_DETECTED,r,nullptr); onScanEvent(LILYGO_NFC_EVENT_NDEF_READ,r,nullptr); snap("03_scan_result");
  lv_obj_scroll_to_y(g_content, 400, LV_ANIM_OFF); snap("03b_scan_result_scrolled");
  scanQrCb(nullptr); snap("04_scan_qr");
  open(buildWrite); snap("05_write_url");
  lv_obj_send_event(wr_f[0], LV_EVENT_CLICKED, NULL); snap("06_write_keyboard");
  kbHide(); lv_dropdown_set_selected(wr_type, WT_WIFI); writeRebuildFields(); snap("07_write_wifi");
  lv_dropdown_set_selected(wr_type, WT_URL); writeRebuildFields(); lv_textarea_set_text(wr_f[0],"https://lilygo.cc"); writeGoCb(nullptr); snap("08_write_done");
  open(buildCopy); copyReadCb(nullptr); panelCloseCb(nullptr); pump(); snap("09_copy");
  open(buildDump); snap("10_dump");
  lv_obj_scroll_to_y(g_content, 500, LV_ANIM_OFF); snap("10b_dump_scrolled");
  open(buildEmulate); emStartStopCb(nullptr);
  LilyGoNfc.st.readerActive=true; LilyGoNfc.st.tech=LILYGO_NFC_EMULATION_TECH_NFCA_T4T; strcpy(LilyGoNfc.st.payloadPreview,"https://lilygo.cc"); LilyGoNfc.st.ndefFileLen=17;
  strcpy(LilyGoNfc.st.activeTechnology,"NFC-A"); strcpy(LilyGoNfc.st.activeInterface,"ISO-DEP"); LilyGoNfc.st.readCount=4; LilyGoNfc.st.exchangeCount=12;
  pump(); snap("11_emulate");
  open(buildField); s_field=true; pump(); snap("12_field");
  open(buildRfLab); lv_obj_add_state(rf_sw, LV_STATE_CHECKED); pump(30); rfBaseCb(nullptr); pump(60); snap("13_rflab");
  lv_obj_scroll_to_y(g_content, 600, LV_ANIM_OFF); snap("13b_rflab_scrolled");
  open(buildHistory); snap("14_history");
  puts("done"); return 0;
}
