// 4x4 Keypad pins configuration
const int rowPins[4] = {16, 4, 0, 2};
const int colPins[4] = {19, 18, 5, 17};

// Keypad layout
char keys[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

String password = "1234";  // Set your password here
String entered = "";       // For storing entered password
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;
char lastKey = '\0';

void setup() {
  Serial.begin(9600);

  // Initialize row pins as OUTPUT and set HIGH
  for (int r = 0; r < 4; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }

  // Initialize column pins as INPUT_PULLUP
  for (int c = 0; c < 4; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }

  Serial.println("Enter 4-digit password and press #:");
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
        Serial.println("Input cleared");
      } else if (key == '#') {
        if (entered == password) {
          Serial.println("Access Granted ✅");
        } else {
          Serial.println("Access Denied ❌");
        }
        entered = "";  // Reset after checking
      } else if (entered.length() < 4) {
        entered += key;
        Serial.print("*");  // Mask input for privacy
      }
      lastDebounceTime = millis();
      lastKey = key;
    }
  } else if (key == '\0') {
    lastKey = '\0';
  }
}