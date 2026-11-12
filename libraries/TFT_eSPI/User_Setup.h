#define USER_SETUP_ID 24
#define ILI9341_2_DRIVER

// Corrige cores invertidas (imagem negativa) neste painel.
// Este painel é "nativamente invertido": OFF (que é o default) continua
// mostrando negativo. ON inverte a polaridade e devolve as cores corretas.
#define TFT_INVERSION_ON

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15  // Chip select control pin
#define TFT_DC    2  // Data Command control pin
#define TFT_RST  -1  // Set to -1 if reset is connected to ESP32 EN pin

#define TFT_BL   21  // LED back-light control pin
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ID = 1
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs turn on and 3KB
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs turn on and 58KB
#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs turn on and 26KB
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs turn on and 24KB
#define LOAD_FONT8  // Font 8. Large 75 pixel font needs turn on and 75KB
#define LOAD_GFXFF  // FreeFonts. Include access to the 48 Adafruit_GFX free fonts FF1 to FF48

#define SMOOTH_FONT

#define SPI_FREQUENCY  55000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
