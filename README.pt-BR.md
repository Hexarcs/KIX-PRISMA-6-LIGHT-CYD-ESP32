> 🇬🇧 [Read this page in English](README.md)

# KIX POS

Um **terminal de ponto de venda (PoS) da Lightning Network** para o **ESP32**. Ele
usa uma tela TFT 240×320 (retrato), um touch **resistivo XPT2046** e a API da
**LNbits** para criar e verificar faturas (invoices) BOLT11.

> O operador digita um valor em **satoshis**, o dispositivo cria uma invoice
> Lightning, exibe-a como um **QR code**, consulta até o cliente pagar e então
> mostra uma tela **"PAGO COM SUCESSO!"** e volta ao teclado.

---

## Recursos

- Interface touch 240×320 em retrato com teclado numérico (3×4).
- Criação de invoices Lightning via LNbits (`POST /api/v1/payments`).
- Renderização do QR code da invoice BOLT11 (biblioteca local `qrcode.c`).
- Consulta automática de pagamento a cada 3 s (`GET /api/v1/payments/{hash}`).
- Consulta de saldo da carteira (`GET /api/v1/wallet`).
- **Portal de configuração** no próprio dispositivo — um pequeno botão `WIFI` abre um
  captive portal para configurar Wi-Fi, URL da LNbits, chave de API e PIN de pareamento
  Bluetooth a partir do navegador do celular.
- Um **botão `BT`** ao lado do `WIFI` liga/desliga o servidor serial Bluetooth.
- Interface de comandos por **serial (UART) e Bluetooth SPP** — controle o dispositivo
  pelo monitor serial USB ou por um app de terminal Bluetooth do celular (pareie com
  **`KIX-POS`**, pareamento "Just Works" — sem PIN).
- Todas as dependências vendidas em `libraries/` (sem necessidade de `arduino-cli
  lib install`), incluindo uma **`TFT_eSPI`** com a configuração do display deste
  hardware, para a correção de cores não se perder em reinstalações.

---

## Hardware

- Placa de desenvolvimento **ESP32** com display + touch integrados.
- TFT **240×320** (driver ILI9341), rotação retrato.
- Touch **resistivo XPT2046** (SPI).
- A pinagem abaixo corresponde ao **ESP32-2432S028** ("Cheap Yellow Display" / CYD),
  variante com display ILI9341 e touch resistivo.

| Sinal               | GPIO |
|---------------------|------|
| TFT MISO            | 12   |
| TFT MOSI (SDA)      | 13   |
| TFT SCLK            | 14   |
| TFT CS              | 15   |
| TFT DC              | 2    |
| TFT RST             | -1 (não conectado) |
| Backlight BL        | 21   |
| Touch MOSI          | 32   |
| Touch MISO          | 39   |
| Touch CLK           | 25   |
| Touch CS            | 33   |
| Touch IRQ           | 36   |

O driver do display é configurado em
[`libraries/TFT_eSPI/User_Setup.h`](libraries/TFT_eSPI/User_Setup.h) com
`ILI9341_2_DRIVER`, 240×320, **mais** `#define TFT_INVERSION_ON`. Este painel é
"nativamente invertido" (sem essa linha as cores saem em negativo: preto vira branco,
verde vira lilás), então a linha de inversão é necessária para as cores corretas. O
firmware usa `tft.setRotation(2)` para a orientação retrato.

---

## Estrutura do repositório

```
.
├── README.md                               # este arquivo (em inglês)
├── README.pt-BR.md                         # este arquivo (tradução em PT-BR)
├── comandos_arduino_cli_gravar.txt         # comandos arduino-cli (PT-BR)
├── architecture.md                         # arquitetura detalhada (EN)
├── arquitetura.md                          # arquitetura detalhada (PT-BR)
├── ultimate_CYD_ESP32_BTC_POS.ino          # firmware principal
├── qrcode.c / qrcode.h                     # gerador de QR code local (MIT)
└── libraries/                              # bibliotecas vendidas
    ├── TFT_eSPI/              2.5.43       # driver do display (config ILI9341 local)
    ├── XPT2046_Touchscreen/   1.4          # driver do touch resistivo
    ├── ArduinoJson/           7.4.3        # parse/serialização JSON
    └── WiFiManager/           2.0.17       # captive portal (modo AP) p/ configurar Wi-Fi
```

> As quatro bibliotecas estão vendidas em `libraries/`, então você **não** precisa de
> `arduino-cli lib install`. A `TFT_eSPI` é mantida **local** porque carrega a
> configuração específica deste display.

> O `WiFiManager` é usado pelo botão `WIFI` do dispositivo para abrir um captive
> portal (modo AP) e guardar as credenciais de Wi-Fi escolhidas no NVS.

---

## Começando

### 1. Instale o Arduino CLI

```bash
# Linux/macOS (adapte a URL/versão conforme necessário)
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# ou baixe um release em:
# https://arduino.github.io/arduino-cli/installation/
```

Verifique:

```bash
arduino-cli version
```

### 2. Instale o core ESP32

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

### 3. Bibliotecas

Todas as bibliotecas estão **vendidas** em `libraries/` (`TFT_eSPI`,
`XPT2046_Touchscreen`, `ArduinoJson`, `WiFiManager`), então você **não** precisa de
`arduino-cli lib install`. Elas são encontradas automaticamente pelo flag
`--libraries` no comando de compilação abaixo.

### 4. Configure as credenciais

Edite o topo de `ultimate_CYD_ESP32_BTC_POS.ino`:

```cpp
const char* ssid     = "SEU_SSID";
const char* password = "SUA_SENHA";

String lnbits_url        = "https://SEU_DOMINIO/api/v1/payments";
String lnbits_wallet_url = "https://SEU_DOMINIO/api/v1/wallet";
String lnbits_api_key    = "SUA_CHAVE_API_LNBITS";
```

> ⚠️ As credenciais no arquivo são **placeholders**. Substitua-as pelos seus próprios
> valores antes de gravar. Nunca commite uma `X-Api-Key` real (o arquivo
> `credenciais.txt` já está no `.gitignore`).

> 💡 **Aviso de heap Bluetooth + HTTPS:** o handshake TLS do ESP32 precisa de um
> bloco grande de heap, e rodar **Bluetooth Classic ao mesmo tempo** pode fazer as
> chamadas HTTPS falharem (a criação de invoice só funciona com o Bluetooth
> desligado). O firmware agora detecta automaticamente `http://` vs `https://` na
> URL: `http://` usa um client leve e funciona junto com o Bluetooth. Se você quiser
> usar o terminal Bluetooth **e** criar invoices ao mesmo tempo, aponte o endpoint
> para **`http://`**. Com `https://`, desligue o Bluetooth (botão `BT`) antes de
> criar uma invoice.

**Configuração over-the-air (sem re-gravar):** o botão `WIFI` do teclado abre um
captive portal (`KIX-POS-Setup` / `kixpos123`). Conecte um celular a essa rede,
navegue até `192.168.4.1` e informe o Wi-Fi local. A mesma página tem campos para a
**URL da LNbits**, a **chave de API** e o **PIN do Bluetooth** (pré-preenchidos com os
valores atuais — deixe em branco para manter os atuais). Tudo é guardado no NVS e
usado automaticamente no próximo boot, recorrendo aos valores hardcoded acima apenas
quando nada foi salvo.

> ⚠️ **Nota sobre o PIN do Bluetooth:** o core **3.x do Arduino ESP32 removeu o
> pareamento por PIN fixo legado**, então o campo `Bluetooth PIN` é atualmente
> **salvo, mas não aplicado** — o pareamento usa "Just Works" (sem PIN). Um PIN fixo
> só funciona se o firmware for recompilado como um componente ESP-IDF com
> `CONFIG_BT_SSP_ENABLED` desabilitado.

---

## Gravando com o Arduino CLI

Rode estes comandos a partir da **raiz do repositório** (a pasta que contém
`ultimate_CYD_ESP32_BTC_POS.ino`).

> `--libraries` aponta para a pasta `libraries/` vendida. **Importante:** o
> `arduino-cli` não detecta sozinho a pasta `libraries/` do sketch, então sem esse
> flag ele usa as bibliotecas do sistema (e as cores ficam invertidas).

### Descubra a porta serial

```bash
arduino-cli board list
```

Valores típicos: `/dev/ttyUSB0` (Linux), `/dev/cu.usbserial-*` (macOS), `COM3` (Windows).

### Compilar

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=huge_app \
  --libraries libraries \
  --verbose \
  .
```

- `PartitionScheme=huge_app` reserva o máximo de espaço de aplicação (as bibliotecas
  de display/JSON são grandes; a partição padrão não cabe este firmware).

### Upload (gravar a placa)

```bash
arduino-cli upload \
  --verbose \
  -p /dev/ttyUSB0 \
  --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=huge_app \
  .
```

Substitua `/dev/ttyUSB0` pela porta de `arduino-cli board list`.

### Compilar + gravar em uma única etapa

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

### Monitor serial

```bash
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

### Exportar um binário compilado (opcional)

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=huge_app \
  --libraries libraries \
  --verbose \
  --output-dir ./build_output \
  .
```

A referência dos comandos também está em
[`comandos_arduino_cli_gravar.txt`](comandos_arduino_cli_gravar.txt) (PT-BR).

---

## Uso

### Na tela touch

1. Digite o valor em **sats** no teclado.
2. Pressione **OK** → a invoice é criada e exibida como um QR code.
3. O cliente escaneia e paga com uma carteira Lightning.
4. O dispositivo consulta automaticamente a cada 3 s (ou pressione **CHECAR**) →
   mostra **PAGO COM SUCESSO!**.
5. Pressione **VOLTAR** para voltar ao teclado.

Toque em **WIFI** (canto superior direito do teclado) para abrir o portal de
configuração de Wi-Fi descrito acima. O botão **`BT`** ao lado liga/desliga o
servidor serial Bluetooth (verde `BT ON` = ligado, cinza `BT OFF` = desligado).

### Via serial (USB ou Bluetooth)

Os mesmos comandos funcionam pelo monitor serial USB (115200 baud) e por um terminal
**Bluetooth SPP** — pareie com o dispositivo chamado **`KIX-POS`** (pareamento
"Just Works", sem PIN; veja a nota na seção de configuração acima).

| Comando   | Ação                                        |
|-----------|---------------------------------------------|
| `I:1500`  | Cria uma invoice de 1500 sats               |
| `C` / `C:`| Verifica o status de pagamento da invoice ativa |
| `L` / `L:`| Consulta o saldo da carteira                |
| `P` / `P:`| Imprime a invoice BOLT11 ativa (copiar/colar) |
| `H`       | Volta para a tela inicial (teclado)         |
| `H:`      | Mostra o status do sistema (Wi-Fi, IP, BT, heap…) |
| `help`    | Lista todos os comandos disponíveis         |

> 📱 **Workaround Bluetooth + TLS:** Bluetooth e HTTPS não podem compartilhar a
> memória do ESP32 ao mesmo tempo, então antes de qualquer chamada TLS (invoice,
> saldo, verificação) o dispositivo **desliga o Bluetooth automaticamente** e avisa
> pelo terminal para reconectar em 10 s. Qualquer operação solicitada **via
> Bluetooth** (`I:` invoice, `C` verificação, `L` saldo) religa o Bluetooth sozinha
> após **10 s**; no caso de invoice, a verificação automática de pagamento também
> roda apenas durante esses 10 s e depois para. Operações pela tela touch ou pela
> serial USB deixam o Bluetooth **desligado** — toque no botão **`BT`** para
> religá-lo. Quando um cliente Bluetooth reconecta, o dispositivo reporta o
> resultado pelo Bluetooth: `PAGO COM SUCESSO!` / o **BOLT11** para uma invoice, ou o
> resultado de verificação/saldo para `C` / `L`. Digite `help` para a lista de
> comandos.

Exemplo:

```
> I:1500
< [INVOICE] ===== INICIO =====
< ...
< [INVOICE] ===== FIM OK =====
> C
< [CHECK] PAGAMENTO CONFIRMADO!
```

---

## Como o código funciona

O firmware é uma **máquina de estados** dirigida por `currentScreen` (um `enum` com
seis estados):

| Estado              | Tela                           |
|---------------------|--------------------------------|
| `SCREEN_CONNECTING` | "Conectando WiFi…"             |
| `SCREEN_KEYPAD`     | Teclado numérico (3×4)         |
| `SCREEN_LOADING`    | "CRIANDO INVOICE…"             |
| `SCREEN_QR_CODE`    | QR code da invoice + valor     |
| `SCREEN_PAID`       | "PAGO COM SUCESSO!"            |
| `SCREEN_ERROR`      | Título + detalhe + botão VOLTAR |

O `loop()` principal é **não-bloqueante** e executa sete fases a cada iteração:

1. **Recepção serial** — lê a UART USB **e o Bluetooth SPP**, despachando os comandos
   `I:` / `C` / `L` / `P` / `H` / `H:` para o mesmo handler.
2. **Touch** — lê o XPT2046 com debounce de 180 ms e faz hit-test na tela atual.
3. **Heartbeat de debug** — imprime uma linha "loop alive" a cada 30 s.
4. **Auto-check** — se estiver na tela de QR, consulta a invoice a cada 3 s.
5. **Auto-retorno** — 5 s após "PAGO", volta ao teclado.
6. **Re-habilitar BT** — para uma operação solicitada via Bluetooth (invoice/verificação/saldo), religa o Bluetooth após 10 s (para invoices, o auto-check também para aos 10 s; USB/touch continuam verificando).
7. **Relatório BT** — quando um cliente reconecta, reporta `PAGO` / não pago + BOLT11.

### Fluxo de rede (LNbits)

- `createInvoice(sats)` → `POST /api/v1/payments` com
  `{"out": false, "amount": sats, "memo": "KIX-POS"}` → faz parse de
  `payment_request` (BOLT11) e `payment_hash` → renderiza o QR code.
- `verificarPagamentoSilencioso()` / `manualCheck()` → `GET /api/v1/payments/{hash}`
  → verifica o campo `paid`.
- `checkBalance()` → `GET /api/v1/wallet` → lê `balance_msat`, divide por 1000.

Todas as requisições autenticam com o header `X-Api-Key`.

> ⚠️ **Nota de segurança:** o código usa `client.setInsecure()` (sem validação do
> certificado TLS) e hardcoda credenciais no topo do arquivo. Isso é aceitável para
> diagnóstico, mas **não** para produção — valide o certificado TLS e mova os
> segredos para config/NVS.

Para um passo a passo completo (diagramas ASCII, referência de funções, notas de
segurança), veja [`architecture.md`](architecture.md) (EN) ou
[`arquitetura.md`](arquitetura.md) (PT-BR).

---

## Licença

O firmware principal e o `qrcode.c` incluído (baseado no gerador de QR code de
Nayuki) são fornecidos sob a licença MIT. As bibliotecas vendidas mantêm suas
próprias licenças (veja cada pasta `libraries/*`). Adicione um arquivo `LICENSE` no
topo se você planeja distribuir este projeto.
