> 🇧🇷 [Leia em português](README.pt-BR.md)

# KIX POS

A **Lightning Network point-of-sale (PoS) terminal** for the **ESP32**. It uses a
240×320 TFT display (portrait), a **resistive XPT2046** touchscreen and the
**LNbits** API to create and verify BOLT11 invoices.

> The operator types an amount in **satoshis**, the device creates a Lightning
> invoice, shows it as a **QR code**, polls until the customer pays, then shows a
> "PAID" screen and returns to the keypad.

---

## Features

- 240×320 portrait touch UI with a numeric keypad (3×4).
- Lightning invoice creation via LNbits (`POST /api/v1/payments`).
- QR code rendering of the BOLT11 invoice (local `qrcode.c` library).
- Automatic payment polling every 3 s (`GET /api/v1/payments/{hash}`).
- Wallet balance check (`GET /api/v1/wallet`).
- On-device **setup portal** — a small `WIFI` button opens a captive portal to
  configure Wi-Fi, LNbits URL, API key and Bluetooth pairing PIN from a phone
  browser.
- A **`BT` toggle button** next to `WIFI` turns the Bluetooth serial server on/off.
- Serial (UART) **and Bluetooth SPP** command interface — control the device from a
  USB serial monitor or a phone's Bluetooth terminal app (pair with **`KIX-POS`**,
  "Just Works" pairing — no PIN).
- All dependencies vendored in `libraries/` (no `arduino-cli lib install` needed),
  including a **`TFT_eSPI`** carrying this display's configuration so the color fix
  is never lost on library reinstall.

---

## Hardware

- **ESP32** dev board with integrated display + touch.
- TFT **240×320** (ILI9341 driver), portrait rotation.
- **Resistive XPT2046** touch (SPI).
- The wiring below matches the **ESP32-2432S028** ("Cheap Yellow Display" / CYD)
  pinout (ILI9341 + resistive-touch variant).

| Signal             | GPIO |
|--------------------|------|
| TFT MISO           | 12   |
| TFT MOSI (SDA)     | 13   |
| TFT SCLK           | 14   |
| TFT CS             | 15   |
| TFT DC             | 2    |
| TFT RST            | -1 (not connected) |
| TFT backlight BL   | 21   |
| Touch MOSI         | 32   |
| Touch MISO         | 39   |
| Touch CLK          | 25   |
| Touch CS           | 33   |
| Touch IRQ          | 36   |

The display driver is configured in
[`libraries/TFT_eSPI/User_Setup.h`](libraries/TFT_eSPI/User_Setup.h) with
`ILI9341_2_DRIVER`, 240×320, **plus** `#define TFT_INVERSION_ON`. This panel is
"natively inverted" — without that line the colors come out as a negative (black
turns white, green turns lilac) — so the inversion line is required for correct
colors. The firmware sets `tft.setRotation(2)` for portrait orientation.

---

## Repository structure

```
.
├── README.md                               # this file
├── README.pt-BR.md                         # Portuguese translation
├── comandos_arduino_cli_gravar.txt         # arduino-cli commands (PT-BR)
├── architecture.md                         # detailed architecture (EN)
├── arquitetura.md                          # detailed architecture (PT-BR)
├── ultimate_CYD_ESP32_BTC_POS.ino          # main firmware
├── qrcode.c / qrcode.h                     # local QR-code generator (MIT)
└── libraries/                              # vendored libraries
    ├── TFT_eSPI/              2.5.43       # display driver (local ILI9341 config)
    ├── XPT2046_Touchscreen/   1.4          # resistive touch driver
    ├── ArduinoJson/           7.4.3        # JSON parse/serialize
    └── WiFiManager/           2.0.17       # captive portal (AP mode) for Wi-Fi setup
```

> All four libraries are vendored in `libraries/`, so you do **not** need
> `arduino-cli lib install`. `TFT_eSPI` is kept **local** because it carries this
> display's specific configuration.

> `WiFiManager` is used by the on-device `WIFI` button to open a captive portal
> (AP mode) and store the chosen Wi-Fi credentials in NVS.

---

## Getting started

### 1. Install Arduino CLI

```bash
# Linux/macOS (adapt the URL/version as needed)
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# or download a release from:
# https://arduino.github.io/arduino-cli/installation/
```

Verify:

```bash
arduino-cli version
```

### 2. Install the ESP32 core

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

### 3. Libraries

All libraries are **vendored** in `libraries/` (`TFT_eSPI`, `XPT2046_Touchscreen`,
`ArduinoJson`, `WiFiManager`), so you do **not** need `arduino-cli lib install`.
They are picked up automatically via the `--libraries` flag in the compile command
below.

### 4. Configure credentials

Edit the top of `ultimate_CYD_ESP32_BTC_POS.ino`:

```cpp
const char* ssid     = "SEU_SSID";
const char* password = "SUA_SENHA";

String lnbits_url        = "https://SEU_DOMINIO/api/v1/payments";
String lnbits_wallet_url = "https://SEU_DOMINIO/api/v1/wallet";
String lnbits_api_key    = "SUA_CHAVE_API_LNBITS";
```

> ⚠️ The credentials in the file are **placeholders**. Replace them with your own
> values before flashing. Never commit a real `X-Api-Key` (the `credenciais.txt`
> file is already in `.gitignore`).

> 💡 **Bluetooth + HTTPS heap warning:** the ESP32's TLS handshake needs a large
> heap chunk, and running **Bluetooth Classic at the same time** can make HTTPS
> calls fail (invoice creation works only with Bluetooth off). The firmware now
> auto-detects `http://` vs `https://` in the URL: `http://` uses a lightweight
> client and works alongside Bluetooth. If you want to use the Bluetooth terminal
> **and** create invoices at the same time, point the endpoint at **`http://`**.
> With `https://`, turn Bluetooth off (the `BT` button) before creating an invoice.

**Over-the-air setup (no re-flash):** the `WIFI` button on the keypad opens a
captive portal (`KIX-POS-Setup` / `kixpos123`). Connect a phone to that network,
browse to `192.168.4.1` and enter the local Wi-Fi. The same page has fields for
the **LNbits URL**, **API key** and **Bluetooth PIN** (pre-filled with the current
values — leave them blank to keep the current ones). Everything is stored in NVS
and used automatically on the next boot, falling back to the hardcoded values
above only when nothing was saved.

> ⚠️ **Bluetooth PIN note:** ESP32 Arduino core **3.x removed legacy fixed-PIN
> pairing**, so the `Bluetooth PIN` field is currently **saved but not enforced** —
> pairing uses "Just Works" (no PIN). A fixed PIN only works if the firmware is
> rebuilt as an ESP-IDF component with `CONFIG_BT_SSP_ENABLED` disabled.

---

## Flashing with Arduino CLI

Run these commands from the **repository root** (the folder that contains
`ultimate_CYD_ESP32_BTC_POS.ino`).

> `--libraries` points to the vendored `libraries/` folder. **Important:**
> `arduino-cli` does not auto-detect the sketch's `libraries/` folder, so without
> this flag it uses the system libraries (and the colors stay inverted).

### Find the serial port

```bash
arduino-cli board list
```

Typical values: `/dev/ttyUSB0` (Linux), `/dev/cu.usbserial-*` (macOS), `COM3` (Windows).

### Compile

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=huge_app \
  --libraries libraries \
  --verbose \
  .
```

- `PartitionScheme=huge_app` reserves the maximum app space (the display/JSON
  libraries are large; the default partition does not fit this firmware).

### Upload (flash the board)

```bash
arduino-cli upload \
  --verbose \
  -p /dev/ttyUSB0 \
  --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=huge_app \
  .
```

Replace `/dev/ttyUSB0` with the port from `arduino-cli board list`.

### Compile + upload in one step

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=huge_app \
  --libraries libraries \
  --verbose \
  . \
&& arduino-cli upload \
  --verbose \
  -p /dev/ttyUSB0 \
  --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=huge_app \
  .
```

### Serial monitor

```bash
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

### Export a compiled binary (optional)

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=huge_app \
  --libraries libraries \
  --verbose \
  --output-dir ./build_output \
  .
```

The command reference is also kept in
[`comandos_arduino_cli_gravar.txt`](comandos_arduino_cli_gravar.txt) (PT-BR).

---

## Usage

### On the touchscreen

1. Type the amount in **sats** on the keypad.
2. Press **OK** → the invoice is created and shown as a QR code.
3. Customer scans and pays with a Lightning wallet.
4. The device auto-checks every 3 s (or press **CHECAR**) → shows **PAGO COM SUCESSO!**.
5. Press **VOLTAR** to go back to the keypad.

Tap **WIFI** (top-right of the keypad) to enter the Wi-Fi setup portal described
above. The **`BT`** button next to it toggles the Bluetooth serial server on/off
(green `BT ON` = on, grey `BT OFF` = off).

### Over serial (USB or Bluetooth)

The same commands work over the USB serial monitor (115200 baud) and over a
**Bluetooth SPP** terminal — pair with the device named **`KIX-POS`** (pairing is
"Just Works", no PIN; see the note in the setup section above).

| Command  | Action                                       |
|----------|----------------------------------------------|
| `I:1500` | Create an invoice for 1500 sats              |
| `C` / `C:`| Check the active invoice's payment status   |
| `L` / `L:`| Query wallet balance                        |
| `P` / `P:`| Print the active BOLT11 invoice (copy/paste) |
| `H`      | Return to the home (keypad) screen           |
| `H:`     | Print system status (Wi-Fi, IP, BT, heap…)   |
| `help`   | List all available commands                  |

> 📱 **Bluetooth + TLS workaround:** Bluetooth and HTTPS can't share the ESP32's
> memory at the same time, so before any TLS call (invoice, balance, check) the
> device **turns Bluetooth off automatically** and warns you over the terminal to
> reconnect in 10 s. Any operation requested **over Bluetooth** (`I:` invoice,
> `C` check, `L` balance) turns Bluetooth back on by itself after **10 s**; for an
> invoice, the automatic payment check also runs only for those 10 s and then
> stops. Operations from the touchscreen or USB serial leave Bluetooth **off** —
> tap the **`BT`** button to turn it back on. When a Bluetooth client reconnects,
> the device reports the result over Bluetooth: `PAGO COM SUCESSO!` / the **BOLT11**
> for an invoice, or the check/balance result for `C` / `L`. Type `help` for the
> command list.

Example:

```
> I:1500
< [INVOICE] ===== INICIO =====
< ...
< [INVOICE] ===== FIM OK =====
> C
< [CHECK] PAGAMENTO CONFIRMADO!
```

---

## How the code works

The firmware is a **state machine** driven by `currentScreen` (an `enum` with six
states):

| State               | Screen                         |
|---------------------|--------------------------------|
| `SCREEN_CONNECTING` | "Conectando WiFi…"             |
| `SCREEN_KEYPAD`     | Numeric keypad (3×4)           |
| `SCREEN_LOADING`    | "CRIANDO INVOICE…"             |
| `SCREEN_QR_CODE`    | Invoice QR code + value        |
| `SCREEN_PAID`       | "PAGO COM SUCESSO!"            |
| `SCREEN_ERROR`      | Title + detail + VOLTAR button |

The main `loop()` is **non-blocking** and runs seven phases every iteration:

1. **Serial reception** — reads USB UART **and Bluetooth SPP**, dispatching
   `I:` / `C` / `L` / `P` / `H` / `H:` commands to the same handler.
2. **Touch** — reads the XPT2046 with 180 ms debounce and hit-tests the current screen.
3. **Debug heartbeat** — prints a "loop alive" line every 30 s.
4. **Auto-check** — if on the QR screen, polls the invoice every 3 s.
5. **Auto-return** — 5 s after "PAID", returns to the keypad.
6. **BT auto-re-enable** — for a Bluetooth-requested operation (invoice/check/balance), turns Bluetooth back on after 10 s (for invoices, the auto-check also stops at 10 s; USB/touch keep auto-checking).
7. **BT report** — once a client reconnects, reports `PAGO` / not-paid + BOLT11.

### Network flow (LNbits)

- `createInvoice(sats)` → `POST /api/v1/payments` with
  `{"out": false, "amount": sats, "memo": "KIX-POS"}` → parses `payment_request`
  (BOLT11) and `payment_hash` → renders the QR code.
- `verificarPagamentoSilencioso()` / `manualCheck()` → `GET /api/v1/payments/{hash}`
  → checks the `paid` field.
- `checkBalance()` → `GET /api/v1/wallet` → reads `balance_msat`, divides by 1000.

All requests authenticate with the `X-Api-Key` header.

> ⚠️ **Security note:** the code uses `client.setInsecure()` (no TLS certificate
> validation) and hardcodes credentials at the top of the file. This is acceptable
> for diagnostics but **not** for production — validate the TLS certificate and move
> secrets to config/NVS.

For a full walkthrough (ASCII diagrams, function reference, security notes), see
[`architecture.md`](architecture.md) (EN) or [`arquitetura.md`](arquitetura.md) (PT-BR).

---

## License

The main firmware and bundled `qrcode.c` (based on Nayuki's QR-Code-generator) are
provided under the MIT license. Individual vendored libraries retain their own
licenses (see each `libraries/*` folder). Add a top-level `LICENSE` file if you
plan to distribute this project.
