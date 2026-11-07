/**
 * nfc_engine.h - one owner for the ST25R3916 on the T-Watch Ultra.
 *
 * Reader "Scan" and tag "Emulation" are delegated to LilyGoLib's LilyGoNfcService
 * (it already contains a careful NFC-A T4T / NFC-F T3T card-emulation stack).
 * Everything else - NDEF writing, tag copy, raw memory dumps, external-field
 * detection and RF/analog diagnostics - talks to the RFAL layer directly.
 *
 * Only one mode runs at a time; starting any mode stops the previous one.
 */
#pragma once
#include <Arduino.h>
#include <LilyGoLib.h>

enum EngineMode : uint8_t {
    ENG_IDLE = 0,
    ENG_SCAN,        // LilyGoNfcService reader (UID + NDEF parse)
    ENG_EMULATE,     // LilyGoNfcService tag emulation
    ENG_DUMP,        // raw memory dump of one tag
    ENG_WRITE,       // write NDEF / erase / format one tag
    ENG_COPY_READ,   // read NDEF message from source tag
    ENG_COPY_WRITE,  // write captured NDEF to a different tag
    ENG_FIELD,       // external RF field detector (phones, readers, terminals)
    ENG_DIAG,        // analog measurements
};

enum WriteOp : uint8_t {
    WRITE_MESSAGE = 0,
    WRITE_ERASE,     // replace content with an empty NDEF record
    WRITE_FORMAT,    // NFC-Forum format (writes CC on virgin T2T/T5T) + empty NDEF
};

enum EngineEventKind : uint8_t {
    ENG_EV_STATUS = 0,   // progress text, operation still running
    ENG_EV_SUCCESS,      // operation finished OK (engine returns to idle)
    ENG_EV_FAIL,         // operation failed (engine returns to idle)
};

/** Results for dump/write/copy are reported through this callback. */
typedef void (*EngineCallback)(EngineEventKind kind, const String &title, const String &body);

struct DiagData {
    uint8_t icIdentity = 0;
    bool isB = false;
    uint8_t icRev = 0;
    uint16_t vdd_mV = 0, vddRf_mV = 0, vddA_mV = 0, vddD_mV = 0, vddAm_mV = 0;
    uint8_t amplitude = 0;   // AD result 0..255 (~13 mV / LSB at RFI)
    uint8_t phase = 0;       // AD result 0..255 (128 ~ 90 deg)
    uint8_t capacitance = 0; // AD result 0..255 (ST25R3916 only, not 3916B)
    bool capSupported = false;
    bool fieldOn = false;
    bool fieldError = false; // collision avoidance refused to turn field on (external field)
    bool extField = false;
};

bool engineBegin();
void engineLoop();
void engineStop();
EngineMode engineMode();
const char *engineModeName(EngineMode m);

/* --- service-backed modes --- */
bool engineStartScan(bool lowPowerWakeup, LilyGoNfcEventCallback cb);
bool engineStartEmulation(const LilyGoNfcEmulationConfig &cfg);

/* --- raw RFAL modes --- */
bool engineStartDump(EngineCallback cb);
bool engineStartWrite(WriteOp op, const uint8_t *msg, size_t len, EngineCallback cb);
bool engineStartCopyRead(EngineCallback cb);
bool engineStartCopyWrite(EngineCallback cb);
bool engineHasCopyBuffer();
size_t engineCopyLength();
String engineCopySummary();

bool engineStartField();
bool engineFieldPresent();
uint32_t engineFieldEvents();

bool engineStartDiag();
bool engineDiagMeasure(DiagData &d, bool wantField);

/* Last dump, for saving to storage. */
const String &engineLastDumpText();
const uint8_t *engineLastDumpBin(size_t *len);
const String &engineLastUid();
const String &engineLastType();

/** Optional hook called during long blocking reads so the UI can repaint. */
void engineSetProgressHook(void (*hook)(const String &msg));
