#include <Arduino.h>
#include "HID-Project.h"

enum ProtocolMode {
  MODE_LN_NONE, // None
  MODE_LN_SR, // Shift Register
  MODE_LN_MCU, // Microcontroller
};

enum DeviceType {
  DEVICE_NONE,
  DEVICE_LN_GC,
  DEVICE_LN_N64,
  DEVICE_LN_SNES,
};

// default protocol
ProtocolMode proto = MODE_LN_NONE;
// default device
DeviceType device = DEVICE_NONE;

// PortD
// PD0 Pin 3   CLK1
// PD1 Pin 2   CLK2
// PD2 Pin 0
// PD3 Pin 1
// PD4 Pin 4   DATA
// PD5 TX LED
// PD6 NC
// PD7 Pin 6

// Pin mapping
const uint8_t D_CLK1 = 0; // [IN] Main Clock, Gate Driver
const uint8_t D_CLK2 = 1; // [IN] Second Clock, SNES Only
const uint8_t D_DATA = 4; // [OUT] Data

// Time-Skew correction for CLK1
const uint8_t TIME_SKEW = 2;

// PMOS gate logic: P-channel high-side, active LOW (gate LOW = power ON)
#define PWR_ACTIVE_HIGH false

inline void clk1_high() {
  #ifdef PWR_ACTIVE_HIGH
    PORTD &= ~_BV(D_CLK1);
  #else
    PORTD |= _BV(D_CLK1);
  #endif
}

inline void clk1_low() {
  #ifdef PWR_ACTIVE_HIGH
    PORTD |= _BV(D_CLK1);
  #else
    PORTD &= ~_BV(D_CLK1);
  #endif
}

inline void clk2_high() {
    PORTD |= _BV(D_CLK2);
}

inline void clk2_low() { 
    PORTD &= ~_BV(D_CLK2);
}

/// Pulse the CLK1 line down and the CLK2 line up
/// This is essentially a half-clock cycle for SNES
inline void pulse_snes() {

  // combine CLK1 low and CLK2 high
  #ifdef PWR_ACTIVE_HIGH
    PORTD |= _BV(D_CLK1) | _BV(D_CLK2);
  #else
    uint8_t portd = PIND;
    portd &= ~_BV(D_CLK1);
    portd |= _BV(D_CLK2);
    PORTD = portd;
  #endif

  delayMicroseconds(3);

  // bring latch back up
  clk1_high();

  delayMicroseconds(20);

  // bring CLK2 down
  //clk2_low();
}

/// Pulse the CLK1 line low for a given length and return it to high
/// @param length Length of the pulse in microseconds
inline void pulse(unsigned int length) {
  clk1_low();
  delayMicroseconds(length + TIME_SKEW);
  clk1_high();
}

/// Send the hello sequence to the MCU protocol
inline void clock_mpu_hello() {
  pulse(6);
  delayMicroseconds(6 - TIME_SKEW);
  pulse(6);
  delayMicroseconds(26 - TIME_SKEW);
}

/// Read a byte from the MCU protocol
/// @return The byte read from the MCU
inline uint8_t read_byte_mcu()
{
  uint8_t value;
  for(int j=0;j<8;j++){
    clk1_low();
    delayMicroseconds(4 + TIME_SKEW);
    value = (value << 1) | ((PIND & _BV(D_DATA)) ? 1 : 0);
    clk1_high();
    delayMicroseconds(4 - TIME_SKEW);
  }
  return value;
}

void setup() {
  Serial.begin(115200);

  DDRD |= _BV(D_CLK1) | _BV(D_CLK2);
  clk1_high();
  clk2_high();

  Gamepad.begin();

  Serial.println(F("LodgeNet USB test host starting..."));
}

static uint8_t last_dpad = 0;
static uint8_t last_menu = 0;

void loop() {
  switch(proto)
  {
    case MODE_LN_NONE:
      proto = MODE_LN_SR; // switch to SR mode for testing
      last_dpad = 0;
      last_menu = 0;
      
      Gamepad.releaseAll();
      Gamepad.dPad1(0);
      Gamepad.xAxis(0);
      Gamepad.yAxis(0);
      Gamepad.zAxis(0);
      Gamepad.rxAxis(0);
      Gamepad.ryAxis(0);
      Gamepad.rzAxis(0);
      Gamepad.write();

      delay(1000); // wait before next read
      break;
    case MODE_LN_SR:
      {
        uint16_t value = 0x0000;

        noInterrupts();
        
        for (int i = 0; i < 16; i++) {
          pulse_snes();
          clk2_low();
          delayMicroseconds(20);
          value = (value << 1) | ((PIND & _BV(D_DATA)) ? 1 : 0);
          clk2_high();
        }
        pulse_snes();
        delayMicroseconds(20);
        bool present = !(PIND & _BV(D_DATA));

        interrupts();

        if (!present) {
          proto = MODE_LN_MCU; // try checking for MCU next (maybe do multiple polls first, stock does 12)
          device = DEVICE_NONE;
          last_dpad = 0;
          last_menu = 0;

          // No controller present, skip processing
          delay(60); // wait before next read
          break;
        }

        device = DEVICE_LN_SNES;

        // dpad bits: 0=Right, 1=Left, 2=Down, 3=Up
        uint8_t dpad = (~value >> 4) & 0x03;
        dpad |= (~value >> 6) & 0x0C;

        // Extract axes
        uint8_t ButtonPlus = (dpad & 0x03) == 0x03; // Left/Right bits
        uint8_t ButtonMinus = (dpad & 0x0C) == 0x0C; // Up/Down bits

        // If both L+R pressed, preserve last L/R
        if ((dpad & 0x03) == 0x03) {
          dpad = (dpad & ~0x03) | (last_dpad & 0x03);
        }
        // If both U+D pressed, preserve last U/D
        if ((dpad & 0x0C) == 0x0C) {
          dpad = (dpad & ~0x0C) | (last_dpad & 0x0C);
        }
        last_dpad = dpad;

        Gamepad.releaseAll();

        static uint8_t hid_dpad = GAMEPAD_DPAD_CENTERED;
        switch(last_dpad){
          case 0x01: hid_dpad = GAMEPAD_DPAD_RIGHT; break;
          case 0x02: hid_dpad = GAMEPAD_DPAD_LEFT; break;
          case 0x04: hid_dpad = GAMEPAD_DPAD_DOWN; break;
          case 0x08: hid_dpad = GAMEPAD_DPAD_UP; break;
          case 0x05: hid_dpad = GAMEPAD_DPAD_DOWN_RIGHT; break;
          case 0x06: hid_dpad = GAMEPAD_DPAD_DOWN_LEFT; break;
          case 0x09: hid_dpad = GAMEPAD_DPAD_UP_RIGHT; break;
          case 0x0A: hid_dpad = GAMEPAD_DPAD_UP_LEFT; break;
          default:   hid_dpad = GAMEPAD_DPAD_CENTERED; break;
        }
        Gamepad.dPad1(hid_dpad);

        if (!((value >> 13) & 0x01)) Gamepad.press(1);
        if (!((value >>  3) & 0x01)) Gamepad.press(2);
        if (!((value >> 12) & 0x01)) Gamepad.press(3);
        if (!((value >>  2) & 0x01)) Gamepad.press(4);
        if (!((value >> 11) & 0x01)) Gamepad.press(5);
        if (!((value >> 10) & 0x01)) Gamepad.press(6);
        if (!((value >>  1) & 0x01)) Gamepad.press(7);
        if (!((value >>  0) & 0x01)) Gamepad.press(8);
        if (!((value >> 14) & 0x01)) Gamepad.press(12);
        if (ButtonPlus) Gamepad.press(14);
        if (! ((value >> 15) & 0x01)) Gamepad.press(15);
        if (ButtonMinus) Gamepad.press(17);

        Gamepad.xAxis(0);
        Gamepad.yAxis(0);
        Gamepad.zAxis(0);
        Gamepad.rxAxis(0);
        Gamepad.ryAxis(0);
        Gamepad.rzAxis(0);
        Gamepad.write();

        delay(65); // wait before next read
      }
      break;
    case MODE_LN_MCU:
      {
        noInterrupts();

        clock_mpu_hello();
        
        uint8_t buttons1 = read_byte_mcu();
        uint8_t buttons2 = read_byte_mcu();
        uint8_t type_flag = (buttons2 & B11000000);
        if (type_flag == B10000000) {
          device = DEVICE_LN_N64;
        } else if (type_flag == B11000000) {
          device = DEVICE_LN_GC;
        } else {
          // impossible MCU response, abort and kick back to idle
          interrupts();
          proto = MODE_LN_NONE;
          device = DEVICE_NONE;
          last_dpad = 0;
          last_menu = 0;
          delay(65); // wait before next read
          break;
        }
        if (buttons2 == 0xFF) {
          interrupts();
          // impossible MCU response, abort and kick back to idle
          // Must be 0xXXXXXX10 for N64 or 0x01XXXX11 for GC
          proto = MODE_LN_NONE;
          device = DEVICE_NONE;
          last_dpad = 0;
          last_menu = 0;
          delay(65); // wait before next read
          break;
        }

        uint8_t x_axis1 = read_byte_mcu();
        uint8_t y_axis1 = read_byte_mcu();
        int8_t x_axis = (int8_t)x_axis1;
        int8_t y_axis = (int8_t)y_axis1;
        uint8_t x_axis2 = 0;
        uint8_t y_axis2 = 0;
        uint8_t l_trigger = 0;
        uint8_t r_trigger = 0;
        if (device == DEVICE_LN_GC)
        {
          x_axis2 = (int8_t)read_byte_mcu();
          y_axis2 = (int8_t)read_byte_mcu();
          l_trigger = read_byte_mcu();
          r_trigger = read_byte_mcu();
        }

        interrupts();

        uint8_t dpad = ~buttons1 & 0x0F; // UDLR bits
        uint8_t encoded_type = 0;

        if ((dpad & 0x03) == 0x03 || (dpad & 0x0C) == 0x0C) {
          // If any dpad input exists, block menu detection
          if (last_dpad == 0) {
              // Only detect menu/encoded buttons if no dpad input
              if (dpad == 0x0F) { encoded_type = 1; } // Reset: all 4
              else if (dpad == 0x0C) { encoded_type = 2; } // Menu: up+down
              else if (dpad == 0x03) { encoded_type = 3; } // *: left+right
              else if (dpad == 0x0D) { encoded_type = 4; } // Select: up+down+right
              else if (dpad == 0x0B) { encoded_type = 5; } // Order: up+left+right
              else if (dpad == 0x0E) { encoded_type = 6; } // #: up+down+left
              if (last_menu == 0) {
                // we can accept a new menu button
                last_menu = encoded_type;
              } if (last_menu != encoded_type) {
                // can't change menu
                encoded_type = last_menu;
              }
          }
        } else {
            last_dpad = dpad;
            last_menu = 0;
        }



        Gamepad.releaseAll();

        static uint8_t hid_dpad = GAMEPAD_DPAD_CENTERED;
        switch(last_dpad){
          case 0x01: hid_dpad = GAMEPAD_DPAD_RIGHT; break;
          case 0x02: hid_dpad = GAMEPAD_DPAD_LEFT; break;
          case 0x04: hid_dpad = GAMEPAD_DPAD_DOWN; break;
          case 0x08: hid_dpad = GAMEPAD_DPAD_UP; break;
          case 0x05: hid_dpad = GAMEPAD_DPAD_DOWN_RIGHT; break;
          case 0x06: hid_dpad = GAMEPAD_DPAD_DOWN_LEFT; break;
          case 0x09: hid_dpad = GAMEPAD_DPAD_UP_RIGHT; break;
          case 0x0A: hid_dpad = GAMEPAD_DPAD_UP_LEFT; break;
          default:   hid_dpad = GAMEPAD_DPAD_CENTERED; break;
        }
        Gamepad.dPad1(hid_dpad);

        if (!((buttons1 >> 6) & 0x01)) Gamepad.press(1);
        if (!((buttons1 >> 7) & 0x01)) Gamepad.press(2);
        if (!((buttons1 >> 5) & 0x01)) Gamepad.press(5);
        if (!((buttons1 >> 4) & 0x01)) Gamepad.press(6);
        if (!((buttons2 >> 5) & 0x01)) Gamepad.press(7);
        if (!((buttons2 >> 4) & 0x01)) Gamepad.press(8);

        if (encoded_type == 5) Gamepad.press(12); // Order: up+left+right
        if (encoded_type == 1) Gamepad.press(13); // Reset: all 4
        if (encoded_type == 2) Gamepad.press(14); // Menu: up+down
        if (encoded_type == 6) Gamepad.press(15); // #: up+down+left
        if (encoded_type == 4) Gamepad.press(16); // Select: up+down+right
        if (encoded_type == 3) Gamepad.press(17); // *: left+right

        Gamepad.xAxis(0);
        Gamepad.yAxis(0);
        Gamepad.zAxis(0);
        Gamepad.rxAxis(0);
        Gamepad.ryAxis(0);
        Gamepad.rzAxis(0);

        if (device == DEVICE_LN_N64) {
          if (!((buttons2 >> 3) & 0x01)) Gamepad.press(9);
          if (!((buttons2 >> 2) & 0x01)) Gamepad.press(4);
          if (!((buttons2 >> 1) & 0x01)) Gamepad.press(3);
          if (!((buttons2 >> 0) & 0x01)) Gamepad.press(10);
          
          // N64: signed int8_t, range -128..127
          Gamepad.xAxis(x_axis * 256);
          Gamepad.yAxis(y_axis * -256);
        }
        if (device == DEVICE_LN_GC) {
          if (!((buttons2 >> 3) & 0x01)) Gamepad.press(3);
          if (!((buttons2 >> 2) & 0x01)) Gamepad.press(4);

          // GC: unsigned uint8_t, range 0..255, convert to signed
          Gamepad.xAxis(((int16_t)x_axis1 - 128) * 256);
          Gamepad.yAxis(((int16_t)y_axis1 - 128) * -256);

          Gamepad.rxAxis(((int16_t)x_axis2 - 128) * 256);
          Gamepad.ryAxis(((int16_t)y_axis2 - 128) * -256);

          Gamepad.zAxis(((int16_t)l_trigger - 128));
          Gamepad.rzAxis(((int16_t)r_trigger - 128));
        }

        Gamepad.write();

        delay(65);
      }
      break;
  }
}