#include "nfc_engine.h"
#include "ndef_builder.h"

/* Objects created by LilyGoLib for the T-Watch Ultra (LilyGoWatchUltra.cpp). */
extern RfalRfST25R3916Class nfc_hw;
extern RfalNfcClass NFCReader;

static NdefClass s_ndef(&NFCReader);

static EngineMode s_mode = ENG_IDLE;
static EngineCallback s_cb = nullptr;
static void (*s_progress)(const String &) = nullptr;

static bool s_discoveryRunning = false;
static bool s_handled = false;

/* write / copy */
static WriteOp s_writeOp = WRITE_MESSAGE;
static uint8_t *s_writeBuf = nullptr;
static size_t s_writeLen = 0;
static uint8_t *s_copyBuf = nullptr;
static size_t s_copyLen = 0;
static String s_copySrcUid;
static uint32_t s_lastSameUidMsg = 0;

/* dump */
static String s_dumpText, s_lastUid, s_lastType;
static uint8_t *s_dumpBin = nullptr;
static size_t s_dumpBinLen = 0;
static const size_t kDumpBinCap = 8192;
static const size_t kMsgCap = 2048;

/* field detector */
static bool s_fieldPresent = false;
static uint32_t s_fieldEvents = 0;
static uint32_t s_lastFieldPoll = 0;

/* diagnostics */
static bool s_diagFieldOn = false;

/* ------------------------------------------------------------------------- */

static void emit(EngineEventKind k, const String &title, const String &body)
{
    if (s_cb) s_cb(k, title, body);
}

static void progress(const String &msg)
{
    if (s_progress) s_progress(msg);
}

void engineSetProgressHook(void (*hook)(const String &msg)) { s_progress = hook; }

const char *engineModeName(EngineMode m)
{
    switch (m) {
    case ENG_SCAN: return "Scan";
    case ENG_EMULATE: return "Emulate";
    case ENG_DUMP: return "Dump";
    case ENG_WRITE: return "Write";
    case ENG_COPY_READ: return "Copy (read)";
    case ENG_COPY_WRITE: return "Copy (write)";
    case ENG_FIELD: return "Field detect";
    case ENG_DIAG: return "Diagnostics";
    default: return "Idle";
    }
}

EngineMode engineMode() { return s_mode; }

static uint8_t *bigAlloc(size_t n)
{
    uint8_t *p = (uint8_t *)ps_malloc(n);
    if (!p) p = (uint8_t *)malloc(n);
    return p;
}

bool engineBegin()
{
    instance.powerControl(POWER_NFC, true);
    delay(10);
    s_writeBuf = bigAlloc(kMsgCap);
    s_copyBuf = bigAlloc(kMsgCap);
    s_dumpBin = bigAlloc(kDumpBinCap);
    return NFCReader.rfalNfcInitialize() == ST_ERR_NONE && s_writeBuf && s_copyBuf && s_dumpBin;
}

void engineStop()
{
    if (LilyGoNfc.isRunning()) LilyGoNfc.stop();
    if (s_discoveryRunning) {
        NFCReader.rfalNfcDeactivate(false);
        s_discoveryRunning = false;
    }
    if (s_diagFieldOn) {
        nfc_hw.rfalFieldOff();
        s_diagFieldOn = false;
    }
    s_mode = ENG_IDLE;
    s_cb = nullptr;
    s_handled = false;
}

/* ------------------------------------------------------------------------- */
/* Service-backed modes                                                      */
/* ------------------------------------------------------------------------- */

bool engineStartScan(bool lowPowerWakeup, LilyGoNfcEventCallback cb)
{
    engineStop();
    LilyGoNfcReaderConfig cfg = {};
    cfg.technologies = RFAL_NFC_POLL_TECH_A | RFAL_NFC_POLL_TECH_B | RFAL_NFC_POLL_TECH_F |
                       RFAL_NFC_POLL_TECH_V | RFAL_NFC_POLL_TECH_ST25TB;
    cfg.discoveryDurationMs = 1000;
    cfg.wakeupEnabled = lowPowerWakeup; // ST25R3916 wake-up timer + amplitude/phase sensing
    cfg.callback = cb;
    cfg.userData = nullptr;
    if (!LilyGoNfc.beginReader(cfg)) return false;
    s_mode = ENG_SCAN;
    return true;
}

bool engineStartEmulation(const LilyGoNfcEmulationConfig &cfg)
{
    engineStop();
    if (!LilyGoNfc.beginEmulation(cfg)) return false;
    s_mode = ENG_EMULATE;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Own discovery loop                                                        */
/* ------------------------------------------------------------------------- */

static void notifyCb(rfalNfcState st)
{
    if (st == RFAL_NFC_STATE_POLL_SELECT) {
        NFCReader.rfalNfcSelect(0);
    }
}

static bool startDiscovery()
{
    if (NFCReader.rfalNfcInitialize() != ST_ERR_NONE) return false;
    rfalNfcDiscoverParam p;
    memset(&p, 0, sizeof(p));
    p.compMode = RFAL_COMPLIANCE_MODE_NFC;
    p.techs2Find = RFAL_NFC_POLL_TECH_A | RFAL_NFC_POLL_TECH_B | RFAL_NFC_POLL_TECH_F |
                   RFAL_NFC_POLL_TECH_V | RFAL_NFC_POLL_TECH_ST25TB;
    p.totalDuration = 800;
    p.devLimit = 1;
    p.nfcfBR = RFAL_BR_212;
    p.ap2pBR = RFAL_BR_424;
    p.notifyCb = notifyCb;
    p.wakeupEnabled = false;
    p.wakeupConfigDefault = true;
    ReturnCode err = NFCReader.rfalNfcDiscover(&p);
    s_discoveryRunning = (err == ST_ERR_NONE);
    s_handled = false;
    return s_discoveryRunning;
}

static bool startRawMode(EngineMode m, EngineCallback cb)
{
    engineStop();
    s_cb = cb;
    if (!startDiscovery()) {
        s_cb = cb;
        emit(ENG_EV_FAIL, "NFC error", "Could not start RF discovery.\nIs the NFC chip powered?");
        s_cb = nullptr;
        return false;
    }
    s_mode = m;
    return true;
}

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static String uidOf(const rfalNfcDevice *dev)
{
    if (dev->type == RFAL_NFC_LISTEN_TYPE_NFCV) {
        uint8_t r[8];
        for (int i = 0; i < 8; i++) r[i] = dev->dev.nfcv.InvRes.UID[7 - i];
        return toHex(r, 8, ':');
    }
    return toHex(dev->nfcid, dev->nfcidLen, ':');
}

static const char *techOf(const rfalNfcDevice *dev)
{
    switch (dev->type) {
    case RFAL_NFC_LISTEN_TYPE_NFCA: return "NFC-A (ISO14443A)";
    case RFAL_NFC_LISTEN_TYPE_NFCB: return "NFC-B (ISO14443B)";
    case RFAL_NFC_LISTEN_TYPE_NFCF: return "NFC-F (FeliCa)";
    case RFAL_NFC_LISTEN_TYPE_NFCV: return "NFC-V (ISO15693)";
    case RFAL_NFC_LISTEN_TYPE_ST25TB: return "ST25TB (ISO14443B')";
    case RFAL_NFC_LISTEN_TYPE_AP2P: return "Active P2P";
    default: return "Unknown";
    }
}

static const char *nfcaNameFromSak(uint8_t sak)
{
    switch (sak) {
    case 0x00: return "MIFARE Ultralight / NTAG (Type 2)";
    case 0x01: return "TNP3xxx";
    case 0x08: return "MIFARE Classic 1K";
    case 0x09: return "MIFARE Mini";
    case 0x10: return "MIFARE Plus 2K (SL2)";
    case 0x11: return "MIFARE Plus 4K (SL2)";
    case 0x18: return "MIFARE Classic 4K";
    case 0x19: return "MIFARE Classic 2K";
    case 0x20: return "ISO14443-4 (DESFire / Plus SL3 / smart card / phone)";
    case 0x28: return "SmartMX + Classic 1K emulation";
    case 0x38: return "SmartMX + Classic 4K emulation";
    case 0x40: return "NFC-DEP (P2P)";
    case 0x60: return "ISO14443-4 + NFC-DEP";
    case 0x88: return "MIFARE Classic 1K (Infineon)";
    default: return (sak & 0x20) ? "ISO14443-4 compliant" : "Unknown NFC-A";
    }
}

static bool isMifareClassicSak(uint8_t sak)
{
    return sak == 0x08 || sak == 0x09 || sak == 0x18 || sak == 0x19 || sak == 0x88 ||
           sak == 0x28 || sak == 0x38 || sak == 0x10 || sak == 0x11;
}

static String basicInfo(const rfalNfcDevice *dev)
{
    String s;
    s += "UID: " + uidOf(dev) + "\n";
    s += String("Tech: ") + techOf(dev) + "\n";
    if (dev->type == RFAL_NFC_LISTEN_TYPE_NFCA) {
        const rfalNfcaListenDevice &a = dev->dev.nfca;
        char b[64];
        snprintf(b, sizeof(b), "ATQA: %02X %02X  SAK: %02X\n", a.sensRes.platformInfo,
                 a.sensRes.anticollisionInfo, a.selRes.sak);
        s += b;
        s += String("Type: ") + nfcaNameFromSak(a.selRes.sak) + "\n";
    } else if (dev->type == RFAL_NFC_LISTEN_TYPE_NFCB) {
        const rfalNfcbListenDevice &bdev = dev->dev.nfcb;
        s += "ATQB: " + toHex((const uint8_t *)&bdev.sensbRes, bdev.sensbResLen) + "\n";
    } else if (dev->type == RFAL_NFC_LISTEN_TYPE_NFCF) {
        const rfalNfcfListenDevice &f = dev->dev.nfcf;
        s += "IDm: " + toHex(f.sensfRes.NFCID2, RFAL_NFCF_NFCID2_LEN) + "\n";
        s += "SENSF_RES: " + toHex((const uint8_t *)&f.sensfRes, f.sensfResLen) + "\n";
    } else if (dev->type == RFAL_NFC_LISTEN_TYPE_NFCV) {
        const uint8_t *u = dev->dev.nfcv.InvRes.UID; // LSB first; u[6]=manufacturer, u[7]=0xE0
        char b[48];
        snprintf(b, sizeof(b), "DSFID: %02X  Mfr code: %02X\n", dev->dev.nfcv.InvRes.DSFID, u[6]);
        s += b;
        if (u[6] == 0x02) s += "Manufacturer: STMicroelectronics\n";
        else if (u[6] == 0x04) s += "Manufacturer: NXP (ICODE)\n";
        else if (u[6] == 0x07) s += "Manufacturer: Texas Instruments\n";
    }
    const char *iface = dev->rfInterface == RFAL_NFC_INTERFACE_ISODEP ? "ISO-DEP" :
                        dev->rfInterface == RFAL_NFC_INTERFACE_NFCDEP ? "NFC-DEP" : "RF frame";
    s += String("Interface: ") + iface + "\n";
    return s;
}

static ReturnCode rawTx(const uint8_t *tx, uint16_t txLen, uint8_t *rx, uint16_t rxCap, uint16_t *rxLen, uint32_t ms = 20)
{
    *rxLen = 0;
    return nfc_hw.rfalTransceiveBlockingTxRx((uint8_t *)tx, txLen, rx, rxCap, rxLen,
                                             RFAL_TXRX_FLAGS_DEFAULT, rfalConvMsTo1fc(ms));
}

/* Re-wake and re-select an NFC-A tag after a command it NAK'd (NAK sends T2T to IDLE). */
static bool reselectA(const rfalNfcDevice *dev)
{
    rfalNfcaSensRes sens;
    rfalNfcaSelRes sel;
    NFCReader.rfalNfcaPollerInitialize();
    if (NFCReader.rfalNfcaPollerCheckPresence(RFAL_14443A_SHORTFRAME_CMD_WUPA, &sens) != ST_ERR_NONE) return false;
    return NFCReader.rfalNfcaPollerSelect(dev->dev.nfca.nfcId1, dev->dev.nfca.nfcId1Len, &sel) == ST_ERR_NONE;
}

/* ISO-DEP APDU through the RFAL NFC activity layer. Response includes SW1 SW2. */
static ReturnCode apdu(const uint8_t *c, uint16_t cl, uint8_t *r, uint16_t rcap, uint16_t *rl)
{
    uint8_t *rxData = nullptr;
    uint16_t *rxLen = nullptr;
    *rl = 0;
    ReturnCode err = NFCReader.rfalNfcDataExchangeStart((uint8_t *)c, cl, &rxData, &rxLen, RFAL_FWT_NONE);
    if (err != ST_ERR_NONE) return err;
    uint32_t t0 = millis();
    uint16_t total = 0;
    for (;;) {
        NFCReader.rfalNfcWorker();
        err = NFCReader.rfalNfcDataExchangeGetStatus();
        if (err == ST_ERR_BUSY) {
            if (millis() - t0 > 3000) return ST_ERR_TIMEOUT;
            continue;
        }
        if (err == ST_ERR_NONE || err == ST_ERR_AGAIN) {
            /* ST_ERR_AGAIN = one chained I-block received; RFAL ACKs and keeps receiving. */
            uint16_t n = rxLen ? *rxLen : 0;
            if (n > rcap - total) n = rcap - total;
            if (rxData && n) memcpy(r + total, rxData, n);
            total += n;
            if (err == ST_ERR_NONE) break;
            continue;
        }
        return err;
    }
    *rl = total;
    return ST_ERR_NONE;
}

static bool swOk(const uint8_t *r, uint16_t n)
{
    return n >= 2 && r[n - 2] == 0x90 && r[n - 1] == 0x00;
}

static void hexLines(String &out, const uint8_t *d, size_t n, size_t perLine, const char *label, uint32_t startIndex)
{
    /* At most 8 bytes per text line so rows fit the watch's ~358 px column. */
    char pre[24];
    for (size_t i = 0; i < n; i += perLine) {
        size_t m = (n - i < perLine) ? n - i : perLine;
        for (size_t j = 0; j < m; j += 8) {
            size_t k8 = (m - j < 8) ? m - j : 8;
            if (j == 0) snprintf(pre, sizeof(pre), "%s%03lX: ", label, (unsigned long)(startIndex + i / perLine));
            else snprintf(pre, sizeof(pre), "%*s", (int)strlen(label) + 5, "");
            out += pre;
            out += toHex(d + i + j, k8);
            out += " |";
            for (size_t k = 0; k < k8; k++) {
                uint8_t c = d[i + j + k];
                out += (c >= 0x20 && c < 0x7F) ? (char)c : '.';
            }
            out += "|\n";
        }
    }
}

/* Walk Type 2 / Type 5 style TLVs and decode the first NDEF message found. */
static String decodeTlvArea(const uint8_t *d, size_t n)
{
    size_t i = 0;
    while (i < n) {
        uint8_t t = d[i++];
        if (t == 0x00) continue;           // NULL TLV
        if (t == 0xFE) break;              // terminator
        if (i >= n) break;
        uint32_t len = d[i++];
        if (len == 0xFF) {
            if (i + 2 > n) break;
            len = (d[i] << 8) | d[i + 1];
            i += 2;
        }
        if (t == 0x03) {
            if (len == 0) return "NDEF TLV present but empty.\n";
            if (i + len > n) len = n - i;
            return ndefDescribeMessage(d + i, len);
        }
        i += len;
    }
    return "No NDEF TLV found.\n";
}

static void dumpBinAppend(const uint8_t *d, size_t n)
{
    if (!s_dumpBin) return;
    size_t room = kDumpBinCap - s_dumpBinLen;
    if (n > room) n = room;
    memcpy(s_dumpBin + s_dumpBinLen, d, n);
    s_dumpBinLen += n;
}

/* ------------------------------------------------------------------------- */
/* Dump implementations                                                      */
/* ------------------------------------------------------------------------- */

static void dumpT2T(rfalNfcDevice *dev, String &out)
{
    uint8_t rx[64];
    uint16_t rl = 0;
    int pages = 0;
    bool isNtag = false;
    String model;

    /* GET_VERSION (NTAG21x / Ultralight EV1) */
    static const uint8_t getVersion[] = {0x60};
    if (rawTx(getVersion, 1, rx, sizeof(rx), &rl) == ST_ERR_NONE && rl >= 8) {
        out += "GET_VERSION: " + toHex(rx, 8) + "\n";
        uint8_t type = rx[2], size = rx[6];
        if (rx[1] == 0x04 && type == 0x04) {
            isNtag = true;
            switch (size) {
            case 0x0B: model = "NTAG210"; pages = 20; break;
            case 0x0E: model = "NTAG212"; pages = 41; break;
            case 0x0F: model = "NTAG213"; pages = 45; break;
            case 0x11: model = "NTAG215"; pages = 135; break;
            case 0x13: model = "NTAG216"; pages = 231; break;
            default: model = "NTAG (unknown size)"; break;
            }
        } else if (rx[1] == 0x04 && type == 0x03) {
            switch (size) {
            case 0x0B: model = "MIFARE Ultralight EV1 (MF0UL11)"; pages = 20; break;
            case 0x0E: model = "MIFARE Ultralight EV1 (MF0UL21)"; pages = 41; break;
            default: model = "MIFARE Ultralight family"; break;
            }
        } else {
            model = "Type 2 tag (vendor " + String(rx[1], HEX) + ")";
        }
    } else {
        /* Plain Ultralight / other T2T: tag NAK'd, it is now IDLE -> re-select. */
        model = "Type 2 tag (no GET_VERSION)";
        if (!reselectA(dev)) {
            out += "Tag lost after GET_VERSION probe.\n";
            return;
        }
    }

    /* Read header pages 0..3 to get the Capability Container */
    if (NFCReader.rfalT2TPollerRead(0, rx, 16, &rl) != ST_ERR_NONE || rl < 16) {
        out += "READ of page 0 failed.\n";
        return;
    }
    if (pages == 0) {
        if (rx[12] == 0xE1 && rx[14] > 0) pages = 4 + (rx[14] * 8) / 4;
        else pages = 16; // Ultralight default (64 bytes)
        if (pages > 256) pages = 256;
    }
    out += "Model: " + model + "\n";
    out += "Pages: " + String(pages) + " (" + String(pages * 4) + " bytes)\n";
    if (rx[12] == 0xE1) {
        char b[80];
        snprintf(b, sizeof(b), "CC: E1 v%u.%u  data area %u B  access %02X\n", rx[13] >> 4, rx[13] & 0x0F,
                 rx[14] * 8, rx[15]);
        out += b;
    } else {
        out += "CC: not NDEF formatted\n";
    }
    out += "\n";

    uint8_t *mem = (uint8_t *)malloc(pages * 4);
    if (!mem) return;
    int got = 0;
    for (int p = 0; p < pages; p += 4) {
        if (p % 32 == 0) progress("Reading page " + String(p) + "/" + String(pages));
        if (NFCReader.rfalT2TPollerRead(p, rx, 16, &rl) != ST_ERR_NONE || rl < 16) {
            out += "(read stopped at page " + String(p) + " - protected or end of memory)\n";
            break;
        }
        int n = (pages - p) >= 4 ? 4 : (pages - p);
        memcpy(mem + p * 4, rx, n * 4);
        got = p + n;
    }
    hexLines(out, mem, got * 4, 4, "P", 0);
    dumpBinAppend(mem, got * 4);

    if (got > 4) {
        out += "\nNDEF:\n";
        out += decodeTlvArea(mem + 16, (got - 4) * 4);
    }
    free(mem);

    if (isNtag) {
        static const uint8_t readSig[] = {0x3C, 0x00};
        if (rawTx(readSig, 2, rx, sizeof(rx), &rl) == ST_ERR_NONE && rl >= 32) {
            out += "\nOriginality signature (ECC):\n" + toHex(rx, 8) + "\n" + toHex(rx + 8, 8) + "\n" + toHex(rx + 16, 8) + "\n" + toHex(rx + 24, 8) + "\n";
        }
    }
}

static void dumpT5T(rfalNfcDevice *dev, String &out)
{
    const uint8_t *uid = dev->dev.nfcv.InvRes.UID;
    uint8_t rx[64];
    uint16_t rl = 0;
    uint16_t blocks = 0;
    uint8_t bsize = 4;

    if (NFCReader.rfalNfcvPollerGetSystemInformation(RFAL_NFCV_REQ_FLAG_DEFAULT, uid, rx, sizeof(rx), &rl) == ST_ERR_NONE &&
        rl >= 10) {
        uint8_t info = rx[1];
        uint8_t idx = 10;
        if (info & 0x01) { out += "DSFID: " + String(rx[idx], HEX) + "\n"; idx++; }
        if (info & 0x02) { out += "AFI: " + String(rx[idx], HEX) + "\n"; idx++; }
        if ((info & 0x04) && idx + 1 < rl) {
            blocks = rx[idx] + 1;
            bsize = (rx[idx + 1] & 0x1F) + 1;
            idx += 2;
        }
        if ((info & 0x08) && idx < rl) { out += "IC reference: " + String(rx[idx], HEX) + "\n"; }
    }
    if (blocks == 0 || blocks == 256) {
        /* Try extended system info for >256-block parts (ST25DV, ICODE DNA ...) */
        if (NFCReader.rfalNfcvPollerExtendedGetSystemInformation(RFAL_NFCV_REQ_FLAG_DEFAULT, uid, 0x3F, rx, sizeof(rx), &rl) ==
                ST_ERR_NONE && rl >= 13) {
            uint8_t info = rx[1];
            uint8_t idx = 10;
            if (info & 0x01) idx++;
            if (info & 0x02) idx++;
            if ((info & 0x04) && idx + 2 < rl) {
                blocks = (uint16_t)(rx[idx] | (rx[idx + 1] << 8)) + 1;
                bsize = (rx[idx + 2] & 0x1F) + 1;
            }
        }
    }
    bool guessed = false;
    if (blocks == 0) { blocks = 64; guessed = true; }
    if (bsize == 0 || bsize > 32) bsize = 4;
    uint32_t total = (uint32_t)blocks * bsize;
    if (total > kDumpBinCap) blocks = kDumpBinCap / bsize;

    out += "Blocks: " + String(blocks) + (guessed ? " (guessed)" : "") + " x " + String(bsize) + " B\n\n";

    uint8_t *mem = (uint8_t *)malloc((size_t)blocks * bsize);
    if (!mem) return;
    uint16_t got = 0;
    for (uint16_t b = 0; b < blocks; b++) {
        if (b % 16 == 0) progress("Reading block " + String(b) + "/" + String(blocks));
        ReturnCode e = (b < 256)
                       ? NFCReader.rfalNfcvPollerReadSingleBlock(RFAL_NFCV_REQ_FLAG_DEFAULT, uid, (uint8_t)b, rx, sizeof(rx), &rl)
                       : NFCReader.rfalNfcvPollerExtendedReadSingleBlock(RFAL_NFCV_REQ_FLAG_DEFAULT, uid, b, rx, sizeof(rx), &rl);
        if (e != ST_ERR_NONE || rl < (uint16_t)(1 + bsize)) {
            out += "(read stopped at block " + String(b) + ")\n";
            break;
        }
        memcpy(mem + (size_t)b * bsize, rx + 1, bsize);
        got = b + 1;
    }
    hexLines(out, mem, (size_t)got * bsize, bsize, "B", 0);
    dumpBinAppend(mem, (size_t)got * bsize);

    size_t n = (size_t)got * bsize;
    if (n >= 8 && (mem[0] == 0xE1 || mem[0] == 0xE2)) {
        size_t ccLen = (mem[2] == 0x00) ? 8 : 4;
        out += "\nCC: " + toHex(mem, ccLen) + "\nNDEF:\n";
        out += decodeTlvArea(mem + ccLen, n - ccLen);
    } else {
        out += "\nNo Type 5 CC (not NDEF formatted).\n";
    }
    free(mem);
}

static void dumpST25TB(rfalNfcDevice *dev, String &out)
{
    (void)dev;
    rfalSt25tbBlock blk;
    uint8_t mem[128 * 4];
    int got = 0;
    for (int b = 0; b < 128; b++) {
        if (NFCReader.rfalSt25tbPollerReadBlock((uint8_t)b, &blk) != ST_ERR_NONE) break;
        memcpy(mem + b * 4, blk, 4);
        got = b + 1;
    }
    out += "Blocks read: " + String(got) + "\n\n";
    hexLines(out, mem, got * 4, 4, "B", 0);
    dumpBinAppend(mem, got * 4);
    if (NFCReader.rfalSt25tbPollerReadBlock(0xFF, &blk) == ST_ERR_NONE) {
        out += "System block FF: " + toHex(blk, 4) + "\n";
    }
}

static void dumpFeliCa(rfalNfcDevice *dev, String &out)
{
    const uint8_t *idm = dev->dev.nfcf.sensfRes.NFCID2;
    uint8_t rx[64];
    uint16_t rl = 0;
    rfalNfcfServ serv = 0x000B; // NDEF / read-only-without-encryption service used by FeliCa Lite(-S)
    rfalNfcfBlockListElem elem;
    rfalNfcfServBlockListParam sb;
    sb.numServ = 1;
    sb.servList = &serv;
    sb.numBlock = 1;
    sb.blockList = &elem;

    out += "Service 0x000B (read w/o encryption):\n";
    int got = 0;
    for (int b = 0; b < 64; b++) {
        elem.conf = RFAL_NFCF_BLOCKLISTELEM_LEN; // 2-byte element, service #0
        elem.blockNum = b;
        if (NFCReader.rfalNfcfPollerCheck(idm, &sb, rx, sizeof(rx), &rl) != ST_ERR_NONE || rl < 1 + 16) break;
        hexLines(out, rx + 1, 16, 16, "B", b);
        dumpBinAppend(rx + 1, 16);
        got++;
    }
    if (got == 0) out += "(no readable blocks - service is protected or absent)\n";
}

static void parseBerForEmv(const uint8_t *d, size_t n, String &out, int depth)
{
    size_t i = 0;
    while (i < n && depth < 6) {
        uint16_t tag = d[i++];
        if ((tag & 0x1F) == 0x1F && i < n) tag = (tag << 8) | d[i++];
        if (i >= n) break;
        uint32_t len = d[i++];
        if (len == 0x81 && i < n) len = d[i++];
        else if (len == 0x82 && i + 1 < n) { len = (d[i] << 8) | d[i + 1]; i += 2; }
        if (i + len > n) break;
        const uint8_t *v = d + i;
        bool constructed = (tag > 0xFF) ? ((tag >> 8) & 0x20) : (tag & 0x20);
        if (tag == 0x4F) out += "  AID: " + toHex(v, len, 0) + "\n";
        else if (tag == 0x50) { out += "  Label: "; for (uint32_t k = 0; k < len; k++) out += (char)v[k]; out += "\n"; }
        else if (tag == 0x87 && len == 1) out += "  Priority: " + String(v[0]) + "\n";
        else if (constructed) parseBerForEmv(v, len, out, depth + 1);
        i += len;
    }
}

static void dumpT4T(rfalNfcDevice *dev, String &out)
{
    (void)dev;
    uint8_t r[260];
    uint16_t rl = 0;

    /* NFC Forum Type 4 NDEF application */
    static const uint8_t selApp[] = {0x00, 0xA4, 0x04, 0x00, 0x07, 0xD2, 0x76, 0x00, 0x00, 0x85, 0x01, 0x01, 0x00};
    if (apdu(selApp, sizeof(selApp), r, sizeof(r), &rl) == ST_ERR_NONE && swOk(r, rl)) {
        out += "NDEF application: present\n";
        static const uint8_t selCC[] = {0x00, 0xA4, 0x00, 0x0C, 0x02, 0xE1, 0x03};
        static const uint8_t readCC[] = {0x00, 0xB0, 0x00, 0x00, 0x0F};
        if (apdu(selCC, sizeof(selCC), r, sizeof(r), &rl) == ST_ERR_NONE && swOk(r, rl) &&
            apdu(readCC, sizeof(readCC), r, sizeof(r), &rl) == ST_ERR_NONE && swOk(r, rl) && rl >= 17) {
            uint8_t cc[15];
            memcpy(cc, r, 15);
            out += "CC: " + toHex(cc, 15) + "\n";
            uint16_t mle = (cc[3] << 8) | cc[4];
            uint8_t fid[2] = {cc[9], cc[10]};
            uint16_t maxSize = (cc[11] << 8) | cc[12];
            char b[96];
            snprintf(b, sizeof(b), "Mapping v%u.%u  MLe %u  NDEF file %02X%02X  max %u B  R:%02X W:%02X\n",
                     cc[2] >> 4, cc[2] & 0xF, mle, fid[0], fid[1], maxSize, cc[13], cc[14]);
            out += b;
            uint8_t selF[] = {0x00, 0xA4, 0x00, 0x0C, 0x02, fid[0], fid[1]};
            uint8_t readLen[] = {0x00, 0xB0, 0x00, 0x00, 0x02};
            if (apdu(selF, sizeof(selF), r, sizeof(r), &rl) == ST_ERR_NONE && swOk(r, rl) &&
                apdu(readLen, sizeof(readLen), r, sizeof(r), &rl) == ST_ERR_NONE && swOk(r, rl) && rl >= 4) {
                uint16_t nlen = (r[0] << 8) | r[1];
                out += "NDEF length: " + String(nlen) + " B\n";
                if (nlen > kDumpBinCap) nlen = kDumpBinCap;
                uint8_t *msg = (uint8_t *)malloc(nlen ? nlen : 1);
                uint16_t pos = 0;
                uint8_t chunk = (mle > 2 && mle - 2 < 0xF0) ? (uint8_t)(mle - 2) : 0xF0;
                while (msg && pos < nlen) {
                    uint8_t want = (nlen - pos > chunk) ? chunk : (uint8_t)(nlen - pos);
                    uint16_t off = pos + 2;
                    uint8_t rb[] = {0x00, 0xB0, (uint8_t)(off >> 8), (uint8_t)off, want};
                    if (apdu(rb, sizeof(rb), r, sizeof(r), &rl) != ST_ERR_NONE || !swOk(r, rl) || rl < 3) break;
                    uint16_t n = rl - 2;
                    if (n > nlen - pos) n = nlen - pos;
                    memcpy(msg + pos, r, n);
                    pos += n;
                }
                if (msg) {
                    hexLines(out, msg, pos, 16, "", 0);
                    dumpBinAppend(msg, pos);
                    out += "NDEF:\n" + ndefDescribeMessage(msg, pos);
                    free(msg);
                }
            }
        }
    } else {
        out += "NDEF application: not present\n";
    }

    /* Payment / transit cards: list applications from PPSE (public directory only). */
    static const uint8_t selPpse[] = {0x00, 0xA4, 0x04, 0x00, 0x0E, '2', 'P', 'A', 'Y', '.', 'S', 'Y', 'S',
                                      '.', 'D', 'D', 'F', '0', '1', 0x00};
    if (apdu(selPpse, sizeof(selPpse), r, sizeof(r), &rl) == ST_ERR_NONE && swOk(r, rl)) {
        out += "\nEMV contactless directory (PPSE):\n";
        parseBerForEmv(r, rl - 2, out, 0);
    }
}

static void doDump(rfalNfcDevice *dev)
{
    s_dumpText = "";
    s_dumpBinLen = 0;
    s_lastUid = uidOf(dev);
    s_lastType = techOf(dev);

    String out = basicInfo(dev);
    out += "\n";
    progress("Reading tag memory...");

    switch (dev->type) {
    case RFAL_NFC_LISTEN_TYPE_NFCA: {
        uint8_t sak = dev->dev.nfca.selRes.sak;
        if (dev->rfInterface == RFAL_NFC_INTERFACE_ISODEP) {
            dumpT4T(dev, out);
        } else if (dev->rfInterface == RFAL_NFC_INTERFACE_NFCDEP) {
            out += "Peer-to-peer (NFC-DEP) device, typically a phone.\nNo memory to dump.\n";
        } else if (isMifareClassicSak(sak)) {
            out += "MIFARE Classic sectors are protected by NXP's proprietary Crypto1 cipher.\n"
                   "The ST25R3916 has no Crypto1 engine, so only UID/ATQA/SAK can be read.\n";
        } else if (dev->dev.nfca.type == RFAL_NFCA_T1T) {
            out += "Type 1 (Topaz) tag. Use Scan to read its NDEF.\n";
        } else {
            dumpT2T(dev, out);
        }
        break;
    }
    case RFAL_NFC_LISTEN_TYPE_NFCB:
        if (dev->rfInterface == RFAL_NFC_INTERFACE_ISODEP) dumpT4T(dev, out);
        else out += "Non ISO-DEP NFC-B card. No generic memory map.\n";
        break;
    case RFAL_NFC_LISTEN_TYPE_NFCF:
        dumpFeliCa(dev, out);
        break;
    case RFAL_NFC_LISTEN_TYPE_NFCV:
        dumpT5T(dev, out);
        break;
    case RFAL_NFC_LISTEN_TYPE_ST25TB:
        dumpST25TB(dev, out);
        break;
    default:
        out += "Unsupported device type.\n";
        break;
    }
    s_dumpText = out;
    emit(ENG_EV_SUCCESS, "Dump: " + s_lastUid, out);
}

/* ------------------------------------------------------------------------- */
/* NDEF write / copy                                                         */
/* ------------------------------------------------------------------------- */

static const char *ndefErr(ReturnCode e)
{
    switch (e) {
    case ST_ERR_NONE: return "OK";
    case ST_ERR_NOMEM: return "message larger than tag";
    case ST_ERR_TIMEOUT: return "timeout (tag moved?)";
    case ST_ERR_REQUEST: return "tag refused request";
    case ST_ERR_WRONG_STATE: return "tag is read-only";
    case ST_ERR_NOTSUPP: return "not supported by this tag";
    case ST_ERR_PARAM: return "tag type not NDEF-capable";
    case ST_ERR_PROTO: return "protocol error";
    default: return "error";
    }
}

/* Prepares the NDEF context; formats virgin tags if needed. */
static ReturnCode prepareNdef(rfalNfcDevice *dev, ndefInfo &info, bool forceFormat, String &log)
{
    if (dev->type == RFAL_NFC_LISTEN_TYPE_NFCA && dev->rfInterface == RFAL_NFC_INTERFACE_RF &&
        isMifareClassicSak(dev->dev.nfca.selRes.sak)) {
        log += "MIFARE Classic cannot be written (Crypto1 not supported by ST25R3916).\n";
        return ST_ERR_NOTSUPP;
    }
    ReturnCode err = s_ndef.ndefPollerContextInitialization(dev);
    if (err != ST_ERR_NONE) {
        log += "NDEF context init failed: " + String(ndefErr(err)) + "\n";
        return err;
    }
    err = s_ndef.ndefPollerNdefDetect(&info);
    if (err != ST_ERR_NONE || forceFormat) {
        log += (err != ST_ERR_NONE) ? "No NDEF structure, formatting...\n" : "Formatting...\n";
        ReturnCode fe = s_ndef.ndefPollerTagFormat(nullptr, 0);
        if (fe != ST_ERR_NONE) {
            log += "Format failed: " + String(ndefErr(fe)) + "\n";
            return (err != ST_ERR_NONE) ? err : fe;
        }
        err = s_ndef.ndefPollerNdefDetect(&info);
        if (err != ST_ERR_NONE) {
            log += "NDEF detect after format failed.\n";
            return err;
        }
    }
    char b[96];
    snprintf(b, sizeof(b), "NDEF v%u.%u  area %lu B  used %lu B  %s\n", info.majorVersion, info.minorVersion,
             (unsigned long)info.areaLen, (unsigned long)info.messageLen,
             info.state == NDEF_STATE_READONLY ? "READ-ONLY" :
             info.state == NDEF_STATE_READWRITE ? "read/write" : "empty");
    log += b;
    return ST_ERR_NONE;
}

static bool writeAndVerify(rfalNfcDevice *dev, const uint8_t *msg, size_t len, bool forceFormat, String &log)
{
    ndefInfo info;
    if (prepareNdef(dev, info, forceFormat, log) != ST_ERR_NONE) return false;
    if (info.state == NDEF_STATE_READONLY) {
        log += "Tag is locked read-only.\n";
        return false;
    }
    if (s_ndef.ndefPollerCheckAvailableSpace(len) != ST_ERR_NONE) {
        log += "Message " + String(len) + " B does not fit (" + String(info.areaLen) + " B area).\n";
        return false;
    }
    ReturnCode err = s_ndef.ndefPollerWriteRawMessage(msg, len);
    if (err != ST_ERR_NONE) {
        log += "Write failed: " + String(ndefErr(err)) + "\n";
        return false;
    }
    /* Read back and compare */
    uint8_t *rb = (uint8_t *)malloc(len + 16);
    uint32_t rl = 0;
    bool same = false;
    if (rb && s_ndef.ndefPollerReadRawMessage(rb, len + 16, &rl) == ST_ERR_NONE) {
        same = (rl == len) && memcmp(rb, msg, len) == 0;
    }
    free(rb);
    log += same ? "Verified OK.\n" : "Written (verify read-back mismatch/unsupported).\n";
    return true;
}

static void doWrite(rfalNfcDevice *dev)
{
    String log = basicInfo(dev) + "\n";
    static const uint8_t emptyMsg[] = {0xD0, 0x00, 0x00};
    const uint8_t *msg = s_writeBuf;
    size_t len = s_writeLen;
    if (s_writeOp != WRITE_MESSAGE) { msg = emptyMsg; len = sizeof(emptyMsg); }

    progress("Writing...");
    bool ok = writeAndVerify(dev, msg, len, s_writeOp == WRITE_FORMAT, log);
    if (ok && s_writeOp == WRITE_MESSAGE) log += "\nContent:\n" + ndefDescribeMessage(msg, len);
    const char *what = s_writeOp == WRITE_ERASE ? "Erase" : s_writeOp == WRITE_FORMAT ? "Format" : "Write";
    emit(ok ? ENG_EV_SUCCESS : ENG_EV_FAIL, String(what) + (ok ? " complete" : " failed"), log);
}

static void doCopyRead(rfalNfcDevice *dev)
{
    String log = basicInfo(dev) + "\n";
    ndefInfo info;
    if (prepareNdef(dev, info, false, log) != ST_ERR_NONE) {
        emit(ENG_EV_FAIL, "Can't read source", log);
        return;
    }
    if (info.messageLen == 0) {
        emit(ENG_EV_FAIL, "Source tag is empty", log);
        return;
    }
    uint32_t rl = 0;
    ReturnCode err = s_ndef.ndefPollerReadRawMessage(s_copyBuf, kMsgCap, &rl);
    if (err != ST_ERR_NONE || rl == 0) {
        log += "Read failed: " + String(ndefErr(err)) + "\n";
        emit(ENG_EV_FAIL, "Can't read source", log);
        return;
    }
    s_copyLen = rl;
    s_copySrcUid = uidOf(dev);
    log += "Captured " + String(rl) + " bytes:\n" + ndefDescribeMessage(s_copyBuf, rl);
    emit(ENG_EV_SUCCESS, "Source captured", log);
}

static bool doCopyWrite(rfalNfcDevice *dev)
{
    String uid = uidOf(dev);
    if (uid == s_copySrcUid) {
        if (millis() - s_lastSameUidMsg > 1500) {
            emit(ENG_EV_STATUS, "Present the TARGET tag", "That's the source tag (" + uid + ").\nRemove it and hold a blank tag.");
            s_lastSameUidMsg = millis();
        }
        return false; // keep polling
    }
    String log = basicInfo(dev) + "\n";
    progress("Writing copy...");
    bool ok = writeAndVerify(dev, s_copyBuf, s_copyLen, false, log);
    emit(ok ? ENG_EV_SUCCESS : ENG_EV_FAIL, ok ? "Copy complete" : "Copy failed", log);
    return true;
}

/* ------------------------------------------------------------------------- */

bool engineStartDump(EngineCallback cb) { return startRawMode(ENG_DUMP, cb); }

bool engineStartWrite(WriteOp op, const uint8_t *msg, size_t len, EngineCallback cb)
{
    if (op == WRITE_MESSAGE && (!msg || !len || len > kMsgCap)) return false;
    s_writeOp = op;
    if (op == WRITE_MESSAGE) {
        memcpy(s_writeBuf, msg, len);
        s_writeLen = len;
    }
    return startRawMode(ENG_WRITE, cb);
}

bool engineStartCopyRead(EngineCallback cb)
{
    s_copyLen = 0;
    s_copySrcUid = "";
    return startRawMode(ENG_COPY_READ, cb);
}

bool engineStartCopyWrite(EngineCallback cb)
{
    if (!s_copyLen) return false;
    return startRawMode(ENG_COPY_WRITE, cb);
}

bool engineHasCopyBuffer() { return s_copyLen > 0; }
size_t engineCopyLength() { return s_copyLen; }
String engineCopySummary() { return s_copyLen ? ndefDescribeMessage(s_copyBuf, s_copyLen) : String(""); }

const String &engineLastDumpText() { return s_dumpText; }
const uint8_t *engineLastDumpBin(size_t *len) { *len = s_dumpBinLen; return s_dumpBin; }
const String &engineLastUid() { return s_lastUid; }
const String &engineLastType() { return s_lastType; }

/* ------------------------------------------------------------------------- */
/* Field detector                                                            */
/* ------------------------------------------------------------------------- */

bool engineStartField()
{
    engineStop();
    if (NFCReader.rfalNfcInitialize() != ST_ERR_NONE) return false; // leaves EFD in automatic mode
    s_fieldPresent = false;
    s_fieldEvents = 0;
    s_mode = ENG_FIELD;
    return true;
}

bool engineFieldPresent() { return s_fieldPresent; }
uint32_t engineFieldEvents() { return s_fieldEvents; }

/* ------------------------------------------------------------------------- */
/* Diagnostics                                                               */
/* ------------------------------------------------------------------------- */

bool engineStartDiag()
{
    engineStop();
    if (NFCReader.rfalNfcInitialize() != ST_ERR_NONE) return false;
    s_mode = ENG_DIAG;
    return true;
}

bool engineDiagMeasure(DiagData &d, bool wantField)
{
    if (s_mode != ENG_DIAG) return false;

    uint8_t id = 0;
    nfc_hw.st25r3916ReadRegister(ST25R3916_REG_IC_IDENTITY, &id);
    d.icIdentity = id;
    d.isB = (id & ST25R3916_REG_IC_IDENTITY_ic_type_mask) == ST25R3916_REG_IC_IDENTITY_ic_type_st25r3916b;
    d.icRev = id & ST25R3916_REG_IC_IDENTITY_ic_rev_mask;

    if (wantField && !s_diagFieldOn) {
        nfc_hw.rfalSetMode(RFAL_MODE_POLL_NFCA, RFAL_BR_106, RFAL_BR_106);
        ReturnCode e = nfc_hw.rfalFieldOnAndStartGT();
        s_diagFieldOn = (e == ST_ERR_NONE);
        d.fieldError = !s_diagFieldOn;
        delay(5);
    } else if (!wantField && s_diagFieldOn) {
        nfc_hw.rfalFieldOff();
        s_diagFieldOn = false;
        d.fieldError = false;
    }
    d.fieldOn = s_diagFieldOn;

    d.vdd_mV = nfc_hw.st25r3916MeasureVoltage(ST25R3916_REG_REGULATOR_CONTROL_mpsv_vdd);
    d.vddRf_mV = nfc_hw.st25r3916MeasureVoltage(ST25R3916_REG_REGULATOR_CONTROL_mpsv_vdd_rf);
    d.vddA_mV = nfc_hw.st25r3916MeasureVoltage(ST25R3916_REG_REGULATOR_CONTROL_mpsv_vdd_a);
    d.vddD_mV = nfc_hw.st25r3916MeasureVoltage(ST25R3916_REG_REGULATOR_CONTROL_mpsv_vdd_d);
    d.vddAm_mV = nfc_hw.st25r3916MeasureVoltage(ST25R3916_REG_REGULATOR_CONTROL_mpsv_vdd_am);

    uint8_t v = 0;
    if (nfc_hw.rfalChipMeasureAmplitude(&v) == ST_ERR_NONE) d.amplitude = v;
    if (nfc_hw.rfalChipMeasurePhase(&v) == ST_ERR_NONE) d.phase = v;
    if (!d.isB && !s_diagFieldOn) {
        d.capSupported = nfc_hw.st25r3916MeasureCapacitance(&v) == ST_ERR_NONE;
        if (d.capSupported) d.capacitance = v;
    } else {
        d.capSupported = !d.isB;
    }
    d.extField = nfc_hw.rfalIsExtFieldOn();
    return true;
}

/* ------------------------------------------------------------------------- */
/* Main loop                                                                 */
/* ------------------------------------------------------------------------- */

void engineLoop()
{
    switch (s_mode) {
    case ENG_SCAN:
    case ENG_EMULATE:
        LilyGoNfc.loop();
        return;

    case ENG_FIELD:
        if (millis() - s_lastFieldPoll >= 15) {
            s_lastFieldPoll = millis();
            bool p = nfc_hw.rfalIsExtFieldOn();
            if (p && !s_fieldPresent) s_fieldEvents++;
            s_fieldPresent = p;
        }
        return;

    case ENG_DUMP:
    case ENG_WRITE:
    case ENG_COPY_READ:
    case ENG_COPY_WRITE:
        break;

    default:
        return;
    }

    if (!s_discoveryRunning) return;
    NFCReader.rfalNfcWorker();
    rfalNfcState st = NFCReader.rfalNfcGetState();

    if (st == RFAL_NFC_STATE_ACTIVATED && !s_handled) {
        s_handled = true;
        rfalNfcDevice *dev = nullptr;
        if (NFCReader.rfalNfcGetActiveDevice(&dev) != ST_ERR_NONE || !dev) {
            NFCReader.rfalNfcDeactivate(true);
            s_handled = false;
            return;
        }
        EngineMode m = s_mode;
        bool finished = true;
        if (m == ENG_DUMP) doDump(dev);
        else if (m == ENG_WRITE) doWrite(dev);
        else if (m == ENG_COPY_READ) doCopyRead(dev);
        else if (m == ENG_COPY_WRITE) finished = doCopyWrite(dev);

        if (finished) {
            /* One-shot: stop the field so a tag isn't written/dumped twice. */
            NFCReader.rfalNfcDeactivate(false);
            s_discoveryRunning = false;
            if (s_mode == m) { s_mode = ENG_IDLE; s_cb = nullptr; }
        } else {
            NFCReader.rfalNfcDeactivate(true); // keep polling for another tag
            s_handled = false;
        }
    } else if (st == RFAL_NFC_STATE_IDLE) {
        startDiscovery(); // discovery ended (error/timeout) -> restart
    }
}
