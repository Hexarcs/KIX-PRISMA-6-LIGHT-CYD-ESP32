> 🇬🇧 [Read in English](architecture.md)

# KIX POS — Documentação da Arquitetura (PT-BR)

> Documentação gerada a partir de `ultimate_CYD_ESP32_BTC_POS.ino`.
> O firmware implementa um **ponto de venda (PDV) Lightning Network** sobre um
> **ESP32**, usando tela TFT 240×320 (retrato), touch **resistivo XPT2046** e a
> API da **LNbits** para gerar/verificar faturas (invoices) BOLT11.

---

## 1. Visão geral

O aparelho é um terminal de pagamento Lightning:

1. O operador digita o valor em **sats** no teclado virtual.
2. O firmware chama a LNbits para **criar uma invoice** (BOLT11).
3. A invoice é exibida como **QR Code** na tela.
4. O cliente paga com a carteira Lightning (escaneando o QR).
5. O firmware **consulta a invoice a cada 3 s** até detectar o pagamento.
6. Ao confirmar, mostra a tela **"PAGO COM SUCESSO!"** e volta ao teclado.

Além da interface touch, existe uma **interface serial** (UART) para controlar o
mesmo fluxo por comandos de texto.

---

## 2. Stack de hardware

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
   │                    |  WiFi/HTTPS (inseguro) ───────► LNbits |
   │                    |         SEU_DOMINIO          |
   └────────────────────┘                                        |
                        +----------------------------------------+

   ┌───────────────┐        ┌───────────────┐        ┌───────────────┐
   │  TFT 240×320  │        │ XPT2046 Touch │        │   LNbits API  │
   │  (retrato)    │        │  resistivo    │        │  (Lightning)  │
   └───────────────┘        └───────────────┘        └───────────────┘
```

| Recurso           | Configuração                                      | Pinos            |
|-------------------|---------------------------------------------------|------------------|
| Display           | `TFT_eSPI`, rotação `2` (retrato), 240×320, `ILI9341_2_DRIVER` + `TFT_INVERSION_ON` | via SPI + `TFT_BL` |
| Touch resistivo   | `XPT2046_Touchscreen` (SPI/VSPI), 240×320         | MOSI=32, MISO=39, CLK=25, CS=33, IRQ=36 |
| Rede              | Wi-Fi + `HTTPClient` (`WiFiClient`/`WiFiClientSecure` adaptativo) | — |
| Bluetooth         | `BluetoothSerial` (SPP), pareamento Just Works    | —                |
| JSON              | `ArduinoJson`                                     | —                |
| QR Code           | `qrcode.h` (versão 14, `ECC_LOW`)                 | —                |

> **Cor de display:** este painel ILI9341 é "nativamente invertido". A `TFT_eSPI`
> local define `ILI9341_2_DRIVER` **e** `#define TFT_INVERSION_ON` em
> `libraries/TFT_eSPI/User_Setup.h`; sem essa linha as cores saem em negativo
> (preto vira branco, verde vira lilás).

> O transporte é escolhido pela URL: `http://` usa `WiFiClient` (leve) e `https://`
> usa `WiFiClientSecure` com `setInsecure()` (sem validar o certificado — modo de
> diagnóstico). **HTTPS + Bluetooth ligado pode estourar o heap do ESP32** e fazer
> a criação de invoice falhar; para usar Bluetooth e invoice ao mesmo tempo, use um
> endpoint `http://` (como o projeto baremetal) ou desligue o BT.

---

## 3. Arquitetura de software

### 3.1 Máquina de estados (telas)

O firmware é dirigido por uma **enum de estados** (`ScreenState`) e uma variável
global `currentScreen`:

```
                 ┌──────────────┐
    boot ──────► │ CONNECTING   │  conecta Wi-Fi (timeout de 15 s)
                 └──────┬───────┘
                        │ (sempre)
                        ▼
                 ┌──────────────┐
         ┌──────►│   KEYPAD     │  entrada do valor em sats
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
         │       │ pagamento    │ VOLTAR / toque
         │       │ confirmado   │
         │       ▼             │
         │  ┌──────────┐       │
         │  │  PAID    │       │
         │  └────┬─────┘       │
         │       │ 5 s / toque │
         └───────┴─────────────┘
```

| Estado              | Tela                               | Função que desenha          |
|---------------------|------------------------------------|-----------------------------|
| `SCREEN_CONNECTING` | "Conectando WiFi…"                 | `connectWiFi()`             |
| `SCREEN_KEYPAD`     | Teclado numérico 3×4               | `desenharTelaTeclado()`     |
| `SCREEN_LOADING`    | "CRIANDO INVOICE…"                 | `desenharTelaCarregando()`  |
| `SCREEN_QR_CODE`    | QR Code da invoice + valor         | `desenharTelaQRCode()`      |
| `SCREEN_PAID`       | "PAGO COM SUCESSO!"                | `desenharTelaPago()`        |
| `SCREEN_ERROR`      | Título + detalhe + botão VOLTAR    | `desenharTelaErro()`        |

---

### 3.2 Fluxo principal (`loop`)

O `loop()` é **não-bloqueante** e executa sete fases a cada iteração:

```
   ┌──────────────────────────────────────────────────────────────┐
   │                                                              │
   │                            loop()                            │
   │                                                              │
   │  1. RECEPÇÃO SERIAL   (não-bloqueante, UART + BT SPP)        │
   │          │                                                   │
   │          ▼                                                   │
   │  2. TOUCH             (debounce 180 ms + hit-test)           │
   │          │                                                   │
   │          ▼                                                   │
   │  3. DEBUG             (heartbeat a cada 30 s no Serial)      │
   │          │                                                   │
   │          ▼                                                   │
   │  4. AUTO-CHECK        (a cada 3 s se estiver em QR_CODE)     │
   │          │                                                   │
   │          ▼                                                   │
   │  5. AUTO-RETORNO      (5 s após PAID → volta ao teclado)     │
   │          │                                                   │
   │          ▼                                                   │
   │  6. RE-LIGAR BT       (operação via BT → +10 s)              │
   │          │                                                   │
   │          ▼                                                   │
   │  7. RELATÓRIO BT      (reconectar → PAGO + BOLT11)           │
   └──────────────────────────────────────────────────────────────┘
```

As fases 6 e 7 se aplicam apenas a operações solicitadas via **Bluetooth**: a fase 6 religa o Bluetooth após 10 s (e, para invoices, também encerra o auto-check aos 10 s — touch/USB continuam verificando), e a fase 7 reporta o resultado (`PAGO` / não pago + BOLT11) ao cliente assim que ele reconecta.

Timers usados (variáveis globais `unsigned long`):

| Timer               | Intervalo | Uso                                            |
|---------------------|-----------|------------------------------------------------|
| `lastTouchTime`     | 180 ms    | Debounce do touch                              |
| `lastAutoCheckTime` | 3000 ms   | Poll da invoice na tela QR                     |
| `paidScreenTime`    | 5000 ms   | Retorno automático após pagamento              |
| `lastDebugPrintTime`| 30000 ms  | Heartbeat de debug no Serial                   |

---

## 4. Comunicação com a LNbits

Endpoints:

| Operação          | Método | URL                                                        |
|-------------------|--------|------------------------------------------------------------|
| Criar invoice     | `POST` | `https://SEU_DOMINIO/api/v1/payments`             |
| Verificar invoice | `GET`  | `.../api/v1/payments/{payment_hash}`                       |
| Saldo da carteira | `GET`  | `https://SEU_DOMINIO/api/v1/wallet`               |

Autenticação via header `X-Api-Key` (chave hardcoded no topo do arquivo).

### 4.1 Criação de invoice — `createInvoice(sats)`

```
  createInvoice(sats)
        │
        ├─ Wi-Fi conectado? ── não ──► tela ERROR "SEM WIFI"
        │
        ├─ POST /api/v1/payments
        │     payload: {"out": false, "amount": sats, "memo": "KIX-POS"}
        │
        ├─ HTTP 200/201? ── não ──► tela ERROR "HTTP <code>"
        │
        ├─ parse JSON → payment_request (BOLT11) + payment_hash
        │
        └─ desenharTelaQRCode()  (gera QR do BOLT11)
```

### 4.2 Verificação de pagamento

```
   verificarPagamentoSilencioso()  (auto, a cada 3 s)
   manualCheck()                   (botão CHECAR ou comando serial "C")
        │
        └─ GET /payments/{hash}
              │
              ├─ 200 + paid==true  ─► desenharTelaPago()
              ├─ 200 + paid==false ─► status "NAO PAGO AINDA"
              └─ erro              ─► status "ERRO: <code>"
```

### 4.3 Consulta de saldo — `checkBalance()`

`GET /wallet` → extrai `balance_msat` → divide por 1000 → exibe "SALDO DA CARTEIRA".

---

## 5. Interface touch (XPT2046 resistivo)

- `verificarTouch()` lê o **XPT2046** via SPI, descarta toques fracos (pressão
  `p.z <= 200`) e mapeia as coordenadas brutas `p.x`/`p.y` para o retângulo 240×320
  usando `TOUCH_MIN_X/MAX_X` e `TOUCH_MIN_Y/MAX_Y`. O debounce é de 180 ms.
- As faixas de calibração estão definidas no topo do `.ino`:
  `TOUCH_MIN_X 200`, `TOUCH_MAX_X 3900`, `TOUCH_MIN_Y 240`, `TOUCH_MAX_Y 3800`
  (ajuste conforme a sua placa; o firmware imprime os valores brutos no Serial para
  facilitar a calibração).
- `tratarToque(px, py)` faz o **hit-test** por região, dependendo da tela atual.

### 5.1 Teclado numérico (layout vertical 3×4)

As coordenadas desenhadas e o hit-test devem bater entre si:

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

> ⚠️ **Observação:** as faixas de `y` do hit-test usam fronteiras levemente
> diferentes das posições desenhadas (ex.: tecla "1" é desenhada em `y=130`
> com altura 42 → termina em 172, mas o teste considera `130..173`). No geral
> as regiões são contíguas, então o acerto é aceitável, mas há uma pequena
> defasagem de 1–2 px em algumas bordas.

### 5.2 Telas de QR / Paid / Error

| Tela           | Ação do toque                                             |
|----------------|-----------------------------------------------------------|
| `QR_CODE`      | `px<120` → `manualCheck()`; senão → volta ao teclado      |
| `PAID`/`ERROR` | Qualquer toque → limpa estado e volta ao teclado           |

---

## 6. Interface serial (UART 115200 baud + Bluetooth SPP)

A mesma sintaxe é aceita pelo **USB serial** e por um terminal **Bluetooth SPP**
(pareie com o dispositivo **`KIX-POS`**; pareamento "Just Works", sem PIN — o core
3.x não suporta PIN fixo). Comandos terminados por `\n` ou `\r`:

| Comando       | Ação                                      | Função chamada             |
|---------------|-------------------------------------------|----------------------------|
| `I:<valor>`   | Cria invoice com o valor em sats          | `createInvoice()`          |
| `C` / `C:`    | Verifica pagamento da invoice ativa       | `manualCheck()`            |
| `L` / `L:`    | Consulta saldo da carteira                | `checkBalance()`           |
| `P` / `P:`    | Imprime a invoice BOLT11 ativa            | (direto no stream)         |
| `H`           | Volta para a tela inicial (teclado)       | `desenharTelaTeclado()`    |
| `H:`          | Mostra status do sistema                  | `mostrarInformacoesSistema()` |
| `help`         | Lista os comandos disponíveis          | (direto no stream)        |

Exemplo de fluxo serial:

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

## 7. Referência de funções

| Função                           | Papel                                                        |
|----------------------------------|--------------------------------------------------------------|
| `kickstartHardware()`            | Liga o backlight (`TFT_BL`) se definido                       |
| `desenharBotao(...)`             | Desenha botão arredondado com texto centralizado              |
| `atualizarVisor()`               | Redesenha o valor digitado no display do teclado              |
| `desenharTelaTeclado()`          | Tela do teclado (header KIX POS + ON/OFFLINE + 3×4)           |
| `desenharTelaCarregando()`       | Tela "CRIANDO INVOICE…"                                       |
| `desenharTelaQRCode()`           | Gera/renderiza QR do BOLT11 + botões CHECAR/VOLTAR            |
| `desenharTelaPago()`             | Tela verde de sucesso                                         |
| `desenharTelaErro(titulo, det)`  | Tela de erro com botão VOLTAR                                 |
| `connectWiFi()`                  | Conecta ao Wi-Fi (timeout 15 s) e vai ao teclado              |
| `iniciarConfiguracaoWiFi()` | Abre portal AP (WiFiManager) p/ Wi-Fi, API e PIN           |
| `carregarConfigLNbits()`    | Carrega URL/key/PIN do NVS (fallback: hardcoded)           |
| `salvarConfigLNbits()`      | Grava URL/key/PIN no NVS                                   |
| `iniciarBluetooth()`        | Liga o Bluetooth SPP (pareamento Just Works, sem PIN)         |
| `alternarBluetooth()`       | Liga/desliga o Bluetooth (botão BT) e redesenha o teclado   |
| `prepararHTTP(...)`        | Escolhe `WiFiClient`/`WiFiClientSecure` conforme a URL       |
| `desligarBTParaTLS(avisarVoltar10s)` | Desliga o BT (se ligado) antes de uma operação HTTPS; avisa "volte em 10s" se veio do BT          |
| `solicitarInvoice(sats, origemBT)` | Desliga o BT, cria a invoice; religa em 10s (e encerra o auto-check) se veio do BT |
| `reportarStatusInvoiceBT()`| Reporta PAGO / não pago + BOLT11 ao cliente BT reconectado    |
| `agendarResultadoBT(msg)` | Agenda um resultado para enviar ao cliente BT após reconectar |
| `checkBalance(origemBT)` | Consulta saldo da carteira; religa o BT em 10s se veio do BT                                    |
| `createInvoice(sats)`            | Cria invoice na LNbits e mostra QR                            |
| `manualCheck(origemBT)` | Verificação manual de pagamento; religa o BT em 10s se veio do BT                               |
| `verificarPagamentoSilencioso()` | Poll automático de pagamento (silencioso)                     |
| `adicionarDigito(c)` / `apagarDigito()` | Edição do valor no teclado                           |
| `processarOK()`                  | Valida valor, salva `current_sats` e cria invoice             |
| `tratarToque(px, py)`            | Hit-test do touch por tela                                    |
| `verificarTouch()`               | Leitura + debounce + filtro de pressão do XPT2046             |
| `mostrarInformacoesSistema(out)` | Dump de status (Wi-Fi, IP, Bluetooth, heap…) no stream       |
| `despacharComando(input, out)`   | Roteia um comando serial para a ação correspondente          |
| `lerComando(in, buf, out)`       | Lê uma linha de um stream (USB ou BT) e despacha             |

---

## 8. Variáveis de estado globais

| Variável               | Tipo     | Descrição                                       |
|------------------------|----------|-------------------------------------------------|
| `currentScreen`        | enum     | Tela atual (máquina de estados)                 |
| `input_amount`         | `String` | Valor sendo digitado (em sats)                  |
| `current_sats`         | `long`   | Valor da invoice ativa                          |
| `current_payment_hash` | `String` | `payment_hash` da invoice ativa                 |
| `current_bolt11`       | `String` | Invoice BOLT11 (exibida como QR)                |
| `btHabilitado`         | `bool`   | Bluetooth ligado/desligado (botão BT)          |
| `bt_pin`               | `String` | PIN de pareamento (salvo, não aplicado no core 3.x) |
| `pagamentoConfirmado`  | `bool`   | resultado do último invoice (pago?)                |
| `relatorioBTpendente`  | `bool`   | há resultado a reportar ao cliente BT após reconectar |
| `btAutoReligarEm`      | `unsigned long` | religa o BT automaticamente (só invoice via BT) |
| `autoCheckAte`         | `unsigned long` | auto-check roda só até este instante (0 = sem limite; só invoice via BT tem limite) |
| `bolt11Relatorio`      | `String` | cópia do BOLT11 para o reporte pós-BT              |
| `relatorioBTTexto`     | `String`  | resultado a reportar ao cliente BT após reconectar (C/L) |

---

## 9. Pontos de atenção / segurança

- **Credenciais hardcoded** (SSID, senha Wi-Fi e `X-Api-Key` da LNbits) estão no
  código-fonte (como placeholders). Em produção, devem vir de config/NVS/secret store.
- **TLS inseguro**: no modo `https://`, `client.setInsecure()` desabilita a
  validação do certificado. O próprio código recomenda validar o certificado após
  confirmar a comunicação. Para coexistir com o Bluetooth, prefira um endpoint
  `http://` (veja a nota de heap na seção de recursos).
- **`String` para concatenação de JSON**: o payload é montado manualmente. Usar
  `ArduinoJson` para serializar evitaria risco de injeção/corrupção de formato
  (o valor vem de `long`, então o risco aqui é baixo).
- **QR limitado a 450 caracteres**: invoices BOLT11 mais longas são recusadas com a
  mensagem "INVOICE MUITO LONGA".
- **Limite de 8 dígitos** no teclado (`adicionarDigito`).

---

## 10. Mapa de arquivos

```
ultimate_CYD_ESP32_BTC_POS/
├── ultimate_CYD_ESP32_BTC_POS.ino   ← firmware principal (este documento)
├── qrcode.c / qrcode.h              ← biblioteca de QR Code local
└── libraries/
    └── TFT_eSPI/        ← driver do display (config ILI9341 local)
```
