// ili9488_driver.hpp - simpele, stabiele driver voor ILI9488 + LVGL
#pragma once
#include <Arduino.h>

// ==== PIN DEFINITIES (pas aan als jouw PCB anders is) ====
#define LCD_D0   10
#define LCD_D1   11
#define LCD_D2   12
#define LCD_D3   13
#define LCD_D4   14
#define LCD_D5   15
#define LCD_D6   16
#define LCD_D7   17

#define LCD_CS   2     // CS van het LCD
#define LCD_RS   45    // RS / DC
#define LCD_WR   20    // WR
#define LCD_RST  -1    // zet op GPIO als je RST aan een pin hebt, anders -1 (aan 3V3)

constexpr uint16_t ILI9488_WIDTH  = 320;
constexpr uint16_t ILI9488_HEIGHT = 480;


static const int lcd_data_pins[8] = {
  LCD_D0, LCD_D1, LCD_D2, LCD_D3,
  LCD_D4, LCD_D5, LCD_D6, LCD_D7
};

inline void lcd_busWrite(uint8_t v)
{
  for (int i = 0; i < 8; i++) {
    digitalWrite(lcd_data_pins[i], (v >> i) & 0x01);
  }
}

inline void lcd_pulseWR()
{
  digitalWrite(LCD_WR, LOW);
  __asm__ __volatile__("nop\nnop\nnop\nnop\nnop\n");
  digitalWrite(LCD_WR, HIGH);
}

inline void lcd_writeCommand(uint8_t cmd)
{
  digitalWrite(LCD_RS, LOW);
  digitalWrite(LCD_CS, LOW);
  lcd_busWrite(cmd);
  lcd_pulseWR();
  digitalWrite(LCD_CS, HIGH);
}

inline void lcd_writeData(uint8_t data)
{
  digitalWrite(LCD_RS, HIGH);
  digitalWrite(LCD_CS, LOW);
  lcd_busWrite(data);
  lcd_pulseWR();
  digitalWrite(LCD_CS, HIGH);
}

inline void lcd_writeColor(uint16_t c)
{
  // GEEN inversie meer hier
  lcd_writeData(c >> 8);
  lcd_writeData(c & 0xFF);
}

inline void ili9488_set_rotation(uint8_t r) {
  uint8_t madctl = 0;

  switch (r & 3) {
    case 0: // portret
      madctl = 0x48;  // MX | BGR
      break;
    case 1: // landscape (90°)
      madctl = 0x28;  // MV | BGR
      break;
    case 2: // portret 180°
      madctl = 0x88;  // MY | BGR
      break;
    case 3: // landscape 180°
      madctl = 0xE8;  // MX | MY | MV | BGR
      break;
  }

  lcd_writeCommand(0x36);
  lcd_writeData(madctl);
}

inline void ili9488_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  uint16_t x2 = x + w - 1;
  uint16_t y2 = y + h - 1;

  // Column address set
  lcd_writeCommand(0x2A);
  lcd_writeData(x >> 8);
  lcd_writeData(x & 0xFF);
  lcd_writeData(x2 >> 8);
  lcd_writeData(x2 & 0xFF);

  // Page address set
  lcd_writeCommand(0x2B);
  lcd_writeData(y >> 8);
  lcd_writeData(y & 0xFF);
  lcd_writeData(y2 >> 8);
  lcd_writeData(y2 & 0xFF);

  // RAMWR
  lcd_writeCommand(0x2C);
}

inline void ili9488_init()
{
  // Datapinnen + control-pinnen als output
  for (int i = 0; i < 8; i++) {
    pinMode(lcd_data_pins[i], OUTPUT);
    digitalWrite(lcd_data_pins[i], LOW);
  }
  pinMode(LCD_CS, OUTPUT);
  pinMode(LCD_RS, OUTPUT);
  pinMode(LCD_WR, OUTPUT);
  digitalWrite(LCD_CS, HIGH);
  digitalWrite(LCD_RS, HIGH);
  digitalWrite(LCD_WR, HIGH);

#if (LCD_RST >= 0)
  pinMode(LCD_RST, OUTPUT);
  digitalWrite(LCD_RST, LOW);
  delay(10);
  digitalWrite(LCD_RST, HIGH);
  delay(120);
#else
  // RST hard aan 3V3
  delay(120);
#endif

  // Software reset
  lcd_writeCommand(0x01);
  delay(120);

  // Sleep out
  lcd_writeCommand(0x11);
  delay(120);

  // 16-bit pixel formaat RGB565
  lcd_writeCommand(0x3A);
  lcd_writeData(0x55);

  // Memory Access Control: portret, geen spiegeling, RGB (BGR=0, MV=0)
  // lcd_writeCommand(0x36);
  // lcd_writeData(0x48);

  // Display inversion OFF (heel belangrijk om zwart/wit omkering uit te zetten)
  lcd_writeCommand(0x20);  // of 0x21, afhankelijk van jouw werkende situatie

  // Display on
  lcd_writeCommand(0x29);
  delay(20);

  ili9488_set_rotation(1);
}

inline void ili9488_fill_screen(uint16_t color)
{
  // compenseer paneel-inversie
  uint16_t c = ~color;

  ili9488_set_window(0, 0, 320, 480);
  uint32_t total = 320UL * 480UL;
  for (uint32_t i = 0; i < total; i++) {
    lcd_writeColor(c);
  }
}


// LVGL buffer schrijven: px_map zijn bytes in RGB565 (little endian)
inline void ili9488_push_pixels(uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h,
                                const uint8_t *px_map)
{
  ili9488_set_window(x, y, w, h);

  uint32_t total = (uint32_t)w * h;
  for (uint32_t i = 0; i < total; i++) {
    uint8_t lo = px_map[2 * i + 0];
    uint8_t hi = px_map[2 * i + 1];

    // maak 16-bit kleur uit LVGL
    uint16_t c = (static_cast<uint16_t>(hi) << 8) | lo;

    // compenseer paneel-inversie
    c = ~c;

    lcd_writeColor(c);
  }
}










