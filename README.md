# Arduino-Control-Team-3
Testing chmaber able to simulate temperature and control humidity

# TC Chamber — Arduino Control System
**Project:** Temperature & Humidity Controlled Chamber  
**My Role:** Electronics & Software (Daniel)  
**Partner:** Owen  
**Board:** Arduino Uno  
**Language:** C++ (Arduino IDE)

---

## What I Built

 wrote the Arduino code that controls the temperature and humidity inside our 12×12×12 inch chamber. The system reads sensor data every 2 seconds, compares it to setpoints I let the user define, and automatically switches a heater, humidifier, and fan ON or OFF based on those readings. I also added 4 push buttons so the user can adjust setpoints live without touching the code, and everything shows up on a 16×2 LCD screen in real time.

 tested the full simulation in Wokwi before moving to real hardware.

---

## Hardware I Used

| Component | Model | What was used  
|Microcontroller | Arduino Uno | Main controller |
| Temp/Humidity Sensor | DHT22 (AM2302) | Reads chamber conditions |
| Relay x3 | SRD-12VDC-SL-C | Switches heater, humidifier, fan |
| LCD Display | 16×2 I2C (0x27) | Shows live readings and setpoints |
| Push Buttons x4 | Tactile switches | Lets user adjust setpoints |
| Fan | I dont know the model|

---

## How I Wired Everything

| Arduino Pin | What it connects to |
|---|---|
| D2 | DHT22 data pin |
| D4 | Relay 1 — Heater |
| D5 | Relay 2 — Humidifier |
| D6 | Relay 3 — Fan |
| D7 | Button: Temp + |
| D8 | Button: Temp - |
| D9 | Button: Humidity + |
| D10 | Button: Humidity - |
| A4 | LCD SDA (I2C) |
| A5 | LCD SCL (I2C) |

---

## Libraries I Installed

I installed these through **Sketch → Include Library → Manage Libraries** in Arduino IDE:

- `DHT sensor library` — by Adafruit — reads the DHT22 sensor
- `Adafruit Unified Sensor` — required dependency for the DHT library
- `LiquidCrystal I2C` — by Frank de Brabander — drives the LCD over I2C

For Wokwi simulation I added a `libraries.txt` file with the same three libraries so Wokwi downloads them automatically on run.

---

## The Code —

### Step 1 — defined the relay behavior first

```cpp
#define RELAY_ON  LOW
#define RELAY_OFF HIGH
```

The relays I'm using (SRD-12VDC-SL-C) are **active LOW** — they turn ON when the signal is LOW, not HIGH. This is the opposite of what most people expect. Instead of writing LOW and HIGH everywhere in the code which would be confusing, I created two macros RELAY_ON and RELAY_OFF so the code reads naturally. If I ever change relay models, I only update 2 lines.

I also set safety limits for the setpoints so the user can't accidentally set a dangerous value:
- Temperature: 15°C min, 60°C max
- Humidity: 20% min, 90% max
- Emergency cutoff at 65°C

---

### Step 2 — used millis() for all timing instead of delay()

```cpp
#define DHT_INTERVAL       2000   // read sensor every 2 seconds
#define DISPLAY_TIMEOUT    3000   // LCD returns to main screen after 3 seconds
#define DEBOUNCE_DELAY     150    // button debounce window
#define RELAY_MIN_INTERVAL 10000  // relay can't switch more than once per 10s
#define SERIAL_INTERVAL    1000   // serial monitor prints once per second
```

Early on I was using delay() to wait between sensor reads which caused the whole program to freeze — buttons wouldn't respond while it was waiting. I switched everything to millis() which works like a stopwatch. The loop keeps running constantly and each feature just checks if enough time has passed before doing its thing.

The RELAY_MIN_INTERVAL of 10 seconds was something I added specifically to protect the relays. Since they're mechanical switches, switching them too fast wears them out. This timer prevents any relay from switching more than once every 10 seconds.

---

### Step 3 —  wrote a proper debounce function for the buttons

```cpp
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
```

Without debounce, pressing a button once could register as 10 to 50 presses because of the mechanical bounce inside the switch. I wrote this function to track each button's previous state and only register a press when the signal has been stable for 150ms. I used INPUT_PULLUP on all 4 buttons so no external resistors are needed — the Arduino handles the pull-up internally. The buttons read HIGH when not pressed and LOW when pressed.

---

### Step 4 — I set up the sensor reading with error handling

```cpp
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
```

The DHT22 needs at least 2 seconds between reads. I check every loop if 2 seconds have passed using millis() and only then read the sensor.

The isnan() check is important — when the DHT22 fails to get a reading it returns NaN which stands for Not a Number. If I tried to use that value to control the heater, it could turn ON or stay ON with completely garbage data. So I check for it and immediately shut everything OFF if it happens, which is the safest state.

---

### Step 5 — implemented deadband control for the heater and humidifier

```cpp
if (temperature < TEMP_SETPOINT - TEMP_DEADBAND) {
  digitalWrite(RELAY_HEATER, RELAY_ON);
} else if (temperature > TEMP_SETPOINT + TEMP_DEADBAND) {
  digitalWrite(RELAY_HEATER, RELAY_OFF);
}
```

A deadband is a buffer zone around the setpoint where the relay does nothing. Without it, if the temperature sat exactly at the setpoint, the relay would switch ON and OFF thousands of times per second destroying the hardware.

Example with setpoint = 28°C and deadband = 1°C:
```
Below 27°C  → Heater ON   (too cold, needs heat)
27°C – 29°C → Do nothing  (close enough, leave it alone)
Above 29°C  → Heater OFF  (warm enough, stop heating)
```

The same logic applies to the humidifier. The chamber naturally oscillates within that ±1°C range which is normal and exactly what we want for a controlled environment.

---

### Step 6 — added an emergency stop

```cpp
if (temperature >= EMERGENCY_TEMP) {
  digitalWrite(RELAY_HEATER, RELAY_OFF);
  digitalWrite(RELAY_HUMID,  RELAY_OFF);
  digitalWrite(RELAY_FAN,    RELAY_ON);
  showEmergency();
  return;
}
```

If the chamber somehow reaches 65°C the system shuts everything off immediately and keeps the fan running to bring the temperature down. The LCD shows !!! OVER TEMP !!!. The return exits the loop early so none of the normal control logic runs during emergency mode.

---

### Step 7 —  set up the LCD with two display modes

The LCD switches between two screens automatically:

**Main screen** — shows live readings and heater status. Returns automatically 3 seconds after the last button press.
```
Row 0 → T:24.0 H:40.0
Row 1 → SP:28  H:ON
```

**Setpoint screen** — appears when a button is pressed so the user can see what value they're changing.
```
Row 0 → Temp SP:
Row 1 → >> 29
```

The 3-second auto-return means the display never gets stuck on the setpoint screen — it always goes back to live readings on its own.

---

### Step 8 — rate-limited the serial output

```cpp
if (millis() - lastSerial >= SERIAL_INTERVAL) {
  lastSerial = millis();
  Serial.print("T:"); Serial.print(temperature);
  Serial.print(" H:"); Serial.print(humidity);
  Serial.print(" Heater:"); Serial.print(...);
  Serial.print(" Humid:"); Serial.println(...);
}
```

Without this the serial monitor was printing hundreds of lines per second making it impossible to read. I added a millis() timer so it only prints once per second. I use this for debugging — confirming the sensor is reading correctly and the relays are responding the right way.

---

## How I Tested It — Wokwi Simulation

Before touching any real hardware I simulated the full system in **Wokwi**. I used:

- DHT22 component with live sliders to simulate temperature and humidity changes
- 3 LEDs (red = heater, blue = humidifier, green = fan) to visually confirm relay behavior
- 4 push buttons wired to the same pins as the real hardware
- 16×2 I2C LCD showing the same output as the real display
- A breadboard as a shared ground rail since the Arduino only has 2 GND pins

What I confirmed during simulation:
- Heater LED turns ON when temperature drops below setpoint minus deadband
- Heater LED turns OFF when temperature rises above setpoint plus deadband
- Humidifier follows the same logic for humidity
- Fan LED stays ON at all times
- Buttons correctly increment and decrement setpoints with limit warnings
- LCD switches between main screen and setpoint screen correctly
- Serial monitor prints clean readable output once per second
Concerns
Buttons needed to be pushed repeatedly to function [Added a bouncing delays
Simulation was working bakcward at some point, Will debug it with Owen at debugging stage


---

## What's Next

- Wire the real DHT22, relays, LCD and buttons on the physical chamber
- Connect real 12V AC loads through the relay contacts
- Run a full closed-loop test inside the chamber and verify readings match expected behavior
