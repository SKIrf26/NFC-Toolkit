#include "storage.h"
#include <LilyGoLib.h>
#include <SD.h>
#include <FFat.h>
#include <time.h>

static fs::FS *g_fs = nullptr;
static const char *g_fsName = "none";
static const char *kDir = "/nfc";
static const char *kHistory = "/nfc/history.tsv";

bool storageBegin()
{
    if (instance.isCardReady()) {
        g_fs = &SD;
        g_fsName = "SD card";
    } else if (FFat.begin(true)) {
        g_fs = &FFat;
        g_fsName = "Internal flash";
    } else {
        g_fs = nullptr;
        g_fsName = "none";
        return false;
    }
    if (!g_fs->exists(kDir)) g_fs->mkdir(kDir);
    return true;
}

const char *storageName() { return g_fsName; }
fs::FS *storageFs() { return g_fs; }

String nowString(bool forFileName)
{
    time_t t = time(nullptr);
    struct tm tmv;
    localtime_r(&t, &tmv);
    char buf[32];
    if (tmv.tm_year < 100) { // RTC never set -> fall back to uptime
        snprintf(buf, sizeof(buf), forFileName ? "up%lu" : "uptime %lus", (unsigned long)(millis() / 1000));
    } else {
        strftime(buf, sizeof(buf), forFileName ? "%Y%m%d_%H%M%S" : "%Y-%m-%d %H:%M", &tmv);
    }
    return String(buf);
}

static String clean(const String &s)
{
    String o = s;
    o.replace("\t", " ");
    o.replace("\n", " ");
    o.replace("\r", " ");
    return o;
}

void historyAdd(const String &uid, const String &type, const String &summary)
{
    if (!g_fs) return;
    File f = g_fs->open(kHistory, FILE_APPEND, true);
    if (!f) return;
    f.printf("%s\t%s\t%s\t%s\n", nowString().c_str(), clean(uid).c_str(), clean(type).c_str(),
             clean(summary).substring(0, 200).c_str());
    f.close();
}

std::vector<HistoryEntry> historyLoad(size_t maxEntries)
{
    std::vector<HistoryEntry> all;
    if (!g_fs) return all;
    File f = g_fs->open(kHistory, FILE_READ);
    if (!f) return all;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length() < 3) continue;
        HistoryEntry e;
        int a = line.indexOf('\t'), b = line.indexOf('\t', a + 1), c = line.indexOf('\t', b + 1);
        if (a < 0 || b < 0 || c < 0) continue;
        e.when = line.substring(0, a);
        e.uid = line.substring(a + 1, b);
        e.type = line.substring(b + 1, c);
        e.summary = line.substring(c + 1);
        all.push_back(e);
        if (all.size() > maxEntries * 2) all.erase(all.begin(), all.begin() + maxEntries);
    }
    f.close();
    std::vector<HistoryEntry> out;
    for (size_t i = all.size(); i > 0 && out.size() < maxEntries; i--) out.push_back(all[i - 1]);
    return out;
}

void historyClear()
{
    if (g_fs) g_fs->remove(kHistory);
}

static String makePath(const String &prefix, const String &uid, const char *ext)
{
    String u = uid;
    u.replace(" ", "");
    u.replace(":", "");
    return String(kDir) + "/" + prefix + "_" + u + "_" + nowString(true) + ext;
}

String storageSaveText(const String &prefix, const String &uid, const String &content)
{
    if (!g_fs) return "";
    String path = makePath(prefix, uid, ".txt");
    File f = g_fs->open(path, FILE_WRITE, true);
    if (!f) return "";
    f.print(content);
    f.close();
    return path;
}

String storageSaveBin(const String &prefix, const String &uid, const uint8_t *data, size_t len)
{
    if (!g_fs) return "";
    String path = makePath(prefix, uid, ".bin");
    File f = g_fs->open(path, FILE_WRITE, true);
    if (!f) return "";
    f.write(data, len);
    f.close();
    return path;
}
