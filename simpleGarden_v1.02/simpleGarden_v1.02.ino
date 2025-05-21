#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>

#define TEST_BUTTON_PIN 4
#define CANCEL_BUTTON_PIN 5

LiquidCrystal_I2C lcd(0x27, 16, 2);  // Try 0x3F if 0x27 doesn’t work
RTC_DS3231 rtc;

// Relay pins
const int zonePins[7] = {A1, A2, A3, D2, D3, A6, A7};

enum Mode { IDLE, NORMAL, TEST };
Mode currentMode = IDLE;

unsigned long zoneStartTime = 0;
int currentZone = 0;
unsigned long zoneDuration = 0;

bool testButtonPrevState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

void setup() {
  delay(2000);
  Serial.begin(115200);
  Wire.begin(8, 9);  // Initialize I2C first
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");

  int retries = 0;
  bool rtcFound = false;
  for (uint8_t address = 1; address < 127; address++) {
   Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
    if (address == 0x68) {
      rtcFound = true;
      break;
      }
    }
  }
if (!rtcFound) {
  printStatus("RTC not found on I2C bus.");
  while (1);
}


  while (!rtc.begin(&Wire)) {
    printStatus("Couldn't find RTC. Retrying...");
    retries++;
    delay(500);

  // Reinitialize I2C in case it crashed or froze
    Wire.end();
    delay(100);
    Wire.begin(8, 9);

  if (retries > 5) {
    printStatus("RTC failed after 5 tries.");
    while (1);
  }
}


  if (rtc.lostPower()) {
    printStatus("RTC lost power, setting time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  for (int i = 0; i < 7; i++) {
    pinMode(zonePins[i], OUTPUT);
    digitalWrite(zonePins[i], LOW);
  }

  pinMode(TEST_BUTTON_PIN, INPUT_PULLUP);
  pinMode(CANCEL_BUTTON_PIN, INPUT_PULLUP);

  printStatus("Controller ready.");
}

void loop() {
  static unsigned long lastTimeUpdate = 0;

  if (millis() - lastTimeUpdate > 1000) {
    lastTimeUpdate = millis();
    DateTime now = rtc.now();
    char timeBuffer[6];
    sprintf(timeBuffer, "%02d:%02d", now.hour(), now.minute());

    lcd.setCursor(0, 0);
    lcd.print("Time: ");
    lcd.print(timeBuffer);
    lcd.print("     ");
  }

  DateTime now = rtc.now();
  checkCancelButton();
  checkTestButton();

  if (currentMode == IDLE && ((now.hour() == 6 && now.minute() == 0 && now.second() == 0) ||
                              (now.hour() == 18 && now.minute() == 0 && now.second() == 0))) {
    startWatering(NORMAL);
  }

  handleWatering();
}

void printStatus(const String &message) {
  Serial.println(message);
  lcd.setCursor(0, 1);
  lcd.print(message);
  int remaining = 16 - message.length();
  for (int i = 0; i < remaining; i++) {
    lcd.print(" ");
  }
}

void startWatering(Mode mode) {
  currentMode = mode;
  currentZone = 0;
  zoneStartTime = millis();
  zoneDuration = (mode == NORMAL) ? 5UL * 60 * 1000 : 30UL * 1000;
  printStatus((mode == NORMAL ? "Normal: Zone " : "Test: Zone ") + String(currentZone + 1));
  digitalWrite(zonePins[currentZone], HIGH);
}

void handleWatering() {
  if (currentMode == IDLE) return;

  if (millis() - zoneStartTime >= zoneDuration) {
    digitalWrite(zonePins[currentZone], LOW);
    printStatus("Finished Zone " + String(currentZone + 1));

    currentZone++;
    if (currentZone >= 7) {
      currentMode = IDLE;
      printStatus("All zones completed.");
    } else {
      zoneStartTime = millis();
      digitalWrite(zonePins[currentZone], HIGH);
      printStatus("Zone " + String(currentZone + 1) + " ON");
    }
  }
}

void checkTestButton() {
  static unsigned long lastCheck = 0;
  static bool lastState = HIGH;
  bool currentState = digitalRead(TEST_BUTTON_PIN);

  if (millis() - lastCheck > debounceDelay) {
    if (lastState == HIGH && currentState == LOW && currentMode == IDLE) {
      printStatus("Test button pressed.");
      startWatering(TEST);
    }
    lastState = currentState;
    lastCheck = millis();
  }
}

void checkCancelButton() {
  if (digitalRead(CANCEL_BUTTON_PIN) == LOW && currentMode != IDLE) {
    printStatus("Cancel pressed.");
    printStatus(currentMode == NORMAL ? "Stopping normal." : "Stopping test.");
    stopAllWatering();
  }
}

void stopAllWatering() {
  for (int i = 0; i < 7; i++) {
    digitalWrite(zonePins[i], LOW);
  }
  currentMode = IDLE;
  currentZone = 0;
}
