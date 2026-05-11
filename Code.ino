#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// ── Pin Definitions ──────────────────────────
#define DHT_PIN      2
#define DHT_TYPE     DHT22

#define RELAY_HEATER 4
#define RELAY_HUMID  5
#define RELAY_FAN    6

#define ENC_CLK      3
#define ENC_DT       4
#define ENC_SW       5

#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// ── Limits ───────────────────────────────────
#define TEMP_MAX  60.0
#define TEMP_MIN  15.0
#define HUMID_MAX 90.0
#define HUMID_MIN 20.0
#define EMERGENCY_TEMP 65.0

// ── Timing ───────────────────────────────────
#define DHT_INTERVAL       2000
#define DISPLAY_TIMEOUT    3000
#define RELAY_MIN_INTERVAL 10000
#define SERIAL_INTERVAL    1000

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

bool editingTemp = true;   // true = adjusting TEMP, false = adjusting HUMID

unsigned long lastDHTRead     = 0;
unsigned long lastEncoderMove = 0;
unsigned long lastSerial      = 0;
unsigned long heaterLastSwitch = 0;
unsigned long humidLastSwitch  = 0;

// ── Encoder state ────────────────────────────
int  lastCLK;
bool lastSWState = HIGH;
unsigned long lastSWPress = 0;

// ── Encoder: read rotation ───────────────────
void handleEncoder() {
  int currentCLK = digitalRead(ENC_CLK);

  if (currentCLK != lastCLK) {
    bool turnedRight = (digitalRead(ENC_DT) != currentCLK);

    if (editingTemp) {
      if (turnedRight && TEMP_SETPOINT < TEMP_MAX) TEMP_SETPOINT++;
      else if (!turnedRight && TEMP_SETPOINT > TEMP_MIN) TEMP_SETPOINT--;
      showTempSetpointLCD();
    } else {
      if (turnedRight && HUMID_SETPOINT < HUMID_MAX) HUMID_SETPOINT++;
      else if (!turnedRight && HUMID_SETPOINT > HUMID_MIN) HUMID_SETPOINT--;
      showHumidSetpointLCD();
    }

    lastEncoderMove = millis();
    lastCLK = currentCLK;
  }
}

// ── Encoder: read button click ───────────────
void handleButton() {
  bool currentSW = digitalRead(ENC_SW);

  if (currentSW == LOW && lastSWState == HIGH &&
      millis() - lastSWPress > 300) {
    editingTemp = !editingTemp;   // toggle mode
    lastSWPress = millis();
    lastEncoderMove = millis();

    if (editingTemp) showTempSetpointLCD();
    else             showHumidSetpointLCD();

    Serial.println(editingTemp ? "MODE: TEMP" : "MODE: HUMID");
  }

  lastSWState = currentSW;
}

// ── LCD screens ──────────────────────────────
void showMainLCD() {
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(temperature, 1);
  lcd.print(" H:");
  lcd.print(humidity, 1);
  lcd.print("   ");

  lcd.setCursor(0, 1);
  lcd.print("SP:");
  lcd.print(editingTemp ? TEMP_SETPOINT : HUMID_SETPOINT, 0);
  lcd.print(editingTemp ? "C " : "% ");
  lcd.print(digitalRead(RELAY_HEATER) == LOW ? "H:ON " : "H:OFF");
}

void showTempSetpointLCD() {
  lcd.clear();
  lcd.print(">> TEMP mode");
  lcd.setCursor(0, 1);
  lcd.print("Setpoint: ");
  lcd.print(TEMP_SETPOINT, 0);
  lcd.print("C");
}

void showHumidSetpointLCD() {
  lcd.clear();
  lcd.print(">> HUMID mode");
  lcd.setCursor(0, 1);
  lcd.print("Setpoint: ");
  lcd.print(HUMID_SETPOINT, 0);
  lcd.print("%");
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

  pinMode(ENC_CLK, INPUT);
  pinMode(ENC_DT,  INPUT);
  pinMode(ENC_SW,  INPUT_PULLUP);
  lastCLK = digitalRead(ENC_CLK);

  lcd.print("Chamber Init...");
  delay(1500);
  lcd.clear();
  showTempSetpointLCD();
}

// ── Main Loop ────────────────────────────────
void loop() {

  // ── Encoder ────────────────────────────────
  handleEncoder();
  handleButton();

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
    digitalWrite(RELAY_FAN,    RELAY_ON);
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

  // ── LCD main screen after timeout ──────────
  if (millis() - lastEncoderMove > DISPLAY_TIMEOUT) {
    showMainLCD();
  }

  // ── Serial ─────────────────────────────────
  if (millis() - lastSerial >= SERIAL_INTERVAL) {
    lastSerial = millis();
    Serial.print("MODE:");
    Serial.print(editingTemp ? "TEMP" : "HUMID");
    Serial.print(" T:");
    Serial.print(temperature);
    Serial.print(" H:");
    Serial.print(humidity);
    Serial.print(" Heater:");
    Serial.print(digitalRead(RELAY_HEATER) == LOW ? "ON" : "OFF");
    Serial.print(" Humid:");
    Serial.println(digitalRead(RELAY_HUMID) == LOW ? "ON" : "OFF");
  }
}