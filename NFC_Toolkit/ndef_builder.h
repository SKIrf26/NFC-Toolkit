/**
 * ndef_builder.h - tiny, dependency-free NDEF message encoder.
 *
 * Builds complete NDEF messages (one or more records) as raw bytes so they can
 * be handed to NdefClass::ndefPollerWriteRawMessage(). Encoding by hand keeps
 * the output byte-exact and easy to verify against a read-back.
 */
#pragma once

#include <Arduino.h>

#define NDEF_TNF_EMPTY      0x00
#define NDEF_TNF_WELL_KNOWN 0x01
#define NDEF_TNF_MIME       0x02
#define NDEF_TNF_URI        0x03
#define NDEF_TNF_EXTERNAL   0x04

/* Wi-Fi Simple Config authentication / encryption values (WSC spec). */
#define WSC_AUTH_OPEN       0x0001
#define WSC_AUTH_WPA_PSK    0x0002
#define WSC_AUTH_WPA2_PSK   0x0020
#define WSC_AUTH_WPA_WPA2   0x0022
#define WSC_ENC_NONE        0x0001
#define WSC_ENC_AES         0x0008
#define WSC_ENC_AES_TKIP    0x000C

class NdefBuilder {
public:
    NdefBuilder(uint8_t *buf, size_t cap) : _buf(buf), _cap(cap) {}

    void reset() { _len = 0; _records = 0; _lastHdr = -1; _overflow = false; }

    /* Generic record. Call finish() after the last record. */
    bool addRecord(uint8_t tnf, const uint8_t *type, uint8_t typeLen,
                   const uint8_t *payload, uint32_t payloadLen);

    bool addUri(const char *uri);                       /* auto-compresses the scheme prefix */
    bool addText(const char *text, const char *lang = "en");
    bool addWifi(const char *ssid, const char *key, uint16_t auth, uint16_t enc);
    bool addMime(const char *mime, const uint8_t *data, uint32_t len);
    bool addAndroidApp(const char *packageName);       /* Android Application Record */
    bool addEmpty();

    /* Sets the ME (message end) flag on the last record. Returns total length or 0 on error. */
    size_t finish();

    size_t length() const { return _len; }
    bool overflow() const { return _overflow; }

private:
    bool put(uint8_t b);
    bool put(const uint8_t *d, size_t n);

    uint8_t *_buf;
    size_t _cap;
    size_t _len = 0;
    int _records = 0;
    long _lastHdr = -1;
    bool _overflow = false;
};

/** Returns the URI prefix string for an NDEF URI identifier code (or "" if unknown). */
const char *ndefUriPrefix(uint8_t code);

/** Decodes a raw NDEF message into a human-readable multi-line summary. */
String ndefDescribeMessage(const uint8_t *msg, size_t len);

/** Convenience: hex string "AA BB CC". */
String toHex(const uint8_t *d, size_t n, char sep = ' ');
