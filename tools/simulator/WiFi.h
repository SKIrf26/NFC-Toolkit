#pragma once
#include "Arduino.h"
typedef int wl_status_t;
#define WL_CONNECTED 3
#define WIFI_STA 1
struct IP { String toString(){return "192.168.1.42";} };
struct SimWiFi { void mode(int){} void begin(const char*,const char*){} wl_status_t status(){return 0;} IP localIP(){return IP();} void disconnect(bool){} };
extern SimWiFi WiFi;
