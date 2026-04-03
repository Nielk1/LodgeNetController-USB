# About
This project is an attempt to interface with LodgeNet controllers.  These controllers come in 3 primary forms: SNES style, N64 style, and GameCube style.  The N64 and GC style controllers use an MCU where the SNES style uses a shift register.

This project has been ongoing for some times, but without a working LBT-44C the project was badly stalled.  After joining the Lodgenet Oasis discord in the end of 2025 I learned my LBT-44C was damaged and aquired a new unit.  From this I was able to sniff protocol traffic for all 3 controller types and create this interface.  This interface thus predates the new interest in LodgeNet hardware after the discovery and release of a controller testing rom.

## Pinout (Female)
```
╔═╤═╤═╤═╤═╤═╤═╗              SNES          N64/GC
║ │ │ │ │ │ │ ║   1   DATA   DATA          DATA(in)
║ 1 2 3 4 5 6 ║   2   CLK1   Power/Latch   Clock(out)
║             ║   3   12V    -             12V
║             ║   4   CLK2   Clock(out)    -
╚════╤═══╤════╝   5   GND    GND           GND
     └───┘        6   IR     -             -
```

## Devices
* All Clock and Data lines are 5V.
* Buttons are pulled low when pressed.
* Analog values are stored normally, highs are 1s lows are 0s.

### Shift Register Based
The Super Nintendo (SNES) style controller utilizes a pair of 8 bit shift registers.  These are CMOS logic based on levels rather than rising or falling edges.  The CLK1 line is used for main power and triggers a "latch" event on a low state.  It is pulled low for each clock pulse, though holding it low for all the pulses appears to work, this is not what the offical hardware does and not worth doing as you risk a brownout.  Note that CLK1, as it is used for main power via a capacitor, can have an inrush draw, so direct driving it with an MCU pin is likely a bad idea.

```
Clock ──────────┐      ┌─────┐      ┌─────┐      ┌─────┐      ┌─────┐      ┌─────┐      ┌─────┐      ┌─────┐      ┌─────┐      ┌─────┐      ┌─────┐      ┌─────┐      ┌─────┐      ┌─────┐      ┌─────┐      ┌─────┐      ┌──────────────
                └──────┘     └──────┘     └──────┘     └──────┘     └──────┘     └──────┘     └──────┘     └──────┘     └──────┘     └──────┘     └──────┘     └──────┘     └──────┘     └──────┘     └──────┘     └──────┘
Latch ────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────────
          └─┘          └─┘          └─┘          └─┘          └─┘          └─┘          └─┘          └─┘          └─┘          └─┘          └─┘          └─┘          └─┘          └─┘          └─┘          └─┘          └─┘
Data  ───────┐──────────┌─┐──────────┌─┐──────────┌─┐──────────┌─┐──────────┌─┐──────────┌─┐──────────┌─┐──────────┌─ ────────── ─ ────────── ─┐──────────┌─┐──────────┌─┐──────────┌─┐──────────┌─┐──────────┌─┐──────────┌─┐        ┌──
             └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘                           └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ └─~~~~~~─┘
                 Menu     Reset/Order       B            Y          Select       Start          ↑            ↓                                      ←            →            A            X            L            R          ???
```
* Latch low pulse are about 3 us
* Highs between latches are about 43.5 us
* Clock highs are about 19.5 us
* CLock lows are about 26 us
* ??? low on Data is about 650 us

### MCU Based
The Nintendo 64 (N64) and Game Cube (GC) style controllers utilize an MCU and thus are more capable and specialized.
Note that a trigger double pulse is only a trigger if no bits are pending.
If bits are pending these can be misunderstood as more clock pulses for pushing data.
Also note that the N64 controller has far tighter tollerances than the GC controller.

```
      |      Trigger       |     Read a byte (4 bytes for N64, 8 for GC)                                           |
      |                    |                                                                                       |
      |    6 6 6    26us   4us  18us  4us  18us  4us  18us  4us  18us  4us  18us  4us  18us  4us  18us  4us  30us  4us
Clock ────┐ ┌─┐ ┌──────────┐┌─────────┐┌─────────┐┌─────────┐┌─────────┐┌─────────┐┌─────────┐┌─────────┐┌─────────┐┌─---
          └─┘ └─┘          └┘         └┘         └┘         └┘         └┘         └┘         └┘         └┘         └┘
Data  ───────────────────────┐──────────┌──────────┌──────────┌──────────┌──────────┌──────────┌──────────┌──────────┌---
                             └──────────┘──────────┘──────────┘──────────┘──────────┘──────────┘──────────┘──────────┘---
```

* The trigger is a 6 us low-high-low pulse.
* Reading a bit is a low pulse for 4us with a 18 us high gap between
* After the trigger and each byte there is a delay of 26 to 32 us, likely from processing lag.
* Transmitted MSB
* Bytes (N64 is 4 bytes, GC is 8 bytes) (N64 is signed, GC is unsigned)
  0. Buttons 1 (low is pressed)
    * `A B Z Start ↑ ↓ ← →`
  1. Buttons 2 (low is pressed)
    * N64: `1 0 L R C↑ C↓ C← C→`
    * GC: `1 1 L R X Y ? ?` (please correct the `?`s if you can)
  2. X Axis
  3. Y Axis
  4. C-X Axis
  5. C-Y Axis
  6. Left Trigger
  7. Right Trigger

## Implementation
A controller must successfully poll multiple times before the hotplug detects it is attached.

### Shift Register Based
```
                 v            v
                 |            |
Clock ─────┐     |┌─────┐     |┌─
           └─────|┘     └─────|┘ 
Latch  ┌─────────|┐ ┌─────────|┐ 
      ─┘         |└─┘         |└─
Data  ──┐────────|─┌─┐────────|─┌
        └────────|─┘ └────────|─┘
                 |            |
Reads            ^            ^
```
* Latch low pulse are 3 us
* Highs between latches are 42 us
* Clock highs are about 20 us
* CLock lows are about 25 us
* We read the value of the data line immediately before setting clock high
* The unusual Data low after the 16th bit (effectively a 17th bit) is a quirk of the hardware and is useful in detecting a Shift Register type controller is attached.

### MCU Based
Always read 9 bytes.  This bypasses any possible issues with confusing N64 and GC controllers due to quirks or communication problems.

* Trigger double pulse is 7 us
* Clock low pulses are 6 us
* Time between pulses is 30 us, no extra delay between bytes

# Current Knowledge
The controllers can operate at a minimum of ~3.2V, but this is the bottom edge of their stability.  Given the MCU based controllers have a voltage drop of at least 0.1V, and the long cable can drop similar, a 3.3V input really is just on the line.  The linear regulator inside the MCU baed controllers will output a clean 5.00V when supplied with at least 5.5V, but will pass through lower voltages.

Thus, 3.3V is likely too low to drive the MCU controllers, 5V appears to be enough though then the Linear Regulators are simply passing the input voltage less their voltage drop.

# Version 0.1
This is the first iteration. It features BAT diodes for each data line to the 5V rail to ensure accidently crossing the "12V" line to a data line will be disipated. The "12V" line is running with 10V via a voltage doubler.