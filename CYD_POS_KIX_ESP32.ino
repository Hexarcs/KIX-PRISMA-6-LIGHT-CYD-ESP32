#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "qrcode.h"

/* --- CONFIGURAÇÃO WI-FI E LNBITS --- */
const char* ssid = "TP-Link_46C2";
const char* password = "80165897";

String lnbits_url = "https://engine-1.hexarcs.com/api/v1/payments";
String lnbits_wallet_url = "https://engine-1.hexarcs.com/api/v1/wallet";
String lnbits_api_key = "5e3e9788bd634d82882f849d7fba3b54";

/* --- DEFINIÇÕES DE PINOS DO TOUCH CYD (ESP32-2432S028) --- */
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

/* --- CALIBRAÇÃO DO TOUCH (MODO PAISAGEM 320x240) --- */
#define TOUCH_MIN_X  200
#define TOUCH_MAX_X  3900
#define TOUCH_MIN_Y  240
#define TOUCH_MAX_Y  3800

/* --- OBJETOS DE HARDWARE --- */
SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

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
void checkBalance();
void createInvoice(int sats);
void manualCheck();
void verificarPagamentoSilencioso();
void adicionarDigito(char d);
void apagarDigito();
void processarOK();
void tratarToque(int px, int py);
void verificarTouch();

/* --- FUNÇÕES DE HARDWARE --- */
void kickstartHardware() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH); // Garante que o backlight do CYD permaneça aceso
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

/* --- ATUALIZAÇÃO DO VISOR DO TECLADO --- */
void atualizarVisor() {
  tft.fillRoundRect(10, 26, 300, 44, 4, 0x0000);
  tft.drawRoundRect(10, 26, 300, 44, 4, 0x4A69);

  tft.setCursor(20, 36);
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
  tft.print(" SATS");
}

/* --- TELA 1: TECLADO PDV / DIGITAR VALOR --- */
void desenharTelaTeclado() {
  currentScreen = SCREEN_KEYPAD;
  tft.fillScreen(0x0000); // Fundo preto

  // Cabeçalho superior
  tft.setTextColor(0xFDA0, 0x0000); // Laranja
  tft.setTextSize(2);
  tft.drawString("KIX CYD POS", 10, 5);

  tft.setTextSize(1);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(0x07E0, 0x0000);
    tft.drawString("ONLINE", 260, 9);
  } else {
    tft.setTextColor(0xF800, 0x0000);
    tft.drawString("OFFLINE", 255, 9);
  }

  // Visor
  atualizarVisor();

  // Teclado numérico (4 linhas x 3 colunas)
  // Linha 0: [ 1 ] [ 2 ] [ 3 ]
  desenharBotao(10, 76, 94, 36, "1", 0x2124, 0xFFFF, 3);
  desenharBotao(113, 76, 94, 36, "2", 0x2124, 0xFFFF, 3);
  desenharBotao(216, 76, 94, 36, "3", 0x2124, 0xFFFF, 3);

  // Linha 1: [ 4 ] [ 5 ] [ 6 ]
  desenharBotao(10, 116, 94, 36, "4", 0x2124, 0xFFFF, 3);
  desenharBotao(113, 116, 94, 36, "5", 0x2124, 0xFFFF, 3);
  desenharBotao(216, 116, 94, 36, "6", 0x2124, 0xFFFF, 3);

  // Linha 2: [ 7 ] [ 8 ] [ 9 ]
  desenharBotao(10, 156, 94, 36, "7", 0x2124, 0xFFFF, 3);
  desenharBotao(113, 156, 94, 36, "8", 0x2124, 0xFFFF, 3);
  desenharBotao(216, 156, 94, 36, "9", 0x2124, 0xFFFF, 3);

  // Linha 3: [ DEL ] [ 0 ] [ OK ]
  desenharBotao(10, 196, 94, 38, "DEL", 0x8800, 0xFFFF, 2);
  desenharBotao(113, 196, 94, 38, "0", 0x2124, 0xFFFF, 3);
  desenharBotao(216, 196, 94, 38, "OK", 0x05E0, 0xFFFF, 3);
}

/* --- TELA: CARREGANDO FATURA --- */
void desenharTelaCarregando() {
  currentScreen = SCREEN_LOADING;
  tft.fillScreen(0x0000);
  tft.setTextColor(0x07FF, 0x0000);
  tft.setTextSize(2);
  tft.drawCentreString("CRIANDO INVOICE...", 160, 70, 1);

  tft.setTextColor(0xFFE0, 0x0000);
  tft.setTextSize(3);
  tft.drawCentreString(String(current_sats) + " SATS", 160, 110, 1);

  tft.setTextColor(0x7BEF, 0x0000);
  tft.setTextSize(1);
  tft.drawCentreString("Comunicando com LNbits...", 160, 165, 1);
}

/* --- TELA 2: QR CODE DA FATURA E BOTÕES --- */
void updateQRStatus(String msg, uint16_t color) {
  tft.fillRect(190, 46, 128, 22, 0x0000);
  tft.setTextColor(color, 0x0000);
  tft.setTextSize(1);
  tft.drawString(msg, 192, 48);
}


void desenharTelaQRCode() {
  currentScreen = SCREEN_QR_CODE;
  tft.fillScreen(0x0000); // Fundo preto da tela

  // 1. Geracao do QR Code
  QRCode qrcode;
  uint8_t qrcodeData[800];

  // Versao 14: matriz de 73 x 73 modulos.
  // ECC_LOW: menor correcao de erros, maior capacidade.
  const uint8_t qrVersion = 14;

  // Limite conservador para evitar dados excessivos nesta biblioteca.
  // A biblioteca utilizada nao valida corretamente o excesso de dados.
  if (current_bolt11.length() > 450) {
    tft.setTextColor(0xF800, 0x0000);
    tft.setTextSize(1);
    tft.drawString("INVOICE MUITO LONGA", 10, 100);

    // Mantem o painel lateral visivel
    tft.setTextColor(0x7BEF, 0x0000);
    tft.drawString("A COBRAR:", 192, 10);

    tft.setTextColor(0xFFE0, 0x0000);
    tft.setTextSize(2);
    tft.drawString(String(current_sats) + " SAT", 192, 24);

    updateQRStatus("Invoice longa", 0xF800);
    desenharBotao(192, 154, 120, 68, "VOLTAR", 0x9000, 0xFFFF, 2);
    return;
  }

  int8_t qrResult = qrcode_initText(
    &qrcode,
    qrcodeData,
    qrVersion,
    ECC_LOW,
    current_bolt11.c_str()
  );

  if (qrResult != 0) {
    tft.setTextColor(0xF800, 0x0000);
    tft.setTextSize(1);
    tft.drawString("ERRO AO GERAR QR", 10, 100);

    tft.setTextColor(0x7BEF, 0x0000);
    tft.drawString("A COBRAR:", 192, 10);

    tft.setTextColor(0xFFE0, 0x0000);
    tft.setTextSize(2);
    tft.drawString(String(current_sats) + " SAT", 192, 24);

    updateQRStatus("Erro no QR", 0xF800);
    desenharBotao(192, 154, 120, 68, "VOLTAR", 0x9000, 0xFFFF, 2);
    return;
  }

  // 2. Desenho convencional: modulos pretos sobre fundo branco.
  // Quiet zone: quatro modulos livres em cada lado.
  const int scale = 2;
  const int quietZone = 4;

  const int qrPixels = qrcode.size * scale;
  const int totalPixels = (qrcode.size + 2 * quietZone) * scale;

  const int xOffset = 6;
  const int yOffset = (240 - totalPixels) / 2;

  // Fundo branco, incluindo a margem de seguranca
  tft.fillRect(
    xOffset,
    yOffset,
    totalPixels,
    totalPixels,
    0xFFFF
  );

  // Desenha os modulos pretos
  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        tft.fillRect(
          xOffset + (x + quietZone) * scale,
          yOffset + (y + quietZone) * scale,
          scale,
          scale,
          0x0000
        );
      }
    }
  }

  // 3. Painel lateral direito: informacoes e acoes
  tft.setTextColor(0x7BEF, 0x0000);
  tft.setTextSize(1);
  tft.drawString("A COBRAR:", 192, 10);

  tft.setTextColor(0xFFE0, 0x0000);
  tft.setTextSize(2);
  tft.drawString(String(current_sats) + " SAT", 192, 24);

  // Status inicial
  updateQRStatus("Aguardando...", 0x07FF);

  // Botao: CHECAR PAGAMENTO
  desenharBotao(192, 72, 120, 68, "CHECAR", 0x03E0, 0xFFFF, 2);

  tft.setTextColor(0xFFE0, 0x03E0);
  tft.setTextSize(1);
  tft.drawCentreString("PAGAMENTO", 192 + 60, 72 + 46, 1);

  // Botao: VOLTAR
  desenharBotao(192, 154, 120, 68, "VOLTAR", 0x9000, 0xFFFF, 2);
}


/* --- TELA 3: PAGAMENTO CONFIRMADO --- */
void desenharTelaPago() {
  currentScreen = SCREEN_PAID;
  paidScreenTime = millis();
  tft.fillScreen(0x05E0); // Fundo verde

  tft.setTextColor(0xFFFF, 0x05E0);
  tft.setTextSize(3);
  tft.drawCentreString("PAGO COM", 160, 25, 1);
  tft.drawCentreString("SUCESSO!", 160, 58, 1);

  tft.setTextColor(0xFFE0, 0x05E0);
  tft.setTextSize(4);
  tft.drawCentreString(String(current_sats) + " SATS", 160, 105, 1);

  tft.setTextColor(0xFFFF, 0x05E0);
  tft.setTextSize(3);
  tft.drawCentreString("[ OK ]", 160, 160, 1);

  tft.setTextSize(1);
  tft.drawCentreString("Toque na tela para nova cobranca", 160, 212, 1);
}

/* --- TELA DE ERRO --- */
void desenharTelaErro(String titulo, String detalhe) {
  currentScreen = SCREEN_ERROR;
  tft.fillScreen(0x0000);
  tft.setTextColor(0xF800, 0x0000);
  tft.setTextSize(2);
  tft.drawCentreString(titulo, 160, 40, 1);

  tft.setTextColor(0xFFFF, 0x0000);
  tft.setTextSize(1);
  tft.drawCentreString(detalhe.substring(0, 45), 160, 90, 1);
  if (detalhe.length() > 45) {
    tft.drawCentreString(detalhe.substring(45, 90), 160, 110, 1);
  }

  desenharBotao(80, 160, 160, 50, "VOLTAR", 0x9000, 0xFFFF, 2);
}

/* --- LÓGICA DE ENTRADA DO TECLADO --- */
void adicionarDigito(char d) {
  if (input_amount.length() >= 8) return; // Limita dígitos para evitar overflow
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
    tft.fillRoundRect(10, 26, 300, 44, 4, 0x8000);
    tft.drawRoundRect(10, 26, 300, 44, 4, 0xF800);
    tft.setTextColor(0xFFFF, 0x8000);
    tft.setTextSize(2);
    tft.setCursor(30, 38);
    tft.print("DIGITE O VALOR!");
    delay(800);
    atualizarVisor();
    return;
  }

  current_sats = sats;
  desenharTelaCarregando();
  createInvoice(sats);
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
  tft.setCursor(20, 30);
  tft.println("KIX CYD POS");

  tft.setTextSize(2);
  tft.setCursor(20, 80);
  tft.setTextColor(0xFFFF, 0x0000);
  tft.println("Conectando WiFi...");
  tft.setTextSize(1);
  tft.setTextColor(0x7BEF, 0x0000);
  tft.setCursor(20, 110);
  tft.println(ssid);

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(ssid, password);
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
      delay(500);
      kickstartHardware();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WIFI] Conectado! IP: " + WiFi.localIP().toString());
    tft.setTextColor(0x07E0, 0x0000);
    tft.setTextSize(2);
    tft.setCursor(20, 140);
    tft.println("WiFi Conectado!");
    delay(800);
  } else {
    Serial.println("[WIFI] Offline");
    tft.setTextColor(0xF800, 0x0000);
    tft.setTextSize(2);
    tft.setCursor(20, 140);
    tft.println("WiFi Offline!");
    delay(1200);
  }

  desenharTelaTeclado();
}

void checkBalance() {
  HTTPClient http;
  http.begin(lnbits_wallet_url);
  http.addHeader("X-Api-Key", lnbits_api_key);
  int code = http.GET();
  if (code == 200) {
    String raw = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, raw);
    long balance = doc["balance_msat"].as<long>() / 1000;

    tft.fillScreen(0x0000);
    tft.setCursor(10, 20); tft.setTextColor(0xFDA0); tft.setTextSize(2);
    tft.println("SALDO DA CARTEIRA:");
    tft.setCursor(10, 80); tft.setTextColor(0xFFFF); tft.setTextSize(4);
    tft.print(balance);
    tft.setTextSize(2); tft.println(" SATS");
    delay(4000);
    desenharTelaTeclado();
  } else {
    Serial.println("[BAL_ERR] " + String(code));
  }
  http.end();
}

void createInvoice(int sats) {
  HTTPClient http;
  http.begin(lnbits_url);
  http.addHeader("X-Api-Key", lnbits_api_key);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"out\": false, \"amount\":" + String(sats) + ", \"memo\": \"KIX-CYD-POS\"}";
  int httpCode = http.POST(payload);
  String response = http.getString();

  if (httpCode == 200 || httpCode == 201) {
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, response);
    String bolt11 = doc["payment_request"].as<String>();

    current_bolt11 = bolt11;
    current_payment_hash = doc["payment_hash"].as<String>();

    Serial.println("\n--- RAW INVOICE ---");
    Serial.println(current_bolt11);
    Serial.println("-------------------\n");

    desenharTelaQRCode();
  } else {
    Serial.println("[HTTP ERR] " + String(httpCode) + " : " + response);
    desenharTelaErro("ERRO NA FATURA", "HTTP " + String(httpCode) + ": " + response);
  }
  http.end();
  kickstartHardware();
}

void manualCheck() {
  Serial.println("[C] Verificando pagamento...");

  if (current_payment_hash == "") {
    Serial.println("[C] ERRO: Nenhuma invoice ativa.");
    updateQRStatus("SEM INVOICE", 0xF800);
    return;
  }

  updateQRStatus("CHECANDO...", 0xFFE0);

  HTTPClient http;
  http.begin(lnbits_url + "/" + current_payment_hash);
  http.addHeader("X-Api-Key", lnbits_api_key);

  int code = http.GET();
  Serial.print("[C] HTTP status: ");
  Serial.println(code);

  if (code == 200) {
    String raw = http.getString();
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, raw);

    if (error) {
      Serial.print("[C] Erro JSON: ");
      Serial.println(error.c_str());
      updateQRStatus("ERRO JSON", 0xF800);
      http.end();
      return;
    }

    if (doc["paid"].as<bool>()) {
      Serial.println("[C] PAGAMENTO CONFIRMADO!");
      http.end();
      desenharTelaPago();
      return;
    } else {
      Serial.println("[C] Pagamento ainda nao recebido.");
      updateQRStatus("NAO PAGO AINDA", 0xFDA0);
    }
  } else {
    Serial.print("[C] Erro HTTP: ");
    Serial.println(code);
    updateQRStatus("ERRO: " + String(code), 0xF800);
  }

  http.end();
  kickstartHardware();
}

void verificarPagamentoSilencioso() {
  if (current_payment_hash == "") return;

  HTTPClient http;
  http.begin(lnbits_url + "/" + current_payment_hash);
  http.addHeader("X-Api-Key", lnbits_api_key);

  int code = http.GET();
  if (code == 200) {
    String raw = http.getString();
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, raw);
    if (!error && doc["paid"].as<bool>()) {
      Serial.println("[AUTO] Pagamento confirmado!");
      http.end();
      desenharTelaPago();
      return;
    }
  }
  http.end();
}

/* --- TRATAMENTO DE TOQUE NA TELA --- */
void tratarToque(int px, int py) {
  if (currentScreen == SCREEN_KEYPAD) {
    // Linha 0 (y: 70 a 113): 1, 2, 3
    if (py >= 70 && py < 114) {
      if (px < 108) adicionarDigito('1');
      else if (px < 212) adicionarDigito('2');
      else adicionarDigito('3');
    }
    // Linha 1 (y: 114 a 153): 4, 5, 6
    else if (py >= 114 && py < 154) {
      if (px < 108) adicionarDigito('4');
      else if (px < 212) adicionarDigito('5');
      else adicionarDigito('6');
    }
    // Linha 2 (y: 154 a 193): 7, 8, 9
    else if (py >= 154 && py < 194) {
      if (px < 108) adicionarDigito('7');
      else if (px < 212) adicionarDigito('8');
      else adicionarDigito('9');
    }
    // Linha 3 (y: 194 a 240): DEL, 0, OK
    else if (py >= 194 && py <= 240) {
      if (px < 108) apagarDigito();
      else if (px < 212) adicionarDigito('0');
      else processarOK();
    }
  }
  else if (currentScreen == SCREEN_QR_CODE) {
    // Área lateral direita (x >= 180)
    if (px >= 180) {
      if (py < 150) {
        // Botão CHECAR
        manualCheck();
      } else {
        // Botão VOLTAR
        current_payment_hash = "";
        current_bolt11 = "";
        input_amount = "";
        desenharTelaTeclado();
      }
    }
  }
  else if (currentScreen == SCREEN_PAID || currentScreen == SCREEN_ERROR) {
    // Qualquer toque nas telas finais retorna ao teclado
    input_amount = "";
    current_payment_hash = "";
    current_bolt11 = "";
    desenharTelaTeclado();
  }
}

void verificarTouch() {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();

    // Filtro de pressão do sensor resistivo
    if (p.z > 200) {
      if (!wasTouched && (millis() - lastTouchTime > 180)) {
        wasTouched = true;
        lastTouchTime = millis();

        int px = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, 320);
        int py = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, 240);
        px = constrain(px, 0, 319);
        py = constrain(py, 0, 239);

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
  kickstartHardware();

  // Inicializa Display TFT
  tft.init();
  tft.setRotation(1); // Paisagem (320x240)
  tft.setTextWrap(false);
  tft.fillScreen(0x0000);

  // Inicializa Sensor Touch XPT2046
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touch.begin(touchscreenSPI);
  touch.setRotation(1);

  // Conexão WiFi e Inicialização da Interface
  connectWiFi();
}

/* --- LOOP PRINCIPAL --- */
void loop() {
  // 1. Processa toques na tela
  verificarTouch();

  // 2. Verificação periódica de pagamento na tela do QR code (a cada 3s)
  if (currentScreen == SCREEN_QR_CODE && current_payment_hash != "") {
    if (millis() - lastAutoCheckTime >= 3000) {
      lastAutoCheckTime = millis();
      verificarPagamentoSilencioso();
    }
  }

  // 3. Retorno automático após exibição da tela de pagamento confirmado (5s)
  if (currentScreen == SCREEN_PAID) {
    if (millis() - paidScreenTime >= 5000) {
      input_amount = "";
      current_payment_hash = "";
      current_bolt11 = "";
      desenharTelaTeclado();
    }
  }

  // 4. Comandos de depuração via Serial mantidos
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.startsWith("I:")) {
      current_sats = input.substring(2).toInt();
      createInvoice(current_sats);
    }
    else if (input.equalsIgnoreCase("C:")) {
      manualCheck();
    }
    else if (input.equalsIgnoreCase("P:")) {
      if (current_bolt11 != "") {
        Serial.flush();
        Serial.println("\n--- START BOLT11 ---");
        Serial.print(current_bolt11.c_str());
        Serial.println("\n--- END BOLT11 ---\n");
      } else {
        Serial.println("\n[!] Nenhuma invoice ativa.\n");
      }
    }
    else if (input.equalsIgnoreCase("L:")) {
      checkBalance();
    }
    else if (input.equalsIgnoreCase("H:")) {
      input_amount = "";
      current_payment_hash = "";
      current_bolt11 = "";
      desenharTelaTeclado();
    }
  }
}
