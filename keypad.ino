#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const int rowPins[4] = {16, 4, 0, 2};
const int colPins[4] = {19, 18, 5, 17};

char keys[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

// typing state
String output = "";
char lastKey = '\0';
int tapIndex = -1;
unsigned long lastPressTime = 0;
const unsigned long MULTITAP_TIMEOUT = 800;

enum Mode { MODE_ABC, MODE_ABC_CAPS, MODE_NUM };
Mode mode = MODE_ABC;

String groupForKey(char k) {
  switch (k) {
    case '2': return "abc";
    case '3': return "def";
    case '4': return "ghi";
    case '5': return "jkl";
    case '6': return "mno";
    case '7': return "pqrs";
    case '8': return "tuv";
    case '9': return "wxyz";
    case '0': return " ";
    default: return "";
  }
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

void commitLastKey() {
  if (lastKey != '\0' && tapIndex >= 0) {
    String group = groupForKey(lastKey);
    if (group.length() > 0) {
      char ch;
      if (mode == MODE_NUM) {
        ch = lastKey;
      } else {
        ch = group.charAt(tapIndex % group.length());
        if (mode == MODE_ABC_CAPS) ch = toupper(ch);
      }
      output += ch;
    }
  }
  lastKey = '\0';
  tapIndex = -1;
}

void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mode:");
  switch (mode) {
    case MODE_ABC: lcd.print("abc"); break;
    case MODE_ABC_CAPS: lcd.print("ABC"); break;
    case MODE_NUM: lcd.print("123"); break;
  }
  lcd.setCursor(0, 1);
  if (output.length() > 16)
    lcd.print(output.substring(output.length() - 16));
  else
    lcd.print(output);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  updateLCD();

  for (int r = 0; r < 4; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }
  for (int c = 0; c < 4; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }

  Serial.println("Mobile keypad ready!");
}

void loop() {
  char key = scanKeypad();
  unsigned long now = millis();

  if (lastKey != '\0' && (now - lastPressTime) > MULTITAP_TIMEOUT) {
    commitLastKey();
    updateLCD();
  }

  if (key != '\0') {
    if (key == 'A') {
      commitLastKey();
      mode = (Mode)((mode + 1) % 3); // toggle abc -> ABC -> 123
      updateLCD();
      delay(200);
    } 
    else if (key == 'B') {
      commitLastKey();
      if (output.length() > 0) output.remove(output.length() - 1);
      updateLCD();
      delay(200);
    } 
    else if (key == 'C') {
      commitLastKey();
      output += '\n';
      updateLCD();
      delay(200);
    } 
    else if (key == 'D') {
      commitLastKey();
      output = "";
      updateLCD();
      delay(200);
    } 
    else {
      String group = groupForKey(key);
      if (group.length() > 0) {
        if (mode == MODE_NUM) {
          commitLastKey();
          output += key;
        } else {
          if (key == lastKey && (now - lastPressTime) < MULTITAP_TIMEOUT) {
            tapIndex = (tapIndex + 1) % group.length();
          } else {
            commitLastKey();
            lastKey = key;
            tapIndex = 0;
          }
        }
        updateLCD();
      }
    }

    lastPressTime = now;
  }
}
