// 4x4 Keypad pins configuration
const int rowPins[4] = {16, 4, 0, 2};  // R1, R2, R3, R4
const int colPins[4] = {19, 18, 5, 17}; // C1, C2, C3, C4

// Keypad layout based on your mapping
char keys[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;  // 200 ms debounce timeout

char lastKey = '\0';

void setup() {
  Serial.begin(9600);

  // Initialize row pins as OUTPUT and set them HIGH (inactive)
  for (int r = 0; r < 4; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }

  // Initialize column pins as INPUT_PULLUP
  for (int c = 0; c < 4; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }

  Serial.println("Keypad Ready...");
}

char scanKeypad() {
  for (int r = 0; r < 4; r++) {
    digitalWrite(rowPins[r], LOW);  // Activate the current row

    for (int c = 0; c < 4; c++) {
      if (digitalRead(colPins[c]) == LOW) {  // Key pressed
        digitalWrite(rowPins[r], HIGH); // Deactivate row before returning
        return keys[r][c];
      }
    }

    digitalWrite(rowPins[r], HIGH);  // Deactivate current row
  }
  return '\0';  // No key pressed
}

void loop() {
  char key = scanKeypad();

  if (key != '\0' && key != lastKey) {
    if (millis() - lastDebounceTime > debounceDelay) {
      Serial.print("Pressed: ");
      Serial.println(key);
      lastDebounceTime = millis();
      lastKey = key;
    }
  } else if (key == '\0') {
    lastKey = '\0';  // Reset key state when no key is pressed
  }
}