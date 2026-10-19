#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scrolling Text:");
}

void loop() {
  lcd.setCursor(0, 1);
  lcd.print("Hello Madhan!   ");
  for (int pos = 0; pos < 16; pos++) {
    lcd.scrollDisplayLeft();
    delay(250);
  }
  for (int pos = 0; pos < 16; pos++) {
    lcd.scrollDisplayRight();
    delay(250);
  }
}
