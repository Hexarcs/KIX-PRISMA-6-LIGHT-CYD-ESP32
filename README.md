# CYD POS KIX — README

Terminal de Point of Sale (PDV) em Bitcoin Lightning, rodando em um ESP32 com display touchscreen (placa CYD — *Cheap Yellow Display*, ESP32-2432S028). O dispositivo se conecta a uma carteira **LNbits** para gerar e acompanhar faturas Lightning (BOLT11), permitindo cobranças em satoshis diretamente pela tela touch.

---

## O que o firmware faz

1. **Conecta ao Wi-Fi** configurado no código (`ssid` / `password`).
2. Exibe um **teclado numérico** no display TFT (320x240 em modo paisagem), onde o operador digita o valor em **sats**.
3. Ao confirmar (`OK`), o ESP32 envia uma requisição à API do **LNbits** para criar uma *invoice* Lightning.
4. Mostra o **QR Code** da invoice na tela, junto com o valor a cobrar e botões de ação:
   - **CHECAR PAGAMENTO** — verifica manualmente se a invoice foi paga.
   - **VOLTAR** — cancela e retorna ao teclado.
5. Enquanto a tela de QR Code está ativa, o firmware **consulta o LNbits automaticamente a cada 3 segundos** (verificação silenciosa).
6. Ao detectar pagamento, mostra a tela verde **"PAGO COM SUCESSO!"** e retorna ao teclado após 5 segundos ou ao toque.
7. Exibe uma tela de erro em caso de falhas HTTP, JSON ou geração de QR Code.

---

## Hardware necessário

- Placa **CYD (ESP32-2432S028)** — ESP32 com TFT de 2,8" ILI9341 e touch resistivo XPT2046.
- Cabo USB para gravação e alimentação.
- Rede Wi-Fi com acesso à internet para alcançar o servidor LNbits.

### Pinos do touch

| Sinal | GPIO |
| :--- | :---: |
| XPT2046 IRQ | 36 |
| XPT2046 MOSI | 32 |
| XPT2046 MISO | 39 |
| XPT2046 CLK | 25 |
| XPT2046 CS | 33 |

> **Atenção:** a biblioteca `TFT_eSPI` precisa estar configurada para a placa CYD. No arquivo `User_Setup.h`, confira os parâmetros abaixo e ajuste conforme sua placa:

```cpp
#define ILI9341_DRIVER
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1
#define TFT_BL   21
```

---

## Configuração necessária antes de compilar

No topo do arquivo `.ino`, configure suas credenciais Wi-Fi e os dados de acesso à API LNbits:

```cpp
const char* ssid     = "SUA_REDE_WIFI";
const char* password = "SUA_SENHA_WIFI";

String lnbits_url        = "https://SEU-SERVIDOR-LNBITS/api/v1/payments";
String lnbits_wallet_url = "https://SEU-SERVIDOR-LNBITS/api/v1/wallet";
String lnbits_api_key    = "SUA_CHAVE_DE_API_LNBITS";
```

A **API key** deve pertencer a uma carteira LNbits com permissões para criar invoices (`invoice:create`) e consultar pagamentos (`payments:read`), conforme a configuração da sua instância.

### Bibliotecas necessárias

Instale as seguintes bibliotecas pelo Arduino Library Manager:

- `TFT_eSPI` — Bodmer.
- `XPT2046_Touchscreen` — Paul Stoffregen.
- `ArduinoJson` — Benoit Blanchon, versão 6.
- `qrcode` — biblioteca que disponibiliza `qrcode.h`.
- `WiFi`, `HTTPClient` e `SPI` — incluídas no core ESP32.

---

## Como gravar o firmware

O projeto pressupõe que o `arduino-cli` esteja instalado e que o core ESP32 esteja configurado.

Instale o core, se necessário:

```bash
arduino-cli core install esp32:esp32
```

### 1. Compilar

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32 \
  ~/CYD_POS_KIX
```

### 2. Enviar para o ESP32

```bash
arduino-cli upload \
  -p /dev/ttyUSB0 \
  --fqbn esp32:esp32:esp32 \
  ~/CYD_POS_KIX
```

> **Dica:** se o upload travar, segure o botão `BOOT` da placa até a gravação começar e solte em seguida.

Ajuste `/dev/ttyUSB0` para a porta correta do seu sistema, como `/dev/ttyUSB1` ou `/dev/ttyACM0`.

Para descobrir as placas e portas disponíveis:

```bash
arduino-cli board list
```

### 3. Monitor Serial

```bash
arduino-cli monitor \
  -p /dev/ttyUSB0 \
  -c baudrate=115200
```

---

## Comandos de depuração via Serial

Configure o monitor serial para **115200 baud**. Envie os comandos com uma quebra de linha (`\n`) ao final.

| Comando | Ação |
| :--- | :--- |
| `I:100` | Cria uma invoice de 100 sats. |
| `C:` | Verifica manualmente o pagamento da invoice atual. |
| `P:` | Imprime o BOLT11 atual no monitor Serial. |
| `L:` | Consulta e exibe o saldo da carteira LNbits. |
| `H:` | Retorna à tela inicial (teclado) e limpa a invoice atual. |

---

## Fluxo de funcionamento

```text
[Wi-Fi]
   |
   v
[Teclado numérico]
   |
   v
[Confirmação do valor - OK]
   |
   v
[Criando invoice LNbits]
   |
   v
[QR Code + verificação automática a cada 3s]
   |
   v
[Pagamento confirmado]
   |
   v
[Tela "PAGO COM SUCESSO!"]
   |
   v
[Retorno ao teclado]
```

---

## Observações importantes

- **Limite do QR Code:** o firmware utiliza QR Code versão 14 com correção de erros `ECC_LOW`. Invoices BOLT11 com mais de aproximadamente 450 caracteres podem exibir o aviso `INVOICE MUITO LONGA` em vez do QR Code.
- **Calibração do touch:** os parâmetros `TOUCH_MIN_X`, `TOUCH_MAX_X`, `TOUCH_MIN_Y` e `TOUCH_MAX_Y` podem exigir ajustes dependendo da unidade CYD. O firmware imprime no monitor Serial os valores brutos e mapeados de cada toque para facilitar a calibração.
- **Conectividade:** o servidor LNbits precisa estar acessível pela rede Wi-Fi do ESP32. O código utiliza URLs `https://`.
- **Segurança HTTPS:** não utilize o dispositivo em produção sem verificar a validação dos certificados TLS e a segurança das requisições HTTP utilizadas pelo firmware.
- **Proteção das credenciais:** nunca publique sua senha Wi-Fi ou a variável `lnbits_api_key` com credenciais reais em um repositório público do GitHub. Considere utilizar configurações locais excluídas pelo `.gitignore`.

---

## Licença

Uso livre para fins pessoais e educacionais. Verifique as licenças das bibliotecas de terceiros utilizadas neste projeto.
