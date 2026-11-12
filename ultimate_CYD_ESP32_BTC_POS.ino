#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <BluetoothSerial.h>
// WiFiManager (e o WebServer que ele usa) deve ser incluído ANTES do TFT_eSPI:
// o TFT_eSPI (com SMOOTH_FONT) define FS_NO_GLOBALS, o que remove o alias global
// "FS" de FS.h. Incluir o WiFiManager primeiro evita "FS was not declared" no
// WebServer da placa (ESP32 core 3.x).
#include <WiFiManager.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "qrcode.h"

/* --- CONFIGURAÇÃO WI-FI E LNBITS --- */
const char* ssid = "SEU_SSID";
const char* password = "SUA_SENHA";

/* --- CREDENCIAIS DO PORTAL DE CONFIGURAÇÃO (AP MODE) --- */
// Rede que a placa abre quando o botão "WIFI" é tocado.
// O cliente conecta o celular nessa rede e configura o WiFi pelo navegador.
const char* WIFI_AP_SSID = "KIX-POS-Setup";
const char* WIFI_AP_PASS = "kixpos123";

String lnbits_url = "https://SEU_DOMINIO/api/v1/payments";
String lnbits_wallet_url = "https://SEU_DOMINIO/api/v1/wallet";
String lnbits_api_key = "SUA_CHAVE_API_LNBITS";

/* --- DEFINIÇÕES DE PINOS DO TOUCH RESISTIVO (XPT2046 / CYD ESP32-2432S028) --- */
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

/* --- CALIBRAÇÃO DO TOUCH RESISTIVO (MODO RETRATO 240x320) --- */
// Ponto de partida (faixas típicas da CYD). Calibre na sua placa usando os
// valores brutos impressos em verificarTouch() e ajuste min/max conforme o toque real.
#define TOUCH_MIN_X  200
#define TOUCH_MAX_X  3900
#define TOUCH_MIN_Y  240
#define TOUCH_MAX_Y  3800

/* --- OBJETOS DE HARDWARE --- */
SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

/* --- SERIAL BLUETOOTH (SPP) --- */
BluetoothSerial SerialBT;
bool btHabilitado = true;   // estado atual do Bluetooth (true = ligado)
String bt_pin = "1234";     // PIN de pareamento (padrão hardcoded)

/* --- ESTADOS DAS TELAS --- */
enum ScreenState {
  SCREEN_CONNECTING,
  SCREEN_KEYPAD,
  SCREEN_LOADING,
  SCREEN_QR_CODE,
  SCREEN_PAID,
  SCREEN_ERROR
};

ScreenState currentScreen = SCREEN_CONNECTING;

/* --- VARIÁVEIS GLOBAIS --- */
String input_amount = "";
long current_sats = 0;
String current_payment_hash = "";
String current_bolt11 = "";

unsigned long lastTouchTime = 0;
unsigned long lastAutoCheckTime = 0;
unsigned long paidScreenTime = 0;
bool wasTouched = false;

/* --- BLUETOOTH DESLIGADO AUTOMATICAMENTE P/ OPERAÇÕES TLS --- */
bool pagamentoConfirmado = false;    // resultado do último invoice (true = pago)
bool relatorioBTpendente = false;    // há resultado a reportar ao cliente BT após reconectar
unsigned long btAutoReligarEm = 0;   // religa o BT automaticamente neste instante (só invoice via BT)
unsigned long autoCheckAte = 0;      // auto-check roda só até este instante (0 = sem limite; só invoice via BT tem limite)
String bolt11Relatorio = "";         // cópia do BOLT11 para o reporte (sobrevive ao VOLTAR/auto-return)
String relatorioBTTexto = "";        // resultado a reportar ao cliente BT após reconectar (C/L)

/* --- DECLARAÇÃO DE FUNÇÕES --- */
void kickstartHardware();
void desenharBotao(int x, int y, int w, int h, const char* texto, uint16_t corFundo, uint16_t corTexto = 0xFFFF, uint8_t textSize = 2);
void atualizarVisor();
void desenharTelaTeclado();
void desenharTelaCarregando();
void updateQRStatus(String msg, uint16_t color);
void desenharTelaQRCode();
void desenharTelaPago();
void desenharTelaErro(String titulo, String detalhe);
void connectWiFi();
void checkBalance(bool origemBT = false);
void createInvoice(int sats);
void manualCheck(bool origemBT = false);
void verificarPagamentoSilencioso();
void adicionarDigito(char d);
void apagarDigito();
void processarOK();
void tratarToque(int px, int py);
void verificarTouch();
void mostrarInformacoesSistema(Print& out = Serial);
void iniciarConfiguracaoWiFi();
void carregarConfigLNbits();
void salvarConfigLNbits();
void iniciarBluetooth();
void alternarBluetooth();
void desligarBTParaTLS(bool avisarVoltar10s = false);
void solicitarInvoice(int sats, bool origemBT);
void reportarStatusInvoiceBT();
void agendarResultadoBT(const String& msg);
void despacharComando(const String& input, Print& out);
void lerComando(Stream& in, String& buffer, Print& out);

/* --- FUNÇÕES DE HARDWARE --- */
void kickstartHardware() {
  #ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  #endif
}

/* --- FUNÇÃO AUXILIAR PARA DESENHAR BOTÕES --- */
void desenharBotao(int x, int y, int w, int h, const char* texto, uint16_t corFundo, uint16_t corTexto, uint8_t textSize) {
  tft.fillRoundRect(x, y, w, h, 6, corFundo);
  tft.drawRoundRect(x, y, w, h, 6, 0xFFFF);
  tft.setTextColor(corTexto, corFundo);
  tft.setTextSize(textSize);

  int textWidth = strlen(texto) * 6 * textSize;
  int textHeight = 8 * textSize;
  int tx = x + (w - textWidth) / 2;
  int ty = y + (h - textHeight) / 2;
  tft.setCursor(tx, ty);
  tft.print(texto);
}

/* --- ATUALIZAÇÃO DO VISOR DO TECLADO (VERTICAL 240x320) --- */
void atualizarVisor() {
  tft.fillRoundRect(10, 40, 220, 54, 4, 0x0000);
  tft.drawRoundRect(10, 40, 220, 54, 4, 0x4A69);

  tft.setCursor(20, 52);
  if (input_amount.length() > 0) {
    tft.setTextColor(0x07E0, 0x0000); // Verde brilhante
    tft.setTextSize(3);
    tft.print(input_amount);
  } else {
    tft.setTextColor(0x7BEF, 0x0000); // Cinza
    tft.setTextSize(3);
    tft.print("0");
  }

  tft.setTextSize(2);
  tft.setTextColor(0xFFFF, 0x0000);
  tft.setCursor(20, 100);
  tft.print("SATS");
}

/* --- TELA 1: TECLADO PDV VERTICAL --- */
void desenharTelaTeclado() {
  currentScreen = SCREEN_KEYPAD;
  tft.fillScreen(0x0000);

  tft.setTextColor(0xFDA0, 0x0000);
  tft.setTextSize(2);
  tft.drawString("KIX POS", 8, 8);

  tft.setTextSize(1);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(0x07E0, 0x0000);
    tft.drawString("ONLINE", 8, 30);
  } else {
    tft.setTextColor(0xF800, 0x0000);
    tft.drawString("OFFLINE", 8, 30);
  }

  // Botão Bluetooth (liga/desliga) — canto superior direito, ao lado do WIFI
  desenharBotao(98, 6, 62, 28, btHabilitado ? "BT ON" : "BT OFF",
                btHabilitado ? 0x05E0 : 0x4208, 0xFFFF, 1);

  // Botão pequeno de configuração WiFi (canto superior direito)
  desenharBotao(168, 6, 64, 28, "WIFI", 0x2124, 0xFFFF, 2);

  atualizarVisor();

  // Teclado numérico em layout vertical (3 colunas x 4 linhas)
  // Largura dos botões: ~66px cada com espaçamento
  // Linha 1: [ 1 ] [ 2 ] [ 3 ]
  desenharBotao(10,  130, 66, 42, "1", 0x2124, 0xFFFF, 3);
  desenharBotao(87,  130, 66, 42, "2", 0x2124, 0xFFFF, 3);
  desenharBotao(164, 130, 66, 42, "3", 0x2124, 0xFFFF, 3);

  // Linha 2: [ 4 ] [ 5 ] [ 6 ]
  desenharBotao(10,  178, 66, 42, "4", 0x2124, 0xFFFF, 3);
  desenharBotao(87,  178, 66, 42, "5", 0x2124, 0xFFFF, 3);
  desenharBotao(164, 178, 66, 42, "6", 0x2124, 0xFFFF, 3);

  // Linha 3: [ 7 ] [ 8 ] [ 9 ]
  desenharBotao(10,  226, 66, 42, "7", 0x2124, 0xFFFF, 3);
  desenharBotao(87,  226, 66, 42, "8", 0x2124, 0xFFFF, 3);
  desenharBotao(164, 226, 66, 42, "9", 0x2124, 0xFFFF, 3);

  // Linha 4: [ DEL ] [ 0 ] [ OK ]
  desenharBotao(10,  274, 66, 38, "DEL", 0x8800, 0xFFFF, 2);
  desenharBotao(87,  274, 66, 38, "0",   0x2124, 0xFFFF, 3);
  desenharBotao(164, 274, 66, 38, "OK",  0x05E0, 0xFFFF, 3);
}

/* --- TELA: CARREGANDO FATURA --- */
void desenharTelaCarregando() {
  currentScreen = SCREEN_LOADING;
  tft.fillScreen(0x0000);
  tft.setTextColor(0x07FF, 0x0000);
  tft.setTextSize(2);
  tft.drawCentreString("CRIANDO INVOICE...", 120, 100, 1);

  tft.setTextColor(0xFFE0, 0x0000);
  tft.setTextSize(3);
  tft.drawCentreString(String(current_sats) + " SATS", 120, 140, 1);

  tft.setTextColor(0x7BEF, 0x0000);
  tft.setTextSize(1);
  tft.drawCentreString("Aguarde...", 120, 190, 1);
}

/* --- TELA 2: QR CODE DA FATURA (VERTICAL) --- */
void updateQRStatus(String msg, uint16_t color) {
  tft.fillRect(10, 250, 220, 20, 0x0000);
  tft.setTextColor(color, 0x0000);
  tft.setTextSize(1);
  tft.drawCentreString(msg, 120, 255, 1);
}

void desenharTelaQRCode() {
  currentScreen = SCREEN_QR_CODE;
  tft.fillScreen(0xFFFF); // fundo branco para facilitar a leitura do QR pelas cameras

  QRCode qrcode;
  uint8_t qrcodeData[800];
  const uint8_t qrVersion = 14;

  if (current_bolt11.length() > 450) {
    tft.setTextColor(0xF800, 0xFFFF);
    tft.setTextSize(1);
    tft.drawCentreString("INVOICE MUITO LONGA", 120, 100, 1);
    desenharBotao(20, 270, 200, 40, "VOLTAR", 0x9000, 0xFFFF, 2);
    return;
  }

  int8_t qrResult = qrcode_initText(&qrcode, qrcodeData, qrVersion, ECC_LOW, current_bolt11.c_str());

  if (qrResult != 0) {
    updateQRStatus("Erro ao gerar QR", 0xF800);
    desenharBotao(20, 270, 200, 40, "VOLTAR", 0x9000, 0xFFFF, 2);
    return;
  }

  // Renderiza o QR Code centralizado na parte superior
  const int scale = 2;
  const int quietZone = 2;
  const int totalPixels = (qrcode.size + 2 * quietZone) * scale;
  const int xOffset = (240 - totalPixels) / 2;
  const int yOffset = 10;

  tft.fillRect(xOffset, yOffset, totalPixels, totalPixels, 0xFFFF);

  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        tft.fillRect(xOffset + (x + quietZone) * scale, yOffset + (y + quietZone) * scale, scale, scale, 0x0000);
      }
    }
  }

  // Valor cobrado abaixo do QR Code
  tft.setTextColor(0x0000, 0xFFFF);
  tft.setTextSize(2);
  tft.drawCentreString(String(current_sats) + " SATS", 120, 225, 1);

  updateQRStatus("Aguardando pagamento...", 0x07FF);

  // Botões inferiores: CHECAR e VOLTAR lado a lado
  desenharBotao(10,  275, 105, 38, "CHECAR", 0x03E0, 0xFFFF, 2);
  desenharBotao(125, 275, 105, 38, "VOLTAR", 0x9000, 0xFFFF, 2);
}

/* --- TELA 3: PAGAMENTO CONFIRMADO --- */
void desenharTelaPago() {
  pagamentoConfirmado = true;   // registra o resultado p/ o reporte pós-BT
  currentScreen = SCREEN_PAID;
  paidScreenTime = millis();
  tft.fillScreen(0x05E0);

  tft.setTextColor(0xFFFF, 0x05E0);
  tft.setTextSize(3);
  tft.drawCentreString("PAGO COM", 120, 50, 1);
  tft.drawCentreString("SUCESSO!", 120, 90, 1);

  tft.setTextColor(0xFFE0, 0x05E0);
  tft.setTextSize(3);
  tft.drawCentreString(String(current_sats) + " SATS", 120, 150, 1);

  tft.setTextSize(1);
  tft.drawCentreString("Toque na tela para continuar", 120, 240, 1);
}

/* --- TELA DE ERRO --- */
void desenharTelaErro(String titulo, String detalhe) {
  currentScreen = SCREEN_ERROR;
  tft.fillScreen(0x0000);
  tft.setTextColor(0xF800, 0x0000);
  tft.setTextSize(2);
  tft.drawCentreString(titulo, 120, 60, 1);

  tft.setTextColor(0xFFFF, 0x0000);
  tft.setTextSize(1);
  tft.drawCentreString(detalhe.substring(0, 35), 120, 120, 1);

  desenharBotao(20, 240, 200, 40, "VOLTAR", 0x9000, 0xFFFF, 2);
}

/* --- LÓGICA DE ENTRADA DO TECLADO --- */
void adicionarDigito(char d) {
  if (input_amount.length() >= 8) return;
  if (input_amount == "0") {
    if (d == '0') return;
    input_amount = String(d);
  } else {
    input_amount += d;
  }
  atualizarVisor();
}

void apagarDigito() {
  if (input_amount.length() > 0) {
    input_amount.remove(input_amount.length() - 1);
  }
  atualizarVisor();
}

void processarOK() {
  long sats = input_amount.toInt();
  if (sats <= 0) {
    tft.fillRoundRect(10, 40, 220, 54, 4, 0x8000);
    tft.drawRoundRect(10, 40, 220, 54, 4, 0xF800);
    tft.setTextColor(0xFFFF, 0x8000);
    tft.setTextSize(2);
    tft.setCursor(20, 56);
    tft.print("DIGITE VALOR!");
    delay(800);
    atualizarVisor();
    return;
  }

  current_sats = sats;         // atualiza ANTES da tela de loading (mostra o valor certo)
  desenharTelaCarregando();
  solicitarInvoice(sats, false);
}

/* --- REDE E COMUNICAÇÃO LNBITS --- */
void connectWiFi() {
  currentScreen = SCREEN_CONNECTING;
  input_amount = "";
  current_bolt11 = "";
  current_payment_hash = "";

  tft.fillScreen(0x0000);
  tft.setTextColor(0xFDA0, 0x0000);
  tft.setTextSize(3);
  tft.setCursor(20, 40);
  tft.println("KIX POS");

  tft.setTextSize(2);
  tft.setCursor(20, 100);
  tft.setTextColor(0xFFFF, 0x0000);
  tft.println("Conectando WiFi...");
  tft.setTextSize(1);
  tft.setTextColor(0x7BEF, 0x0000);
  tft.setCursor(20, 135);
  tft.println(ssid);

  if (WiFi.status() != WL_CONNECTED) {
    // Tenta primeiro as credenciais salvas pelo portal (armazenadas em NVS).
    WiFiManager wm;
    String savedSSID = wm.getWiFiSSID();
    String savedPass = wm.getWiFiPass();

    if (savedSSID.length() > 0) {
      Serial.print("[WIFI] Usando credenciais salvas: ");
      Serial.println(savedSSID);
      WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    } else {
      Serial.print("[WIFI] Usando credenciais padrao: ");
      Serial.println(ssid);
      WiFi.begin(ssid, password);
    }

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
      delay(500);
      kickstartHardware();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(0x07E0, 0x0000);
    tft.setTextSize(2);
    tft.setCursor(20, 170);
    tft.println("Conectado!");
    delay(800);
  } else {
    tft.setTextColor(0xF800, 0x0000);
    tft.setTextSize(2);
    tft.setCursor(20, 170);
    tft.println("Offline!");
    delay(1200);
  }

  desenharTelaTeclado();
}

/* --- CONFIGURAÇÃO DE WIFI VIA AP MODE (PORTAL CAPTIVO) --- */
// Chamada quando o botão "WIFI" é tocado. Coloca a placa em modo AP e abre um
// portal captivo: o cliente conecta o celular na rede "KIX-POS-Setup", abre o
// navegador (192.168.4.1) e informa o SSID/senha do WiFi do local. As
// credenciais ficam salvas em NVS e são usadas no próximo boot.
void iniciarConfiguracaoWiFi() {
  Serial.println("\n[WIFI] Entrando em modo de configuracao (AP)...");

  // Limpa o estado de pagamento atual antes de abrir o portal
  current_payment_hash = "";
  current_bolt11 = "";
  input_amount = "";

  // Tela de instruções para o cliente
  tft.fillScreen(0x0000);

  tft.setTextColor(0xFFE0, 0x0000);
  tft.setTextSize(2);
  tft.drawCentreString("MODO SETUP WIFI", 120, 20, 1);

  tft.setTextColor(0xFFFF, 0x0000);
  tft.drawCentreString("1. No celular,", 120, 60, 1);
  tft.drawCentreString("conecte na rede:", 120, 82, 1);

  tft.setTextColor(0x07FF, 0x0000);
  tft.drawCentreString(WIFI_AP_SSID, 120, 110, 1);

  tft.setTextColor(0xFFFF, 0x0000);
  tft.drawCentreString("2. Abra o navegador", 120, 150, 1);
  tft.drawCentreString("e acesse:", 120, 172, 1);

  tft.setTextColor(0x07FF, 0x0000);
  tft.drawCentreString("192.168.4.1", 120, 200, 1);

  tft.setTextColor(0x7BEF, 0x0000);
  tft.drawCentreString("3. Escolha o WiFi", 120, 240, 1);
  tft.drawCentreString("e digite a senha", 120, 262, 1);

  tft.setTextSize(1);
  tft.drawCentreString("Opcional: URL, chave da API e PIN BT", 120, 292, 1);

  // Abre o portal de configuração (bloqueante até salvar ou dar timeout)
  WiFiManager wm;
  wm.setConfigPortalTimeout(180); // 3 minutos
  wm.setSaveConfigCallback([]() {
    Serial.println("[WIFI] Credenciais salvas pelo portal.");
  });

  // Campos extras no portal: endpoint, chave da API LNbits e PIN do Bluetooth.
  // Pré-preenchidos com os valores atuais; se ficarem vazios, mantém o atual.
  WiFiManagerParameter p_url("lnbits_url", "LNbits URL", lnbits_url.c_str(), 120);
  WiFiManagerParameter p_key("lnbits_key", "LNbits API Key", lnbits_api_key.c_str(), 80);
  WiFiManagerParameter p_pin("bt_pin", "Bluetooth PIN", bt_pin.c_str(), 16);
  wm.addParameter(&p_url);
  wm.addParameter(&p_key);
  wm.addParameter(&p_pin);

  bool ok = wm.startConfigPortal(WIFI_AP_SSID, WIFI_AP_PASS);

  Serial.print("[WIFI] Portal encerrado: ");
  Serial.println(ok ? "sucesso" : "timeout/cancelado");

  // Aplica os valores de API digitados no portal (vazio/inválido = mantém o atual)
  String novaUrl = String(p_url.getValue());
  String novaKey = String(p_key.getValue());
  String novoPin = String(p_pin.getValue());
  novaUrl.trim();
  novaKey.trim();
  novoPin.trim();

  if (novaUrl.length() > 0 && novaUrl.startsWith("http")) {
    lnbits_url = novaUrl;
  }
  if (novaKey.length() > 0) {
    lnbits_api_key = novaKey;
  }
  // PIN do Bluetooth: 1 a 16 bytes (padrão hardcoded "1234" se vazio/inválido).
  // Obs.: o core 3.x não aplica PIN fixo no pareamento (ver iniciarBluetooth),
  // então o valor é apenas salvo no NVS para uso futuro / build customizada.
  if (novoPin.length() >= 1 && novoPin.length() <= 16) {
    bt_pin = novoPin;
  }

  salvarConfigLNbits();
  Serial.print("[WIFI] URL: "); Serial.println(lnbits_url);
  Serial.print("[WIFI] KEY: "); Serial.println(lnbits_api_key);
  Serial.print("[WIFI] PIN BT (salvo, nao aplicado no core 3.x): "); Serial.println(bt_pin);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WIFI] Conectado com as novas credenciais.");
    Serial.print("[WIFI] SSID: ");
    Serial.println(WiFi.SSID());
  } else {
    Serial.println("[WIFI] Sem conexao apos o portal. Tentando credenciais padrao...");
    connectWiFi();
    return;
  }

  desenharTelaTeclado();
}

/* --- PERSISTÊNCIA DAS CONFIGS LNBITS + PIN BT (NVS) --- */
// Carrega URL/key/PIN salvos no NVS. Se não existirem (ou forem inválidos),
// mantém os valores hardcoded do topo do arquivo.
void carregarConfigLNbits() {
  Preferences prefs;
  if (!prefs.begin("kixpos", true)) { // somente leitura
    Serial.println("[CONFIG] NVS indisponivel; mantendo valores hardcoded.");
    return;
  }

  String url = prefs.getString("lnbits_url", "");
  String key = prefs.getString("lnbits_key", "");
  String pin = prefs.getString("bt_pin", "1234");
  prefs.end();

  url.trim();
  key.trim();
  pin.trim();

  if (url.length() > 0 && url.startsWith("http")) {
    lnbits_url = url;
  }
  if (key.length() > 0) {
    lnbits_api_key = key;
  }
  if (pin.length() >= 1 && pin.length() <= 16) {
    bt_pin = pin;
  }

  Serial.println("[CONFIG] Config carregada do NVS:");
  Serial.print("  URL: "); Serial.println(lnbits_url);
  Serial.print("  KEY: "); Serial.println(lnbits_api_key);
  Serial.print("  PIN BT: "); Serial.println(bt_pin);
}

// Grava URL/key/PIN atuais no NVS.
void salvarConfigLNbits() {
  Preferences prefs;
  if (!prefs.begin("kixpos", false)) { // leitura/escrita
    Serial.println("[CONFIG] Nao foi possivel abrir NVS para gravacao.");
    return;
  }

  prefs.putString("lnbits_url", lnbits_url);
  prefs.putString("lnbits_key", lnbits_api_key);
  prefs.putString("bt_pin", bt_pin);
  prefs.end();

  Serial.println("[CONFIG] Config salva no NVS.");
}

/* --- CONTROLE DO BLUETOOTH (liga/desliga + PIN) --- */
// Liga o Bluetooth SPP (se ainda não estiver ligado).
//
// NOTA IMPORTANTE: o core ESP32 Arduino 3.x NÃO suporta mais pareamento legado
// com PIN fixo (SerialBT.setPin(...)). A própria lib avisa "Since version 3.0.0
// this library does not support legacy pairing (fixed PIN)". Chamar setPin() aqui
// deixava o pareamento/SPP inconsistente e o celular caía com "socket closed /
// read ret:-1". Por isso usamos o pareamento padrão "Just Works" (sem PIN), igual
// ao exemplo oficial SerialToSerialBT. O valor de bt_pin continua salvo no NVS
// (campo do portal), mas só é aplicado se o firmware for compilado como IDF
// component com CONFIG_BT_SSP_ENABLED desabilitado.
void iniciarBluetooth() {
  if (SerialBT.begin("KIX-POS", false, true)) {
    btHabilitado = true;
    Serial.println("[BT] Bluetooth ligado. Pareie como 'KIX-POS' (pareamento Just Works, sem PIN).");
  } else {
    btHabilitado = false;
    Serial.println("[BT][ERRO] Falha ao iniciar Bluetooth.");
  }
}

// Alterna o estado do Bluetooth. Desliga com end() (libera o stack sem precisar
// de reset; ao contrário de memrelease(), permite religar depois).
void alternarBluetooth() {
  if (btHabilitado) {
    SerialBT.end();
    btHabilitado = false;
    Serial.println("[BT] Bluetooth desligado.");
  } else {
    iniciarBluetooth();
  }
  desenharTelaTeclado(); // atualiza o rótulo/cor do botão BT
}

// Prepara um HTTPClient para a URL, escolhendo o transporte conforme o esquema:
//   - http://  -> WiFiClient (leve). ESSENCIAL para gerar invoice com o Bluetooth
//                 ligado: o TLS/mbedTLS do HTTPS consome ~40-60 KB de heap e, junto
//                 com o Bluetooth Classic, estoura/fragmenta a memória do ESP32,
//                 fazendo a conexão HTTPS falhar quando o BT está ativo.
//   - https:// -> WiFiClientSecure (TLS). Usa setInsecure() (sem validar o
//                 certificado) — modo de diagnóstico; consome mais heap.
// Retorna true se http.begin() teve sucesso.
bool prepararHTTP(HTTPClient& http, const String& url, WiFiClient& plain, WiFiClientSecure& secure, int timeoutMs) {
  http.setReuse(false);
  http.setTimeout(timeoutMs);

  if (url.startsWith("https://")) {
    secure.setInsecure();
    secure.setTimeout(timeoutMs);
    return http.begin(secure, url);
  }

  plain.setTimeout(timeoutMs);
  return http.begin(plain, url);
}

// Desliga o Bluetooth se estiver ligado, para liberar memória antes de uma
// operação TLS/HTTPS (invoice, saldo, checagem). Quando avisarVoltar10s é true
// (operação vinda do BT), avisa o cliente para se reconectar em 10s; senão, o
// usuário deve tocar no botão BT para religar manualmente.
void desligarBTParaTLS(bool avisarVoltar10s) {
  if (!btHabilitado) return;

  if (SerialBT.hasClient()) {
    SerialBT.println("\n[BT] Operacao segura (TLS) detectada.");
    SerialBT.println("[BT] Bluetooth sera desligado para liberar memoria.");
    if (avisarVoltar10s) {
      SerialBT.println("[BT] Voce pode se reconectar em 10 segundos.");
    } else {
      SerialBT.println("[BT] Religue manualmente pelo botao BT quando quiser.");
    }
  }

  SerialBT.end();
  btHabilitado = false;
  Serial.println("[BT] Bluetooth desligado automaticamente (necessario p/ HTTPS).");
}

// Fluxo do invoice: desliga o BT (se preciso) antes de criar a invoice via TLS.
// Para invoice originada no Bluetooth (origemBT), o auto-check roda só por 10s,
// depois para e o BT religa sozinho; para touch/USB, o auto-check segue sem limite
// e o BT fica desligado até o usuário tocar no botão BT.
void solicitarInvoice(int sats, bool origemBT) {
  pagamentoConfirmado = false;
  bolt11Relatorio = "";
  relatorioBTTexto = "";
  relatorioBTpendente = (btHabilitado && SerialBT.hasClient());
  btAutoReligarEm = 0;   // limpa qualquer timer anterior
  autoCheckAte = 0;      // padrão: auto-check sem limite (touch/USB)

  desligarBTParaTLS(origemBT);
  current_sats = sats;
  createInvoice(sats);

  // Exceção: invoice vinda do BT — 10s de auto-check, depois para e religa o BT.
  if (origemBT && relatorioBTpendente) {
    btAutoReligarEm = millis() + 10000;
    autoCheckAte = millis() + 10000;
  }
}

// Envia ao cliente BT o resultado do invoice (pago / não pago + BOLT11).
void reportarStatusInvoiceBT() {
  SerialBT.println("\n[BT] === RESULTADO DO INVOICE ===");
  if (pagamentoConfirmado) {
    SerialBT.println("[BT] PAGO COM SUCESSO!");
  } else if (bolt11Relatorio.length() > 0) {
    SerialBT.println("[BT] AINDA NAO PAGO. Fatura BOLT11:");
    SerialBT.println("\n--- START BOLT11 ---");
    SerialBT.print(bolt11Relatorio);
    SerialBT.println("\n--- END BOLT11 ---");
  } else {
    SerialBT.println("[BT] FALHA AO CRIAR A INVOICE (sem BOLT11).");
  }
}

// Agenda uma mensagem de resultado para ser enviada ao cliente BT após reconectar
// (usada por C/L, já que o BT fica desligado durante o HTTPS e religa em 10s).
void agendarResultadoBT(const String& msg) {
  relatorioBTTexto = msg;
  relatorioBTpendente = true;
}

void checkBalance(bool origemBT) {
  Serial.println("\n[WALLET] Iniciando consulta de saldo...");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WALLET][ERRO] Wi-Fi desconectado.");
    desenharTelaErro("SEM WIFI", "Verifique a conexao");
    if (origemBT) agendarResultadoBT("[BT] SEM WIFI - Verifique a conexao.");
    return;
  }

  desligarBTParaTLS(origemBT);   // libera heap antes do HTTPS
  if (origemBT) {
    btAutoReligarEm = millis() + 10000;   // religa o BT sozinho após a consulta
  }

  WiFiClient plain;
  WiFiClientSecure secure;
  HTTPClient http;

  Serial.print("[WALLET] GET ");
  Serial.println(lnbits_wallet_url);

  if (!prepararHTTP(http, lnbits_wallet_url, plain, secure, 15000)) {
    Serial.println("[WALLET][ERRO] http.begin() falhou.");
    desenharTelaErro("ERRO HTTP", "Falha ao iniciar");
    if (origemBT) agendarResultadoBT("[BT] ERRO HTTP - Falha ao iniciar.");
    return;
  }

  http.addHeader("X-Api-Key", lnbits_api_key);

  Serial.println("[WALLET] Enviando requisicao...");
  int code = http.GET();

  Serial.print("[WALLET] HTTP status: ");
  Serial.println(code);

  if (code > 0) {
    String raw = http.getString();
    Serial.println("[WALLET] Resposta recebida:");
    Serial.println(raw);

    if (code == 200) {
      JsonDocument doc;
      DeserializationError jsonError = deserializeJson(doc, raw);

      if (jsonError) {
        Serial.print("[WALLET][ERRO] JSON invalido: ");
        Serial.println(jsonError.c_str());
        http.end();
        desenharTelaErro("ERRO JSON", "Resposta invalida");
        if (origemBT) agendarResultadoBT("[BT] ERRO JSON - Resposta invalida.");
        return;
      }

      if (doc["balance_msat"].isNull()) {
        Serial.println("[WALLET][ERRO] Campo balance_msat ausente.");
        http.end();
        desenharTelaErro("ERRO JSON", "Saldo nao encontrado");
        if (origemBT) agendarResultadoBT("[BT] ERRO - Saldo nao encontrado.");
        return;
      }

      long balance = doc["balance_msat"].as<long>() / 1000;

      Serial.print("[WALLET] Saldo: ");
      Serial.print(balance);
      Serial.println(" sats");

      if (origemBT) agendarResultadoBT("[BT] Saldo da carteira: " + String(balance) + " sats");

      http.end();

      tft.fillScreen(0x0000);
      tft.setCursor(10, 40);
      tft.setTextColor(0xFDA0);
      tft.setTextSize(2);
      tft.println("SALDO DA CARTEIRA:");
      tft.setCursor(10, 110);
      tft.setTextColor(0xFFFF);
      tft.setTextSize(3);
      tft.print(balance);
      tft.setTextSize(2);
      tft.println(" SATS");
      delay(4000);
      desenharTelaTeclado();
      return;
    }
  } else {
    Serial.print("[WALLET][ERRO] Falha de transporte: ");
    Serial.println(http.errorToString(code));
  }

  http.end();
  desenharTelaErro("ERRO NA CARTEIRA", "HTTP " + String(code));
  if (origemBT) agendarResultadoBT("[BT] ERRO NA CARTEIRA: HTTP " + String(code));
}


void createInvoice(int sats) {
  Serial.println("\n[INVOICE] ===== INICIO =====");
  Serial.print("[INVOICE] Valor solicitado: ");
  Serial.print(sats);
  Serial.println(" sats");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[INVOICE][ERRO] Wi-Fi desconectado.");
    desenharTelaErro("SEM WIFI", "Verifique a conexao");
    return;
  }

  WiFiClient plain;
  WiFiClientSecure secure;
  HTTPClient http;

  Serial.print("[INVOICE] Endpoint: ");
  Serial.println(lnbits_url);

  if (!prepararHTTP(http, lnbits_url, plain, secure, 20000)) {
    Serial.println("[INVOICE][ERRO] http.begin() falhou.");
    desenharTelaErro("ERRO HTTP", "Falha ao iniciar");
    return;
  }

  http.addHeader("X-Api-Key", lnbits_api_key);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"out\": false, \"amount\":" + String(sats) + ", \"memo\": \"KIX-POS\"}";

  Serial.print("[INVOICE] Payload: ");
  Serial.println(payload);
  Serial.println("[INVOICE] Enviando POST...");

  int httpCode = http.POST(payload);

  Serial.print("[INVOICE] HTTP status: ");
  Serial.println(httpCode);

  String response = "";

  if (httpCode > 0) {
    response = http.getString();
    Serial.println("[INVOICE] Resposta da API:");
    Serial.println(response);
  } else {
    Serial.print("[INVOICE][ERRO] Falha de transporte: ");
    Serial.println(http.errorToString(httpCode));
  }

  if (httpCode == 200 || httpCode == 201) {
    JsonDocument doc;
    DeserializationError jsonError = deserializeJson(doc, response);

    if (jsonError) {
      Serial.print("[INVOICE][ERRO] JSON invalido: ");
      Serial.println(jsonError.c_str());
      http.end();
      desenharTelaErro("ERRO JSON", "Resposta invalida");
      return;
    }

    current_bolt11 = doc["payment_request"].as<String>();
    current_payment_hash = doc["payment_hash"].as<String>();

    if (current_bolt11.length() == 0 || current_payment_hash.length() == 0) {
      Serial.println("[INVOICE][ERRO] API nao retornou payment_request/payment_hash.");
      http.end();
      desenharTelaErro("ERRO INVOICE", "Campos ausentes");
      return;
    }

    bolt11Relatorio = current_bolt11;   // cópia p/ o reporte pós-BT (sobrevive ao VOLTAR/auto-return)

    Serial.println("[INVOICE] Invoice criada com sucesso.");
    Serial.print("[INVOICE] BOLT11 recebido (");
    Serial.print(current_bolt11.length());
    Serial.println(" caracteres).");
    Serial.println("[INVOICE] Exibindo QR Code...");

    http.end();
    desenharTelaQRCode();

    Serial.println("[INVOICE] ===== FIM OK =====");
    return;
  }

  http.end();
  Serial.println("[INVOICE] ===== FIM COM ERRO =====");
  desenharTelaErro("ERRO NA FATURA", "HTTP " + String(httpCode));
}


void manualCheck(bool origemBT) {
  Serial.println("\n[CHECK] Verificacao manual solicitada.");

  if (current_payment_hash == "") {
    Serial.println("[CHECK] Nao existe invoice ativa.");
    updateQRStatus("SEM INVOICE", 0xF800);
    if (origemBT) agendarResultadoBT("[BT] Nao existe invoice ativa.");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[CHECK][ERRO] Wi-Fi desconectado.");
    updateQRStatus("SEM WIFI", 0xF800);
    if (origemBT) agendarResultadoBT("[BT] SEM WIFI - Verifique a conexao.");
    return;
  }

  desligarBTParaTLS(origemBT);   // libera heap antes do HTTPS
  if (origemBT) {
    btAutoReligarEm = millis() + 10000;   // religa o BT sozinho após a checagem
  }

  updateQRStatus("CHECANDO...", 0xFFE0);

  WiFiClient plain;
  WiFiClientSecure secure;
  HTTPClient http;

  String checkUrl = lnbits_url + "/" + current_payment_hash;

  Serial.print("[CHECK] GET ");
  Serial.println(checkUrl);

  if (!prepararHTTP(http, checkUrl, plain, secure, 15000)) {
    Serial.println("[CHECK][ERRO] http.begin() falhou.");
    updateQRStatus("ERRO AO CONECTAR", 0xF800);
    if (origemBT) agendarResultadoBT("[BT] ERRO AO CONECTAR.");
    return;
  }

  http.addHeader("X-Api-Key", lnbits_api_key);

  Serial.println("[CHECK] Enviando GET...");
  int code = http.GET();

  Serial.print("[CHECK] HTTP status: ");
  Serial.println(code);

  if (code == 200) {
    String raw = http.getString();
    Serial.println("[CHECK] Resposta da API:");
    Serial.println(raw);

    JsonDocument doc;
    DeserializationError jsonError = deserializeJson(doc, raw);

    if (jsonError) {
      Serial.print("[CHECK][ERRO] JSON invalido: ");
      Serial.println(jsonError.c_str());
      http.end();
      updateQRStatus("ERRO NA RESPOSTA", 0xF800);
      if (origemBT) agendarResultadoBT("[BT] ERRO NA RESPOSTA (JSON invalido).");
      return;
    }

    bool paid = doc["paid"].as<bool>();
    http.end();

    if (paid) {
      Serial.println("[CHECK] PAGAMENTO CONFIRMADO!");
      desenharTelaPago();
      if (origemBT) agendarResultadoBT("[BT] PAGAMENTO CONFIRMADO!");
      return;
    }

    Serial.println("[CHECK] Invoice ainda nao foi paga.");
    updateQRStatus("NAO PAGO AINDA", 0xFDA0);
    if (origemBT) agendarResultadoBT("[BT] AINDA NAO PAGO.");
    return;
  }

  if (code > 0) {
    String raw = http.getString();
    Serial.println("[CHECK] Resposta de erro:");
    Serial.println(raw);
  } else {
    Serial.print("[CHECK][ERRO] Falha de transporte: ");
    Serial.println(http.errorToString(code));
  }

  http.end();
  updateQRStatus("ERRO: " + String(code), 0xF800);
  if (origemBT) agendarResultadoBT("[BT] ERRO: HTTP " + String(code));
}


void verificarPagamentoSilencioso() {
  if (current_payment_hash == "") return;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[AUTO-CHECK] Wi-Fi desconectado; verificacao ignorada.");
    return;
  }

  desligarBTParaTLS();   // garante heap livre (BT desligado) antes do HTTPS

  WiFiClient plain;
  WiFiClientSecure secure;
  HTTPClient http;

  String checkUrl = lnbits_url + "/" + current_payment_hash;

  if (!prepararHTTP(http, checkUrl, plain, secure, 12000)) {
    Serial.println("[AUTO-CHECK][ERRO] http.begin() falhou.");
    return;
  }

  http.addHeader("X-Api-Key", lnbits_api_key);

  int code = http.GET();

  if (code == 200) {
    String raw = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, raw);

    if (!error && doc["paid"].as<bool>()) {
      Serial.println("[AUTO-CHECK] PAGAMENTO CONFIRMADO!");
      http.end();
      desenharTelaPago();
      return;
    }

    if (error) {
      Serial.print("[AUTO-CHECK][ERRO] JSON: ");
      Serial.println(error.c_str());
    }
  } else if (code < 0) {
    Serial.print("[AUTO-CHECK][ERRO] ");
    Serial.println(http.errorToString(code));
  }

  http.end();
}


void mostrarInformacoesSistema(Print& out) {
  out.println("\n========== KIX POS / STATUS ==========");

  out.print("Estado: ");
  if (WiFi.status() == WL_CONNECTED) {
    out.println("ONLINE");
  } else {
    out.println("OFFLINE");
  }

  out.print("SSID: ");
  out.println(WiFi.SSID());

  out.print("IP: ");
  out.println(WiFi.localIP());

  out.print("Gateway: ");
  out.println(WiFi.gatewayIP());

  out.print("Mascara: ");
  out.println(WiFi.subnetMask());

  out.print("DNS: ");
  out.println(WiFi.dnsIP());

  out.print("MAC: ");
  out.println(WiFi.macAddress());

  out.print("Sinal Wi-Fi (RSSI): ");
  if (WiFi.status() == WL_CONNECTED) {
    out.print(WiFi.RSSI());
    out.println(" dBm");
  } else {
    out.println("indisponivel");
  }

  out.print("Bluetooth: ");
  if (!btHabilitado) {
    out.println("DESLIGADO");
  } else if (SerialBT.hasClient()) {
    out.println("CONECTADO");
  } else {
    out.println("LIGADO / AGUARDANDO");
  }

  out.print("Tempo ligado: ");
  out.print(millis() / 1000);
  out.println(" segundos");

  out.print("Heap livre: ");
  out.print(ESP.getFreeHeap());
  out.println(" bytes");

  out.print("Maior bloco livre: ");
  out.print(ESP.getMaxAllocHeap());
  out.println(" bytes");

  out.print("PSRAM livre: ");
  out.print(ESP.getFreePsram());
  out.println(" bytes");

  out.print("LNbits endpoint: ");
  out.println(lnbits_url);
  out.print("Transporte: ");
  out.println(lnbits_url.startsWith("https://") ? "HTTPS/TLS (usa mais heap)" : "HTTP (leve, recomendado p/ Bluetooth)");

  out.print("Invoice ativa: ");
  out.println(current_payment_hash.length() > 0 ? "SIM" : "NAO");

  out.println("======================================");
}

/* --- TRATAMENTO DE TOQUE RESISTIVO (VERTICAL) --- */
void tratarToque(int px, int py) {
  if (currentScreen == SCREEN_KEYPAD) {
    // Botão BT (liga/desliga Bluetooth) — canto superior direito, antes do WIFI
    if (py >= 6 && py <= 34 && px >= 98 && px < 168) {
      alternarBluetooth();
      return;
    }
    // Botão WIFI (configuração via AP mode) — canto superior direito
    if (py >= 6 && py <= 34 && px >= 168) {
      iniciarConfiguracaoWiFi();
      return;
    }
    // Linha 1: 1, 2, 3 (y: 130 a 172)
    if (py >= 130 && py < 174) {
      if (px < 79) adicionarDigito('1');
      else if (px < 156) adicionarDigito('2');
      else adicionarDigito('3');
    }
    // Linha 2: 4, 5, 6 (y: 178 a 220)
    else if (py >= 174 && py < 222) {
      if (px < 79) adicionarDigito('4');
      else if (px < 156) adicionarDigito('5');
      else adicionarDigito('6');
    }
    // Linha 3: 7, 8, 9 (y: 226 a 268)
    else if (py >= 222 && py < 270) {
      if (px < 79) adicionarDigito('7');
      else if (px < 156) adicionarDigito('8');
      else adicionarDigito('9');
    }
    // Linha 4: DEL, 0, OK (y: 274 a 320)
    else if (py >= 270 && py <= 320) {
      if (px < 79) apagarDigito();
      else if (px < 156) adicionarDigito('0');
      else processarOK();
    }
  }
  else if (currentScreen == SCREEN_QR_CODE) {
    // Botões inferiores na tela de QR Code (y >= 270)
    if (py >= 270) {
      if (px < 120) {
        manualCheck();
      } else {
        current_payment_hash = "";
        current_bolt11 = "";
        input_amount = "";
        desenharTelaTeclado();
      }
    }
  }
  else if (currentScreen == SCREEN_PAID || currentScreen == SCREEN_ERROR) {
    input_amount = "";
    current_payment_hash = "";
    current_bolt11 = "";
    desenharTelaTeclado();
  }
}

void verificarTouch() {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();

    // Filtro de pressão do sensor resistivo: rejeita toques muito fracos.
    if (p.z > 200) {
      if (!wasTouched && (millis() - lastTouchTime > 180)) {
        wasTouched = true;
        lastTouchTime = millis();

        // Mapeia o toque bruto para a tela em modo retrato (240x320).
        // ATENÇÃO: calibre TOUCH_MIN/MAX usando os valores brutos impressos abaixo.
        int px = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, 240);
        int py = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, 320);
        px = constrain(px, 0, 239);
        py = constrain(py, 0, 319);

        Serial.printf("[TOUCH] Raw: (%d, %d, z=%d) -> Tela: (%d, %d)\n", p.x, p.y, p.z, px, py);
        tratarToque(px, py);
      }
    }
  } else {
    wasTouched = false;
  }
}

/* --- SETUP --- */
void setup() {
  Serial.begin(115200);
  delay(500); // Pequena pausa para estabilizar a Serial

  Serial.println("\n[BOOT] Inicializando KIX POS...");
  Serial.println("[BOOT] Serial pronta em 115200 baud.");
  
  kickstartHardware();

  tft.init();
  tft.setRotation(2); // Orientação Vertical (Portrait)
  tft.setTextWrap(false);
  tft.fillScreen(0x0000);

  // Inicializa o barramento SPI do touch resistivo XPT2046
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  
  // Inicializa o touch resistivo XPT2046 (rotação acompanha o display)
  touch.begin(touchscreenSPI);
  touch.setRotation(2); // retrato 240x320 (acompanha tft.setRotation(2))

  // Carrega URL/key/PIN salvos no NVS (fallback: hardcoded)
  carregarConfigLNbits();

  // Liga o Bluetooth SPP com o PIN carregado ("KIX-POS", PIN padrão "1234")
  iniciarBluetooth();

  connectWiFi();
}

/* --- VARIÁVEL GLOBAL PARA O TIMER DE DEBUG --- */
unsigned long lastDebugPrintTime = 0;

/* --- INTERFACE SERIAL (USB + BLUETOOTH) --- */
// Despacha um comando de texto (mesma sintaxe do terminal serial) para a ação
// correspondente. "out" é o stream de resposta (Serial ou SerialBT).
void despacharComando(const String& input, Print& out) {
  // I:VALOR - criar invoice
  if (input.startsWith("I:")) {
    String valor = input.substring(2);
    valor.trim();
    long sats = valor.toInt();

    if (sats > 0) {
      bool origemBT = (&out == (Print*)&SerialBT);
      solicitarInvoice(sats, origemBT);
      // No BT a resposta é adiada para a reconexão (o BT foi desligado acima).
      // Aqui respondemos imediatamente apenas quando o comando veio do USB.
      if (!origemBT) {
        out.println(current_bolt11.length() > 0
          ? "[OK] Invoice criada. Envie 'P' para copiar o BOLT11."
          : "[ERRO] Falha ao criar invoice.");
      }
    } else {
      out.println("[ERRO] Valor de sats invalido.");
    }
  }
  // C ou C: - verificar pagamento
  else if (input.equalsIgnoreCase("C") || input.equalsIgnoreCase("C:")) {
    manualCheck(&out == (Print*)&SerialBT);
  }
  // L ou L: - consultar saldo
  else if (input.equalsIgnoreCase("L") || input.equalsIgnoreCase("L:")) {
    checkBalance(&out == (Print*)&SerialBT);
  }
  // P ou P: - imprimir a invoice BOLT11 ativa (útil para copiar no terminal BT)
  else if (input.equalsIgnoreCase("P") || input.equalsIgnoreCase("P:")) {
    if (current_bolt11.length() > 0) {
      out.println("\n--- START BOLT11 ---");
      out.print(current_bolt11);
      out.println("\n--- END BOLT11 ---");
    } else {
      out.println("[!] Nenhuma invoice ativa.");
    }
  }
  // H - voltar para a tela inicial
  else if (input.equalsIgnoreCase("H")) {
    input_amount = "";
    current_payment_hash = "";
    current_bolt11 = "";
    desenharTelaTeclado();
    out.println("[OK] Tela inicial.");
  }
  // H: - mostrar informações do sistema
  else if (input.equalsIgnoreCase("H:")) {
    mostrarInformacoesSistema(out);
  }
  // help - listar os comandos disponíveis
  else if (input.equalsIgnoreCase("help")) {
    out.println("\n[HELP] Comandos disponiveis:");
    out.println("  I:VALOR  - criar invoice (ex.: I:1500)");
    out.println("  C        - verificar pagamento da invoice ativa");
    out.println("  L        - consultar saldo da carteira");
    out.println("  P        - imprimir o BOLT11 ativo (para copiar)");
    out.println("  H        - voltar para a tela inicial");
    out.println("  H:       - status do sistema (Wi-Fi, IP, BT, heap)");
    out.println("  help     - esta lista de comandos");
  }
  else {
    out.println("[SERIAL] Comando desconhecido.");
    out.println("Comandos: H, H:, I:1500, C, L, P, help");
  }
}

// Lê uma linha terminada por \n ou \r de "in" e a despacha para "out".
// Mantém o estado do buffer em "buffer" (deve ser estático/persistente por stream).
void lerComando(Stream& in, String& buffer, Print& out) {
  while (in.available() > 0) {
    char c = in.read();

    if (c == '\n' || c == '\r') {
      buffer.trim();
      if (buffer.length() > 0) {
        out.print("\n[SERIAL RX] Comando: [");
        out.print(buffer);
        out.println("]");
        despacharComando(buffer, out);
      }
      buffer = "";
    }
    else if (c >= 32 && c <= 126) {
      if (buffer.length() < 64) {
        buffer += c;
      } else {
        out.println("[ERRO] Comando muito longo; descartado.");
        buffer = "";
      }
    }
  }
}

/* --- LOOP PRINCIPAL --- */

void loop() {

  // =====================================================
  // 1. RECEPCAO SERIAL (USB + BLUETOOTH) - NAO BLOQUEANTE
  // =====================================================

  static String usbInput = "";
  static String btInput = "";

  lerComando(Serial, usbInput, Serial);
  if (btHabilitado) {
    lerComando(SerialBT, btInput, SerialBT);
  }


  // =====================================================
  // 2. VERIFICAR TOUCH
  // =====================================================

  verificarTouch();


  // =====================================================
  // 3. DEBUG - CONFIRMAR LOOP ATIVO
  // =====================================================

  if (millis() - lastDebugPrintTime >= 30000) {
    lastDebugPrintTime = millis();
    Serial.println("[DEBUG] Loop ativo - Sistema rodando normalmente.");
  }


  // =====================================================
  // 4. VERIFICACAO AUTOMATICA DE PAGAMENTO
  // =====================================================

  if (
    currentScreen == SCREEN_QR_CODE &&
    current_payment_hash != "" &&
    (autoCheckAte == 0 || millis() < autoCheckAte)
  ) {
    if (millis() - lastAutoCheckTime >= 3000) {
      lastAutoCheckTime = millis();
      verificarPagamentoSilencioso();
    }
  }


  // =====================================================
  // 5. RETORNO AUTOMATICO APOS PAGAMENTO
  // =====================================================

  if (currentScreen == SCREEN_PAID) {
    if (millis() - paidScreenTime >= 5000) {
      input_amount = "";
      current_payment_hash = "";
      current_bolt11 = "";

      desenharTelaTeclado();
    }
  }


  // =====================================================
  // 6. RELIGAR BLUETOOTH AUTOMATICAMENTE (SÓ OPERAÇÕES VIA BT)
  // =====================================================

  if (btAutoReligarEm != 0 && millis() >= btAutoReligarEm) {
    btAutoReligarEm = 0;
    if (!btHabilitado) {
      iniciarBluetooth();
      Serial.println("[BT] Operacao via BT: religando Bluetooth apos 10s.");
    }
  }


  // =====================================================
  // 7. REPORTAR RESULTADO AO RECONECTAR (BT)
  // =====================================================

  if (relatorioBTpendente && btHabilitado && SerialBT.hasClient()) {
    relatorioBTpendente = false;
    if (relatorioBTTexto.length() > 0) {
      SerialBT.println("\n[BT] === RESULTADO ===");
      SerialBT.println(relatorioBTTexto);
      relatorioBTTexto = "";
    } else {
      reportarStatusInvoiceBT();
    }
    SerialBT.println("[BT] Comandos: I:VALOR, C, L, P, H, help.");
  }
}
