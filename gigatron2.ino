#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <U8g2lib.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <NonBlockingDallas.h>
#include <ArduinoOTA.h>
#include <BlynkSimpleEsp8266.h>
#include "time.h"
#include <StreamLib.h>
#include <FastLED.h>
#include <Preferences.h>
#include <RTClib.h>
#include <WiFiManager.h>

RTC_DS3231 rtc;
const unsigned long WATCHDOG_RESET = 5000; // 5 seconds
const unsigned long WATCHDOG_INTERVAL = 15; // Reset watchdog every 10 seconds
unsigned long lastWatchdogReset = 0;

#define LED_PIN D8
#define pushbutton D4
#define pinSSR D7
enum { PinA = D6, PinB = D5, IPINMODE = INPUT };
const int oneWireBus = D3;

#define NUM_LEDS 1
CRGB leds[NUM_LEDS];

// Initialize the SHT31 sensor
Adafruit_SHT31 sht31 = Adafruit_SHT31();

// Initialize the SH1106 OLED (I2C, 128x64)
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

OneWire oneWire(oneWireBus);
DallasTemperature dallasTemp(&oneWire);
NonBlockingDallas sensorDs18b20(&dallasTemp);

Preferences preferences;

#define every(interval) \
    static uint32_t __every__##interval = millis(); \
    if (millis() - __every__##interval >= interval && (__every__##interval = millis()))

char auth[] = "xzOx2JrFeyFk7ea6zAZmHCzqBRC_ciuH";
volatile bool rotLeft = false;   // Flag for left rotation
volatile bool rotRight = false;  // Flag for right rotation
const char* ssid = "mikesnet";
const char* password = "springchicken";
unsigned long millisBlynk, millisTemp, millisPage;
bool onwrongpage = false;
int buttoncounter;
int hours, mins, secs;
int savecount;
int chours, cmins, shours, smins, whours, wmins, whoursdouble, wminsdouble, shoursdouble, sminsdouble;
bool isAwake = true;
int page = 1;

int ledValue;
bool haschanged = false;
bool haschanged2 = false;
bool timechanged = false;

bool ssrState = false;
float hysteresis = 0.6;
bool partymode = false;

const int DEBOUNCE_DELAY = 50;
int lastSteadyState = LOW;
int lastFlickerableState = LOW;
int currentState;
bool buttonstate = false;
unsigned long lastDebounceTime = 0;

static byte abOld;
volatile int count = 200;
int old_count;
int halfcount;

float shtTemp, shtHum, temperatureC, absHum;
float setTemp = 20.0;
float waketemp = 21.3;
float sleeptemp = 19.0;
int encoder0Pos;
float tempOffset = 0;

uint8_t startHue = 0;
uint8_t deltaHue = 0;

enum MenuState {
  MENU_NONE,
  MENU_MAIN,
  MENU_EDIT_WAKE_HOUR,
  MENU_EDIT_WAKE_MIN,
  MENU_EDIT_SLEEP_HOUR,
  MENU_EDIT_SLEEP_MIN,
  MENU_EDIT_WAKE_TEMP,
  MENU_EDIT_SLEEP_TEMP,
  MENU_EDIT_SET_TIME_HOUR,
  MENU_EDIT_SET_TIME_MIN,
  MENU_CONFIRM_RESET_WIFI
};

struct MenuOption {
  const char* label;
  MenuState nextState;
  bool visible;
};

volatile MenuState menuState = MENU_NONE;
int menuIndex = 0;

MenuOption menuOptions[] = {
  {"Wake Time", MENU_EDIT_WAKE_HOUR, true},
  {"Sleep Time", MENU_EDIT_SLEEP_HOUR, true},
  {"Wake Temp", MENU_EDIT_WAKE_TEMP, true},
  {"Sleep Temp", MENU_EDIT_SLEEP_TEMP, true},
  {"Reset WiFi", MENU_CONFIRM_RESET_WIFI, true},
  {"Set Time", MENU_EDIT_SET_TIME_HOUR, false} // Only visible if no WiFi
};
const int menuOptionCount = sizeof(menuOptions)/sizeof(menuOptions[0]);

static int editValue = 0;
static float editTempValue = 0.0;
static int editMin = 0, editMax = 0;
static int editStep = 1;
static bool editing = false;
static int editStage = 0; // 0 = hour, 1 = min for time edits

void updateMenuVisibility() {
    menuOptions[5].visible = (WiFi.status() != WL_CONNECTED);
}

// Draw edit screen for hour/min/temp
void drawEditScreen(const char* label, int value, int minVal, int maxVal, bool highlight) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(2, 14, label);
    
    char valbuf[8];
    snprintf(valbuf, sizeof(valbuf), "%02d", value);
    int strWidth = u8g2.getStrWidth(valbuf);
    if (highlight) {

        u8g2.setDrawColor(1);
        u8g2.setFont(u8g2_font_helvB24_tn);
        u8g2.setCursor(48, 48);
        drawCenteredInvStr(u8g2, 64, 48, valbuf, 24);
        //u8g2.setDrawColor(2);
        //u8g2.drawBox(40, 24, strWidth, 24);
        //u8g2.print(valbuf);
        u8g2.setDrawColor(1);
    } else {
        u8g2.setFont(u8g2_font_helvB24_tn);
        u8g2.setCursor(48, 48);
        drawCenteredStr(u8g2, 64, 48, valbuf);
    }
    u8g2.sendBuffer();
}

void drawEditTempScreen(const char* label, float value, bool highlight) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(2, 14, label);
    char valbuf[8];
    snprintf(valbuf, sizeof(valbuf), "%.1f", value);
    int strWidth = u8g2.getStrWidth(valbuf);
    if (highlight) {

        u8g2.setDrawColor(1);
        u8g2.setFont(u8g2_font_helvB24_tn);
        u8g2.setCursor(48, 48);
        drawCenteredInvStr(u8g2, 64, 48, valbuf, 24);
        //u8g2.setDrawColor(2);
        //u8g2.drawBox(40, 24, strWidth, 24);
        u8g2.setDrawColor(1);
    } else {
        u8g2.setFont(u8g2_font_helvB24_tn);
        u8g2.setCursor(48, 48);
        drawCenteredStr(u8g2, 64, 48, valbuf);
    }
    u8g2.sendBuffer();
}

void drawConfirmScreen(const char* msg) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(2, 24, msg);
    u8g2.drawStr(2, 48, "Press button to confirm");
    u8g2.sendBuffer();
}

void setupWiFiWithManager() {
  WiFiManager wm;
  bool res;
  unsigned long startAttempt = millis();
  bool skipWifi = false;

  // Try to connect, or start config portal if fails
  wm.setConfigPortalTimeout(180); // 3 minutes timeout
  wm.setAPCallback([](WiFiManager *myWM) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 12, "Please connect to");
    u8g2.drawStr(0, 24, "FloorHeatWifi");
    u8g2.drawStr(0, 36, "and browse to");
    u8g2.drawStr(0, 48, "192.168.4.1");
    u8g2.drawStr(0, 60, "Press button to skip");
    u8g2.sendBuffer();
  });
    leds[0] = CRGB(100, 100, 0);
   FastLED.show();
  //wm.setConfigPortalBlocking(false);
  res = wm.autoConnect("FloorHeatWifi");
  //wm.process();
  /*while (WiFi.status() != WL_CONNECTED && millis() < 30000) {
    wm.process();
    leds[0] = CRGB(100, 100, 0);
    FastLED.show();
    delay(100);
    leds[0] = CRGB(0, 0, 0);
    FastLED.show();
    delay(100);
    Serial.print(".");
  }*/
  // If failed or user pressed button, skip WiFi
  if (!res || !pushbutton) {
    leds[0] = CRGB(100, 0, 0);
    FastLED.show();
    //delay(1000);
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 12, "WiFi failed");
    u8g2.drawStr(0, 24, "or skipped.");
    u8g2.sendBuffer();
    delay(1000);
    WiFi.disconnect(true);
  }
  else {
    leds[0] = CRGB(0, 100, 0);
    FastLED.show();
    //delay(1000);
    /*u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 12, "WiFi connected.");
    u8g2.drawStr(0, 24, "IP: ");
    u8g2.drawStr(0, 36, WiFi.localIP().toString().c_str());
    u8g2.sendBuffer();
    delay(1000);*/
  }
}

void drawMenu() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  int y = 16;
  for (int i = 0, shown = 0; i < menuOptionCount; ++i) {
    if (!menuOptions[i].visible) continue;
    if (shown == menuIndex) {
      u8g2.setDrawColor(1);
      u8g2.drawBox(0, y-10, 128, 12);
      u8g2.setDrawColor(0);
      u8g2.drawStr(2, y, menuOptions[i].label);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(2, y, menuOptions[i].label);
    }
    y += 14;
    shown++;
  }
  u8g2.sendBuffer();
}

void handleMenuInput() {
  static bool lastButton = false;
    int shownOptions = 0;
    for (int i = 0; i < menuOptionCount; ++i)
        if (menuOptions[i].visible) shownOptions++;

    // Encoder navigation using rotLeft/rotRight
    if (rotRight) {
        menuIndex = (menuIndex + 1) % shownOptions;
        rotRight = false;
        drawMenu();
    }
    if (rotLeft) {
        menuIndex = (menuIndex - 1 + shownOptions) % shownOptions;
        rotLeft = false;
        drawMenu();
    }

  // Button press
  if (buttonstate && !lastButton) {
    // Enter selected menu
    if (menuState == MENU_NONE)
    {savecount = halfcount;}
    menuState = menuOptions[menuIndex].nextState;
    // TODO: Draw edit screen for selected option
  }
  lastButton = buttonstate;
}


IRAM_ATTR void pinChangeISR() {
  enum { upMask = 0x66, downMask = 0x99 };
  byte abNew = (digitalRead(PinA) << 1) | digitalRead(PinB);
  byte criterion = abNew ^ abOld;
  if (criterion == 1 || criterion == 2) {
    if (upMask & (1 << (2 * abOld + abNew / 2))) {
      count++;
        if (menuState == MENU_NONE) {
            setTemp += 0.05;
        }
    } else {
      count--;
        if (menuState == MENU_NONE) {
            setTemp -= 0.05;
        }
    }
  }
  abOld = abNew;
  haschanged = true;
}


WidgetTerminal terminal(V0);


BLYNK_WRITE(V0) {
  if (String("help") == param.asStr()) {
    terminal.println("==List of available commands:==");
    terminal.println("wifi");
    terminal.println("blink");
    terminal.println("temp");
    terminal.println("invert");
    terminal.println("reset");
    terminal.println("==End of list.==");
  }
  if (String("wifi") == param.asStr()) {
    terminal.print("Connected to: ");
    terminal.println(ssid);
    terminal.print("IP address:");
    terminal.println(WiFi.localIP());
    terminal.print("Signal strength: ");
    terminal.println(WiFi.RSSI());
    printLocalTime();
  }
  if (String("temp") == param.asStr()) {
    shtTemp = sht31.readTemperature();
    shtHum = sht31.readHumidity();
    shtTemp += tempOffset;
    terminal.print("SHTTemp: ");
    terminal.print(shtTemp);
    terminal.print(", SHTHum: ");
    terminal.print(shtHum);
    terminal.print("ProbeTemp: ");
    terminal.print(temperatureC);
  }
  if (String("reset") == param.asStr()) {
    terminal.println("Restarting...");
    terminal.flush();
    ESP.restart();
  }
  terminal.flush();
}

BLYNK_WRITE(V40) {
  float pinValue = param.asFloat();
  setTemp = pinValue;
  Blynk.virtualWrite(V5, setTemp);
  Blynk.virtualWrite(V41, ledValue);
  haschanged2 = true;
}



void printLocalTime() {
  time_t rawtime;
  struct tm* timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);
  terminal.print(asctime(timeinfo));
}

void handleTemperatureChange(int deviceIndex, int32_t temperatureRAW){
  temperatureC = sensorDs18b20.rawToCelsius(temperatureRAW);
}

void handleIntervalElapsed(int deviceIndex, int32_t temperatureRAW)
{
  temperatureC = sensorDs18b20.rawToCelsius(temperatureRAW);
}

void handleDeviceDisconnected(int deviceIndex)
{
  terminal.print(F("[NonBlockingDallas] handleDeviceDisconnected ==> deviceIndex="));
  terminal.print(deviceIndex);
  terminal.println(F(" disconnected."));
  printLocalTime();
  terminal.flush();
}

struct tm timeinfo;
bool isSetNtp = false;
unsigned long localTimeUnix;

void cbSyncTime() {
  Serial.println("NTP time synched");
  Serial.println("getlocaltime");
  getLocalTime(&timeinfo);

  time_t rawtime;
  struct tm* timeinfo;
  time(&rawtime);
  timeinfo = localtime(&rawtime);

  Serial.println(asctime(timeinfo));
  time_t now = time(nullptr); // local-adjusted time
  localTimeUnix = static_cast<uint32_t>(now); // 32-bit to send via ESP-NOW
  isSetNtp = true;
}

void initSNTP() {  
  // Set timezone and start NTP
  setTimezone();
  configTime("EST5EDT,M3.2.0,M11.1.0",  "192.168.50.197","pool.ntp.org");
  wait4SNTP();
  cbSyncTime();

}

void wait4SNTP() {
  Serial.print("Waiting for time...");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) { // Wait for valid time (after 2010)
    delay(1000);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("Time set!");
}

void setTimezone() {  
  setenv("TZ","EST5EDT,M3.2.0,M11.1.0",1);
  tzset();
}

void drawCenteredStr(U8G2 &u8g2, int displayWidth, int y, const char *str) {
  int strWidth = u8g2.getStrWidth(str);
  int x = (displayWidth - strWidth) / 2;
  u8g2.setCursor(x, y);
  u8g2.print(str);
}

void drawCenteredInvStr(U8G2 &u8g2, int displayWidth, int y, const char *str, int height) {
  int strWidth = u8g2.getStrWidth(str);
  int x = (displayWidth - strWidth) / 2;
  u8g2.setCursor(x, y);
  u8g2.print(str);
  u8g2.setDrawColor(2); // Invert color
  u8g2.drawBox(x, y-height-1, strWidth, height+2);
}

void page1() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);
  char buf[16];
  int hour12 = timeinfo.tm_hour % 12;
  if (hour12 == 0) hour12 = 12;
  snprintf(buf, sizeof(buf), "%d:%02d:%02d %s", hour12, timeinfo.tm_min, timeinfo.tm_sec, (timeinfo.tm_hour < 12) ? "AM" : "PM");
  char shtBuff[8], setBuff[16], probeBuff[16];
    char degBuff[4];
    snprintf(degBuff, sizeof(degBuff), "%cC", 176);
  snprintf(shtBuff, sizeof(shtBuff), "%.1f", shtTemp, 176);
  snprintf(setBuff, sizeof(setBuff), "%.1f%cC", setTemp, 176);
  snprintf(probeBuff, sizeof(probeBuff), "%.1f%cC", temperatureC, 176);
//u8g2_font_8x13_tf
  u8g2.firstPage();
  do {
    // Main temp at top, largest font that fits
    u8g2.setFont(u8g2_font_5x7_tf); // Small font

    int degWidth = u8g2.getStrWidth(degBuff);
    int degX = 64 - degWidth - 2; // 2px padding from right edge (for 64px wide area)
    int degY = 7; // Adjust Y as needed to align with main temp
    u8g2.setCursor(degX, degY);
    //u8g2.print(degBuff);

    u8g2.setFont(u8g2_font_helvB24_tn); // Good size for 64px width
    int shtWidth = u8g2.getStrWidth(shtBuff);
    int shtX = (64 - shtWidth) / 2;
    int shtY = 25;
    drawCenteredStr(u8g2, 64, shtY, shtBuff);

    // Degree symbol and C
     degX = shtX + shtWidth + 2;


//u8g2_font_fub11_tf
    u8g2.setFont(u8g2_font_8x13_tf);
    // Set temp in middle
    int setY = shtY + 25;
    int setWidth = u8g2.getStrWidth(setBuff);
    drawCenteredStr(u8g2, 64, setY, "Set:");
    u8g2.setFont(u8g2_font_fub14_tf);
    drawCenteredInvStr(u8g2, 64, setY + 22, setBuff, 14);

    // Knob icon to left of set temp
    //u8g2.drawCircle(5, setY - 6, 3, U8G2_DRAW_ALL);

    // Floor/probe temp at bottom
    int probeY = 128 - 25;
    int probeWidth = u8g2.getStrWidth(probeBuff);
    u8g2.setFont(u8g2_font_5x7_tf);
    drawCenteredStr(u8g2, 64, probeY, "Floor:");
    //u8g2.setFont(u8g2_font_fub14_tf);
    drawCenteredStr(u8g2, 64, probeY + 12, probeBuff);
    // underline for "Flr"
    u8g2.drawHLine((64 - probeWidth) / 2, probeY + 14, probeWidth);
    u8g2.setFont(u8g2_font_5x7_tf);
    drawCenteredStr(u8g2, 64, 128, buf);
  } while (u8g2.nextPage());
}

void page2() {
  char tbuff[16];
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(tbuff, sizeof(tbuff), "%02d:%02d", hours, mins);
  } else {
    snprintf(tbuff, sizeof(tbuff), "%02d:%02d", hours, mins);
  }

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(2, 10);
    if (WiFi.status() == WL_CONNECTED)
      u8g2.print("WiFi Time:");
    else
      u8g2.print("Set hour:");

    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.setCursor((128-u8g2.getStrWidth(tbuff))/2, 32);
    u8g2.print(tbuff);

    if (WiFi.status() != WL_CONNECTED) {
      u8g2.setFont(u8g2_font_helvB14_tr);
      u8g2.setCursor(32, 56);
      u8g2.print("^");
    }
  } while (u8g2.nextPage());
}

void page3() {
  char tbuff[16];
  snprintf(tbuff, sizeof(tbuff), "%02d:%02d", hours, mins);

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(2, 10);
    u8g2.print("Set minute:");

    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.setCursor((128-u8g2.getStrWidth(tbuff))/2, 32);
    u8g2.print(tbuff);

    u8g2.setCursor(96, 56);
    u8g2.print("^");
  } while (u8g2.nextPage());
}

void page4() {
  char tbuff[16];
  snprintf(tbuff, sizeof(tbuff), "%02d:%02d", whours, wmins);

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(2, 10);
    u8g2.print("Wake hour:");

    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.setCursor((128-u8g2.getStrWidth(tbuff))/2, 32);
    u8g2.print(tbuff);

    u8g2.setCursor(32, 56);
    u8g2.print("^");
  } while (u8g2.nextPage());
}

void page5() {
  char tbuff[16];
  snprintf(tbuff, sizeof(tbuff), "%02d:%02d", whours, wmins);

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(2, 10);
    u8g2.print("Wake min:");

    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.setCursor((128-u8g2.getStrWidth(tbuff))/2, 32);
    u8g2.print(tbuff);

    u8g2.setCursor(96, 56);
    u8g2.print("^");
  } while (u8g2.nextPage());
}

void page6() {
  char tbuff[16];
  snprintf(tbuff, sizeof(tbuff), "%02d:%02d", shours, smins);

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(2, 10);
    u8g2.print("Sleep hour:");

    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.setCursor((128-u8g2.getStrWidth(tbuff))/2, 32);
    u8g2.print(tbuff);

    u8g2.setCursor(32, 56);
    u8g2.print("^");
  } while (u8g2.nextPage());
}

void page7() {
  char tbuff[16];
  snprintf(tbuff, sizeof(tbuff), "%02d:%02d", shours, smins);

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(2, 10);
    u8g2.print("Sleep min:");

    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.setCursor((128-u8g2.getStrWidth(tbuff))/2, 32);
    u8g2.print(tbuff);

    u8g2.setCursor(96, 56);
    u8g2.print("^");
  } while (u8g2.nextPage());
}

void page8() {
  char tbuff[16];
  snprintf(tbuff, sizeof(tbuff), "%.1f°C", waketemp);

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(2, 10);
    u8g2.print("Wake temp:");

    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.setCursor((128-u8g2.getStrWidth(tbuff))/2, 32);
    u8g2.print(tbuff);

    u8g2.setCursor(64, 56);
    u8g2.print("^");
  } while (u8g2.nextPage());
}

void page9() {
  char tbuff[16];
  snprintf(tbuff, sizeof(tbuff), "%.1f°C", sleeptemp);

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(2, 10);
    u8g2.print("Sleep temp:");

    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.setCursor((128-u8g2.getStrWidth(tbuff))/2, 32);
    u8g2.print(tbuff);

    u8g2.setCursor(64, 56);
    u8g2.print("^");
  } while (u8g2.nextPage());
}

void u8g2DrawWordWrap(U8G2 &u8g2, int x, int y, int lineHeight, const char *text, int maxWidth) {
  char buf[128];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *line = buf;
  while (*line) {
    int len = 0;
    int width = 0;
    while (line[len] && (width = u8g2.getStrWidth(String(line).substring(0, len + 1).c_str())) <= maxWidth) {
      len++;
    }
    if (width > maxWidth && len > 1) len--; // backtrack if over
    char saved = line[len];
    line[len] = '\0';
    u8g2.drawStr(x, y, line);
    line[len] = saved;
    y += lineHeight;
    line += len;
    while (*line == ' ') line++; // skip spaces
  }
}

void initializeWatchdog() {
  Serial.println("Initializing DS3231 watchdog...");
  
  // Clear any existing alarms
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
 // rtc.disableAlarm(1);
  rtc.disableAlarm(2);
  
  // Set up Alarm 1 to trigger in 2 minutes (failsafe timeout)
  DateTime now = rtc.now();
  DateTime alarmTime = now + TimeSpan(WATCHDOG_INTERVAL); // 2 minutes from now
  
  rtc.setAlarm1(alarmTime, DS3231_A1_Hour); // Match hour, minute, second

  Serial.print("Watchdog alarm set for: ");
char buf[32];
Serial.println(alarmTime.toString(buf));
}

void resetWatchdog() {
  Serial.println("Resetting watchdog timer...");
  
  // Clear the alarm flag
  rtc.clearAlarm(1);
  
  // Set new alarm for 2 minutes from now
  DateTime now = rtc.now();
  DateTime newAlarmTime = now + TimeSpan(WATCHDOG_INTERVAL); // WATCHDOG_INTERVAL in seconds
  
  rtc.setAlarm1(newAlarmTime, DS3231_A1_Hour);
  //rtc.enableAlarm1();
  
  Serial.print("Watchdog reset. Next timeout: ");
char buf[32];
Serial.println(newAlarmTime.toString(buf));
}


void setup() {
  Serial.begin(115200);
  //setTimezone();
  pinMode(pinSSR, OUTPUT);
  digitalWrite(pinSSR, LOW);
  whours = 5;
  shours = 22;
  Wire.begin(D2, D1);  // SDA, SCL
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
  }
  rtc.disable32K();
  rtc.writeSqwPinMode(DS3231_OFF);
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  leds[0] = CRGB(0, 0, 100);
  FastLED.show();
  delay(100);
  leds[0] = CRGB(0, 0, 0);
  FastLED.show();
  sht31.begin(0x44);
  pinMode(PinA, IPINMODE);
  pinMode(PinB, IPINMODE);
  pinMode(pushbutton, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(PinA), pinChangeISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PinB), pinChangeISR, CHANGE);
  abOld = count = old_count = 0;
  u8g2.begin();
  u8g2.setDisplayRotation(U8G2_R1); 
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 12, "Connecting...");
  u8g2.sendBuffer();
  
  WiFi.mode(WIFI_STA);
  WiFi.setPhyMode(WIFI_PHY_MODE_11B);
 // WiFi.begin(ssid, password);
 setupWiFiWithManager();
  // --- U8G2 display initialization ---



 // initSNTP();
  //Serial.println("getlocaltime");
  //getLocalTime(&timeinfo);


  preferences.begin("my-app", false);
  whours = preferences.getInt("whours", 0);
  wmins  = preferences.getInt("wmins", 0);
  shours = preferences.getInt("shours", 0);
  smins  = preferences.getInt("smins", 0);
  waketemp = preferences.getFloat("waketemp", 0);
  sleeptemp  = preferences.getFloat("sleeptemp", 0);
  preferences.end();
  whoursdouble = whours * 2;
  wminsdouble = wmins * 2;
  shoursdouble = shours * 2;
  sminsdouble = smins * 2;

  u8g2.clearBuffer();
  if (WiFi.status() == WL_CONNECTED) {

    //delay(1000);
    ArduinoOTA.setHostname("Gigatron");
    ArduinoOTA.begin();
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Blynk.config(auth, IPAddress(192, 168, 50, 197), 8080);
    Blynk.connect();

    char buff[64];
    snprintf(buff, sizeof(buff), "%s, %s", ssid, WiFi.localIP().toString().c_str());
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2DrawWordWrap(u8g2, 0, 12, 12, buff, 64);
    u8g2.sendBuffer();
    delay(250);
    initSNTP();
   // delay(250);
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    hours = timeinfo.tm_hour;
    mins = timeinfo.tm_min;
    secs = timeinfo.tm_sec;
    time_t rawtime;
    time(&rawtime); // rawtime is UTC
    struct tm *utc_tm = gmtime(&rawtime); // get UTC time struct
    DateTime ntpTime(
      utc_tm->tm_year + 1900,
      utc_tm->tm_mon + 1,
      utc_tm->tm_mday,
      utc_tm->tm_hour,
      utc_tm->tm_min,
      utc_tm->tm_sec
    );
      // Set RTC
      rtc.adjust(ntpTime);

      DateTime now = rtc.now();
      Serial.println(now.timestamp());
    if (hours > whours) {
      isAwake = true;
      setTemp = waketemp;
    }
    else {
      isAwake = false;
      setTemp = sleeptemp;
    }
    terminal.println("**********Gigatron 2.0***********");
    terminal.print("Connected to ");
    terminal.println(ssid);
    terminal.print("IP address: ");
    terminal.println(WiFi.localIP());
    terminal.print("Signal strength: ");
    terminal.println(WiFi.RSSI());
    String comma = ", ";
    printLocalTime();
    terminal.println("Loaded values whours, wmins, shours, smins, waketemp, sleeptemp:");
    terminal.println(whours + comma + wmins + comma + shours + comma + smins + comma + waketemp + comma + sleeptemp);
    terminal.flush();
    Blynk.virtualWrite(V40, setTemp);
    leds[0] = CRGB(0, 0, 0);
    FastLED.show();
  }
  else {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(0, 12, "ERR: NO WIFI.");
    u8g2.sendBuffer();
  }
  u8g2.clearBuffer();
  // No direct invertDisplay in U8G2, but you can use drawBox or invert colors if needed

  sensorDs18b20.begin(NonBlockingDallas::resolution_12, 4000);
  sensorDs18b20.onTemperatureChange(handleTemperatureChange);
  sensorDs18b20.onIntervalElapsed(handleIntervalElapsed);
  sensorDs18b20.onDeviceDisconnected(handleDeviceDisconnected);
  sensorDs18b20.update();
  shtTemp = sht31.readTemperature();
  shtHum = sht31.readHumidity();
  shtTemp += tempOffset;
  initializeWatchdog();
    leds[0] = CRGB(0, 0, 0);
    FastLED.show();
}

void loop() {
  sensorDs18b20.update();
  whours = whoursdouble / 2;
  wmins = wminsdouble /2;
  shours = shoursdouble /2;
  smins = sminsdouble/2;
  int tempcount = count / 2;
  
updateMenuVisibility();
  if (old_count > tempcount) {  //if the encoder was turned left
    rotLeft = true;
  } else if (old_count < tempcount) {  //if the encoder was turned right
    rotRight = true;
  }
  old_count = tempcount;
    // --- MENU SYSTEM ---
    if (menuState == MENU_NONE) {
        // Main screen
        page1();
        
        // Open menu on button press
        if (digitalRead(pushbutton) == LOW) {
            delay(50); // debounce
            if (digitalRead(pushbutton) == LOW) {
                menuState = MENU_MAIN;
                menuIndex = 0;
                drawMenu();
                while (digitalRead(pushbutton) == LOW); // wait for release
                delay(50);
            }
        }
    }
    else if (menuState == MENU_MAIN) {
    drawMenu();
    handleMenuInput();
    // Enter edit mode on button press
    if (digitalRead(pushbutton) == LOW) {
        delay(50);
        if (digitalRead(pushbutton) == LOW) {
            MenuState next = menuOptions[menuIndex].nextState;
            switch (next) {
                case MENU_EDIT_WAKE_HOUR:
                    editValue = whours;
                    editMin = 0; editMax = 23; editStep = 1; editStage = 0;
                    menuState = MENU_EDIT_WAKE_HOUR;
                    break;
                case MENU_EDIT_SLEEP_HOUR:
                    editValue = shours;
                    editMin = 0; editMax = 23; editStep = 1; editStage = 0;
                    menuState = MENU_EDIT_SLEEP_HOUR;
                    break;
                case MENU_EDIT_WAKE_TEMP:
                    editTempValue = waketemp;
                    menuState = MENU_EDIT_WAKE_TEMP;
                    break;
                case MENU_EDIT_SLEEP_TEMP:
                    editTempValue = sleeptemp;
                    menuState = MENU_EDIT_SLEEP_TEMP;
                    break;
                case MENU_CONFIRM_RESET_WIFI:
                    menuState = MENU_CONFIRM_RESET_WIFI;
                    break;
                case MENU_EDIT_SET_TIME_HOUR:
                    editValue = hours;
                    editMin = 0; editMax = 23; editStep = 1; editStage = 0;
                    menuState = MENU_EDIT_SET_TIME_HOUR;
                    break;
                default:
                    menuState = MENU_NONE;
                    break;
            }
            while (digitalRead(pushbutton) == LOW); // wait for release
            delay(50);
        }
    }
} else if (menuState == MENU_EDIT_WAKE_HOUR) {
    drawEditScreen("Wake Hour", editValue, 0, 23, true);
    if (rotLeft) {
        //editValue = (editValue < 23) ? editValue + 1 : 0;
        editValue = (editValue + 23) % 24;
        rotLeft = false;
    }
    if (rotRight) {
        //editValue = (editValue > 0) ? editValue - 1 : 23;
        editValue = (editValue + 1) % 24;
        rotRight = false;
    }
    if (digitalRead(pushbutton) == LOW) {
        delay(50);
        if (digitalRead(pushbutton) == LOW) {
            whours = editValue;
            editValue = wmins;
            editMin = 0; editMax = 59; editStep = 1;
            menuState = MENU_EDIT_WAKE_MIN;
            while (digitalRead(pushbutton) == LOW); delay(50);
        }
    }
} else if (menuState == MENU_EDIT_WAKE_MIN) {
    drawEditScreen("Wake Min", editValue, 0, 59, true);
    if (rotLeft) {
        //editValue = (editValue < 59) ? editValue + 1 : 0;
        editValue = (editValue + 59) % 60;
        rotLeft = false;
    }
    if (rotRight) {
        //editValue = (editValue > 0) ? editValue - 1 : 59;
        editValue = (editValue + 1) % 60;
        rotRight = false;
    }
    if (digitalRead(pushbutton) == LOW) {
        delay(50);
        if (digitalRead(pushbutton) == LOW) {
            wmins = editValue;
            whoursdouble = whours * 2;
            wminsdouble = wmins * 2;
            preferences.begin("my-app", false);
            preferences.putInt("whours", whours);
            preferences.putInt("wmins", wmins);
            preferences.end();
            halfcount = savecount;
            menuState = MENU_NONE;
            while (digitalRead(pushbutton) == LOW); delay(50);
        }
    }
}
// Repeat this pattern for all other edit screens:
else if (menuState == MENU_EDIT_SLEEP_HOUR) {
    drawEditScreen("Sleep Hour", editValue, 0, 23, true);
    if (rotLeft) {
        //editValue = (editValue < 23) ? editValue + 1 : 0;
        editValue = (editValue + 23) % 24;
        rotLeft = false;
    }
    if (rotRight) {
        //editValue = (editValue > 0) ? editValue - 1 : 23;
        editValue = (editValue + 1) % 24;
        rotRight = false;
    }
    if (digitalRead(pushbutton) == LOW) {
        delay(50);
        if (digitalRead(pushbutton) == LOW) {
            shours = editValue;
            editValue = smins;
            editMin = 0; editMax = 59; editStep = 1;
            menuState = MENU_EDIT_SLEEP_MIN;
            while (digitalRead(pushbutton) == LOW); delay(50);
        }
    }
}
else if (menuState == MENU_EDIT_SLEEP_MIN) {
    drawEditScreen("Sleep Min", editValue, 0, 59, true);
    if (rotLeft) {
        //editValue = (editValue < 59) ? editValue + 1 : 0;
        editValue = (editValue + 59) % 60;
        rotLeft = false;
    }
    if (rotRight) {
        //editValue = (editValue > 0) ? editValue - 1 : 59;
        editValue = (editValue + 1) % 60;
        rotRight = false;
    }
    if (digitalRead(pushbutton) == LOW) {
        delay(50);
        if (digitalRead(pushbutton) == LOW) {
            smins = editValue;
            shoursdouble = shours * 2;
            sminsdouble = smins * 2;
            preferences.begin("my-app", false);
            preferences.putInt("shours", shours);
            preferences.putInt("smins", smins);
            preferences.end();
            halfcount = savecount;
            menuState = MENU_NONE;
            while (digitalRead(pushbutton) == LOW); delay(50);
        }
    }
}
else if (menuState == MENU_EDIT_WAKE_TEMP) {
    drawEditTempScreen("Wake Temp", editTempValue, true);
    if (rotRight) {
        editTempValue += 0.1;
        if (editTempValue > 40.0) editTempValue = 40.0;
        rotRight = false;
    }
    if (rotLeft) {
        editTempValue -= 0.1;
        if (editTempValue < 5.0) editTempValue = 5.0;
        rotLeft = false;
    }
    if (digitalRead(pushbutton) == LOW) {
        delay(50);
        if (digitalRead(pushbutton) == LOW) {
            waketemp = editTempValue;
            preferences.begin("my-app", false);
            preferences.putFloat("waketemp", waketemp);
            preferences.end();
            halfcount = savecount;
            menuState = MENU_NONE;
            while (digitalRead(pushbutton) == LOW); delay(50);
        }
    }
}
else if (menuState == MENU_EDIT_SLEEP_TEMP) {
    drawEditTempScreen("Sleep Temp", editTempValue, true);
    if (rotRight) {
        editTempValue += 0.1;
        if (editTempValue > 40.0) editTempValue = 40.0;
        rotRight = false;
    }
    if (rotLeft) {
        editTempValue -= 0.1;
        if (editTempValue < 5.0) editTempValue = 5.0;
        rotLeft = false;
    }
    if (digitalRead(pushbutton) == LOW) {
        delay(50);
        if (digitalRead(pushbutton) == LOW) {
            sleeptemp = editTempValue;
            preferences.begin("my-app", false);
            preferences.putFloat("sleeptemp", sleeptemp);
            preferences.end();
            halfcount = savecount;
            menuState = MENU_NONE;
            while (digitalRead(pushbutton) == LOW); delay(50);
        }
    }
}
else if (menuState == MENU_EDIT_SET_TIME_HOUR) {
    drawEditScreen("Set Hour", editValue, 0, 23, true);
    if (rotLeft) {
        editValue = (editValue < 23) ? editValue + 1 : 0;
        rotLeft = false;
    }
    if (rotRight) {
        editValue = (editValue > 0) ? editValue - 1 : 23;
        rotRight = false;
    }
    if (digitalRead(pushbutton) == LOW) {
        delay(50);
        if (digitalRead(pushbutton) == LOW) {
            hours = editValue;
            editValue = mins;
            editMin = 0; editMax = 59; editStep = 1;
            menuState = MENU_EDIT_SET_TIME_MIN;
            while (digitalRead(pushbutton) == LOW); delay(50);
        }
    }
}
else if (menuState == MENU_EDIT_SET_TIME_MIN) {
    drawEditScreen("Set Min", editValue, 0, 59, true);
    if (rotLeft) {
        editValue = (editValue < 59) ? editValue + 1 : 0;
        rotLeft = false;
    }
    if (rotRight) {
        editValue = (editValue > 0) ? editValue - 1 : 59;
        rotRight = false;
    }
    if (digitalRead(pushbutton) == LOW) {
        delay(50);
        if (digitalRead(pushbutton) == LOW) {
            mins = editValue;
            DateTime newTime(2020, 1, 1, hours, mins, 0);
            rtc.adjust(newTime);
            halfcount = savecount;
            menuState = MENU_NONE;
            while (digitalRead(pushbutton) == LOW); delay(50);
        }
    }
}

  if ((shtTemp < setTemp) && (shtTemp > 0)) {
    ssrState = true;
    digitalWrite(pinSSR, HIGH);
    leds[0] = CRGB(100, 0, 0);
    ledValue = 255;
  }
  if (shtTemp > (setTemp + hysteresis)) {
    ssrState = false;
    digitalWrite(pinSSR, LOW);
    leds[0] = CRGB(0, 0, 0);
    ledValue = 0;
  }

  FastLED.show();
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
    ArduinoOTA.handle();
  }

  currentState = digitalRead(pushbutton);
  if (currentState != lastFlickerableState) {
    lastDebounceTime = millis();
    lastFlickerableState = currentState;
  }
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (lastSteadyState == HIGH && currentState == LOW) {
      millisPage = millis();
      buttonstate = true;
    } else if (lastSteadyState == LOW && currentState == HIGH) {
      buttonstate = false;
    }
    lastSteadyState = currentState;
  }

  every(5000){
      resetWatchdog();
  }

  every (10000) {
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    hours = timeinfo.tm_hour;
    mins = timeinfo.tm_min;
    secs = timeinfo.tm_sec;
    shtTemp = sht31.readTemperature();
    shtHum = sht31.readHumidity();
    shtTemp += tempOffset;
    buttoncounter = 0;
    partymode = false;
    if (haschanged) {
      if (WiFi.status() == WL_CONNECTED) {
        Blynk.virtualWrite(V40, setTemp);
        Blynk.virtualWrite(V5, setTemp);
        Blynk.virtualWrite(V41, ledValue);
        terminal.print("> Knob fiddled to ");
        terminal.print(setTemp);
        terminal.print(" at ");
        printLocalTime();
        terminal.flush();
      }
      preferences.begin("my-app", false);
      preferences.putInt("whours", whours);
      preferences.putInt("wmins", wmins);
      preferences.putInt("shours", shours);
      preferences.putInt("smins", smins);
      preferences.putFloat("waketemp", waketemp);
      preferences.putFloat("sleeptemp", sleeptemp);
      preferences.end();
      haschanged = false;
    }
    if (haschanged2) {
      if (WiFi.status() == WL_CONNECTED) {
        terminal.print("> Temp set to ");
        terminal.print(setTemp);
        terminal.print(" at ");
        printLocalTime();
        terminal.flush();
      }
      haschanged2 = false;
    }
  }


  if ((timechanged) && (WiFi.status() != WL_CONNECTED)) {
    hours = chours / 2;
    mins = cmins / 2;
    struct tm t;
    t.tm_year = 70;   // 1970
    t.tm_mon = 0;     // January
    t.tm_mday = 1;    // 1st
    t.tm_hour = hours;
    t.tm_min = mins;
    t.tm_sec = secs;
    t.tm_isdst = 0;

    time_t epoch = mktime(&t);

    struct timeval now = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&now, nullptr);
    timechanged = false;
  }

  if (hours == whours && mins == wmins && page == 1) {
    isAwake = true;
    setTemp = waketemp;
  }

  if (hours == shours && mins == smins && page == 1) {
    isAwake = false;
    setTemp = sleeptemp;
  }
  
  every (30000) {
    shtTemp = sht31.readTemperature();
    shtHum = sht31.readHumidity();
    absHum = (6.112 * pow(2.71828, ((17.67 * shtTemp) / (shtTemp + 243.5))) * shtHum * 2.1674) / (273.15 + shtTemp);
    shtTemp += tempOffset;
    if (WiFi.status() == WL_CONNECTED) {
      if ((shtTemp > 0) && (shtHum > 0)) {
        Blynk.virtualWrite(V7, shtTemp);
        Blynk.virtualWrite(V8, shtHum);
        Blynk.virtualWrite(V4, absHum);
      }
      if (temperatureC > 0) {Blynk.virtualWrite(V2, temperatureC);}
      Blynk.virtualWrite(V5, setTemp);
      Blynk.virtualWrite(V6, ssrState);
      Blynk.virtualWrite(V40, setTemp);
      Blynk.virtualWrite(V41, ledValue);}
  }

}
