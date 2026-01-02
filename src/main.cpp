// Created by John "Nielk1" Klein

#include <Arduino.h>
#include "PluggableUSB.h"
#include "HID.h"

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
const uint8_t D_CLK1 = 0; // [OUT] Main Clock, Gate Driver
const uint8_t D_CLK2 = 1; // [OUT] Second Clock, SNES Only
const uint8_t D_DATA = 4; // [IN] Data

// bInterval = 1 → host polls every 1 ms → max 1000 reports/s
// bInterval = 2 → every 2 ms → 500 Hz
// bInterval = 8 → every 8 ms → 125 Hz
// bInterval = 10 (0x0A) → every 10 ms → 100 Hz
//#define USB_HID_INTERVAL 0x0A
#define USB_HID_INTERVAL 0x02

#define LOG_TO_SERIAL true

#define MCU_HELLO_PULSE 7
//#define MCU_PULSE_TIME_LOW 4
//#define MCU_PULSE_TIME_HIGH 4
#define MCU_PULSE_TIME_LOW 6 // if this is too low analog reads become unstable
#define MCU_PULSE_TIME_HIGH 30
//#define MCU_PULSE_TIME_HIGH 6

#define SR_PULSE_TIME_LOW 3
#define SR_PULSE_TIME_HIGH 20

// Time-Skew correction for CLK1
#define TIME_SKEW 0
//#define TIME_SKEW 2

// PMOS gate logic: P-channel high-side, active LOW (gate LOW = power ON)
#define PWR_ACTIVE_LOW true

#define GOOD_READS_SR 15 // number of consecutive good reads to confirm presence
#define GOOD_READS_MCU 15 // number of consecutive good reads to confirm presence
#define BAD_READS_MCU 5 // number of consecutive bad reads to confirm failure


#define STICK_CENTER 0x80

#define DPAD_UP 0
#define DPAD_UP_RIGHT 1
#define DPAD_RIGHT 2
#define DPAD_DOWN_RIGHT 3
#define DPAD_DOWN 4
#define DPAD_DOWN_LEFT 5
#define DPAD_LEFT 6
#define DPAD_UP_LEFT 7
#define DPAD_CENTER 8

#define CLEAR_STATE last_dpad = 0;\
last_menu = 0;\
good_reads = 0;\
mcu_fail_count = 0;

#define SLEEP_NONE delay(1000); // Sleep between polls whe no controller set
#define SLEEP_SR delay(1); // Sleep between polls when using Shift Register based controller
#define SLEEP_MCU delay(16); // Sleep between polls when using Microcontroller based controller
#define SLEEP_BETWEEN_PROTOCOLS delay(1); // Sleep between protocols


enum ProtocolMode {
    MODE_LN_NONE, // None
    MODE_LN_SR, // Shift Register
    MODE_LN_MCU, // Microcontroller
};

enum DeviceType {
    DEVICE_NONE,
    DEVICE_LN_SNES,
    DEVICE_LN_N64,
    DEVICE_LN_GC,
};

// default protocol
ProtocolMode proto = MODE_LN_NONE;

static uint8_t last_dpad = 0;
static uint8_t last_menu = 0;
static uint8_t good_reads = 0;
static uint8_t mcu_fail_count = 0;





void print_bits_array(uint16_t value) {
    //for (int i = 7; i >= 0; i--) {
    for (int i = 0; i < 16; i++) {
        Serial.print((value & (1 << i)) ? '1' : '0');
    }
}

void print_dpad_direction(uint8_t hat) {

    switch (hat) {
        case 0: Serial.print("↑"); break;
        case 1: Serial.print("↗"); break;
        case 2: Serial.print("→"); break;
        case 3: Serial.print("↘"); break;
        case 4: Serial.print("↓"); break;
        case 5: Serial.print("↙"); break;
        case 6: Serial.print("←"); break;
        case 7: Serial.print("↖"); break;
        default: Serial.print("·"); break;
    }
    //switch (hat) {
    //    case 0: Serial.print(" ^ "); break;
    //    case 1: Serial.print(" /^"); break;
    //    case 2: Serial.print(" ->"); break;
    //    case 3: Serial.print(" \\v"); break;
    //    case 4: Serial.print(" v "); break;
    //    case 5: Serial.print("v/ "); break;
    //    case 6: Serial.print("<- "); break;
    //    case 7: Serial.print("^\\ "); break;
    //    default: Serial.print(" - "); break;
    //}
}

void print_stick_direction(uint8_t x, uint8_t y) {
    if (x < 85) { // left
        if (y < 85) { // up
            Serial.print("↖");
        } else if (y > 170) { // down
            Serial.print("↙");
        } else {
            Serial.print("←");
        }
    } else if (x > 170) { // right
        if (y < 85) { // up
            Serial.print("↗");
        } else if (y > 170) { // down
            Serial.print("↘");
        } else {
            Serial.print("→");
        }
    } else { // center x
        if (y < 85) { // up
            Serial.print("↑");
        } else if (y > 170) { // down
            Serial.print("↓");
        } else {
            Serial.print("·");
        }
    }
}

void print_trigger(uint8_t value) {
    if (value > 200) {
        Serial.print("█");
    } else if (value > 150) {
        Serial.print("▓");
    } else if (value > 100) {
        Serial.print("▒");
    } else if (value > 50) {
        Serial.print("░");
    } else {
        Serial.print("·");
    }
}

const uint8_t customHIDReportDescriptor[] PROGMEM = {
// Gamepad Input Report (Report ID 1)
0x05, 0x01,        // Usage Page (Generic Desktop)
0x09, 0x05,        // Usage (Gamepad)
0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,      //   Report ID (1)

    // Buttons
    0x05, 0x09,      //   Usage Page (Button)
    0x19, 0x01,      //   Usage Minimum (Button 1)
    0x29, 0x10,      //   Usage Maximum (Button 16)
    0x15, 0x00,      //   Logical Minimum (0)
    0x25, 0x01,      //   Logical Maximum (1)
    0x95, 0x10,      //   Report Count (16)
    0x75, 0x01,      //   Report Size (1)
    0x81, 0x02,      //   Input (Data,Var,Abs)
    
    // Hat
    0x05, 0x01,      //   Usage Page (Generic Desktop)
    0x09, 0x39,      //   Usage (Hat switch)
    0x15, 0x00,      //   Logical Minimum (0)
    0x25, 0x07,      //   Logical Maximum (7)
    0x35, 0x00,      //   Physical Minimum (0)
    0x46, 0x3B, 0x01,//   Physical Maximum (315)
    0x65, 0x14,      //   Unit (Eng Rot:Angular Pos)
    0x75, 0x04,      //   Report Size (4)
    0x95, 0x01,      //   Report Count (1)
    0x81, 0x02,      //   Input (Data,Var,Abs)

    // Padding
    //0x75, 0x04,      //   Report Size (4)
    //0x95, 0x01,      //   Report Count (1)
    //0x81, 0x03,      //   Input (Const,Var,Abs) // padding to next byte

    // Controller Type
    0x06, 0x00, 0xFF,//   Usage Page (Vendor-defined 0xFF00)
    0x09, 0x01,      //   Usage (0x01) -- arbitrary tag for this nibble
    0x15, 0x00,      //   Logical Min (0)
    0x25, 0x0F,      //   Logical Max (15)
    0x75, 0x04,      //   Report Size (4 bits)
    0x95, 0x01,      //   Report Count (1)
    0x81, 0x02,      //   Input (Data,Var,Abs)

    // Axes
    0x05, 0x01,      //   Usage Page (Generic Desktop)
    0x09, 0x30,      //   Usage (X)
    0x09, 0x31,      //   Usage (Y)
    0x09, 0x33,      //   Usage (Rx)
    0x09, 0x34,      //   Usage (Ry)
    0x15, 0x00,      //   Logical Minimum (0)
    0x26, 0xFF, 0x00,//   Logical Maximum (255)
    0x75, 0x08,      //   Report Size (8)
    0x95, 0x04,      //   Report Count (4)
    0x81, 0x02,      //   Input (Data,Var,Abs)

    // Triggers
    0x05, 0x01,      //   Usage Page (Generic Desktop)
    0x09, 0x36,      //   Slider (left trigger)
    0x09, 0x36,      //   Slider (right trigger)
    0x15, 0x00,      //   Logical Minimum (0)
    0x26, 0xFF, 0x00,//   Logical Maximum (255)
    0x75, 0x08,      //   Report Size (8)
    0x95, 0x02,      //   Report Count (2)
    0x81, 0x02,      //   Input (Data,Var,Abs)

    // --- Vendor Feature Report (ID 2) inside the gamepad collection ---
    //0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
    //0x09, 0x01,        //   Usage (0x01)
    //0xA1, 0x02,        //   Collection (Logical)   ; optional wrapper
    //    0x85, 0x02,      //   Report ID (2)
    //    0x09, 0x02,      //   Usage (0x02)
    //    0x15, 0x00,      //   Logical Minimum (0)
    //    0x26, 0xFF, 0x00,//   Logical Maximum (255)
    //    0x75, 0x08,      //   Report Size (8)
    //    0x95, 0x01,      //   Report Count (1)
    //    0xB1, 0x02,      //   Feature (Data,Var,Abs)
    //0xC0,              //   End Collection (Logical)

// End Gamepad
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
                sizeof(InterfaceDescriptor),
                0x04,
                pluggedInterface,
                0x00,
                0x01,
                0x03,  // HID
                0x00,  // subclass
                0x00,  // protocol
                0x00
            },
            // HIDDescDescriptor – use the same byte pattern as D_HIDREPORT
            {
                9,                                // bLength
                0x21,                             // bDescriptorType = HID
                0x01,                             // bcdHID LSB = 0x0101 (HID v1.01) or change to 0x11 for v1.11
                0x01,                             // bcdHID MSB
                0x00,                             // bCountryCode
                0x01,                             // bNumDescriptors
                0x22,                             // bDescriptorType (Report)
                (uint8_t)sizeof(customHIDReportDescriptor),
                (uint8_t)(sizeof(customHIDReportDescriptor) >> 8)
            },
            // EndpointDescriptor
            {
                sizeof(EndpointDescriptor),
                0x05,
                USB_ENDPOINT_IN(_endpointIn),
                0x03,
                0x0010,
                USB_HID_INTERVAL
            }
        };

        return USB_SendControl(0, &hidInterface, sizeof(hidInterface));
    }

    int getDescriptor(USBSetup& setup) override {
        // Only answer HID report descriptor requests for *our* interface
        if (setup.bmRequestType != REQUEST_DEVICETOHOST_STANDARD_INTERFACE) return 0;
        if (setup.wValueH != HID_REPORT_DESCRIPTOR_TYPE) return 0;
        if (setup.wIndex != pluggedInterface) return 0;

        return USB_SendControl(TRANSFER_PGM, customHIDReportDescriptor, sizeof(customHIDReportDescriptor));
    }

    bool setup(USBSetup& setup) override {
        if (setup.wIndex != pluggedInterface) {
            return false; // not for us
        }

        if (setup.bmRequestType == REQUEST_DEVICETOHOST_CLASS_INTERFACE &&
            setup.bRequest      == HID_GET_REPORT &&
            setup.wValueH       == HID_REPORT_TYPE_FEATURE &&
            setup.wValueL       == 2) // Report ID 2
        {
            uint8_t buffer[2];
            buffer[0] = 0x02; // Report ID 2
            //buffer[1] = (good_reads < GOOD_READS_SR) ? DEVICE_NONE : device;
            USB_SendControl(0, buffer, sizeof(buffer));
            return true;
        }

        if (setup.bmRequestType == REQUEST_HOSTTODEVICE_CLASS_INTERFACE &&
            setup.bRequest      == HID_SET_REPORT &&
            setup.wValueH       == HID_REPORT_TYPE_FEATURE &&
            setup.wValueL       == 2)
        {
            // Optional: read data if you want to consume host->device feature report
            // uint8_t buf[1]; USB_RecvControl(buf, 1);
            return true;
        }

        return false;
    }

    //uint8_t getEndpoint() const { return _endpointIn; }

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

    int sendState(ProtocolMode proto, DeviceType device, uint16_t buttons, uint8_t hat, uint8_t x, uint8_t y, uint8_t rx, uint8_t ry, uint8_t l_trigger, uint8_t r_trigger) {
        uint8_t report[10];
        report[0] = 0x01; // Report ID 1
        report[1] = buttons & 0xFF;
        report[2] = (buttons >> 8) & 0xFF;
        report[3] = (hat & 0x0F) | ((device << 4) & 0x30) | ((proto << 6) & 0xC0);
        report[4] = x;
        report[5] = y;
        report[6] = rx;
        report[7] = ry;
        report[8] = l_trigger;
        report[9] = r_trigger;

#if LOG_TO_SERIAL
        Serial.print("State: ");

        switch(proto) {
            case MODE_LN_NONE: Serial.print("NONE"); break;
            case MODE_LN_SR:   Serial.print("SR  "); break;
            case MODE_LN_MCU:  Serial.print("MCU "); break;
            default:           Serial.print("UNK "); break;
        }
        Serial.print(" ");
        switch(device) {
            case DEVICE_NONE:    Serial.print("NONE "); break;
            case DEVICE_LN_GC:   Serial.print("GC   "); break;
            case DEVICE_LN_N64:  Serial.print("N64  "); break;
            case DEVICE_LN_SNES: Serial.print("SNES "); break;
            default:             Serial.print("UNK  "); break;
        }

        print_bits_array(buttons);
        Serial.print(" ");
        print_dpad_direction(hat);

        static char buf[5];

        Serial.print("  "); snprintf(buf, sizeof(buf), "%3d", x); Serial.print(buf);
        Serial.print(" "); snprintf(buf, sizeof(buf), "%3d", y); Serial.print(buf);
        Serial.print(" "); print_stick_direction(x, y);

        Serial.print("  "); snprintf(buf, sizeof(buf), "%3d", rx); Serial.print(buf);
        Serial.print(" "); snprintf(buf, sizeof(buf), "%3d", ry); Serial.print(buf);
        Serial.print(" "); print_stick_direction(rx, ry);

        Serial.print("  "); snprintf(buf, sizeof(buf), "%3d", l_trigger); Serial.print(buf);
        Serial.print(" "); print_trigger(l_trigger);
        Serial.print(" "); snprintf(buf, sizeof(buf), "%3d", r_trigger); Serial.print(buf);
        Serial.print(" "); print_trigger(r_trigger);

        Serial.println();
#endif

        return sendReport(report, sizeof(report));
    }

private:
    uint8_t epType[1];
    uint8_t _endpointIn;
};

MyCustomHID_ MyCustomHID;





//////////////////////////////////////////////////////////
// Pin Manipulation Inline Functions
//////////////////////////////////////////////////////////

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

/// Pulse the CLK1 line low and set the CLK2 line up
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

    delayMicroseconds(SR_PULSE_TIME_LOW + TIME_SKEW);

    // bring latch back up
    clk1_high();

    delayMicroseconds(SR_PULSE_TIME_HIGH - TIME_SKEW);

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
    pulse(MCU_HELLO_PULSE);
    delayMicroseconds(MCU_HELLO_PULSE - TIME_SKEW);
    pulse(MCU_HELLO_PULSE);
    delayMicroseconds(20 + MCU_HELLO_PULSE - TIME_SKEW);
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
        //delayMicroseconds(17 - MCU_PULSE_TIME_HIGH);
    }
    //delayMicroseconds(30 - 17 - MCU_PULSE_TIME_HIGH);
    return value;
}

//////////////////////////////////////////////////////////
// Controller Logic
//////////////////////////////////////////////////////////

void process_none_controller() {
    CLEAR_STATE

    // Send neutral gamepad report (all released, axes centered)
    MyCustomHID.sendState(proto, DEVICE_NONE, 0x0000, 0x08, STICK_CENTER, STICK_CENTER, STICK_CENTER, STICK_CENTER, 0, 0);

    SLEEP_NONE
    proto = MODE_LN_SR; // switch to SR mode for testing
}

void process_sr_controller() {
    uint16_t value = 0x0000;

    noInterrupts();
    for (int i = 0; i < 16; i++) {
        pulse_snes();
        clk2_low();
        delayMicroseconds(20);
        value = (value << 1) | ((PIND & _BV(D_DATA)) ? 1 : 0);
    }
    pulse_snes();
    delayMicroseconds(20);
    interrupts();

    if (PIND & _BV(D_DATA)) {
        // controller is not present so try as an MCU type next
        //MyCustomHID.sendState(proto, DEVICE_NONE, 0x0000, DPAD_CENTER, STICK_CENTER, STICK_CENTER, STICK_CENTER, STICK_CENTER, 0, 0);
        proto = MODE_LN_MCU;
        CLEAR_STATE
        SLEEP_BETWEEN_PROTOCOLS
        return;
    }

    if (good_reads < GOOD_READS_SR) {
        // not enough good reads yet to confirm presence
        ++good_reads;
        MyCustomHID.sendState(proto, DEVICE_NONE, 0x0000, DPAD_CENTER, STICK_CENTER, STICK_CENTER, STICK_CENTER, STICK_CENTER, 0, 0);
        SLEEP_SR
        return;
    }

    // dpad bits: 0=Right, 1=Left, 2=Down, 3=Up
    uint8_t dpad = (~value >> 4) & 0x03;
    dpad |= (~value >> 6) & 0x0C;

    // Extract axes
    uint8_t ButtonPlus = (dpad & 0x03) == 0x03; // Left/Right bits
    uint8_t ButtonMinus = (dpad & 0x0C) == 0x0C; // Up/Down bits

    // If both L+R pressed, preserve last L/R
    if ((dpad & 0x03) == 0x03)
        dpad = (dpad & ~0x03) | (last_dpad & 0x03);

    // If both U+D pressed, preserve last U/D
    if ((dpad & 0x0C) == 0x0C)
        dpad = (dpad & ~0x0C) | (last_dpad & 0x0C);

    // Save last dpad state
    last_dpad = dpad;

    uint16_t buttons = 0;

    // Map SNES buttons to report bits
    if (!(value & 0x2000)) buttons |= 0x0001; // B
    if (!(value & 0x0008)) buttons |= 0x0002; // A
    if (!(value & 0x1000)) buttons |= 0x0004; // Y
    if (!(value & 0x0004)) buttons |= 0x0008; // X
    if (!(value & 0x0800)) buttons |= 0x0010; // Select
    if (!(value & 0x0400)) buttons |= 0x0020; // Start/*
    if (!(value & 0x0002)) buttons |= 0x0040; // L
    if (!(value & 0x0001)) buttons |= 0x0080; // R
    if (!(value & 0x4000)) buttons |= 0x0400; // Reset/Order
    if (ButtonPlus)        buttons |= 0x1000; // Plus
    if (!(value & 0x8000)) buttons |= 0x2000; // Menu
    if (ButtonMinus)       buttons |= 0x8000; // Minus

    // Dpad bits: 0=Right, 1=Left, 2=Down, 3=Up
    // Map to HID hat switch (0=Up, 1=Up-Right, ..., 7=Up-Left, 8=Centered)
    uint8_t hid_hat = 8;
    switch (dpad) {
        case 0x08: hid_hat = DPAD_UP;         break; // Up
        case 0x09: hid_hat = DPAD_UP_RIGHT;   break; // Up-Right
        case 0x01: hid_hat = DPAD_RIGHT;      break; // Right
        case 0x05: hid_hat = DPAD_DOWN_RIGHT; break; // Down-Right
        case 0x04: hid_hat = DPAD_DOWN;       break; // Down
        case 0x06: hid_hat = DPAD_DOWN_LEFT;  break; // Down-Left
        case 0x02: hid_hat = DPAD_LEFT;       break; // Left
        case 0x0A: hid_hat = DPAD_UP_LEFT;    break; // Up-Left
        default:   hid_hat = DPAD_CENTER;     break; // Centered/neutral
    }

    MyCustomHID.sendState(proto, DEVICE_LN_SNES, buttons, hid_hat, STICK_CENTER, STICK_CENTER, STICK_CENTER, STICK_CENTER, 0, 0);

    SLEEP_SR
}

void process_mcu_controller() {
    bool mcu_read_success = true;
    uint8_t buttons1 = 0, buttons2 = 0;
    bool has_MCU, has_GC, has_forced_fail = false;

    noInterrupts();
    clock_mpu_hello();
    buttons1 = read_byte_mcu();
    buttons2 = read_byte_mcu();
    uint8_t analog1 = read_byte_mcu();
    uint8_t analog2 = read_byte_mcu();
    uint8_t analog3 = read_byte_mcu();
    uint8_t analog4 = read_byte_mcu();
    uint8_t analog5 = read_byte_mcu();
    uint8_t analog6 = read_byte_mcu();
    read_byte_mcu();
    interrupts();

    has_MCU = (buttons2 & B10000000);
    // Check for valid type_flag and not all-high (0xFF)
    if (has_MCU) {
        has_GC = (buttons2 & B01000000);
        if (has_GC) {
            has_forced_fail = (buttons2 & B00000001);
            if (has_forced_fail) {
                mcu_read_success = false; 
            }
        }
    } else {
        mcu_read_success = false; // this bit seems to be how the real thing knows if it has data
    }

    if (mcu_read_success) {
        mcu_fail_count = 0;
    } else {
        if (mcu_fail_count < BAD_READS_MCU) {
            ++mcu_fail_count;
            SLEEP_MCU
        } else {
            proto = MODE_LN_NONE;
            CLEAR_STATE
            SLEEP_BETWEEN_PROTOCOLS
        }
        return;
    }

    uint8_t dpad = ~buttons1 & 0x0F; // UDLR bits
    uint8_t encoded_type = 0;

    if ((dpad & 0x03) == 0x03 || (dpad & 0x0C) == 0x0C) {
        // If any dpad input exists, block menu detection
        if (last_dpad == 0) {
            // Only detect menu/encoded buttons if no dpad input
            if (dpad == 0x0F) { encoded_type = 1; } // Reset: all 4
            if (dpad == 0x0C) { encoded_type = 2; } // Menu: up+down
            if (dpad == 0x03) { encoded_type = 3; } // *: left+right
            if (dpad == 0x0D) { encoded_type = 4; } // Select: up+down+right
            if (dpad == 0x0B) { encoded_type = 5; } // Order: up+left+right
            if (dpad == 0x0E) { encoded_type = 6; } // #: up+down+left
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

    if (good_reads < GOOD_READS_MCU) {
        // not enough good reads yet to confirm presence
        ++good_reads;
        MyCustomHID.sendState(proto, DEVICE_NONE, 0x0000, DPAD_CENTER, STICK_CENTER, STICK_CENTER, STICK_CENTER, STICK_CENTER, 0, 0);
        SLEEP_MCU
        return;
    }

    // Dpad bits: 0=Right, 1=Left, 2=Down, 3=Up
    // Map to HID hat switch (0=Up, 1=Up-Right, ..., 7=Up-Left, 8=Centered)
    uint8_t hid_hat = DPAD_CENTER;
    switch (last_dpad) {
        case 0x08: hid_hat = DPAD_UP;         break; // Up
        case 0x09: hid_hat = DPAD_UP_RIGHT;   break; // Up-Right
        case 0x01: hid_hat = DPAD_RIGHT;      break; // Right
        case 0x05: hid_hat = DPAD_DOWN_RIGHT; break; // Down-Right
        case 0x04: hid_hat = DPAD_DOWN;       break; // Down
        case 0x06: hid_hat = DPAD_DOWN_LEFT;  break; // Down-Left
        case 0x02: hid_hat = DPAD_LEFT;       break; // Left
        case 0x0A: hid_hat = DPAD_UP_LEFT;    break; // Up-Left
        default:   hid_hat = DPAD_CENTER;     break; // Centered/neutral
    }

    // Build and send custom gamepad report for MCU devices
    uint32_t buttons = 0;
    // Map buttons for N64/GC
    if (!(buttons1 & 0x40)) buttons |= 0x00000001;  // B
    if (!(buttons1 & 0x80)) buttons |= 0x00000002;  // A
    if (!(buttons1 & 0x20)) buttons |= 0x00000010;  // Z
    if (!(buttons1 & 0x10)) buttons |= 0x00000020;  // Start
    if (!(buttons2 & 0x20)) buttons |= 0x00000040;  // L
    if (!(buttons2 & 0x10)) buttons |= 0x00000080;  // R

    if (encoded_type == 5) buttons |= 0x00000400; // Order
    if (encoded_type == 1) buttons |= 0x00000800; // Reset
    if (encoded_type == 2) buttons |= 0x00001000; // Menu
    if (encoded_type == 6) buttons |= 0x00002000; // #
    if (encoded_type == 4) buttons |= 0x00004000; // Select
    if (encoded_type == 3) buttons |= 0x00008000; // *

    if (has_GC) { // GC
        if (!(buttons2 & 0x08)) buttons |= 0x00000004; // X
        if (!(buttons2 & 0x04)) buttons |= 0x00000008; // Y
        MyCustomHID.sendState(proto, DEVICE_LN_GC, buttons, hid_hat, analog1, 255 - analog2, analog3, 255 - analog4, analog5, analog6);
    } else { // N64
        if (!(buttons2 & 0x08)) buttons |= 0x00000004; // C-Up
        if (!(buttons2 & 0x04)) buttons |= 0x00000008; // C-Down
        if (!(buttons2 & 0x02)) buttons |= 0x00000100; // C-Left
        if (!(buttons2 & 0x01)) buttons |= 0x00000200; // C-Right
        MyCustomHID.sendState(proto, DEVICE_LN_N64, buttons, hid_hat, (uint8_t)(analog1 + 128), (uint8_t)(127 - analog2), STICK_CENTER, STICK_CENTER, 0, 0);
    }

    SLEEP_MCU
}

//////////////////////////////////////////////////////////
// Core Functions
//////////////////////////////////////////////////////////

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

    // Ensure data pin is input with internal pull-up
    pinMode(D_DATA, INPUT_PULLUP);

    Serial.println(F("LodgeNet USB test host starting..."));
}

void loop() {
    switch(proto)
    {
        case MODE_LN_NONE:
            process_none_controller();
            break;
        case MODE_LN_SR:
            process_sr_controller();
            break;
        case MODE_LN_MCU:
            process_mcu_controller();
            break;
    }
}