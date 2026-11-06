#include "ndef_builder.h"

static const char *const kUriPrefixes[] = {
    "",                           // 0x00
    "http://www.",                // 0x01
    "https://www.",               // 0x02
    "http://",                    // 0x03
    "https://",                   // 0x04
    "tel:",                       // 0x05
    "mailto:",                    // 0x06
    "ftp://anonymous:anonymous@", // 0x07
    "ftp://ftp.",                 // 0x08
    "ftps://",                    // 0x09
    "sftp://",                    // 0x0A
    "smb://",                     // 0x0B
    "nfs://",                     // 0x0C
    "ftp://",                     // 0x0D
    "dav://",                     // 0x0E
    "news:",                      // 0x0F
    "telnet://",                  // 0x10
    "imap:",                      // 0x11
    "rtsp://",                    // 0x12
    "urn:",                       // 0x13
    "pop:",                       // 0x14
    "sip:",                       // 0x15
    "sips:",                      // 0x16
    "tftp:",                      // 0x17
    "btspp://",                   // 0x18
    "btl2cap://",                 // 0x19
    "btgoep://",                  // 0x1A
    "tcpobex://",                 // 0x1B
    "irdaobex://",                // 0x1C
    "file://",                    // 0x1D
    "urn:epc:id:",                // 0x1E
    "urn:epc:tag:",               // 0x1F
    "urn:epc:pat:",               // 0x20
    "urn:epc:raw:",               // 0x21
    "urn:epc:",                   // 0x22
    "urn:nfc:",                   // 0x23
};
static const uint8_t kUriPrefixCount = sizeof(kUriPrefixes) / sizeof(kUriPrefixes[0]);

const char *ndefUriPrefix(uint8_t code)
{
    return code < kUriPrefixCount ? kUriPrefixes[code] : "";
}

String toHex(const uint8_t *d, size_t n, char sep)
{
    static const char hx[] = "0123456789ABCDEF";
    String s;
    s.reserve(n * 3);
    for (size_t i = 0; i < n; i++) {
        if (i && sep) s += sep;
        s += hx[d[i] >> 4];
        s += hx[d[i] & 0x0F];
    }
    return s;
}

bool NdefBuilder::put(uint8_t b)
{
    if (_len >= _cap) { _overflow = true; return false; }
    _buf[_len++] = b;
    return true;
}

bool NdefBuilder::put(const uint8_t *d, size_t n)
{
    if (_len + n > _cap) { _overflow = true; return false; }
    if (n) memcpy(_buf + _len, d, n);
    _len += n;
    return true;
}

bool NdefBuilder::addRecord(uint8_t tnf, const uint8_t *type, uint8_t typeLen,
                            const uint8_t *payload, uint32_t payloadLen)
{
    const bool sr = payloadLen < 256;
    uint8_t hdr = (tnf & 0x07) | (sr ? 0x10 : 0x00);
    if (_records == 0) hdr |= 0x80; // MB
    _lastHdr = (long)_len;
    bool ok = put(hdr) && put(typeLen);
    if (sr) {
        ok = ok && put((uint8_t)payloadLen);
    } else {
        ok = ok && put((uint8_t)(payloadLen >> 24)) && put((uint8_t)(payloadLen >> 16)) &&
             put((uint8_t)(payloadLen >> 8)) && put((uint8_t)payloadLen);
    }
    ok = ok && put(type, typeLen) && put(payload, payloadLen);
    if (ok) _records++;
    return ok;
}

bool NdefBuilder::addUri(const char *uri)
{
    if (!uri) return false;
    uint8_t best = 0;
    size_t bestLen = 0;
    for (uint8_t i = 1; i < kUriPrefixCount; i++) {
        size_t pl = strlen(kUriPrefixes[i]);
        if (pl > bestLen && strncasecmp(uri, kUriPrefixes[i], pl) == 0) {
            best = i;
            bestLen = pl;
        }
    }
    const char *rest = uri + bestLen;
    size_t restLen = strlen(rest);
    uint8_t *tmp = (uint8_t *)malloc(restLen + 1);
    if (!tmp) return false;
    tmp[0] = best;
    memcpy(tmp + 1, rest, restLen);
    static const uint8_t t = 'U';
    bool ok = addRecord(NDEF_TNF_WELL_KNOWN, &t, 1, tmp, restLen + 1);
    free(tmp);
    return ok;
}

bool NdefBuilder::addText(const char *text, const char *lang)
{
    if (!text) text = "";
    if (!lang || !*lang) lang = "en";
    size_t ll = strlen(lang) & 0x3F, tl = strlen(text);
    uint8_t *tmp = (uint8_t *)malloc(1 + ll + tl);
    if (!tmp) return false;
    tmp[0] = (uint8_t)ll; // bit7 = 0 -> UTF-8
    memcpy(tmp + 1, lang, ll);
    memcpy(tmp + 1 + ll, text, tl);
    static const uint8_t t = 'T';
    bool ok = addRecord(NDEF_TNF_WELL_KNOWN, &t, 1, tmp, 1 + ll + tl);
    free(tmp);
    return ok;
}

static size_t wscAttr(uint8_t *p, uint16_t id, const uint8_t *v, uint16_t n)
{
    p[0] = id >> 8; p[1] = id & 0xFF; p[2] = n >> 8; p[3] = n & 0xFF;
    if (n) memcpy(p + 4, v, n);
    return 4 + n;
}

bool NdefBuilder::addWifi(const char *ssid, const char *key, uint16_t auth, uint16_t enc)
{
    if (!ssid) ssid = "";
    if (!key) key = "";
    size_t sl = strlen(ssid), kl = strlen(key);
    if (sl > 32 || kl > 64) return false;

    uint8_t cred[160];
    size_t c = 0;
    const uint8_t netIdx = 0x01;
    const uint8_t authB[2] = {(uint8_t)(auth >> 8), (uint8_t)auth};
    const uint8_t encB[2] = {(uint8_t)(enc >> 8), (uint8_t)enc};
    const uint8_t mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    c += wscAttr(cred + c, 0x1026, &netIdx, 1);              // Network Index
    c += wscAttr(cred + c, 0x1045, (const uint8_t *)ssid, sl); // SSID
    c += wscAttr(cred + c, 0x1003, authB, 2);                // Authentication Type
    c += wscAttr(cred + c, 0x100F, encB, 2);                 // Encryption Type
    c += wscAttr(cred + c, 0x1027, (const uint8_t *)key, kl);  // Network Key
    c += wscAttr(cred + c, 0x1020, mac, 6);                  // MAC Address (broadcast)

    uint8_t payload[176];
    size_t p = wscAttr(payload, 0x100E, cred, c);           // Credential
    static const char mime[] = "application/vnd.wfa.wsc";
    return addRecord(NDEF_TNF_MIME, (const uint8_t *)mime, sizeof(mime) - 1, payload, p);
}

bool NdefBuilder::addMime(const char *mime, const uint8_t *data, uint32_t len)
{
    size_t ml = strlen(mime);
    if (ml > 255) return false;
    return addRecord(NDEF_TNF_MIME, (const uint8_t *)mime, (uint8_t)ml, data, len);
}

bool NdefBuilder::addAndroidApp(const char *packageName)
{
    static const char t[] = "android.com:pkg";
    return addRecord(NDEF_TNF_EXTERNAL, (const uint8_t *)t, sizeof(t) - 1,
                     (const uint8_t *)packageName, strlen(packageName));
}

bool NdefBuilder::addEmpty()
{
    return addRecord(NDEF_TNF_EMPTY, nullptr, 0, nullptr, 0);
}

size_t NdefBuilder::finish()
{
    if (_overflow || _records == 0 || _lastHdr < 0) return 0;
    _buf[_lastHdr] |= 0x40; // ME
    return _len;
}

/* ------------------------------------------------------------------------- */
/* Decoder                                                                   */
/* ------------------------------------------------------------------------- */

static String printable(const uint8_t *d, size_t n, size_t maxLen = 160)
{
    String s;
    for (size_t i = 0; i < n && i < maxLen; i++) {
        char ch = (char)d[i];
        if (ch == '\r') continue;
        s += (ch == '\n' || ch == '\t' || ((uint8_t)ch >= 0x20)) ? ch : '.';
    }
    if (n > maxLen) s += "...";
    return s;
}

static String describeWsc(const uint8_t *p, size_t n)
{
    String ssid, key;
    uint16_t auth = 0;
    // The credential may be nested in 0x100E; scan all TLVs at any depth linearly.
    for (size_t i = 0; i + 4 <= n;) {
        uint16_t id = (p[i] << 8) | p[i + 1];
        uint16_t len = (p[i + 2] << 8) | p[i + 3];
        if (id == 0x100E) { i += 4; continue; } // descend
        if (i + 4 + len > n) break;
        const uint8_t *v = p + i + 4;
        if (id == 0x1045) ssid = printable(v, len);
        else if (id == 0x1027) key = printable(v, len);
        else if (id == 0x1003 && len == 2) auth = (v[0] << 8) | v[1];
        i += 4 + len;
    }
    String s = "Wi-Fi  SSID: " + ssid;
    s += "\n  Auth: ";
    s += (auth == WSC_AUTH_OPEN) ? "Open" : (auth & 0x0020) ? "WPA2" : (auth & 0x0002) ? "WPA" : String(auth, HEX);
    if (key.length()) s += "\n  Key: " + key;
    return s;
}

String ndefDescribeMessage(const uint8_t *msg, size_t len)
{
    String out;
    size_t i = 0;
    int idx = 1;
    while (i < len) {
        uint8_t hdr = msg[i++];
        if (i >= len) break;
        uint8_t tnf = hdr & 0x07;
        bool sr = hdr & 0x10, il = hdr & 0x08;
        uint8_t typeLen = msg[i++];
        uint32_t payLen = 0;
        if (sr) {
            if (i >= len) break;
            payLen = msg[i++];
        } else {
            if (i + 4 > len) break;
            payLen = ((uint32_t)msg[i] << 24) | ((uint32_t)msg[i + 1] << 16) | (msg[i + 2] << 8) | msg[i + 3];
            i += 4;
        }
        uint8_t idLen = 0;
        if (il) { if (i >= len) break; idLen = msg[i++]; }
        if (i + typeLen + idLen + payLen > len) { out += "(truncated record)\n"; break; }
        const uint8_t *type = msg + i; i += typeLen + idLen;
        const uint8_t *pay = msg + i; i += payLen;

        out += String(idx++) + ". ";
        if (tnf == NDEF_TNF_EMPTY) {
            out += "Empty record";
        } else if (tnf == NDEF_TNF_WELL_KNOWN && typeLen == 1 && type[0] == 'U' && payLen >= 1) {
            out += "URI: ";
            out += ndefUriPrefix(pay[0]);
            out += printable(pay + 1, payLen - 1, 400);
        } else if (tnf == NDEF_TNF_WELL_KNOWN && typeLen == 1 && type[0] == 'T' && payLen >= 1) {
            uint8_t ll = pay[0] & 0x3F;
            if ((uint32_t)ll + 1 <= payLen) {
                out += "Text [" + printable(pay + 1, ll) + "]: ";
                out += printable(pay + 1 + ll, payLen - 1 - ll, 400);
            }
        } else if (tnf == NDEF_TNF_WELL_KNOWN && typeLen == 2 && type[0] == 'S' && type[1] == 'p') {
            out += "Smart Poster:\n" + ndefDescribeMessage(pay, payLen);
        } else if (tnf == NDEF_TNF_MIME && typeLen == 23 && memcmp(type, "application/vnd.wfa.wsc", 23) == 0) {
            out += describeWsc(pay, payLen);
        } else if (tnf == NDEF_TNF_MIME) {
            out += "MIME " + printable(type, typeLen) + ":\n" + printable(pay, payLen, 300);
        } else if (tnf == NDEF_TNF_EXTERNAL) {
            out += "External " + printable(type, typeLen) + ": " + printable(pay, payLen);
        } else if (tnf == NDEF_TNF_URI) {
            out += "Absolute URI: " + printable(type, typeLen);
        } else {
            out += "TNF " + String(tnf) + " type '" + printable(type, typeLen) + "' " + String(payLen) + " B";
        }
        out += "\n";
        if (hdr & 0x40) break; // ME
    }
    if (!out.length()) out = "(no records)\n";
    return out;
}
