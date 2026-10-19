#!/bin/bash

# Configurações do projeto e da placa
SKETCH_PATH="$HOME/CYD_POS_KIX"
PORT="/dev/ttyUSB0"
FQBN="esp32:esp32:esp32"
BAUDRATE="115200"

echo "=========================================="
echo "  Iniciando compilação do CYD_POS_KIX... "
echo "=========================================="

arduino-cli compile --fqbn "$FQBN" "$SKETCH_PATH"

if [ $? -ne 0 ]; then
    echo "❌ Erro na compilação. Processo interrompido."
    exit 1
fi

echo "=========================================="
echo "  Enviando código para o ESP32...         "
echo "  (Segure o botão BOOT se necessário)     "
echo "=========================================="

arduino-cli upload -p "$PORT" --fqbn "$FQBN" "$SKETCH_PATH"

if [ $? -ne 0 ]; then
    echo "❌ Erro durante o upload. Verifique a conexão do ESP32."
    exit 1
fi

echo "=========================================="
echo "  Upload concluído com sucesso!            "
echo "  Iniciando o Monitor Serial...           "
echo "=========================================="

arduino-cli monitor -p "$PORT" -c baudrate="$BAUDRATE"
