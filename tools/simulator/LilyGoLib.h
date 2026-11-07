#pragma once
#include "Arduino.h"
typedef uint16_t ReturnCode; typedef uint8_t ndefTypeId;
#define ST_ERR_NONE 0
#include "nfc_types.inc"
struct SimInstance { void vibrator(){} int getBatteryPercent(){return 87;} bool isCardReady(){return true;} };
extern SimInstance instance;
struct SimNfc { LilyGoNfcEmulationStatus st{}; bool isRunning(){return false;} void stop(){} void loop(){}
  const LilyGoNfcEmulationStatus &emulationStatus(){return st;} };
extern SimNfc LilyGoNfc;
const char *lilygoNfcRecordKindName(LilyGoNfcRecordKind k);
const char *lilygoNfcEmulationKindName(LilyGoNfcEmulationKind k);
const char *lilygoNfcEmulationTechName(LilyGoNfcEmulationTech t);
