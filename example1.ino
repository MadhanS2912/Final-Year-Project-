#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// === WiFi CREDENTIALS ===
const char* ssid = "Batch_5";        // Change this
const char* password_wifi = "9876543210";  // Change this

// === API CONFIGURATION ===
const char* API_BASE_URL = "https://agnicom-server.onrender.com";
String MY_USER_ID = "hmCRDTpQc1T0";  // Madhan's user ID - Change as needed
String MY_PIN = "1234";               // Your PIN

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

// === PUSH BUTTONS ===
const int BTN_UP = 15;      // Button to scroll up
const int BTN_DOWN = 13;    // Button to scroll down
const int BTN_SELECT = 12;  // Button to select user

// === STATE MANAGEMENT ===
enum State {
  STATE_PASSWORD,
  STATE_LOGIN,
  STATE_USER_LIST,
  STATE_VIEW_SEND_OPTION,
  STATE_VIEW_MESSAGE,
  STATE_SEND_MESSAGE
};

State currentState = STATE_PASSWORD;

// === USER DATA ===
struct User {
  String user_id;
  String username;
};

User users[10];  // Max 10 users
int userCount = 0;
int selectedUserIndex = 0;
String selectedRecipientId = "";

// === PASSWORD ENTRY ===
String password = "1234";
String entered = "";

// === KEYPAD DEBOUNCING ===
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;
char lastKey = '\0';

// === BUTTON DEBOUNCING ===
unsigned long lastBtnUpTime = 0;
unsigned long lastBtnDownTime = 0;
unsigned long lastBtnSelectTime = 0;
const unsigned long btnDebounceDelay = 300;

// === MESSAGE DATA ===
String lastMessageDecrypted = "";

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  
  // LCD setup
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("AgniCom Chat");
  delay(1500);
  
  // Keypad pin setup
  for (int r = 0; r < 4; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }
  for (int c = 0; c < 4; c++) {
    pinMode(colPins[c], INPUT_PULLUP);
  }

  // Button setup
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  // Connect to WiFi
  connectWiFi();
  
  // Start with password entry
  lcd.clear();
  lcd.print("Enter Password:");
  currentState = STATE_PASSWORD;
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
    for (int i = 0; i <= attempts % 16; i++) {
      lcd.print(".");
    }
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
    lcd.setCursor(0, 1);
    lcd.print("Check Settings");
    Serial.println("\nWiFi Connection Failed!");
    while(1); // Halt
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

bool loginUser(String pin) {
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
  doc["user_id"] = MY_USER_ID;
  doc["pin"] = pin;
  
  String requestBody;
  serializeJson(doc, requestBody);
  
  Serial.println("Logging in...");
  Serial.println(requestBody);
  
  int httpResponseCode = http.POST(requestBody);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Response: " + response);
    
    StaticJsonDocument<300> responseDoc;
    deserializeJson(responseDoc, response);
    
    if (responseDoc.containsKey("message") && 
        String((const char*)responseDoc["message"]) == "Login successful") {
      http.end();
      return true;
    }
  }
  
  Serial.println("Login failed. Code: " + String(httpResponseCode));
  http.end();
  return false;
}

void getAllUsers() {
  if (WiFi.status() != WL_CONNECTED) {
    lcd.clear();
    lcd.print("No WiFi!");
    delay(1500);
    return;
  }

  HTTPClient http;
  String url = String(API_BASE_URL) + "/users";
  
  http.begin(url);
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Users Response: " + response);
    
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, response);
    
    JsonArray usersArray = doc["users"].as<JsonArray>();
    userCount = 0;
    
    for (JsonObject user : usersArray) {
      if (userCount >= 10) break;
      
      users[userCount].user_id = user["user_id"].as<String>();
      users[userCount].username = user["username"].as<String>();
      
      // Don't show current user in list
      if (users[userCount].user_id != MY_USER_ID) {
        userCount++;
      }
    }
    
    Serial.println("Loaded " + String(userCount) + " users");
  } else {
    Serial.println("Failed to get users. Code: " + String(httpResponseCode));
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
  
  String displayText = users[selectedUserIndex].username;
  if (displayText.length() > 16) {
    displayText = displayText.substring(0, 16);
  }
  lcd.print(displayText);
}

void getLastMessage(String senderId, String recipientId) {
  if (WiFi.status() != WL_CONNECTED) {
    lcd.clear();
    lcd.print("No WiFi!");
    delay(1500);
    return;
  }

  HTTPClient http;
  String url = String(API_BASE_URL) + "/messages/" + senderId + "/" + recipientId;
  
  http.begin(url);
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Messages: " + response);
    
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, response);
    
    JsonArray messages = doc.as<JsonArray>();
    
    if (messages.size() > 0) {
      JsonObject lastMsg = messages[messages.size() - 1];
      String messageId = lastMsg["message_id"].as<String>();
      
      // Decrypt the message
      decryptMessage(messageId);
    } else {
      lcd.clear();
      lcd.print("No messages");
      lastMessageDecrypted = "No messages";
      delay(1500);
    }
  } else {
    lcd.clear();
    lcd.print("Failed to get");
    lcd.setCursor(0, 1);
    lcd.print("messages");
    Serial.println("Failed. Code: " + String(httpResponseCode));
    delay(1500);
  }
  
  http.end();
}

void decryptMessage(String messageId) {
  if (WiFi.status() != WL_CONNECTED) {
    lcd.clear();
    lcd.print("No WiFi!");
    delay(1500);
    return;
  }

  HTTPClient http;
  String url = String(API_BASE_URL) + "/messages/" + messageId + "/decrypt?user_id=" + MY_USER_ID;
  
  http.begin(url);
  int httpResponseCode = http.POST("");
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Decrypted: " + response);
    
    StaticJsonDocument<512> doc;
    deserializeJson(doc, response);
    
    lastMessageDecrypted = doc["decrypted_message"].as<String>();
    
    lcd.clear();
    lcd.print("Last Message:");
    lcd.setCursor(0, 1);
    String displayMsg = lastMessageDecrypted;
    if (displayMsg.length() > 16) {
      displayMsg = displayMsg.substring(0, 16);
    }
    lcd.print(displayMsg);
  } else {
    lcd.clear();
    lcd.print("Decrypt failed");
    Serial.println("Decrypt failed. Code: " + String(httpResponseCode));
  }
  
  http.end();
  delay(3000);
}

void sendMessage(String recipientId, String message) {
  if (WiFi.status() != WL_CONNECTED) {
    lcd.clear();
    lcd.print("No WiFi!");
    delay(1500);
    return;
  }

  HTTPClient http;
  String url = String(API_BASE_URL) + "/messages?sender_user_id=" + MY_USER_ID;
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<300> doc;
  doc["recipient_user_id"] = recipientId;
  doc["message"] = message;
  
  String requestBody;
  serializeJson(doc, requestBody);
  
  Serial.println("Sending message...");
  Serial.println(requestBody);
  
  lcd.clear();
  lcd.print("Sending...");
  
  int httpResponseCode = http.POST(requestBody);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Send response: " + response);
    
    lcd.clear();
    lcd.print("Message Sent!");
    delay(1500);
  } else {
    lcd.clear();
    lcd.print("Send Failed!");
    Serial.println("Failed. Code: " + String(httpResponseCode));
    delay(1500);
  }
  
  http.end();
}

void loop() {
  char key = scanKeypad();
  
  // Handle keypad input with debouncing
  if (key != '\0' && key != lastKey) {
    if (millis() - lastDebounceTime > debounceDelay) {
      handleKeypress(key);
      lastDebounceTime = millis();
      lastKey = key;
    }
  } else if (key == '\0') {
    lastKey = '\0';
  }
  
  // Handle button presses
  handleButtons();
}

void handleKeypress(char key) {
  switch (currentState) {
    case STATE_PASSWORD:
      handlePasswordInput(key);
      break;
      
    case STATE_LOGIN:
      handleLoginInput(key);
      break;
      
    case STATE_VIEW_SEND_OPTION:
      handleViewSendOption(key);
      break;
      
    default:
      break;
  }
}

void handlePasswordInput(char key) {
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
      delay(1000);
      
      // Move to login state
      currentState = STATE_LOGIN;
      entered = "";
      lcd.clear();
      lcd.print("Enter PIN:");
      lcd.setCursor(0, 1);
      lcd.print("Then press #");
    } else {
      lcd.print("Access Denied");
      Serial.println("❌ Access Denied");
      entered = "";
      delay(1500);
      lcd.clear();
      lcd.print("Enter Password:");
    }
  } 
  else if (entered.length() < 4) {
    entered += key;
    Serial.print("*");
    lcd.setCursor(0, 1);
    for (int i = 0; i < entered.length(); i++) {
      lcd.print('*');
    }
  }
}

void handleLoginInput(char key) {
  if (key == '*') {
    entered = "";
    lcd.clear();
    lcd.print("Enter PIN:");
    lcd.setCursor(0, 1);
    lcd.print("Then press #");
  } 
  else if (key == '#') {
    if (entered.length() > 0) {
      lcd.clear();
      lcd.print("Logging in...");
      
      if (loginUser(entered)) {
        lcd.clear();
        lcd.print("Login Success!");
        delay(1500);
        
        // Get all users
        lcd.clear();
        lcd.print("Loading users...");
        getAllUsers();
        
        // Move to user list
        currentState = STATE_USER_LIST;
        selectedUserIndex = 0;
        displayUserList();
      } else {
        lcd.clear();
        lcd.print("Login Failed!");
        delay(1500);
        lcd.clear();
        lcd.print("Enter PIN:");
      }
      entered = "";
    }
  } 
  else if (entered.length() < 6) {
    entered += key;
    lcd.setCursor(0, 1);
    for (int i = 0; i < entered.length(); i++) {
      lcd.print('*');
    }
  }
}

void handleViewSendOption(char key) {
  if (key == '0') {
    // View message
    currentState = STATE_VIEW_MESSAGE;
    lcd.clear();
    lcd.print("Loading msg...");
    
    // Get messages between current user and selected recipient
    getLastMessage(MY_USER_ID, selectedRecipientId);
    
    delay(3000);
    currentState = STATE_USER_LIST;
    displayUserList();
  } 
  else if (key == '1') {
    // Send message
    currentState = STATE_SEND_MESSAGE;
    lcd.clear();
    lcd.print("Sending test...");
    
    // Send default test message
    String testMsg = "test - $ [1,2,3,4]";
    sendMessage(selectedRecipientId, testMsg);
    
    currentState = STATE_USER_LIST;
    displayUserList();
  }
}

void handleButtons() {
  // UP button
  if (digitalRead(BTN_UP) == LOW && (millis() - lastBtnUpTime > btnDebounceDelay)) {
    lastBtnUpTime = millis();
    
    if (currentState == STATE_USER_LIST && userCount > 0) {
      selectedUserIndex--;
      if (selectedUserIndex < 0) {
        selectedUserIndex = userCount - 1;
      }
      displayUserList();
    }
  }
  
  // DOWN button
  if (digitalRead(BTN_DOWN) == LOW && (millis() - lastBtnDownTime > btnDebounceDelay)) {
    lastBtnDownTime = millis();
    
    if (currentState == STATE_USER_LIST && userCount > 0) {
      selectedUserIndex++;
      if (selectedUserIndex >= userCount) {
        selectedUserIndex = 0;
      }
      displayUserList();
    }
  }
  
  // SELECT button
  if (digitalRead(BTN_SELECT) == LOW && (millis() - lastBtnSelectTime > btnDebounceDelay)) {
    lastBtnSelectTime = millis();
    
    if (currentState == STATE_USER_LIST && userCount > 0) {
      selectedRecipientId = users[selectedUserIndex].user_id;
      
      lcd.clear();
      lcd.print("Selected:");
      lcd.setCursor(0, 1);
      lcd.print(users[selectedUserIndex].username);
      delay(1000);
      
      // Show view/send options
      currentState = STATE_VIEW_SEND_OPTION;
      lcd.clear();
      lcd.print("View[0] Send[1]");
      lcd.setCursor(0, 1);
      lcd.print("Select option");
    }
    else if (currentState == STATE_VIEW_SEND_OPTION || 
             currentState == STATE_VIEW_MESSAGE || 
             currentState == STATE_SEND_MESSAGE) {
      // Go back to user list
      currentState = STATE_USER_LIST;
      displayUserList();
    }
  }
}