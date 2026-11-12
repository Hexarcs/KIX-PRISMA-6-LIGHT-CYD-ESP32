> 🇧🇷 [Leia em português](arquitetura.md)

# KIX POS — Architecture Documentation

> Documentation generated from `ultimate_CYD_ESP32_BTC_POS.ino`.
> The firmware implements a **Lightning Network point-of-sale (PoS)** on an
> **ESP32**, using a 240×320 TFT display (portrait), a **resistive XPT2046**
> touchscreen and the **LNbits** API to create/verify BOLT11 invoices.

---

## 1. Overview

The device is a Lightning payment terminal:

1. The operator types the amount in **sats** on the on-screen keypad.
2. The firmware calls LNbits to **create an invoice** (BOLT11).
3. The invoice is shown as a **QR code** on the display.
4. The customer pays with a Lightning wallet (scanning the QR).
5. The firmware **polls the invoice every 3 s** until it detects the payment.
6. On confirmation, it shows the **"PAGO COM SUCESSO!"** screen and returns to the keypad.

Besides the touch interface, there is a **serial interface** (UART) to control the
same flow via text commands.

---

## 2. Hardware stack

```
                        +----------------------------------------+
                        |                 ESP32                  |
                        |                                        |
   SPI (touch)          |  GPIO32  MOSI ─────────────┐           |
   ┌──────────────────► |  GPIO39  MISO ◄────────────┼──► XPT2046 |
   │                    |  GPIO25  CLK ──────────────┤   Touch   |
   │                    |  GPIO33  CS  ──────────────┤  (resist.)|
   │                    |  GPIO36  IRQ ──────────────┘           |
   │                    |                                        |
   │                    |  SPI (bus) ──────────────────► TFT_eSPI|
   │                    |  TFT_BL   ──────────────────► Backlight|
   │                    |                                        |
   │                    |  WiFi/HTTPS (insecure) ──────► LNbits  |
   │                    |         SEU_DOMINIO          |
   └────────────────────┘                                        |
                        +----------------------------------------+

   ┌───────────────┐        ┌───────────────┐        ┌───────────────┐
   │  TFT 240×320  │        │ XPT2046 Touch │        │   LNbits API  │
   │  (portrait)   │        │  resistive    │        │  (Lightning)  │
   └───────────────┘        └───────────────┘        └───────────────┘
```

| Resource          | Configuration                                        | Pins                        |
|-------------------|------------------------------------------------------|-----------------------------|
| Display           | `TFT_eSPI`, rotation `2` (portrait), 240×320, `ILI9341_2_DRIVER` + `TFT_INVERSION_ON` | via SPI + `TFT_BL`          |
| Resistive touch   | `XPT2046_Touchscreen` (SPI/VSPI), 240×320            | MOSI=32, MISO=39, CLK=25, CS=33, IRQ=36 |
| Network           | Wi-Fi + `HTTPClient` (adaptive `WiFiClient`/`WiFiClientSecure`) | —             |
| Bluetooth         | `BluetoothSerial` (SPP), Just Works pairing          | —                           |
| JSON              | `ArduinoJson`                                        | —                           |
| QR code           | `qrcode.h` (version 14, `ECC_LOW`)                   | —                           |

> **Display colors:** this ILI9341 panel is "natively inverted". The local `TFT_eSPI`
> defines `ILI9341_2_DRIVER` **and** `#define TFT_INVERSION_ON` in
> `libraries/TFT_eSPI/User_Setup.h`; without that line the colors come out as a
> negative (black turns white, green turns lilac).

> The transport is chosen by the URL: `http://` uses `WiFiClient` (lightweight)
> and `https://` uses `WiFiClientSecure` with `setInsecure()` (no certificate
> validation — diagnostic mode). **HTTPS + Bluetooth on can exhaust the ESP32's
> heap** and make invoice creation fail; to use Bluetooth and invoices at the same
> time, point at an `http://` endpoint (like the baremetal project) or turn BT off.

---

## 3. Software architecture

### 3.1 State machine (screens)

The firmware is driven by a **state enum** (`ScreenState`) and a global variable
`currentScreen`:

```
                 ┌──────────────┐
    boot ──────► │ CONNECTING   │  connects Wi-Fi (15 s timeout)
                 └──────┬───────┘
                        │ (always)
                        ▼
                 ┌──────────────┐
         ┌──────►│   KEYPAD     │  amount entry in sats
         │       └──────┬───────┘
         │              │ OK / "I:VALOR"
         │              ▼
         │       ┌──────────────┐
         │       │   LOADING    │  "CRIANDO INVOICE..."
         │       └──────┬───────┘
         │              │ POST /payments
         │        ┌─────┴─────┐
         │        ▼           ▼
         │  ┌──────────┐  ┌──────────┐
         │  │ QR_CODE  │  │  ERROR   │  HTTP/JSON/offline
         │  └────┬─────┘  └────┬─────┘
         │       │ payment     │ BACK / tap
         │       │ confirmed   │
         │       ▼             │
         │  ┌──────────┐       │
         │  │  PAID    │       │
         │  └────┬─────┘       │
         │       │ 5 s / tap   │
         └───────┴─────────────┘
```

| State               | Screen                            | Drawing function           |
|---------------------|-----------------------------------|----------------------------|
| `SCREEN_CONNECTING` | "Conectando WiFi…"                | `connectWiFi()`            |
| `SCREEN_KEYPAD`     | 3×4 numeric keypad                | `desenharTelaTeclado()`    |
| `SCREEN_LOADING`    | "CRIANDO INVOICE…"                | `desenharTelaCarregando()` |
| `SCREEN_QR_CODE`    | Invoice QR code + value           | `desenharTelaQRCode()`     |
| `SCREEN_PAID`       | "PAGO COM SUCESSO!"               | `desenharTelaPago()`       |
| `SCREEN_ERROR`      | Title + detail + VOLTAR button    | `desenharTelaErro()`       |

---

### 3.2 Main flow (`loop`)

The `loop()` is **non-blocking** and runs seven phases every iteration:

```
   ┌──────────────────────────────────────────────────────────────┐
   │                                                              │
   │                            loop()                            │
   │                                                              │
   │  1. SERIAL RECEPTION  (non-blocking, UART + BT SPP)          │
   │          │                                                   │
   │          ▼                                                   │
   │  2. TOUCH             (180 ms debounce + hit-test)           │
   │          │                                                   │
   │          ▼                                                   │
   │  3. DEBUG             (heartbeat every 30 s on Serial)       │
   │          │                                                   │
   │          ▼                                                   │
   │  4. AUTO-CHECK        (every 3 s if on QR_CODE)              │
   │          │                                                   │
   │          ▼                                                   │
   │  5. AUTO-RETURN       (5 s after PAID → back to keypad)      │
   │          │                                                   │
   │          ▼                                                   │
   │  6. BT AUTO-REENABLE  (BT-originated op → +10 s)             │
   │          │                                                   │
   │          ▼                                                   │
   │  7. BT REPORT         (reconnect → PAGO + BOLT11)            │
   └──────────────────────────────────────────────────────────────┘
```

Phases 6 and 7 only apply to operations requested over **Bluetooth**: phase 6 turns Bluetooth back on after 10 s (and, for invoices, also stops the auto-check at 10 s — touch/USB keep auto-checking), and phase 7 reports the result (`PAGO` / not-paid + BOLT11) to the client once it reconnects.

Timers used (global `unsigned long` variables):

| Timer                | Interval | Use                                          |
|----------------------|----------|----------------------------------------------|
| `lastTouchTime`      | 180 ms   | Touch debounce                                |
| `lastAutoCheckTime`  | 3000 ms  | Invoice poll on the QR screen                 |
| `paidScreenTime`     | 5000 ms  | Automatic return after payment                |
| `lastDebugPrintTime` | 30000 ms | Debug heartbeat on Serial                     |

---

## 4. Communication with LNbits

Endpoints:

| Operation          | Method | URL                                                        |
|--------------------|--------|------------------------------------------------------------|
| Create invoice     | `POST` | `https://SEU_DOMINIO/api/v1/payments`                      |
| Verify invoice     | `GET`  | `.../api/v1/payments/{payment_hash}`                       |
| Wallet balance     | `GET`  | `https://SEU_DOMINIO/api/v1/wallet`                        |

Authentication via the `X-Api-Key` header (hardcoded key at the top of the file).

### 4.1 Invoice creation — `createInvoice(sats)`

```
  createInvoice(sats)
        │
        ├─ Wi-Fi connected? ── no ──► ERROR screen "SEM WIFI"
        │
        ├─ POST /api/v1/payments
        │     payload: {"out": false, "amount": sats, "memo": "KIX-POS"}
        │
        ├─ HTTP 200/201? ── no ──► ERROR screen "HTTP <code>"
        │
        ├─ parse JSON → payment_request (BOLT11) + payment_hash
        │
        └─ desenharTelaQRCode()  (renders QR from BOLT11)
```

### 4.2 Payment verification

```
   verificarPagamentoSilencioso()  (auto, every 3 s)
   manualCheck()                   (CHECAR button or serial "C" command)
        │
        └─ GET /payments/{hash}
              │
              ├─ 200 + paid==true  ─► desenharTelaPago()
              ├─ 200 + paid==false ─► status "NAO PAGO AINDA"
              └─ error             ─► status "ERRO: <code>"
```

### 4.3 Balance query — `checkBalance()`

`GET /wallet` → extracts `balance_msat` → divides by 1000 → shows "SALDO DA CARTEIRA".

---

## 5. Touch interface (resistive XPT2046)

- `verificarTouch()` reads the **XPT2046** over SPI, discards weak touches (pressure
  `p.z <= 200`) and maps the raw `p.x`/`p.y` coordinates to the 240×320 rectangle
  using `TOUCH_MIN_X/MAX_X` and `TOUCH_MIN_Y/MAX_Y`. Debounce is 180 ms.
- The calibration ranges are defined at the top of the `.ino`:
  `TOUCH_MIN_X 200`, `TOUCH_MAX_X 3900`, `TOUCH_MIN_Y 240`, `TOUCH_MAX_Y 3800`
  (adjust for your board; the firmware prints the raw values on Serial to make
  calibration easier).
- `tratarToque(px, py)` does the **hit-test** by region, depending on the current screen.

### 5.1 Numeric keypad (3×4 vertical layout)

The drawn coordinates and the hit-test must match each other:

```
      x:  0    79    156    240
      ┌───────┬───────┬───────┐
  y   │   1   │   2   │   3   │  130..173
      ├───────┼───────┼───────┤
 130  │   4   │   5   │   6   │  174..221
      ├───────┼───────┼───────┤
 174  │   7   │   8   │   9   │  222..269
      ├───────┼───────┼───────┤
 222  │  DEL  │   0   │  OK   │  270..320
      └───────┴───────┴───────┘
```

> ⚠️ **Note:** the hit-test `y` ranges use slightly different boundaries than the
> drawn positions (e.g. key "1" is drawn at `y=130` with height 42 → ends at 172,
> but the test considers `130..173`). Overall the regions are contiguous, so the
> hit accuracy is acceptable, but there is a small 1–2 px offset on some edges.

### 5.2 QR / Paid / Error screens

| Screen         | Tap action                                             |
|----------------|--------------------------------------------------------|
| `QR_CODE`      | `px<120` → `manualCheck()`; otherwise → back to keypad |
| `PAID`/`ERROR` | Any tap → clears state and returns to the keypad        |

---

## 6. Serial interface (UART 115200 baud + Bluetooth SPP)

The same syntax is accepted by the **USB serial** and by a **Bluetooth SPP**
terminal (pair with the device **`KIX-POS`**; "Just Works" pairing, no PIN — core
3.x does not support a fixed PIN). Commands are terminated by `\n` or `\r`:

| Command      | Action                                       | Function called            |
|--------------|----------------------------------------------|----------------------------|
| `I:<value>`  | Creates an invoice for the value in sats     | `createInvoice()`          |
| `C` / `C:`   | Checks the active invoice's payment          | `manualCheck()`            |
| `L` / `L:`   | Queries the wallet balance                   | `checkBalance()`           |
| `P` / `P:`   | Prints the active BOLT11 invoice             | (directly on the stream)   |
| `H`          | Returns to the home (keypad) screen          | `desenharTelaTeclado()`    |
| `H:`         | Shows system status                          | `mostrarInformacoesSistema()` |
| `help`       | Lists the available commands                 | (directly on the stream)   |

Example serial flow:

```
  > I:1500
  < [SERIAL RX] Comando recebido: [I:1500]
  < [INVOICE] ===== INICIO =====
  ...
  < [INVOICE] ===== FIM OK =====
  > C
  < [CHECK] PAGAMENTO CONFIRMADO!
```

---

## 7. Function reference

| Function                            | Role                                                        |
|-------------------------------------|-------------------------------------------------------------|
| `kickstartHardware()`               | Turns on the backlight (`TFT_BL`) if defined                |
| `desenharBotao(...)`                | Draws a rounded button with centered text                   |
| `atualizarVisor()`                  | Redraws the typed value on the keypad display               |
| `desenharTelaTeclado()`             | Keypad screen (KIX POS header + ON/OFFLINE + 3×4)           |
| `desenharTelaCarregando()`          | "CRIANDO INVOICE…" screen                                   |
| `desenharTelaQRCode()`              | Renders the BOLT11 QR + CHECAR/VOLTAR buttons               |
| `desenharTelaPago()`                | Green success screen                                        |
| `desenharTelaErro(titulo, det)`     | Error screen with a VOLTAR button                           |
| `connectWiFi()`                     | Connects to Wi-Fi (15 s timeout) and goes to the keypad     |
| `iniciarConfiguracaoWiFi()`         | Opens the AP portal (WiFiManager) for Wi-Fi, API and PIN    |
| `carregarConfigLNbits()`            | Loads URL/key/PIN from NVS (fallback: hardcoded)            |
| `salvarConfigLNbits()`              | Saves URL/key/PIN to NVS                                    |
| `iniciarBluetooth()`                | Turns on Bluetooth SPP (Just Works pairing, no PIN)         |
| `alternarBluetooth()`               | Toggles Bluetooth (BT button) and redraws the keypad        |
| `prepararHTTP(...)`                 | Picks `WiFiClient`/`WiFiClientSecure` based on the URL      |
| `desligarBTParaTLS(avisarVoltar10s)` | Turns BT off (if on) before an HTTPS operation; warns "come back in 10s" if the request came from BT |
| `solicitarInvoice(sats, origemBT)`  | Turns BT off, creates the invoice; re-enables in 10s (and stops auto-check) if from BT |
| `reportarStatusInvoiceBT()`         | Reports PAID / not-paid + BOLT11 to the reconnected BT client |
| `agendarResultadoBT(msg)`           | Schedules a result to send to the BT client after reconnect |
| `checkBalance(origemBT)`            | Queries the wallet balance; re-enables BT in 10s if from BT |
| `createInvoice(sats)`               | Creates the invoice on LNbits and shows the QR              |
| `manualCheck(origemBT)`             | Manual payment check; re-enables BT in 10s if from BT       |
| `verificarPagamentoSilencioso()`    | Automatic (silent) payment poll                            |
| `adicionarDigito(c)` / `apagarDigito()` | Edit the value on the keypad                           |
| `processarOK()`                     | Validates the value, saves `current_sats` and creates invoice |
| `tratarToque(px, py)`               | Touch hit-test by screen                                    |
| `verificarTouch()`                  | XPT2046 read + debounce + pressure filter                   |
| `mostrarInformacoesSistema(out)`    | Status dump (Wi-Fi, IP, Bluetooth, heap…) on the stream     |
| `despacharComando(input, out)`      | Routes a serial command to the matching action              |
| `lerComando(in, buf, out)`          | Reads a line from a stream (USB or BT) and dispatches       |

---

## 8. Global state variables

| Variable              | Type           | Description                                          |
|-----------------------|----------------|------------------------------------------------------|
| `currentScreen`       | enum           | Current screen (state machine)                       |
| `input_amount`        | `String`       | Value being typed (in sats)                          |
| `current_sats`        | `long`         | Active invoice value                                 |
| `current_payment_hash`| `String`       | `payment_hash` of the active invoice                 |
| `current_bolt11`      | `String`       | BOLT11 invoice (shown as QR)                         |
| `btHabilitado`        | `bool`         | Bluetooth on/off (BT button)                         |
| `bt_pin`              | `String`       | Pairing PIN (saved, not enforced on core 3.x)        |
| `pagamentoConfirmado` | `bool`         | Result of the last invoice (paid?)                   |
| `relatorioBTpendente` | `bool`         | there is a result to report to the BT client after reconnect |
| `btAutoReligarEm`     | `unsigned long`| Re-enables BT automatically (BT-originated invoice only) |
| `autoCheckAte`        | `unsigned long`| Auto-check runs only until this instant (0 = no limit; only BT-originated invoices have a limit) |
| `bolt11Relatorio`     | `String`       | Copy of the BOLT11 for the post-BT report            |
| `relatorioBTTexto`    | `String`       | Result to report to the BT client after reconnect (C/L) |

---

## 9. Notes / security

- **Hardcoded credentials** (SSID, Wi-Fi password and LNbits `X-Api-Key`) are in the
  source code (as placeholders). In production they should come from config/NVS/secret store.
- **Insecure TLS**: in `https://` mode, `client.setInsecure()` disables certificate
  validation. The code itself recommends validating the certificate after confirming
  the communication. To coexist with Bluetooth, prefer an `http://` endpoint (see the
  heap note in the resources section).
- **`String` JSON concatenation**: the payload is assembled manually. Using
  `ArduinoJson` to serialize would avoid injection/format-corruption risk (the value
  comes from a `long`, so the risk here is low).
- **QR limited to 450 characters**: longer BOLT11 invoices are rejected with the
  "INVOICE MUITO LONGA" message.
- **8-digit limit** on the keypad (`adicionarDigito`).

---

## 10. File map

```
ultimate_CYD_ESP32_BTC_POS/
├── ultimate_CYD_ESP32_BTC_POS.ino   ← main firmware (this document)
├── qrcode.c / qrcode.h              ← local QR-code library
└── libraries/
    └── TFT_eSPI/        ← display driver (local ILI9341 config)
```
