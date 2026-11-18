#include <Wire.h>
#include <U8g2lib.h>

// OLED 1 : I2C matériel
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled1(
  U8G2_R0,
  U8X8_PIN_NONE
);

// OLED 2 : I2C logiciel
// SCL = D8, SDA = D9
U8G2_SSD1306_128X64_NONAME_F_SW_I2C oled2(
  U8G2_R0,
  8,
  9,
  U8X8_PIN_NONE
);

void setup() {
  Wire.begin();
  Wire.setClock(100000);

  oled1.setI2CAddress(0x3C * 2);
  oled2.setI2CAddress(0x3C * 2);

  oled1.begin();
  oled2.begin();

  oled1.clearBuffer();
  oled1.setFont(u8g2_font_ncenB08_tr);
  oled1.drawStr(0, 20, "OLED 1 OK");
  oled1.drawStr(0, 40, "I2C materiel");
  oled1.sendBuffer();

  oled2.clearBuffer();
  oled2.setFont(u8g2_font_ncenB08_tr);
  oled2.drawStr(0, 20, "OLED 2 OK");
  oled2.drawStr(0, 40, "I2C logiciel");
  oled2.sendBuffer();
}

void loop() {
}