#!/usr/bin/env bash
# Compila y ejecuta el banco del nucleo del lanzador File-Hunter en el PC (WSL).
# No hay stubs: FileHunter.cpp es C++ puro (ni Arduino ni IDF) a proposito, para
# que la logica que comparten el C6 y el S3 se pueda probar sin placa.
#
#   wsl -d Ubuntu-24.04 bash -lc "/mnt/c/Users/alber/proyectosAI/msx/ESP32-UNAPI-Firmware/host_test/test_filehunter.sh"
#
# Devuelve 0 si pasan todas las comprobaciones.
cd "$(dirname "$0")" || exit 1

g++ -std=c++11 -O2 -Wall -Wextra -Werror -o test_filehunter test_filehunter.cpp || {
    echo "FAIL: no compila (o hay avisos: se compila con -Werror)"
    exit 1
}
echo "=== compilado OK (-Wall -Wextra -Werror: cero avisos) ==="

./test_filehunter
rc=$?

if [ $rc -eq 0 ]; then
    echo "PASS: el nucleo del lanzador supera el banco"
else
    echo "FAIL: revisa la salida"
fi
exit $rc
