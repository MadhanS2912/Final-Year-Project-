#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// === LCD SETUP ===
LiquidCrystal_I2C lcd(0x27, 16, 2); // I2C address 0x27, 16x2 LCD

// === KEYPAD SETUP ===
const int rowPins[4] = {16, 4, 0, 2};
const int colPins[4] = {19, 18, 5, 17};

char keys[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

String password = "1234";  // Set your password
String entered = "";       // Store entered password

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;
char lastKey = '\0';

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);   // SDA=21, SCL=22 (ESP32 default)
  
  // LCD setup
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter Password:");

  // Keypad pin setup
  for (int r = 0; r < 4; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }
  for (int c = 0; c < 4; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }

  Serial.println("Enter 4-digit password and press #");
}

char scanKeypad() {
  for (int r = 0; r < 4; r++) {
    digitalWrite(rowPins[r], LOW);

    for (int c = 0; c < 4; c++) {
      if (digitalRead(colPins[c]) == LOW) {
        digitalWrite(rowPins[r], HIGH);
        return keys[r][c];
      }
    }

    digitalWrite(rowPins[r], HIGH);
  }
  return '\0';
}

void loop() {
  char key = scanKeypad();

  if (key != '\0' && key != lastKey) {
    if (millis() - lastDebounceTime > debounceDelay) {
      if (key == '*') {
        entered = "";
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Input Cleared");
        delay(800);
        lcd.clear();
        lcd.print("Enter Password:");
      } 
      else if (key == '#') {
        lcd.clear();
        lcd.setCursor(0, 0);
        if (entered == password) {
          lcd.print("Access Granted");
          Serial.println("✅ Access Granted");
        } else {
          lcd.print("Access Denied");
          Serial.println("❌ Access Denied");
        }
        entered = "";
        delay(1500);
        lcd.clear();
        lcd.print("Enter Password:");
      } 
      else if (entered.length() < 4) {
        entered += key;
        Serial.print("*");
        lcd.setCursor(0, 1);
        for (int i = 0; i < entered.length(); i++) {
          lcd.print('*');  // Mask password
        }
      }

      lastDebounceTime = millis();
      lastKey = key;
    }
  } else if (key == '\0') {
    lastKey = '\0';
  }
}
