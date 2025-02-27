#include <SPI.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include "TM1637Display.h"
#include <WebServer.h>
#include <Preferences.h>

#define CLK  2
#define DIO  0

#define SSID ""
#define PASSWORD ""

#if !defined(SSID) || !defined(PASSWORD)
  #error "Please fill SSID and PASSWORD"
#endif

#define STORAGE "clock"
#define TIME_ZONE_OFFSET 1
#define UPDATE_INTERVAL 3600

static const char *const hostname = "unicornclock";

hw_timer_t *My_timer = nullptr;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
TM1637Display display = TM1637Display(CLK, DIO);
Preferences preferences;
WebServer server(80);

const uint8_t prog[6][1] = {
        {SEG_A | SEG_DP},
        {SEG_B | SEG_DP},
        {SEG_C | SEG_DP},
        {SEG_D | SEG_DP},
        {SEG_E | SEG_DP},
        {SEG_F | SEG_DP}};

const uint8_t done[] = {
        SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,         // d
        SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F, // O
        SEG_C | SEG_E | SEG_G,                         // n
        SEG_A | SEG_D | SEG_E | SEG_F | SEG_G          // E
};

const uint8_t helo[] = {
        SEG_F | SEG_B | SEG_G | SEG_E | SEG_C,         // h
        SEG_F | SEG_A | SEG_G | SEG_E | SEG_D,         // E
        SEG_F | SEG_E | SEG_D,                         // L
        SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F  // O
};

int p = 0;

int brightness = 3;
volatile int hour = -1;
volatile int minute = -1;
volatile int second = -1;
volatile int lastUpdate = 0;


void OTA() {
    ArduinoOTA
            .onStart([]() {
                display.clear();
            })
            .onEnd([]() {
                display.setSegments(done);
                delay(1000);
            })
            .onProgress([](unsigned int progress, unsigned int total) {
                display.setSegments(prog[p], 1, 0);
                p++;
                if (p > 5) p = 0;
                display.showNumberDecEx(progress / (total / 100), 0b00001111, false, 4, 0);
            })
            .onError([](ota_error_t error) {
            });
    mdns_init();
    mdns_hostname_set(hostname);
    mdns_instance_name_set(hostname);
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.begin();
}

void initWiFi() {
    if(WiFi.status() == WL_CONNECTED) return;
    WiFi.begin(SSID, PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
    }
}

int lastSunday(int year, int month) {
    int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
        daysInMonth[1] = 29;
    }
    int lastDay = daysInMonth[month - 1];
    for (int day = lastDay; day > lastDay - 7; day--) {
        struct tm timeStruct = {0, 0, 0, day, month - 1, year - 1900};
        time_t t = mktime(&timeStruct);
        if (localtime(&t)->tm_wday == 0) { // Sunday
            return day;
        }
    }
    return lastDay;
}

bool isDaylightSavingTime(int year, int month, int day, int hour) {
    int dstStart = lastSunday(year, 3);
    int dstEnd = lastSunday(year, 10);
    if ((month > 3 && month < 10) ||
        (month == 3 && (day > dstStart || (day == dstStart && hour >= 2))) ||
        (month == 10 && (day < dstEnd || (day == dstEnd && hour < 3)))) {
        return true;
    }
    return false;
}

void updateNTP() {
    if (WiFi.status() != WL_CONNECTED) return;

    timeClient.update();
    time_t rawTime = timeClient.getEpochTime();
    struct tm *timeInfo = gmtime(&rawTime);

    hour = (timeClient.getHours() + (isDaylightSavingTime(timeInfo->tm_year + 1900, timeInfo->tm_mon + 1, timeInfo->tm_mday, timeInfo->tm_hour) ? (TIME_ZONE_OFFSET + 1) : TIME_ZONE_OFFSET)) % 24;
    minute = timeClient.getMinutes();
    second = timeClient.getSeconds();
}

void incrementTime();

void IRAM_ATTR onTimer() {
    incrementTime();
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
    WiFi.begin(SSID, PASSWORD);
}

void initTimer() {
    My_timer = timerBegin(1, 80, true);
    timerAttachInterrupt(My_timer, &onTimer, true);
    timerAlarmWrite(My_timer, 1000000, true);
    timerAlarmEnable(My_timer);
}

bool updateDisplay = false;

void incrementTime() {
    if (++second > 59) {
        second = 0;
        if (++minute > 59) {
            minute = 0;
            hour = (hour + 1) % 24;
        }
    }
    lastUpdate++;
    updateDisplay = true;
}

void handleRoot() {
    String html = "<!DOCTYPE html>"
                  "<html lang='hu'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
                  "<title>Fényerő Beállítás</title>"
                  "<style>body { font-family: Arial, sans-serif; text-align: center; padding: 20px; }"
                  "input { width: 80%; max-width: 300px; }</style>"
                  "</head><body><h2>Fényerő Beállítás</h2>"
                  "<input type='range' min='0' max='7' value='" + String(brightness) + "' id='brightnessSlider' onchange='updateBrightness(this.value)'>"
                                                                                       "<p id='brightnessValue'>" + String(brightness) + "</p>"
                                                                                                                                         "<script>function updateBrightness(val) {document.getElementById('brightnessValue').innerText = val; fetch('/set?b='+val);}</script>""</body></html>";
    server.send(200, "text/html", html);
}

void handleSetBrightness() {
    if (server.hasArg("b")) {
        brightness = server.arg("b").toInt();
        brightness = constrain(brightness, 0, 0x0F);

        preferences.begin(STORAGE, false);
        preferences.putInt("brightness", brightness);
        preferences.end();

        display.setBrightness(brightness);

        int timeValue = hour * 100 + minute;
        display.showNumberDecEx(timeValue, (second % 2) ? 0b11100000 : 0, true, 4, 0);
    }
    server.send(200, "text/plain", "OK");
}

void setup() {
    preferences.begin(STORAGE, false);
    brightness = preferences.getInt("brightness", 1);
    preferences.end();

    display.clear();
    display.setBrightness(brightness);
    display.clear();
    display.setSegments(helo);

    initWiFi();
    WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    OTA();

    server.on("/", handleRoot);
    server.on("/set", handleSetBrightness);
    server.begin();


    timeClient.begin();
    initTimer();
    updateNTP();
}

void loop() {
    if((hour < 0) || (lastUpdate > UPDATE_INTERVAL)) {
        updateNTP();
        lastUpdate = 0;
        if(hour > 7 && hour < 16) {
            display.setBrightness(brightness);
        } else {
            display.setBrightness(0);
        }
    }
    ArduinoOTA.handle();
    server.handleClient();
    if(updateDisplay) {
        updateDisplay = false;
        int time = hour * 100 + minute;
        display.showNumberDecEx(time, (second % 2) ? 0b11100000 : 0, true, 4, 0);
    }
}