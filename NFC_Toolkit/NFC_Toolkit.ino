/**
 * NFC Toolkit for the LilyGO T-Watch Ultra (ESP32-S3 + ST25R3916)
 *
 *   Scan      - read UID / tech / NDEF of NFC-A/B/F/V + ST25TB tags (optional low-power wake-up)
 *   Write     - URL, text, Wi-Fi, phone, SMS, email, vCard, geo, Android app, erase, format
 *   Copy      - clone the NDEF message of one tag onto another
 *   Dump      - full memory dump (NTAG/Ultralight + signature, ISO15693, ST25TB, FeliCa, ISO-DEP)
 *   Emulate   - act as an NFC tag (NFC-A Type 4 / NFC-F Type 3) with URL, text or Wi-Fi
 *   Field     - passive external RF-field detector
 *   RF Lab    - chip ID, supply voltages, antenna amplitude/phase, capacitive sensor
 *   History   - log of everything scanned, on microSD (or internal flash)
 *
 * Build: Arduino IDE / arduino-cli, board "LilyGo T-Watch-Ultra" (esp32 core >= 3.3.0),
 *        libraries LilyGoLib + LilyGoLib-ThirdParty. See README.md.
 */
#include <LilyGoLib.h>
#include <LV_Helper.h>
#include "nfc_engine.h"
#include "storage.h"
#include "ui.h"

#if !defined(ARDUINO_T_WATCH_S3_ULTRA)
#error "This sketch targets the LilyGO T-Watch Ultra. Select board 'LilyGo T-Watch-Ultra'."
#endif

void setup()
{
    Serial.begin(115200);

    /* Skip peripherals this app doesn't use: faster boot, less power. */
    LilyGoDeviceInitOptions opts;
    opts.initGps = false;
    opts.initRadio = false;
    opts.initAudio = false;
    opts.initSensor = false;
    instance.begin(opts);

    beginLvglHelper(instance);
    instance.setBrightness(180);

    if (!engineBegin()) {
        Serial.println("[NFC] ST25R3916 init failed");
    }
    storageBegin();
    Serial.printf("[NFC] storage: %s\n", storageName());

    uiBegin();
}

void loop()
{
    instance.loop();
    engineLoop();
    uiLoop();
    lv_task_handler();
    delay(2);
}
