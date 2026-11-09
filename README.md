# Christmas TrESP32 — SmartThings Matter Christmas Tree Controller

**Manufacturer:** `Pixel Paw Studio`  
**Model:** `Christmas TrESP32`

A high-performance DIY replacement for the stock 29V DC controller/power brick and **Polygroup PDT-015-14** RF remote found on Home Depot pre-lit Christmas trees.

Built using **ESP-IDF** and the official **`esp-matter`** SDK on an **ESP32-S3**, this controller pairs natively with **Samsung SmartThings** over local Wi-Fi via **Matter** and places all controls—Power, Silent Ultrasonic Dimming (`25 kHz` PWM), Warm White vs. RGB Multicolor selection, and Peaceful Twinkle with variable speed—onto a **single SmartThings device card** with no custom Edge drivers required.

---

## 1. Hardware Bill of Materials (BOM)

| Component | Recommendation | Estimated Cost | Notes |
| :--- | :--- | :--- | :--- |
| **Microcontroller** | **ESP32-S3 DevKit** (8MB or 16MB Flash) | $5 – $9 | Dual-core Xtensa LX7, 2.4 GHz Wi-Fi + BLE 5.0 for Matter commissioning. |
| **H-Bridge Driver** | **Adafruit / TI DRV8871 Breakout Board** | $3 – $8 | Rated 6.5V – 45V DC @ 3.6A peak. Includes `POWER (+/-)` and `MOTOR` screw terminals plus `GND`, `VM`, `IN1`, `IN2` logic pins. Driven at `25 kHz` ultrasonic PWM for silent dimming. |
| **Power Supply (Tree LEDs)** | **Generic 29V 2A DC Power Supply** (Amazon) | $12 – $18 | Standard 29V DC / 2A (58W) replacement power brick for 29V LED Christmas trees. |
| **ESP32 Power** | **USB-C 5V Adapter** *or* **LM2596HV Buck Converter** | $0 – $4 | Power the ESP32-S3 directly via USB-C, or step down the 29V supply to 5.0V DC using an `LM2596HV` buck converter into `5V/VIN`. |

---

## 2. Electrical Wiring Diagram

The tree uses **anti-parallel LED strings** over a 2-wire bus:
- **Polarity A (`+29V` on `OUT1`, `GND` on `OUT2` — driven by `IN1` / `GPIO 5`):** Illuminates the **Warm White** LEDs.
- **Polarity B (`GND` on `OUT1`, `+29V` on `OUT2` — driven by `IN2` / `GPIO 6`):** Illuminates the **RGB Festive Multicolor** LEDs.
- **Motor Screw Terminal Polarity:** Because the tree LEDs are wired in anti-parallel across the two wires, the `MOTOR` screw terminals are **non-polarized** with respect to safety—connecting your tree cable's `+` and `-` wires one way vs. the other simply determines which polarity lights Warm White vs. RGB Multicolor. If White and RGB appear swapped in the app, simply swap the two tree wires on the `MOTOR` terminal block.

### Option A: Powering ESP32-S3 via USB-C (Recommended & Simplest)

```
                      [ 120V AC Wall Plug ]
                                │
                                ▼
                     ┌─────────────────────┐
                     │ Generic 29V 2A PSU  │
                     │ Output: 29V DC 2.0A │
                     └───────┬─────┬───────┘
                    +29V DC  │     │  -29V (GND)
                             ▼     ▼
                  ┌───────────────────────────┐
   [ 5V USB-C ]   │  DRV8871 Breakout Board   │
        │         │                           │
        ▼         │  Screw Terminals:         │
┌───────────────┐ │   POWER + <── +29V DC     │
│ ESP32-S3      │ │   POWER - <── -29V (GND)  │
│               │ │   MOTOR 1 ────────────────┼───> Tree Wire +
│  GPIO 5 (5~) ─┼─┼─> IN1                     │
│  GPIO 6 (6~) ─┼─┼─> IN2     MOTOR 2 ────────┼───> Tree Wire -
│  GND ─────────┼─┼─> GND                     │
└───────────────┘ │  (VM pin is internally    │
                  │   tied to POWER +)        │
                  └───────────────────────────┘
```

### Option B: Single-Plug Power via LM2596HV Buck Converter (29V $\to$ 5.0V)

```
                      [ 120V AC Wall Plug ]
                                │
                                ▼
                     ┌─────────────────────┐
                     │ Generic 29V 2A PSU  │
                     │ Output: 29V DC 2.0A │
                     └──────────┬──────────┘
                                │  +29V DC & GND
                 ┌──────────────┴──────────────┐
                 │                             │
                 ▼                             ▼
      ┌────────────────────┐        ┌──────────────────────────┐
      │ LM2596HV Buck      │        │ DRV8871 Breakout Board   │
      │ IN+:  +29V DC      │        │ POWER + (VM): +29V DC    │
      │ IN-:  GND (Common) │        │ POWER - / GND: Common GND│
      │ OUT+: +5.0V DC     │        │ IN1: <── ESP32 GPIO 5    │
      │ OUT-: GND (Common) │        │ IN2: <── ESP32 GPIO 6    │
      └──────────┬─────────┘        │ MOTOR 1 ──┐              │
                 │ +5.0V            │ MOTOR 2 ──┼─┐            │
                 ▼                  └───────────┼─┼────────────┘
      ┌────────────────────┐                    │ │
      │ ESP32-S3 DevKit    │                    │ │
      │ 5V / VIN <── (+5V) │                    │ │
      │ GND      <── (GND) │                    │ │
      │ GPIO 5 (5~) ──> IN1│                    │ │
      │ GPIO 6 (6~) ──> IN2│                    │ │
      └────────────────────┘                    ▼ ▼
                                     ┌──────────────────────┐
                                     │ Tree 2-Pin Connector │
                                     │ Wire + : MOTOR 1     │
                                     │ Wire - : MOTOR 2     │
                                     └──────────────────────┘
```

### Pin Summary Table

| DRV8871 Pin / Terminal | Connected To | Purpose |
| :--- | :--- | :--- |
| **`POWER +`** (Screw Terminal) | `29V 2A PSU` **`+29V`** | High-voltage motor/LED supply (`29V DC`) |
| **`POWER -`** (Screw Terminal) | `29V 2A PSU` **`-` (`GND`)** | High-voltage return ground (internally tied to `GND` pin) |
| **`GND`** (Through-Hole Pin) | **ESP32-S3 `GND`** | Common logic ground reference between ESP32 and DRV8871 |
| **`VM`** (Through-Hole Pin) | *Leave unconnected* | Internally connected to `POWER +` on the breakout board |
| **`IN1`** (Through-Hole Pin) | **ESP32-S3 `GPIO 5` (`5~`)** | `25 kHz` PWM input for **Warm White** (`Polarity A`) |
| **`IN2`** (Through-Hole Pin) | **ESP32-S3 `GPIO 6` (`6~`)** | `25 kHz` PWM input for **RGB Multicolor** (`Polarity B`) |
| **`MOTOR`** (Screw Terminals) | **Tree `+` and `-` Cable** | Switched `29V` H-bridge output to the tree LED strings |

> [!CAUTION]
> **If using an LM2596HV Buck Converter:** Connect the 29V power supply to the buck converter inputs (`IN+` and `IN-`) **before** connecting the ESP32. Measure `OUT+` and `OUT-` with a multimeter and adjust the trim pot to **5.0V DC** before wiring `OUT+` to the ESP32 `5V/VIN` pin.

---

## 3. SmartThings Single-Card Controls & Feature Mapping

SmartThings' built-in Matter driver (`matter-switch`) only places all four controls onto a **single device card** when the device exposes the Matter **Extended Color Light (`0x010D` / `light-color-level`)** profile on **Endpoint 1**. 

To give you every tree control on one unified screen without custom Edge drivers, `Christmas TrESP32` maps the standard SmartThings light controls as follows:

| Desired Tree Control | SmartThings UI Control (Single Card) | Matter Cluster & Attribute | Behavior |
| :--- | :--- | :--- | :--- |
| **Power On / Off** | **Main Power Button** | `OnOff` (`OnOff`) | Turns the tree lights completely On or Off. |
| **Variable Dimmer Slider** | **Dimmer Slider (`0% – 100%`)** | `LevelControl` (`CurrentLevel`) | Smoothly dims the active tree mode (`0 – 254`) using silent `25 kHz` ultrasonic PWM. |
| **Warm White vs. RGB Toggle** | **White Swatch vs. Rainbow Swatch** | `ColorControl` (`CurrentHue`, `CurrentSaturation`, `CurrentX/Y`) | • **Tap the White Square (right):** Selects **Warm White** (`GPIO 5` / `IN1`).<br>• **Pick ANY color in the Rainbow Square (left):** Selects **RGB Festive Multicolor** (`GPIO 6` / `IN2`). |
| **Twinkle Toggle & Speed Slider** | **Color Temperature Slider (`2,200 K – 6,500 K`)** | `ColorControl` (`ColorTemperatureMireds` `455 – 154`) | • **Slide far left (`2,200 K`):** **Twinkle OFF** (Steady illumination).<br>• **Slide right (`2,400 K – 6,500 K`):** **Twinkle ON** — adjusts speed from a slow, peaceful ~8s organic shimmer on the left to a faster ~1.6s festive twinkle on the right. |

> [!NOTE]
> - **Why a Rainbow Wheel is Shown:** SmartThings hardcodes the color wheel UI whenever a Matter device uses the single-card `light-color-level` profile. Because the physical 2-wire tree has fixed multi-color RGB LEDs in one polarity and Warm White LEDs in the other, picking **any** saturated color on the wheel (`Saturation >= 25`) activates the **RGB Festive Multicolor** string, while tapping the **White square** (`Saturation < 25`) activates the **Warm White** string.
> - **Avoiding the "Circadian Lighting" Popup:** In the SmartThings iOS app, the rightmost running-person/sun icon in the "Presets" row opens Samsung's built-in Circadian Lighting dialog. Ignore that icon and instead drag the **horizontal Color Temperature slider bar (`2,200 K – 6,500 K`)** directly below the Rainbow/White squares to toggle Twinkle and adjust Twinkle Speed.
> - **Peaceful Twinkle Engine:** Rather than harsh full-tree strobing, the twinkle effect gently modulates above a `48%` steady baseline using a dual-harmonic sine/cosine wave so individual bulbs with higher forward voltage ($V_f$) softly shimmer while the rest of the tree stays warmly lit.
> - **Automatic State Persistence:** Power, brightness, Warm White/RGB selection, twinkle state, and twinkle speed are automatically persisted in NVS flash (`tree_state`) and restored after a power outage.

---

## 4. Contained Build & Flash Workflow

This project uses a self-contained build and flash workflow on macOS:
- **Build Environment (Docker):** Compiles inside the official `espressif/esp-matter` container (`ESP-IDF v6.0.2`).
- **Flashing & Serial Monitor (Local `.venv`):** Uses a lightweight Python virtual environment (`.venv`) with `esptool` and `pyserial` on the macOS host to communicate with `/dev/cu.usbmodem*`.

### Step 1: Build the Firmware in Docker
Ensure Docker Desktop is running, then run:
```bash
./build-docker.sh
```
This compiles `build/christmas_tree_matter.bin`, `build/bootloader/bootloader.bin`, and `build/partition_table/partition-table.bin`.

### Step 2: Flash to ESP32-S3
Plug the ESP32-S3 into your Mac via USB and run:
```bash
./flash.sh
```

Additional `./flash.sh` commands:
```bash
./flash.sh info       # Read ESP32-S3 chip info and MAC address
./flash.sh monitor    # Open 115200-baud serial monitor (Ctrl+] to exit)
./flash.sh erase      # Erase entire flash (clears NVS & Matter fabrics)
```

---

## 5. Pairing & Resetting with Samsung SmartThings

### Commissioning Steps
1. Open the serial monitor (`./flash.sh monitor`) and press the `RST` button on the ESP32-S3. On boot, the firmware prints a `SetupQRCode` URL and an 11-digit `Manual pairing code` to the serial console.
2. Open the **Samsung SmartThings** app on your phone (connected to your **2.4 GHz Wi-Fi** network with Bluetooth enabled).
3. Tap **"+"** $\to$ **Add device** $\to$ **Scan QR code** (or **Partner devices** $\to$ **Matter**).
4. Scan the generated QR code or enter the 11-digit manual pairing code displayed in your serial console.
5. If prompted about an **Uncertified Accessory**, tap **Add Anyway**.

### Resetting / Re-Entering Pairing Mode
- **Automatic Reset on Delete:** Whenever you delete the device from the SmartThings app while it is online, the firmware intercepts `DeviceEventType::kFabricRemoved` and automatically runs `esp_matter::factory_reset()`, wiping all fabrics and immediately restarting BLE pairing advertisements.
- **Physical `BOOT` Button Reset (3-Second Hold):** Hold down the **`BOOT`** button (`GPIO 0`) on the ESP32-S3 for **3 seconds** at any time to trigger a Matter factory reset and reopen BLE pairing mode without needing a computer.
- **USB Erase:** Run `./flash.sh erase && ./flash.sh flash` over USB.

> [!TIP]
> **iPhone / iOS Re-Pairing Tip (Error `39-900`):**
> When pairing via an iPhone, iOS first commissions the device into the Apple Home / iOS Matter Keychain before handing it off to the SmartThings Hub. If you delete the device and re-pair later, check **iPhone Settings $\to$ General $\to$ Matter Accessories** and remove any leftover entry for the device first so iOS does not attempt to reuse a deleted fabric. Also ensure your iPhone, SmartThings Hub, and ESP32 are on the same 2.4 GHz Wi-Fi subnet with local network access enabled for SmartThings.
