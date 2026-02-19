// Created by John "Nielk1" Klein

#include <Arduino.h>
#include "PluggableUSB.h"
#include "HID.h"

// PIN  PORT  SPI        GAME  TV
// ---  ----  ---------  ----  ----
// D15  PB1   SCK(out)   DATA  CLK
// D14  PB3   MISO(in)   CLK   MTI
// D16  PB2   MOSI(out)  CLK2  DATA

// PIN  PORT  Ext Interrupt
// ---  ----  -------------
// D3   PD0   INT0

// ╔═╤═╤═╤═╤═╤═╤═╗       GAME        TV                MTI
// ║ │ │ │ │ │ │ ║   1   DATA(in)    CLK(out)          IR(in)
// ║ 1 2 3 4 5 6 ║   2   CLK(out)    MTI(in)           GND
// ║             ║   3   12V         N/C               DATA(in)
// ║             ║   4   CLK2(out)   DATA(out)         N/C
// ╚════╤═══╤════╝   5   GND         GND               MTI(out)
//      └───┘        6   IR          IR(out)           CLK(in)

// The IR_DATA line used edge transition timing to function, so we probably need it on Pin 2 or 3 so we can use them as interrupts
// This would mean moving our CLK1 and CLK2 lines to other pins
// CLK1 to Pin 4 (PD4) and CLK2 to Pin 6 (PD7)
// DATA to Pin 12 (PD6) (google suggestion, which this indicates as NC, but we can use another Port if needed for this)

// Pin mapping
const uint8_t B_DATA = 1; // [IN] Data
const uint8_t B_CLK1 = 3; // [OUT] Main Clock, Gate Driver
const uint8_t B_CLK2 = 2; // [OUT] Second Clock, SNES Only

const uint8_t D_IR = 0; // [?] IR signal, normally from TV but bridged to MPI as well

// bInterval = 1 → host polls every 1 ms → max 1000 reports/s
// bInterval = 2 → every 2 ms → 500 Hz
// bInterval = 8 → every 8 ms → 125 Hz
// bInterval = 10 (0x0A) → every 10 ms → 100 Hz
//#define USB_HID_INTERVAL 0x0A
#define USB_HID_INTERVAL 0x02

#define LOG_TO_SERIAL true

#define MCU_HELLO_PULSE 7
#define MCU_PULSE_TIME_LOW 6 // if this is too low analog reads become unstable
#define MCU_PULSE_TIME_HIGH 30

#define SR_PULSE_TIME_LOW 3
#define SR_PULSE_TIME_HIGH 20
#define SR_PULSE_TIME_LOW2 25

// Time-Skew correction for CLK1
#define TIME_SKEW 0


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

#define BUTTON_1 0x00000001
#define BUTTON_2 0x00000002
#define BUTTON_3 0x00000004
#define BUTTON_4 0x00000008
#define BUTTON_5 0x00000010
#define BUTTON_6 0x00000020
#define BUTTON_7 0x00000040
#define BUTTON_8 0x00000080
#define BUTTON_9 0x00000100
#define BUTTON_10 0x00000200
#define BUTTON_11 0x00000400
#define BUTTON_12 0x00000800
#define BUTTON_13 0x00001000
#define BUTTON_14 0x00002000
#define BUTTON_15 0x00004000
#define BUTTON_16 0x00008000
#define BUTTON_17 0x00010000
#define BUTTON_18 0x00020000
#define BUTTON_19 0x00040000
#define BUTTON_20 0x00080000
#define BUTTON_21 0x00100000
#define BUTTON_22 0x00200000
#define BUTTON_23 0x00400000
#define BUTTON_24 0x00800000
#define BUTTON_25 0x01000000
#define BUTTON_26 0x02000000
#define BUTTON_27 0x04000000
#define BUTTON_28 0x08000000
#define BUTTON_29 0x10000000
#define BUTTON_30 0x20000000
#define BUTTON_31 0x40000000
#define BUTTON_32 0x80000000

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
    DDRB &= ~_BV(B_CLK1); // Set as input for high (external pullup)
}

inline void clk1_low() {
    DDRB |= _BV(B_CLK1);  // Set as output
}

inline void clk2_high() {
    DDRB &= ~_BV(B_CLK2); // Set as input for high (external pullup)
}

inline void clk2_low() { 
    DDRB |= _BV(B_CLK2);  // Set as output
}

/// Pulse the CLK1 line low and set the CLK2 line up
/// This is essentially a half-clock cycle for SNES
inline void pulse_snes() {

    // Set CLK1 low (output low) and CLK2 high (input)
    DDRB |= _BV(B_CLK1);
    DDRB &= ~_BV(B_CLK2);
    //PORTB &= ~_BV(B_CLK1);

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
        value = (value << 1) | ((PINB & _BV(B_DATA)) ? 1 : 0);
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
        delayMicroseconds(SR_PULSE_TIME_LOW2 + TIME_SKEW);
        value = (value << 1) | ((PINB & _BV(B_DATA)) ? 1 : 0);
    }
    pulse_snes();
    delayMicroseconds(SR_PULSE_TIME_LOW2 + TIME_SKEW);
    interrupts();

    if (PINB & _BV(B_DATA)) {
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
    if (!(value & 0x2000)) buttons |= BUTTON_1; // B
    if (!(value & 0x0008)) buttons |= BUTTON_2; // A
    if (!(value & 0x1000)) buttons |= BUTTON_3; // Y
    if (!(value & 0x0004)) buttons |= BUTTON_4; // X
    if (!(value & 0x0800)) buttons |= BUTTON_5; // Select
    if (!(value & 0x0400)) buttons |= BUTTON_6; // Start/*
    if (!(value & 0x0002)) buttons |= BUTTON_7; // L
    if (!(value & 0x0001)) buttons |= BUTTON_8; // R
    if (!(value & 0x4000)) buttons |= BUTTON_11; // Reset/Order
    if (ButtonPlus)        buttons |= BUTTON_13; // Plus
    if (!(value & 0x8000)) buttons |= BUTTON_14; // Menu
    if (ButtonMinus)       buttons |= BUTTON_16; // Minus

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
    uint8_t analog3 = read_byte_mcu(); // if we're n64 mode this is extra, but who cares, we can just ignore it
    uint8_t analog4 = read_byte_mcu(); // "
    uint8_t analog5 = read_byte_mcu(); // "
    uint8_t analog6 = read_byte_mcu(); // "
    read_byte_mcu(); // extra just to make sure we've cleared the buffer
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
    uint16_t buttons = 0;
    // Map buttons for N64/GC
    if (!(buttons1 & 0x40)) buttons |= BUTTON_1;  // B
    if (!(buttons1 & 0x80)) buttons |= BUTTON_2;  // A
    if (!(buttons1 & 0x20)) buttons |= BUTTON_5;  // Z
    if (!(buttons1 & 0x10)) buttons |= BUTTON_6;  // Start
    if (!(buttons2 & 0x20)) buttons |= BUTTON_7;  // L
    if (!(buttons2 & 0x10)) buttons |= BUTTON_8;  // R

    if (encoded_type == 5) buttons |= BUTTON_11; // Order
    if (encoded_type == 1) buttons |= BUTTON_12; // Reset
    if (encoded_type == 2) buttons |= BUTTON_13; // Menu
    if (encoded_type == 6) buttons |= BUTTON_14; // #
    if (encoded_type == 4) buttons |= BUTTON_15; // Select
    if (encoded_type == 3) buttons |= BUTTON_16; // *

    if (has_GC) { // GC
        if (!(buttons2 & 0x08)) buttons |= BUTTON_3; // X
        if (!(buttons2 & 0x04)) buttons |= BUTTON_4; // Y
        MyCustomHID.sendState(proto, DEVICE_LN_GC, buttons, hid_hat, analog1, 255 - analog2, analog3, 255 - analog4, analog5, analog6);
    } else { // N64
        if (!(buttons2 & 0x08)) buttons |= BUTTON_3; // C-Up
        if (!(buttons2 & 0x04)) buttons |= BUTTON_4; // C-Down
        if (!(buttons2 & 0x02)) buttons |= BUTTON_9; // C-Left
        if (!(buttons2 & 0x01)) buttons |= BUTTON_10; // C-Right
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

    DDRB &= ~(_BV(B_CLK1) | _BV(B_CLK2)); // Set CLK pins as input initially (high via external pullup)
    clk1_high();
    clk2_high();

    // Ensure data pin is input with internal pull-up
    pinMode(B_DATA, INPUT_PULLUP);
    PORTB &= ~_BV(B_CLK1);

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