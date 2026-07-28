#!/usr/bin/env bash
# Compila y ejecuta el banco de pruebas de ScreenS3 en el PC (WSL). No toca el
# hardware: se stubean Arduino.h y Arduino_GFX con las MISMAS firmas que la
# libreria real, asi que si compila aqui encaja tambien en la placa.
#
# El modulo son DOS ficheros (ScreenS3.cpp = driver, ScreenS3Boot.cpp =
# narrativa del arranque); los dos entran por el #include de test_screens3.cpp,
# asi que aqui solo hay una unidad de compilacion.
#
#   wsl -d Ubuntu-24.04 bash -lc "/mnt/c/Users/alber/proyectosAI/msx/ESP32-UNAPI-Firmware/host_test/test_screens3.sh"
#
# Devuelve 0 si pasan los dos escenarios (arranque normal y sin WiFi).
cd "$(dirname "$0")" || exit 1

g++ -std=c++11 -O2 -Wall -Wextra -Werror -o test_screens3 test_screens3.cpp \
    -I stub_screen -DSCREEN_USE_UNAPI_GLOBALS=0 || {
    echo "FAIL: no compila (o hay avisos: se compila con -Werror)"
    exit 1
}
echo "=== compilado OK (-Wall -Wextra -Werror: cero avisos) ==="

rc=0
./test_screens3       || rc=1     # escenario 1: arranque normal
./test_screens3 fallo || rc=1     # escenario 2: sin WiFi

if [ $rc -eq 0 ]; then
    echo "PASS: ScreenS3 supera los dos escenarios"
else
    echo "FAIL: revisa la salida"
fi
exit $rc
