#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// ── Pin Definitions ──────────────────────────
#define DHT_PIN        2
#define DHT_TYPE       DHT22

#define RELAY_HEATER   4
#define RELAY_HUMID    5
#define RELAY_FAN      6

#define BTN_TEMP_UP    7
#define BTN_TEMP_DOWN  8
#define BTN_HUMID_UP   9
#define BTN_HUMID_DOWN 10

#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// ── Limits ───────────────────────────────────
#define TEMP_MAX  60.0
#define TEMP_MIN  15.0
#define HUMID_MAX 90.0
#define HUMID_MIN 20.0

#define EMERGENCY_TEMP 65.0

// ── Timing ───────────────────────────────────
#define DHT_INTERVAL      2000
#define DISPLAY_TIMEOUT   3000
#define DEBOUNCE_DELAY    150
#define RELAY_MIN_INTERVAL 10000
#define SERIAL_INTERVAL   1000   // print once per second

// ── Objects ──────────────────────────────────
DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ── Setpoints ────────────────────────────────
float TEMP_SETPOINT  = 28.0;
float HUMID_SETPOINT = 60.0;
float TEMP_DEADBAND  = 1.0;
float HUMID_DEADBAND = 2.0;

// ── State ────────────────────────────────────
float temperature = 0;
float humidity    = 0;

unsigned long lastDHTRead      = 0;
unsigned long lastButtonPress  = 0;
unsigned long lastSerial       = 0;   // ← added

// Relay timing
unsigned long heaterLastSwitch = 0;
unsigned long humidLastSwitch  = 0;

// Button states
bool lastBtnState[4]          = {HIGH, HIGH, HIGH, HIGH};
unsigned long lastDebounceTime[4] = {0, 0, 0, 0};

// ── Helper: Button handler ───────────────────
bool buttonPressed(int pin, int index) {
  bool reading = digitalRead(pin);

  if (reading != lastBtnState[index]) {
    lastDebounceTime[index] = millis();
  }

  if ((millis() - lastDebounceTime[index]) > DEBOUNCE_DELAY) {
    if (reading == LOW && lastBtnState[index] == HIGH) {
      lastBtnState[index] = reading;
      return true;
    }
  }

  lastBtnState[index] = reading;
  return false;
}

// ── LCD ──────────────────────────────────────
void showMainLCD() {
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(temperature, 1);
  lcd.print(" H:");
  lcd.print(humidity, 1);
  lcd.print("   ");

  lcd.setCursor(0, 1);
  lcd.print("SP:");
  lcd.print(TEMP_SETPOINT, 0);
  lcd.print(" ");
  lcd.print(digitalRead(RELAY_HEATER) == LOW ? "H:ON " : "H:OFF");
}

void showTempSetpointLCD() {
  lcd.clear();
  lcd.print("Temp SP:");
  lcd.setCursor(0, 1);
  lcd.print(TEMP_SETPOINT, 0);
}

void showHumidSetpointLCD() {
  lcd.clear();
  lcd.print("Humid SP:");
  lcd.setCursor(0, 1);
  lcd.print(HUMID_SETPOINT, 0);
}

void showEmergency() {
  lcd.clear();
  lcd.print("!!! OVER TEMP !!!");
  lcd.setCursor(0, 1);
  lcd.print("SYSTEM STOPPED");
}

// ── Setup ────────────────────────────────────
void setup() {
  Serial.begin(9600);
  dht.begin();

  lcd.init();
  lcd.backlight();

  pinMode(RELAY_HEATER, OUTPUT);
  pinMode(RELAY_HUMID,  OUTPUT);
  pinMode(RELAY_FAN,    OUTPUT);

  digitalWrite(RELAY_HEATER, RELAY_OFF);
  digitalWrite(RELAY_HUMID,  RELAY_OFF);
  digitalWrite(RELAY_FAN,    RELAY_OFF);

  pinMode(BTN_TEMP_UP,    INPUT_PULLUP);
  pinMode(BTN_TEMP_DOWN,  INPUT_PULLUP);
  pinMode(BTN_HUMID_UP,   INPUT_PULLUP);
  pinMode(BTN_HUMID_DOWN, INPUT_PULLUP);

  lcd.print("Chamber Init...");
  delay(1500);
  lcd.clear();
}

// ── Main Loop ────────────────────────────────
void loop() {

  // ── Buttons ────────────────────────────────
  if (buttonPressed(BTN_TEMP_UP, 0)) {
    if (TEMP_SETPOINT < TEMP_MAX) TEMP_SETPOINT++;
    showTempSetpointLCD();
    lastButtonPress = millis();
  }

  if (buttonPressed(BTN_TEMP_DOWN, 1)) {
    if (TEMP_SETPOINT > TEMP_MIN) TEMP_SETPOINT--;
    showTempSetpointLCD();
    lastButtonPress = millis();
  }

  if (buttonPressed(BTN_HUMID_UP, 2)) {
    if (HUMID_SETPOINT < HUMID_MAX) HUMID_SETPOINT++;
    showHumidSetpointLCD();
    lastButtonPress = millis();
  }

  if (buttonPressed(BTN_HUMID_DOWN, 3)) {
    if (HUMID_SETPOINT > HUMID_MIN) HUMID_SETPOINT--;
    showHumidSetpointLCD();
    lastButtonPress = millis();
  }

  // ── Sensor Read ────────────────────────────
  if (millis() - lastDHTRead >= DHT_INTERVAL) {
    lastDHTRead = millis();

    humidity    = dht.readHumidity();
    temperature = dht.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
      digitalWrite(RELAY_HEATER, RELAY_OFF);
      digitalWrite(RELAY_HUMID,  RELAY_OFF);
      digitalWrite(RELAY_FAN,    RELAY_OFF);
      lcd.clear();
      lcd.print("SENSOR ERROR");
      return;
    }
  }

  // ── Emergency Stop ─────────────────────────
  if (temperature >= EMERGENCY_TEMP) {
    digitalWrite(RELAY_HEATER, RELAY_OFF);
    digitalWrite(RELAY_HUMID,  RELAY_OFF);
    digitalWrite(RELAY_FAN,    RELAY_ON);   // keep fan ON to cool down
    showEmergency();
    return;
  }

  // ── Heater Control ─────────────────────────
  if (millis() - heaterLastSwitch > RELAY_MIN_INTERVAL) {
    if (temperature < TEMP_SETPOINT - TEMP_DEADBAND) {
      digitalWrite(RELAY_HEATER, RELAY_ON);
      heaterLastSwitch = millis();
    } else if (temperature > TEMP_SETPOINT + TEMP_DEADBAND) {
      digitalWrite(RELAY_HEATER, RELAY_OFF);
      heaterLastSwitch = millis();
    }
  }

  // ── Humidifier Control ─────────────────────
  if (millis() - humidLastSwitch > RELAY_MIN_INTERVAL) {
    if (humidity < HUMID_SETPOINT - HUMID_DEADBAND) {
      digitalWrite(RELAY_HUMID, RELAY_ON);
      humidLastSwitch = millis();
    } else if (humidity > HUMID_SETPOINT + HUMID_DEADBAND) {
      digitalWrite(RELAY_HUMID, RELAY_OFF);
      humidLastSwitch = millis();
    }
  }

  // ── Fan always ON ───────────────────────────
  digitalWrite(RELAY_FAN, RELAY_ON);

  // ── LCD ────────────────────────────────────
  if (millis() - lastButtonPress > DISPLAY_TIMEOUT) {
    showMainLCD();
  }

  // ── Serial — once per second ────────────────
  if (millis() - lastSerial >= SERIAL_INTERVAL) {
    lastSerial = millis();
    Serial.print("T:");
    Serial.print(temperature);
    Serial.print(" H:");
    Serial.print(humidity);
    Serial.print(" Heater:");
    Serial.print(digitalRead(RELAY_HEATER) == LOW ? "ON" : "OFF");
    Serial.print(" Humid:");
    Serial.println(digitalRead(RELAY_HUMID) == LOW ? "ON" : "OFF");
  }
}
