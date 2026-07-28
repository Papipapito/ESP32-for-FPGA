#!/usr/bin/env bash
# Verificacion en el PC de XInputHost. Dos pasos INDEPENDIENTES:
#
#   1) PARTE PURA: compila y EJECUTA test_xinput.cpp (troceado de reports +
#      cadena completa hasta el byte MSX). Esto si prueba comportamiento.
#   2) PARTE DE TRANSPORTE: compila XInputHost.cpp fingiendo ser un ESP32-S3,
#      contra cabeceras de ESP-IDF transcritas (host_test/stub_idf). Esto NO
#      prueba comportamiento: prueba que cada llamada al usb_host existe y con
#      los tipos correctos. Lo demas necesita la placa.
#
#   wsl -d Ubuntu-24.04 bash -lc "/mnt/c/Users/alber/proyectosAI/msx/ESP32-UNAPI-Firmware/host_test/test_xinput.sh"
cd "$(dirname "$0")" || exit 1

rc=0

echo "=== 1/2  parte pura (se ejecuta) ==="
g++ -std=c++11 -O2 -Wall -Wextra -Werror -o test_xinput \
    test_xinput.cpp ../XInputHost.cpp ../MsxHid.cpp || {
    echo "FAIL: no compila la parte pura"
    exit 1
}
./test_xinput || rc=1

echo
echo "=== 2/2  transporte USB (solo compila, contra firmas reales de ESP-IDF) ==="
# -DARDUINO_ARCH_ESP32 hace que XInputHost.cpp incluya sdkconfig.h; el stub de
# sdkconfig.h define CONFIG_IDF_TARGET_ESP32S3 y con eso entra la PARTE B.
# -c: no se enlaza (las funciones del usb_host no tienen cuerpo aqui, a proposito).
g++ -std=c++11 -O2 -Wall -Wextra -Werror -c -o /dev/null \
    -DARDUINO_ARCH_ESP32 -I stub_idf ../XInputHost.cpp || {
    echo "FAIL: el transporte NO compila contra las firmas de ESP-IDF"
    exit 1
}
echo "OK: el transporte compila (firmas de usb_host correctas)"

echo
if [ $rc -eq 0 ]; then
    echo "PASS: XInputHost (parte pura verificada + transporte compilando)"
    echo "      PENDIENTE DE HARDWARE: enumeracion, claim y polling reales."
else
    echo "FAIL: hay casos rotos (ver arriba)"
fi
exit $rc
