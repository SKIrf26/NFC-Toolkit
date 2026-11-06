/**
 * storage.h - scan history + dump files.
 * Uses the microSD card when present, otherwise the watch's internal FAT partition.
 */
#pragma once
#include <Arduino.h>
#include <FS.h>
#include <vector>

struct HistoryEntry {
    String when;    // "2026-11-06 11:02"
    String uid;
    String type;
    String summary; // one line
};

bool storageBegin();
const char *storageName();          // "SD card" / "Internal flash" / "none"
fs::FS *storageFs();

void historyAdd(const String &uid, const String &type, const String &summary);
std::vector<HistoryEntry> historyLoad(size_t maxEntries = 60); // newest first
void historyClear();

/** Writes a text file under /nfc/. Returns the full path or "" on failure. */
String storageSaveText(const String &prefix, const String &uid, const String &content);
/** Writes a binary file under /nfc/. */
String storageSaveBin(const String &prefix, const String &uid, const uint8_t *data, size_t len);

String nowString(bool forFileName = false);
