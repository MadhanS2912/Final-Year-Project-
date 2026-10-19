// 4x4 Keypad pins configuration
const int r1 = 16;
const int r2 = 4;
const int r3 = 0;
const int r4 = 2;

const int c1 = 19;
const int c2 = 18;
const int c3 = 5;
const int c4 = 17;

void setup() {
  // Initialize row pins as OUTPUT
  pinMode(r1, OUTPUT);
  pinMode(r2, OUTPUT);
  pinMode(r3, OUTPUT);
  pinMode(r4, OUTPUT);

  // Initialize column pins as INPUT_PULLUP
  pinMode(c1, INPUT_PULLUP);
  pinMode(c2, INPUT_PULLUP);
  pinMode(c3, INPUT_PULLUP);
  pinMode(c4, INPUT_PULLUP);

  Serial.begin(9600); // Open Serial Monitor at 9600 baud
}

char key_pad() {
  const int rows[] = { r1, r2, r3, r4 };
  const char keys[4][4] = {
    { '1', '2', '3', 'A' },
    { '4', '5', '6', 'B' },
    { '7', '8', '9', 'C' },
    { '*', '0', '#', 'D' }
  };

  for (int r = 0; r < 4; r++) {
    // Set selected row to LOW
    pinMode(rows[r], OUTPUT);
    digitalWrite(rows[r], LOW);

    // Set other rows as INPUT to avoid interference
    for (int rr = 0; rr < 4; rr++) {
      if (rr != r) {
        pinMode(rows[rr], INPUT);
      }
    }

    // Scan columns
    if (!digitalRead(c1)) return keys[r][0];
    if (!digitalRead(c2)) return keys[r][1];
    if (!digitalRead(c3)) return keys[r][2];
    if (!digitalRead(c4)) return keys[r][3];

    // Reset row to avoid ghosting
    pinMode(rows[r], INPUT);
  }

  return '\0'; // No key pressed
}

void loop() {
  char key = key_pad();
  if (key != '\0') {
    Serial.print("Pressed: ");
    Serial.println(key);
    delay(300);  // Debounce delay
    while (key_pad() != '\0');  // Wait until key is released
  }
}