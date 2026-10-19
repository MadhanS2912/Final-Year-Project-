#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_Fingerprint.h>
#include <HardwareSerial.h>
#include <EEPROM.h>

// === FINGERPRINT SETUP ===
#define EEPROM_SIZE 512
#define MAX_FINGERPRINTS 127
#define NAME_LENGTH 20
#define EEPROM_START 0

HardwareSerial fingerprintSerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerprintSerial);

// === WiFi CREDENTIALS ===
const char* ssid = "madhan_phone";
const char* password_wifi = "Madhan2912";

// === API CONFIGURATION ===
const char* API_BASE_URL = "https://agnicom-server.onrender.com";
String MY_USER_ID = "ZUFAzDN3WiVv";  // Will be set from fingerprint
String MY_PIN = "1234";      // Will be entered via keypad

// === LCD SETUP ===
LiquidCrystal_I2C lcd(0x27, 16, 2);

// === KEYPAD SETUP ===
const int rowPins[4] = {16, 4, 0, 2};
const int colPins[4] = {19, 18, 5, 17};

char keys[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

// === NOKIA MULTI-TAP MAPPING ===
const char* keyMap[10] = {
  " 0",    // 0
  "1.,?!", // 1
  "2ABC",  // 2
  "3DEF",  // 3
  "4GHI",  // 4
  "5JKL",  // 5
  "6MNO",  // 6
  "7PQRS", // 7
  "8TUV",  // 8
  "9WXYZ"  // 9
};

// === STATE MANAGEMENT ===
enum State {
  STATE_FINGERPRINT_SCAN,
  STATE_FINGERPRINT_ENROLL,
  STATE_ENTER_NAME,
  STATE_ENTER_PIN,
  STATE_LOGIN,
  STATE_USER_LIST,
  STATE_VIEW_SEND_OPTION,
  STATE_VIEW_MESSAGE,
  STATE_COMPOSE_MESSAGE,
  STATE_DELETE_MENU,
  STATE_DELETE_CONFIRM,
  STATE_DELETE_ALL_CONFIRM
};

State currentState = STATE_FINGERPRINT_SCAN;

// === USER DATA ===
struct User {
  String user_id;
  String username;
};

User users[10];
int userCount = 0;
int selectedUserIndex = 0;
String selectedRecipientId = "";

// === INPUT HANDLING ===
String entered = "";
String nameBuffer = "";
String composedMessage = "";

// === KEYPAD DEBOUNCING ===
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;
char lastKey = '\0';

// === MESSAGE COMPOSITION ===
unsigned long lastPressTime = 0;
char lastMultiTapKey = '\0';
int pressCount = 0;
const unsigned long multiTapTimeout = 1000;

// === MESSAGE DATA ===
String lastMessageDecrypted = "";

// === FINGERPRINT ENROLLMENT ===
int enrollingID = -1;
int enrollStep = 0;

// === FINGERPRINT DELETION ===
int deleteID = -1;
String deleteUserName = "";

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);

  // LCD setup
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AgniCom Chat");
  lcd.setCursor(0, 1);
  lcd.print("Fingerprint Auth");
  delay(2000);

  // Fingerprint sensor setup
  fingerprintSerial.begin(57600, SERIAL_8N1, 26, 27);
  delay(100);
  
  lcd.clear();
  lcd.print("Init Sensor...");
  
  if (finger.verifyPassword()) {
    lcd.clear();
    lcd.print("Sensor OK!");
    Serial.println("✅ Fingerprint sensor detected!");
    delay(1000);
  } else {
    lcd.clear();
    lcd.print("Sensor Error!");
    Serial.println("❌ Fingerprint sensor not found!");
    while (1) { delay(1); }
  }

  // Keypad pin setup
  for (int r = 0; r < 4; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }
  for (int c = 0; c < 4; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }

  // Connect to WiFi
  connectWiFi();

  lcd.clear();
  lcd.print("Place Finger");
  lcd.setCursor(0, 1);
  lcd.print("A:New B:Delete");
  currentState = STATE_FINGERPRINT_SCAN;
  
  Serial.println("\n=== AgniCom Fingerprint System ===");
  Serial.println("Place finger to login");
  Serial.println("Press 'A' button to enroll new fingerprint");
  Serial.println("Press 'B' button to delete fingerprint");
}

void connectWiFi() {
  lcd.clear();
  lcd.print("Connecting WiFi");
  Serial.println("Connecting to WiFi...");

  WiFi.begin(ssid, password_wifi);
  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    lcd.setCursor(0, 1);
    lcd.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    lcd.clear();
    lcd.print("WiFi Connected!");
    Serial.println("\nWiFi Connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    delay(1500);
  } else {
    lcd.clear();
    lcd.print("WiFi Failed!");
    Serial.println("WiFi Connection Failed!");
    while (1);
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

// === FINGERPRINT FUNCTIONS ===

int getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    return finger.fingerID;
  }
  return -1;
}

void saveName(uint8_t id, String name) {
  int address = EEPROM_START + (id * (NAME_LENGTH + 2));
  
  EEPROM.write(address, id);
  EEPROM.write(address + 1, 1); // isUsed flag
  
  for (int i = 0; i < NAME_LENGTH; i++) {
    if (i < name.length()) {
      EEPROM.write(address + 2 + i, name[i]);
    } else {
      EEPROM.write(address + 2 + i, 0);
    }
  }
  
  EEPROM.commit();
}

String getName(uint8_t id) {
  int address = EEPROM_START + (id * (NAME_LENGTH + 2));
  
  uint8_t storedId = EEPROM.read(address);
  uint8_t isUsed = EEPROM.read(address + 1);
  
  if (storedId != id || isUsed != 1) {
    return "";
  }
  
  String name = "";
  for (int i = 0; i < NAME_LENGTH; i++) {
    char c = EEPROM.read(address + 2 + i);
    if (c == 0) break;
    name += c;
  }
  
  return name;
}

int getNextAvailableID() {
  for (int i = 1; i <= MAX_FINGERPRINTS; i++) {
    if (finger.loadModel(i) != FINGERPRINT_OK) {
      return i;
    }
  }
  return -1;
}

void startEnrollment() {
  enrollingID = getNextAvailableID();
  if (enrollingID == -1) {
    lcd.clear();
    lcd.print("Database Full!");
    Serial.println("❌ Cannot enroll more fingerprints");
    delay(2000);
    currentState = STATE_FINGERPRINT_SCAN;
    lcd.clear();
    lcd.print("Place Finger");
    lcd.setCursor(0, 1);
    lcd.print("A:New B:Delete");
    return;
  }
  
  enrollStep = 0;
  nameBuffer = "";
  lcd.clear();
  lcd.print("Enter User ID:");
  lcd.setCursor(0, 1);
  lcd.print("(Serial Monitor)");
  currentState = STATE_ENTER_NAME;
  
  Serial.println("\n=== Enrolling New Fingerprint ===");
  Serial.print("Assigning ID #");
  Serial.println(enrollingID);
  Serial.println("Enter User ID (user_id from server):");
}

// === FINGERPRINT DELETION FUNCTIONS ===

void deleteName(uint8_t id) {
  int address = EEPROM_START + (id * (NAME_LENGTH + 2));
  
  EEPROM.write(address, 0);
  EEPROM.write(address + 1, 0); // Clear isUsed flag
  
  for (int i = 0; i < NAME_LENGTH; i++) {
    EEPROM.write(address + 2 + i, 0);
  }
  
  EEPROM.commit();
}

void startDeletion() {
  lcd.clear();
  lcd.print("Delete Options:");
  lcd.setCursor(0, 1);
  lcd.print("0:One 1:All *:Back");
  currentState = STATE_DELETE_MENU;
  
  Serial.println("\n=== Delete Fingerprint ===");
  Serial.println("Press '0' to delete single fingerprint");
  Serial.println("Press '1' to delete ALL fingerprints");
  Serial.println("Press '*' to go back");
}

void deleteFingerprint(int id) {
  String userName = getName(id);
  
  lcd.clear();
  lcd.print("Delete ID:" + String(id));
  lcd.setCursor(0, 1);
  if (userName != "") {
    lcd.print(userName.substring(0, 16));
  } else {
    lcd.print("Unknown");
  }
  
  deleteID = id;
  deleteUserName = userName;
  
  delay(2000);
  
  lcd.clear();
  lcd.print("Press # Confirm");
  lcd.setCursor(0, 1);
  lcd.print("Press * Cancel");
  
  currentState = STATE_DELETE_CONFIRM;
  
  Serial.println("Found fingerprint ID: " + String(id));
  if (userName != "") {
    Serial.println("User ID: " + userName);
  }
  Serial.println("Press '#' to confirm deletion");
  Serial.println("Press '*' to cancel");
}

void confirmDeletion() {
  uint8_t p = finger.deleteModel(deleteID);
  
  if (p == FINGERPRINT_OK) {
    deleteName(deleteID);
    
    lcd.clear();
    lcd.print("Deleted!");
    lcd.setCursor(0, 1);
    lcd.print("ID:" + String(deleteID));
    
    Serial.println("✅ Fingerprint deleted successfully!");
    Serial.println("Deleted ID: " + String(deleteID));
    if (deleteUserName != "") {
      Serial.println("User ID: " + deleteUserName);
    }
  } else {
    lcd.clear();
    lcd.print("Delete Failed!");
    Serial.println("❌ Failed to delete fingerprint");
  }
  
  delay(2000);
  
  deleteID = -1;
  deleteUserName = "";
  
  currentState = STATE_FINGERPRINT_SCAN;
  lcd.clear();
  lcd.print("Place Finger");
  lcd.setCursor(0, 1);
  lcd.print("A:New B:Delete");
}

void deleteAllFingerprints() {
  lcd.clear();
  lcd.print("Delete ALL?");
  lcd.setCursor(0, 1);
  lcd.print("# Confirm * Back");
  
  currentState = STATE_DELETE_ALL_CONFIRM;
  
  Serial.println("\n⚠  WARNING: Delete ALL Fingerprints?");
  Serial.println("This will erase the entire database!");
  Serial.println("Press '#' to confirm");
  Serial.println("Press '*' to cancel");
}

void confirmDeleteAll() {
  lcd.clear();
  lcd.print("Deleting All...");
  Serial.println("\n🗑  Deleting all fingerprints...");
  
  int deletedCount = 0;
  
  // Delete all fingerprints from sensor
  for (int i = 1; i <= MAX_FINGERPRINTS; i++) {
    if (finger.deleteModel(i) == FINGERPRINT_OK) {
      deletedCount++;
      Serial.print(".");
    }
  }
  
  // Clear all EEPROM data
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  
  lcd.clear();
  lcd.print("All Deleted!");
  lcd.setCursor(0, 1);
  lcd.print("Count: " + String(deletedCount));
  
  Serial.println("\n✅ All fingerprints deleted!");
  Serial.println("Total deleted: " + String(deletedCount));
  Serial.println("EEPROM cleared!");
  
  delay(3000);
  
  currentState = STATE_FINGERPRINT_SCAN;
  lcd.clear();
  lcd.print("Place Finger");
  lcd.setCursor(0, 1);
  lcd.print("A:New B:Delete");
}

// === API FUNCTIONS ===

bool loginUser(String user_id, String pin) {
  if (WiFi.status() != WL_CONNECTED) {
    lcd.clear();
    lcd.print("No WiFi!");
    delay(1500);
    return false;
  }

  HTTPClient http;
  String url = String(API_BASE_URL) + "/login";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<200> doc;
  doc["user_id"] = user_id;
  doc["pin"] = pin;

  String requestBody;
  serializeJson(doc, requestBody);

  Serial.println("Sending login request...");
  Serial.println("User ID: " + user_id);
  
  int httpResponseCode = http.POST(requestBody);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Response: " + response);
    StaticJsonDocument<300> responseDoc;
    deserializeJson(responseDoc, response);

    if (responseDoc["message"] == "Login successful") {
      http.end();
      return true;
    }
  }

  http.end();
  return false;
}

void getAllUsers() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(API_BASE_URL) + "/users";
  http.begin(url);
  int code = http.GET();

  if (code > 0) {
    String response = http.getString();
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, response);

    JsonArray arr = doc["users"].as<JsonArray>();
    userCount = 0;

    for (JsonObject user : arr) {
      if (userCount >= 10) break;
      if (user["user_id"].as<String>() != MY_USER_ID) {
        users[userCount].user_id = user["user_id"].as<String>();
        users[userCount].username = user["username"].as<String>();
        userCount++;
      }
    }
  }
  http.end();
}

void displayUserList() {
  lcd.clear();
  if (userCount == 0) {
    lcd.print("No users found");
    return;
  }
  lcd.setCursor(0, 0);
  lcd.print("Select User:");
  lcd.setCursor(0, 1);
  String name = users[selectedUserIndex].username;
  if (name.length() > 16) name = name.substring(0, 16);
  lcd.print(name);
}

void getLastMessage(String senderId, String recipientId) {
  HTTPClient http;
  String url = String(API_BASE_URL) + "/messages/" + senderId + "/" + recipientId;
  http.begin(url);
  int code = http.GET();

  if (code > 0) {
    String response = http.getString();
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, response);

    JsonArray msgs = doc.as<JsonArray>();
    if (msgs.size() > 0) {
      String msgId = msgs[0]["message_id"].as<String>();
      decryptMessage(msgId);
    } else {
      lcd.clear();
      lcd.print("No messages");
      delay(2000);
    }
  } else {
    lcd.clear();
    lcd.print("Failed to get msg");
    delay(2000);
  }
  http.end();
}

void decryptMessage(String messageId) {
  HTTPClient http;
  String url = String(API_BASE_URL) + "/messages/" + messageId + "/decrypt?user_id=" + MY_USER_ID;
  http.begin(url);
  int code = http.POST("");

  if (code > 0) {
    String response = http.getString();
    StaticJsonDocument<512> doc;
    deserializeJson(doc, response);
    lastMessageDecrypted = doc["decrypted_message"].as<String>();

    lcd.clear();
    lcd.print("Last Msg:");
    lcd.setCursor(0, 1);
    lcd.print(lastMessageDecrypted.substring(0, 16));
  } else {
    lcd.clear();
    lcd.print("Decrypt failed");
  }
  http.end();
  delay(2000);
}

void sendMessage(String recipientId, String msg) {
  HTTPClient http;
  String url = String(API_BASE_URL) + "/messages?sender_user_id=" + MY_USER_ID;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<300> doc;
  doc["recipient_user_id"] = recipientId;
  doc["message"] = msg;

  String body;
  serializeJson(doc, body);
  int code = http.POST(body);

  if (code > 0) {
    lcd.clear();
    lcd.print("Message Sent!");
  } else {
    lcd.clear();
    lcd.print("Send Failed!");
  }

  http.end();
  delay(1500);
}

char getMultiTapChar(char key, int count) {
  int index = key - '0';
  if (index >= 0 && index <= 9) {
    const char* chars = keyMap[index];
    int len = strlen(chars);
    return chars[count % len];
  }
  return key;
}

void updateMessageDisplay() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Msg: ");
  
  int startPos = max(0, (int)composedMessage.length() - 11);
  lcd.print(composedMessage.substring(startPos));
  
  lcd.setCursor(0, 1);
  lcd.print("*Del #Send D=OK");
}

void loop() {
  // Handle serial input for name entry
  if (currentState == STATE_ENTER_NAME && Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    if (input.length() > 0 && input.length() < NAME_LENGTH) {
      nameBuffer = input;
      Serial.println("User ID: " + nameBuffer);
      Serial.println("\nPlace finger on sensor...");
      
      lcd.clear();
      lcd.print("Place Finger");
      lcd.setCursor(0, 1);
      lcd.print("ID:" + nameBuffer.substring(0, 13));
      
      currentState = STATE_FINGERPRINT_ENROLL;
      enrollStep = 1;
    } else {
      Serial.println("❌ Invalid User ID length!");
      Serial.println("Enter User ID:");
    }
  }

  // Handle fingerprint enrollment
  if (currentState == STATE_FINGERPRINT_ENROLL) {
    handleEnrollment();
    return;
  }

  // Handle fingerprint scanning
  if (currentState == STATE_FINGERPRINT_SCAN) {
    int id = getFingerprintID();
    if (id >= 0) {
      String userName = getName(id);
      if (userName != "") {
        MY_USER_ID = userName;
        lcd.clear();
        lcd.print("Welcome!");
        lcd.setCursor(0, 1);
        lcd.print(userName.substring(0, 16));
        Serial.println("\n✅ Fingerprint recognized!");
        Serial.println("User ID: " + userName);
        delay(2000);
        
        lcd.clear();
        lcd.print("Enter PIN:");
        currentState = STATE_ENTER_PIN;
        entered = "";
      } else {
        lcd.clear();
        lcd.print("Fingerprint OK");
        lcd.setCursor(0, 1);
        lcd.print("But no name!");
        Serial.println("⚠ Fingerprint matched but no User ID stored!");
        delay(2000);
        lcd.clear();
        lcd.print("Place Finger");
        lcd.setCursor(0, 1);
        lcd.print("A:New B:Delete");
      }
      delay(500);
    }
  }

  // Handle multi-tap timeout in compose mode
  if (currentState == STATE_COMPOSE_MESSAGE) {
    if (lastMultiTapKey != '\0' && (millis() - lastPressTime) > multiTapTimeout) {
      char finalChar = getMultiTapChar(lastMultiTapKey, pressCount);
      composedMessage += finalChar;
      lastMultiTapKey = '\0';
      pressCount = 0;
      updateMessageDisplay();
    }
  }

  // Handle keypad input
  char key = scanKeypad();

  if (key != '\0' && key != lastKey) {
    if (millis() - lastDebounceTime > debounceDelay) {
      handleKey(key);
      lastDebounceTime = millis();
      lastKey = key;
    }
  } else if (key == '\0') {
    lastKey = '\0';
  }

  delay(10);
}

void handleEnrollment() {
  int p = -1;
  
  if (enrollStep == 1) {
    // First fingerprint capture
    p = finger.getImage();
    if (p == FINGERPRINT_OK) {
      p = finger.image2Tz(1);
      if (p == FINGERPRINT_OK) {
        Serial.println("✓ First image captured");
        lcd.clear();
        lcd.print("Remove Finger");
        delay(2000);
        enrollStep = 2;
        
        while (finger.getImage() != FINGERPRINT_NOFINGER) { delay(10); }
        
        lcd.clear();
        lcd.print("Place Same");
        lcd.setCursor(0, 1);
        lcd.print("Finger Again");
        Serial.println("Place SAME finger again...");
      } else {
        lcd.clear();
        lcd.print("Error! Retry");
        delay(1500);
        lcd.clear();
        lcd.print("Place Finger");
      }
    }
  } 
  else if (enrollStep == 2) {
    // Second fingerprint capture
    p = finger.getImage();
    if (p == FINGERPRINT_OK) {
      p = finger.image2Tz(2);
      if (p == FINGERPRINT_OK) {
        Serial.println("✓ Second image captured");
        lcd.clear();
        lcd.print("Processing...");
        
        p = finger.createModel();
        if (p == FINGERPRINT_OK) {
          p = finger.storeModel(enrollingID);
          if (p == FINGERPRINT_OK) {
            saveName(enrollingID, nameBuffer);
            
            lcd.clear();
            lcd.print("Success!");
            lcd.setCursor(0, 1);
            lcd.print("ID:" + String(enrollingID) + " " + nameBuffer.substring(0, 10));
            
            Serial.println("✅ Fingerprint enrolled successfully!");
            Serial.println("ID: " + String(enrollingID));
            Serial.println("User ID: " + nameBuffer);
            
            delay(3000);
            
            currentState = STATE_FINGERPRINT_SCAN;
            lcd.clear();
            lcd.print("Place Finger");
            lcd.setCursor(0, 1);
            lcd.print("A:New B:Delete");
          } else {
            lcd.clear();
            lcd.print("Store Failed!");
            Serial.println("❌ Failed to store fingerprint");
            delay(2000);
            currentState = STATE_FINGERPRINT_SCAN;
            lcd.clear();
            lcd.print("Place Finger");
            lcd.setCursor(0, 1);
            lcd.print("A:New B:Delete");
          }
        } else if (p == FINGERPRINT_ENROLLMISMATCH) {
          lcd.clear();
          lcd.print("Not Matched!");
          Serial.println("❌ Fingerprints did not match");
          delay(2000);
          currentState = STATE_FINGERPRINT_SCAN;
          lcd.clear();
          lcd.print("Place Finger");
          lcd.setCursor(0, 1);
          lcd.print("A:New B:Delete");
        } else {
          lcd.clear();
          lcd.print("Model Failed!");
          delay(2000);
          currentState = STATE_FINGERPRINT_SCAN;
          lcd.clear();
          lcd.print("Place Finger");
          lcd.setCursor(0, 1);
          lcd.print("A:New B:Delete");
        }
        enrollStep = 0;
      }
    }
  }
}

void handleKey(char key) {
  switch (currentState) {
    case STATE_FINGERPRINT_SCAN:
      if (key == 'A') {
        startEnrollment();
      } else if (key == 'B') {
        startDeletion();
      }
      break;

    case STATE_DELETE_MENU:
      if (key == '0') {
        // Delete single fingerprint
        lcd.clear();
        lcd.print("Place Finger");
        lcd.setCursor(0, 1);
        lcd.print("to Delete");
        
        Serial.println("\nPlace the finger you want to delete...");
        
        // Wait for fingerprint scan
        while (true) {
          int id = getFingerprintID();
          if (id >= 0) {
            deleteFingerprint(id);
            break;
          }
          
          // Check for cancel
          char cancelKey = scanKeypad();
          if (cancelKey == '*') {
            currentState = STATE_FINGERPRINT_SCAN;
            lcd.clear();
            lcd.print("Place Finger");
            lcd.setCursor(0, 1);
            lcd.print("A:New B:Delete");
            break;
          }
          delay(50);
        }
      } else if (key == '1') {
        // Delete all fingerprints
        deleteAllFingerprints();
      } else if (key == '*') {
        // Cancel
        currentState = STATE_FINGERPRINT_SCAN;
        lcd.clear();
        lcd.print("Place Finger");
        lcd.setCursor(0, 1);
        lcd.print("A:New B:Delete");
      }
      break;

    case STATE_DELETE_CONFIRM:
      if (key == '#') {
        confirmDeletion();
      } else if (key == '*') {
        lcd.clear();
        lcd.print("Cancelled");
        delay(1000);
        deleteID = -1;
        deleteUserName = "";
        currentState = STATE_FINGERPRINT_SCAN;
        lcd.clear();
        lcd.print("Place Finger");
        lcd.setCursor(0, 1);
        lcd.print("A:New B:Delete");
      }
      break;

    case STATE_DELETE_ALL_CONFIRM:
      if (key == '#') {
        confirmDeleteAll();
      } else if (key == '*') {
        lcd.clear();
        lcd.print("Cancelled");
        delay(1000);
        currentState = STATE_FINGERPRINT_SCAN;
        lcd.clear();
        lcd.print("Place Finger");
        lcd.setCursor(0, 1);
        lcd.print("A:New B:Delete");
      }
      break;

    case STATE_ENTER_PIN:
      handlePinEntry(key);
      break;

    case STATE_USER_LIST:
      handleUserList(key);
      break;

    case STATE_VIEW_SEND_OPTION:
      handleViewSendOption(key);
      break;

    case STATE_COMPOSE_MESSAGE:
      handleComposeMessage(key);
      break;

    default:
      break;
  }
}

void handlePinEntry(char key) {
  if (key == '*') {
    entered = "";
    lcd.clear();
    lcd.print("Enter PIN:");
  } else if (key == '#') {
    if (entered.length() > 0) {
      MY_PIN = entered;
      lcd.clear();
      lcd.print("Logging in...");
      Serial.println("Attempting login...");
      Serial.println("User ID: " + MY_USER_ID);
      Serial.println("PIN: " + MY_PIN);
      
      if (loginUser(MY_USER_ID, MY_PIN)) {
        lcd.clear();
        lcd.print("Login Success!");
        Serial.println("✅ Login successful!");
        delay(1500);
        getAllUsers();
        currentState = STATE_USER_LIST;
        displayUserList();
      } else {
        lcd.clear();
        lcd.print("Login Failed!");
        Serial.println("❌ Login failed");
        delay(2000);
        lcd.clear();
        lcd.print("Enter PIN:");
      }
      entered = "";
    }
  } else if (entered.length() < 6 && key >= '0' && key <= '9') {
    entered += key;
    lcd.setCursor(0, 1);
    for (int i = 0; i < entered.length(); i++) lcd.print('*');
  }
}

void handleUserList(char key) {
  if (key == 'A') {
    if (userCount > 0) {
      selectedUserIndex--;
      if (selectedUserIndex < 0) selectedUserIndex = userCount - 1;
      displayUserList();
    }
  } else if (key == 'B') {
    if (userCount > 0) {
      selectedUserIndex++;
      if (selectedUserIndex >= userCount) selectedUserIndex = 0;
      displayUserList();
    }
  } else if (key == 'C') {
    selectedRecipientId = users[selectedUserIndex].user_id;
    lcd.clear();
    lcd.print("Selected:");
    lcd.setCursor(0, 1);
    lcd.print(users[selectedUserIndex].username.substring(0, 16));
    delay(1000);
    lcd.clear();
    lcd.print("View[0] Send[1]");
    currentState = STATE_VIEW_SEND_OPTION;
  }
}

void handleViewSendOption(char key) {
  if (key == '0') {
    lcd.clear();
    lcd.print("Loading msg...");
    getLastMessage(MY_USER_ID, selectedRecipientId);
    currentState = STATE_USER_LIST;
    displayUserList();
  } else if (key == '1') {
    currentState = STATE_COMPOSE_MESSAGE;
    composedMessage = "";
    lastMultiTapKey = '\0';
    pressCount = 0;
    updateMessageDisplay();
  } else if (key == 'C') {
    currentState = STATE_USER_LIST;
    displayUserList();
  }
}

void handleComposeMessage(char key) {
  unsigned long now = millis();
  
  if (key >= '0' && key <= '9') {
    if (key == lastMultiTapKey && (now - lastPressTime) < multiTapTimeout) {
      pressCount++;
    } else {
      if (lastMultiTapKey != '\0') {
        char finalChar = getMultiTapChar(lastMultiTapKey, pressCount);
        composedMessage += finalChar;
      }
      pressCount = 0;
      lastMultiTapKey = key;
    }
    lastPressTime = now;
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Msg: ");
    int startPos = max(0, (int)composedMessage.length() - 10);
    lcd.print(composedMessage.substring(startPos));
    lcd.print(getMultiTapChar(key, pressCount));
    
    lcd.setCursor(0, 1);
    lcd.print("*Del #Send D=OK");
    
  } else if (key == '*') {
    if (lastMultiTapKey != '\0') {
      lastMultiTapKey = '\0';
      pressCount = 0;
    } else if (composedMessage.length() > 0) {
      composedMessage.remove(composedMessage.length() - 1);
    }
    updateMessageDisplay();
    
  } else if (key == '#') {
    if (lastMultiTapKey != '\0') {
      char finalChar = getMultiTapChar(lastMultiTapKey, pressCount);
      composedMessage += finalChar;
      lastMultiTapKey = '\0';
      pressCount = 0;
    }
    
    if (composedMessage.length() > 0) {
      lcd.clear();
      lcd.print("Sending...");
      sendMessage(selectedRecipientId, composedMessage);
      composedMessage = "";
      currentState = STATE_USER_LIST;
      displayUserList();
    } else {
      lcd.clear();
      lcd.print("Empty message!");
      delay(1000);
      updateMessageDisplay();
    }
    
  } else if (key == 'D') {
    if (lastMultiTapKey != '\0') {
      char finalChar = getMultiTapChar(lastMultiTapKey, pressCount);
      composedMessage += finalChar;
      lastMultiTapKey = '\0';
      pressCount = 0;
      updateMessageDisplay();
    }
    
  } else if (key == 'C') {
    composedMessage = "";
    lastMultiTapKey = '\0';
    pressCount = 0;
    currentState = STATE_USER_LIST;
    displayUserList();
  }
}