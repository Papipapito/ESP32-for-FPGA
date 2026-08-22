#!/usr/bin/env bash
# Compila y ejecuta el banco del enlace SPI con la FPGA, en el PC (WSL).
# Companion.cpp es C++ puro (ni Arduino ni IDF) a proposito: el protocolo se
# prueba sin placa, que es lo unico que permite iterar rapido en algo que de
# otro modo exige soldar y flashear para cada cambio.
#
#   wsl -d Ubuntu-24.04 bash -lc "/mnt/c/Users/alber/proyectosAI/msx/ESP32-UNAPI-Firmware/host_test/test_companion.sh"
cd "$(dirname "$0")" || exit 1

g++ -std=c++11 -O2 -Wall -Wextra -Werror -o test_companion test_companion.cpp || {
    echo "FAIL: no compila (o hay avisos: se compila con -Werror)"
    exit 1
}
echo "=== compilado OK (-Wall -Wextra -Werror: cero avisos) ==="

./test_companion
rc=$?
[ $rc -eq 0 ] && echo "PASS: el enlace SPI supera el banco" || echo "FAIL: revisa la salida"
exit $rc
