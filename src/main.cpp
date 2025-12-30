#include <Arduino.h>
#include "PluggableUSB.h"
#include "HID.h"


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


#define MCU_PULSE_TIME_LOW 4
#define MCU_PULSE_TIME_HIGH 18

// Time-Skew correction for CLK1
#define TIME_SKEW 0
//#define TIME_SKEW 2

// PMOS gate logic: P-channel high-side, active LOW (gate LOW = power ON)
#define PWR_ACTIVE_LOW true

#define GOOD_READS 15



static uint8_t last_dpad = 0;
static uint8_t last_menu = 0;
static uint8_t good_reads = 0;




const uint8_t customHIDReportDescriptor[] PROGMEM = {
// Gamepad Input Report (Report ID 1)
0x05, 0x01,        // Usage Page (Generic Desktop)
0x09, 0x05,        // Usage (Gamepad)
0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,      //   Report ID (1)
  0x05, 0x09,      //   Usage Page (Button)
  0x19, 0x01,      //   Usage Minimum (Button 1)
  0x29, 0x11,      //   Usage Maximum (Button 17)
  0x15, 0x00,      //   Logical Minimum (0)
  0x25, 0x01,      //   Logical Maximum (1)
  0x95, 0x11,      //   Report Count (17)
  0x75, 0x01,      //   Report Size (1)
  0x81, 0x02,      //   Input (Data,Var,Abs)
  // Dpad (Hat) in upper nibble of 3rd button byte, pad to byte boundary
  0x05, 0x01,      //   Usage Page (Generic Desktop)
  0x09, 0x39,      //   Usage (Hat switch)
  0x15, 0x00,      //   Logical Minimum (0)
  0x25, 0x07,      //   Logical Maximum (7)
  0x35, 0x00,      //   Physical Minimum (0)
  0x46, 0x3B, 0x01,//   Physical Maximum (315)
  0x65, 0x14,      //   Unit (Eng Rot:Angular Pos)
  0x75, 0x04,      //   Report Size (4)
  0x95, 0x01,      //   Report Count (1)
  0x81, 0x42,      //   Input (Data,Var,Abs,Null)
  0x75, 0x03,      //   Report Size (3) - padding
  0x95, 0x01,      //   Report Count (1)
  0x81, 0x03,      //   Input (Const,Var,Abs)
  // Axes
  0x05, 0x01,      //   Usage Page (Generic Desktop)
  0x09, 0x30,      //   Usage (X)
  0x09, 0x31,      //   Usage (Y)
  0x09, 0x32,      //   Usage (Z)
  0x09, 0x33,      //   Usage (Rx)
  0x09, 0x34,      //   Usage (Ry)
  0x09, 0x35,      //   Usage (Rz)
  0x16, 0x00, 0x80,//   Logical Minimum (-32768)
  0x26, 0xFF, 0x7F,//   Logical Maximum (32767)
  0x75, 0x10,      //   Report Size (16)
  0x95, 0x06,      //   Report Count (6)
  0x81, 0x02,      //   Input (Data,Var,Abs)
// End Gamepad
0xC0,              // End Collection

// Vendor Feature Report (Report ID 2)
0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
0x09, 0x01,        // Usage (0x01)
0xA1, 0x01,        // Collection (Application)
  0x85, 0x02,      //   Report ID (2)
  0x09, 0x02,      //   Usage (0x02)
  0x15, 0x00,      //   Logical Minimum (0)
  0x26, 0xFF, 0x00,//   Logical Maximum (255)
  0x75, 0x08,      //   Report Size (8)
  0x95, 0x01,      //   Report Count (1)
  0xB1, 0x02,      //   Feature (Data,Var,Abs)
0xC0               // End Collection
};

class MyCustomHID_ : public PluggableUSBModule {
public:
  MyCustomHID_() :
    PluggableUSBModule(1, 1, epType), _endpointIn(0)
  {
    epType[0] = EP_TYPE_INTERRUPT_IN;
    PluggableUSB().plug(this);      // allocates pluggedInterface & pluggedEndpoint
    _endpointIn = pluggedEndpoint;  // now it's valid
  }

  int getInterface(uint8_t* interfaceCount) override {
    *interfaceCount += 1; // we use 1 interface

    HIDDescriptor hidInterface = {
      // InterfaceDescriptor
      {
        sizeof(InterfaceDescriptor), // len = 9
        0x04,                        // dtype = INTERFACE
        pluggedInterface,            // number = interface number assigned by core
        0x00,                        // alternate
        0x01,                        // numEndpoints
        0x03,                        // interfaceClass = HID
        0x00,                        // interfaceSubClass
        0x00,                        // protocol
        0x00                         // iInterface
      },
      // HIDDescDescriptor
      {
        sizeof(HIDDescDescriptor),   // len = 9
        0x21,                        // dtype = HID
        0x01,                        // addr = bNumDescriptors (we have 1)
        0x11,                        // versionL = low byte of bcdHID (0x0111)
        0x01,                        // versionH = high byte of bcdHID
        0x00,                        // country = 0 (not localized)
        0x22,                        // desctype = Report
        (uint8_t)sizeof(customHIDReportDescriptor),              // descLenL
        (uint8_t)(sizeof(customHIDReportDescriptor) >> 8)        // descLenH
      },
      // EndpointDescriptor
      {
        sizeof(EndpointDescriptor),       // len = 7
        0x05,                             // dtype = ENDPOINT
        USB_ENDPOINT_IN(_endpointIn),     // addr = IN | endpoint number
        0x03,                             // attr = Interrupt
        0x0010,                           // packetSize = 16 bytes
        0x0A                              // interval = 10 ms
      }
    };

    return USB_SendControl(0, &hidInterface, sizeof(hidInterface));
  }

  int getDescriptor(USBSetup& setup) override {
    // Only answer HID report descriptor requests for *our* interface
    if (setup.bmRequestType != REQUEST_DEVICETOHOST_STANDARD_INTERFACE) return 0;
    if (setup.wValueH != HID_REPORT_DESCRIPTOR_TYPE) return 0;
    if (setup.wIndex != pluggedInterface) return 0;

    return USB_SendControl(TRANSFER_PGM,
                           customHIDReportDescriptor,
                           sizeof(customHIDReportDescriptor));
  }

  bool setup(USBSetup& setup) override {
    if (setup.wIndex != pluggedInterface) {
      return false; // not for us
    }

    if (setup.bmRequestType == REQUEST_DEVICETOHOST_CLASS_INTERFACE &&
        setup.bRequest     == HID_GET_REPORT &&
        setup.wValueH      == HID_REPORT_TYPE_FEATURE &&
        setup.wValueL      == 2) // Report ID 2
    {
      uint8_t buffer[2];
      buffer[0] = 0x02; // Report ID 2
      buffer[1] = (good_reads < GOOD_READS) ? DEVICE_NONE : device;
      USB_SendControl(0, buffer, sizeof(buffer));
      return true;
    }

    if (setup.bmRequestType == REQUEST_HOSTTODEVICE_CLASS_INTERFACE &&
        setup.bRequest     == HID_SET_REPORT &&
        setup.wValueH      == HID_REPORT_TYPE_FEATURE &&
        setup.wValueL      == 2)
    {
      // Optional: read data if you want to consume host->device feature report
      // uint8_t buf[1]; USB_RecvControl(buf, 1);
      return true;
    }

    return false;
  }

  uint8_t getEndpoint() const { return _endpointIn; }

  // NEW: safe send wrapper
  int sendReport(const void* data, int len) {
    // Don't try to send before the host has configured USB
    if (!USBDevice.configured()) {
      return -1;
    }
    // Mirror Arduino's HID_::SendReport pattern: send whole report on our endpoint
    // with TRANSFER_RELEASE so the buffer is released when done.
    return USB_Send(_endpointIn | TRANSFER_RELEASE, data, len);
  }

private:
  uint8_t epType[1];
  uint8_t _endpointIn;
};

MyCustomHID_ MyCustomHID;




void print_bits_array(uint8_t value) {
  for (int i = 7; i >= 0; i--) {
    Serial.print((value & (1 << i)) ? '1' : '0');
  }
}




inline void clk1_high() {
  #if PWR_ACTIVE_LOW
    PORTD &= ~_BV(D_CLK1);
  #else
    PORTD |= _BV(D_CLK1);
  #endif
}

inline void clk1_low() {
  #if PWR_ACTIVE_LOW
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
  #if PWR_ACTIVE_LOW
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
  uint8_t value = 0;
  for(int j=0;j<8;j++){
    clk1_low();
    delayMicroseconds(MCU_PULSE_TIME_LOW + TIME_SKEW);
    value = (value << 1) | ((PIND & _BV(D_DATA)) ? 1 : 0);
    clk1_high();
    delayMicroseconds(MCU_PULSE_TIME_HIGH - TIME_SKEW);
  }
  return value;
}

void EmitActiveController() {
  // Nothing needed here for now; feature report is handled in setup()
}

void setup() {
  Serial.begin(115200);

  // Optional: wait up to 2 seconds for a host to grab the port
  unsigned long start = millis();
  while (!Serial && (millis() - start < 2000)) {
    digitalWrite(17, HIGH);
    delay(500);
    digitalWrite(17, LOW);
    delay(500);
  }

  Serial.println(F("LodgeNet USB test host starting..."));

  DDRD |= _BV(D_CLK1) | _BV(D_CLK2);
  clk1_high();
  clk2_high();

  Serial.println(F("LodgeNet USB test host starting..."));
}

void loop() {
  static uint8_t report[16] = {0};
  switch(proto)
  {
    case MODE_LN_NONE:
      proto = MODE_LN_SR; // switch to SR mode for testing
      last_dpad = 0;
      last_menu = 0;
      good_reads = 0;

      EmitActiveController();

      // Send neutral gamepad report (all released, axes centered)
      memset(report, 0, sizeof(report));

      report[0] = 0x01; // Report ID 1
      report[3] = 0x08 << 1; // Dpad centered (0x08)

      MyCustomHID.sendReport(report, sizeof(report));

      Serial.print("NONE");
      for(int i=0;i<sizeof(report);i++) {
        Serial.print(" ");
        print_bits_array(report[i]);
      }
      Serial.println();

      //Serial.println("No SR controller detected checking for MCU next.");

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
          //clk2_high();
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
          good_reads = 0;

          Serial.println("No SR controller detected checking for MCU next.");
          // No controller present, skip processing
          delay(60); // wait before next read
          break;
        }

        device = DEVICE_LN_SNES;
        if (good_reads < GOOD_READS)
        {
          good_reads++;
          if (good_reads == GOOD_READS) {
            EmitActiveController();
          }else{
            break; // don't count this read
          }
        }

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

        //Serial.print("SNES");
        //Serial.print("   Menu: "        + String(((value >> 15) & 0x01) ? 0 : 1));
        //Serial.print("   Reset/Order: " + String(((value >> 14) & 0x01) ? 0 : 1));
        //Serial.print("   -: "           + String(ButtonMinus ? 1 : 0));
        //Serial.print("   +: "           + String(ButtonPlus  ? 1 : 0));
        //Serial.print("   B: "           + String(((value >> 13) & 0x01) ? 0 : 1));
        //Serial.print("   Y: "           + String(((value >> 12) & 0x01) ? 0 : 1));
        //Serial.print("   Select: "      + String(((value >> 11) & 0x01) ? 0 : 1));
        //Serial.print("   Start/*: "     + String(((value >> 10) & 0x01) ? 0 : 1));
        //Serial.print("   Up: "          + String((last_dpad & 0x08) ? 1 : 0));
        //Serial.print("   Down: "        + String((last_dpad & 0x04) ? 1 : 0));
        //Serial.print("   Left: "        + String((last_dpad & 0x02) ? 1 : 0));
        //Serial.print("   Right: "       + String((last_dpad & 0x01) ? 1 : 0));
        //Serial.print("   A: "           + String(((value >>  3) & 0x01) ? 0 : 1));
        //Serial.print("   X: "           + String(((value >>  2) & 0x01) ? 0 : 1));
        //Serial.print("   L: "           + String(((value >>  1) & 0x01) ? 0 : 1));
        //Serial.print("   R: "           + String(((value >>  0) & 0x01) ? 0 : 1));
        //Serial.println();

        // Build and send custom gamepad report
        memset(report, 0, sizeof(report));
        report[0] = 0x01; // Report ID 1
        uint32_t buttons = 0;
        // Map SNES buttons to report bits
        if (!((value >> 13) & 0x01)) buttons |= 0x00000001; // B
        if (!((value >>  3) & 0x01)) buttons |= 0x00000002; // A
        if (!((value >> 12) & 0x01)) buttons |= 0x00000004; // Y
        if (!((value >>  2) & 0x01)) buttons |= 0x00000008; // X
        if (!((value >> 11) & 0x01)) buttons |= 0x00000010; // Select
        if (!((value >> 10) & 0x01)) buttons |= 0x00000020; // Start/*
        if (!((value >>  1) & 0x01)) buttons |= 0x00000040; // L
        if (!((value >>  0) & 0x01)) buttons |= 0x00000080; // R
        if (!((value >> 14) & 0x01)) buttons |= 0x00000800; // Reset/Order
        if (ButtonPlus)              buttons |= 0x00002000; // Plus
        if (!((value >> 15) & 0x01)) buttons |= 0x00004000; // Menu
        if (ButtonMinus)             buttons |= 0x00010000; // Minus


        // Dpad bits: 0=Right, 1=Left, 2=Down, 3=Up
        // Map to HID hat switch (0=Up, 1=Up-Right, ..., 7=Up-Left, 8=Centered)
        uint8_t hid_hat = 8;
        switch (last_dpad) {
          case 0x08: hid_hat = 0; break; // Up
          case 0x09: hid_hat = 1; break; // Up-Right
          case 0x01: hid_hat = 2; break; // Right
          case 0x05: hid_hat = 3; break; // Down-Right
          case 0x04: hid_hat = 4; break; // Down
          case 0x06: hid_hat = 5; break; // Down-Left
          case 0x02: hid_hat = 6; break; // Left
          case 0x0A: hid_hat = 7; break; // Up-Left
          default:   hid_hat = 8; break; // Centered/neutral
        }

        // Pack 17 button bits and dpad (hat) into 3 bytes, pad upper 3 bits
        report[1] = buttons & 0xFF;            // buttons 0-7
        report[2] = (buttons >> 8) & 0xFF;     // buttons 8-15
        report[3] = ((buttons >> 16) & 0x01)   // button 16 (bit 0)
              | ((hid_hat & 0x0F) << 1) // dpad (bits 1-4)
              | (0x07 << 5);            // pad bits 5-7 set to 1 (or 0 if you prefer)
        // Axes: all zero for SNES
        for (int i = 0; i < 6; ++i) {
          report[4 + i * 2] = 0x00;
          report[5 + i * 2] = 0x00;
        }
        MyCustomHID.sendReport(report, sizeof(report));

        Serial.print("SR  ");
        for(int i=0;i<sizeof(report);i++) {
          Serial.print(" ");
          print_bits_array(report[i]);
        }
        Serial.println();

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
          good_reads = 0;
          // include bits of buttons2 for debugging
          Serial.print("No MCU controller detected checking for NONE next b");
          print_bits_array(buttons1);
          Serial.print(" b");
          print_bits_array(buttons2);
          Serial.println();
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
          good_reads = 0;
          Serial.print("No MCU controller detected checking for NONE next b");
          print_bits_array(buttons1);
          Serial.print(" b");
          print_bits_array(buttons2);
          Serial.println();
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



        if (good_reads < GOOD_READS)
        {
          good_reads++;
          if (good_reads == GOOD_READS) {
            EmitActiveController();
          }else{
            delay(65); // wait before next read
            break; // don't count this read
          }
        }



        //switch(device){
        //  case DEVICE_LN_N64:
        //    Serial.print("N64 ");
        //    break;
        //  case DEVICE_LN_GC:
        //    Serial.print("GC  ");
        //    break;
        //}
        ////print_bits_array(&buttons1);
        //Serial.print(" ");
        ////print_bits_array(&buttons2);
//
        //Serial.print("   Reset: "       + String((encoded_type == 1) ? 1 : 0)); // Reset: all 4
        //Serial.print("   Menu: "        + String((encoded_type == 2) ? 1 : 0)); // Menu: up+down
        //Serial.print("   *: "           + String((encoded_type == 3) ? 1 : 0)); // *: left+right
        //Serial.print("   Select: "      + String((encoded_type == 4) ? 1 : 0)); // Select: up+down+right
        //Serial.print("   Order: "       + String((encoded_type == 5) ? 1 : 0)); // Order: up+left+right
        //Serial.print("   #: "           + String((encoded_type == 6) ? 1 : 0)); // #: up+down+left
////
        //Serial.print("   A: "           + String(((buttons1 >> 7) & 0x01) ? 0 : 1));
        //Serial.print("   B: "           + String(((buttons1 >> 6) & 0x01) ? 0 : 1));
        //Serial.print("   Z: "           + String(((buttons1 >> 5) & 0x01) ? 0 : 1));
        //Serial.print("   Start: "       + String(((buttons1 >> 4) & 0x01) ? 0 : 1));
        //Serial.print("   Up: "          + String((last_dpad & 0x08) ? 1 : 0));
        //Serial.print("   Down: "        + String((last_dpad & 0x04) ? 1 : 0));
        //Serial.print("   Left: "        + String((last_dpad & 0x02) ? 1 : 0));
        //Serial.print("   Right: "       + String((last_dpad & 0x01) ? 1 : 0));
        //Serial.print("   L: "           + String(((buttons2 >> 5) & 0x01) ? 0 : 1));
        //Serial.print("   R: "           + String(((buttons2 >> 4) & 0x01) ? 0 : 1));
        //
        //switch(device){
        //  case DEVICE_LN_N64:
        //    Serial.print("   C-Up: "        + String(((buttons2 >> 3) & 0x01) ? 0 : 1));
        //    Serial.print("   C-Down: "      + String(((buttons2 >> 2) & 0x01) ? 0 : 1));
        //    Serial.print("   C-Left: "      + String(((buttons2 >> 1) & 0x01) ? 0 : 1));
        //    Serial.print("   C-Right: "     + String(((buttons2 >> 0) & 0x01) ? 0 : 1));
        //    break;
        //  case DEVICE_LN_GC:
        //    Serial.print("   Y: "           + String(((buttons2 >> 3) & 0x01) ? 0 : 1));
        //    Serial.print("   X: "           + String(((buttons2 >> 2) & 0x01) ? 0 : 1));
        //    break;
        //}


        // Dpad bits: 0=Right, 1=Left, 2=Down, 3=Up
        // Map to HID hat switch (0=Up, 1=Up-Right, ..., 7=Up-Left, 8=Centered)
        uint8_t hid_hat = 8;
        switch (last_dpad) {
          case 0x08: hid_hat = 0; break; // Up
          case 0x09: hid_hat = 1; break; // Up-Right
          case 0x01: hid_hat = 2; break; // Right
          case 0x05: hid_hat = 3; break; // Down-Right
          case 0x04: hid_hat = 4; break; // Down
          case 0x06: hid_hat = 5; break; // Down-Left
          case 0x02: hid_hat = 6; break; // Left
          case 0x0A: hid_hat = 7; break; // Up-Left
          default:   hid_hat = 8; break; // Centered/neutral
        }

        // Build and send custom gamepad report for MCU devices
        memset(report, 0, sizeof(report));
        report[0] = 0x01; // Report ID 1
        uint32_t buttons = 0;
        // Map buttons for N64/GC
        if (!((buttons1 >> 6) & 0x01)) buttons |= 0x00000001;  // B
        if (!((buttons1 >> 7) & 0x01)) buttons |= 0x00000002;  // A
        if (!((buttons1 >> 5) & 0x01)) buttons |= 0x00000010;  // Z
        if (!((buttons1 >> 4) & 0x01)) buttons |= 0x00000020;  // Start
        if (!((buttons2 >> 5) & 0x01)) buttons |= 0x00000040;  // L
        if (!((buttons2 >> 4) & 0x01)) buttons |= 0x00000080;  // R

        if (encoded_type == 5) buttons |= 0x00000800; // Order
        if (encoded_type == 1) buttons |= 0x00001000; // Reset
        if (encoded_type == 2) buttons |= 0x00002000; // Menu
        if (encoded_type == 6) buttons |= 0x00004000; // #
        if (encoded_type == 4) buttons |= 0x00008000; // Select
        if (encoded_type == 3) buttons |= 0x00010000; // *

        //char buf[6];
        if (device == DEVICE_LN_N64) {
          //sprintf(buf, "%4d", x_axis);
          //Serial.print(" X: "); Serial.print(buf);
          //sprintf(buf, "%4d", y_axis);
          //Serial.print(" Y: "); Serial.print(buf);

          if (!((buttons2 >> 3) & 0x01)) buttons |= 0x00000004; // C-Up
          if (!((buttons2 >> 2) & 0x01)) buttons |= 0x00000008; // C-Down
          if (!((buttons2 >> 1) & 0x01)) buttons |= 0x00000100; // C-Left
          if (!((buttons2 >> 0) & 0x01)) buttons |= 0x00000200; // C-Right
          // N64: signed int8_t, range -128..127
          int16_t x = x_axis * 256;
          int16_t y = y_axis * -256;
          report[1] = buttons & 0xFF;
          report[2] = (buttons >> 8) & 0xFF;
          report[3] = ((buttons >> 16) & 0x01) | ((hid_hat & 0x0F) << 1) | (0x07 << 5);
          report[4] = x & 0xFF;
          report[5] = (x >> 8) & 0xFF;
          report[6] = y & 0xFF;
          report[7] = (y >> 8) & 0xFF;
          // N64 only uses X/Y axes, rest zero
          for (int i = 2; i < 6; ++i) {
            report[4 + i * 2] = 0x00;
            report[5 + i * 2] = 0x00;
          }
        }
        if (device == DEVICE_LN_GC) {
          //sprintf(buf, "%3d", x_axis1);
          //Serial.print(" X1: "); Serial.print(buf);
          //sprintf(buf, "%3d", y_axis1);
          //Serial.print(" Y1: "); Serial.print(buf);
//
          //sprintf(buf, "%3d", x_axis2);
          //Serial.print(" X2: "); Serial.print(buf);
          //sprintf(buf, "%3d", y_axis2);
          //Serial.print(" Y2: "); Serial.print(buf);
          ////
          //sprintf(buf, "%3d", l_trigger);
          //Serial.print(" L: "); Serial.print(buf);
          //sprintf(buf, "%3d", r_trigger);
          //Serial.print(" R: "); Serial.print(buf);

          if (!((buttons2 >> 3) & 0x01)) buttons |= 0x00000004; // X
          if (!((buttons2 >> 2) & 0x01)) buttons |= 0x00000008; // Y
          // GC: unsigned uint8_t, range 0..255, convert to signed
          int16_t x = ((int16_t)x_axis1 - 128) * 256;
          int16_t y = ((int16_t)y_axis1 - 128) * -256;
          int16_t rx = ((int16_t)x_axis2 - 128) * 256;
          int16_t ry = ((int16_t)y_axis2 - 128) * -256;
          int16_t z = ((int16_t)l_trigger - 128);
          int16_t rz = ((int16_t)r_trigger - 128);
          report[1] = buttons & 0xFF;
          report[2] = (buttons >> 8) & 0xFF;
          report[3] = ((buttons >> 16) & 0x01) | ((hid_hat & 0x0F) << 1) | (0x07 << 5);
          report[4] = x & 0xFF;
          report[5] = (x >> 8) & 0xFF;
          report[6] = y & 0xFF;
          report[7] = (y >> 8) & 0xFF;
          report[8] = rx & 0xFF;
          report[9] = (rx >> 8) & 0xFF;
          report[10] = ry & 0xFF;
          report[11] = (ry >> 8) & 0xFF;
          report[12] = z & 0xFF;
          report[13] = (z >> 8) & 0xFF;
          report[14] = rz & 0xFF;
          report[15] = 0x00;
        }
        MyCustomHID.sendReport(report, sizeof(report));

        //Serial.println();

        Serial.print("MCU ");
        for(int i=0;i<sizeof(report);i++) {
          Serial.print(" ");
          print_bits_array(report[i]);
        }
        Serial.println();

        delay(65);
      }
      break;
  }
}