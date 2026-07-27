#!/usr/bin/env bash
# Compila y ejecuta el banco de pruebas de MsxHid en el PC (WSL). No toca el
# hardware: MsxHid es C++11 puro y emite por callback, asi que el flujo de bytes
# que veria el FPGA se compara aqui byte a byte.
#
#   wsl -d Ubuntu-24.04 bash -lc "/mnt/c/.../host_test/test_msxhid.sh"
#
# Devuelve 0 si pasan TODOS los casos, 1 si falla alguno.
cd "$(dirname "$0")" || exit 1

g++ -std=c++11 -O2 -Wall -Wextra -Werror -o test_msxhid test_msxhid.cpp ../MsxHid.cpp || {
    echo "FAIL: no compila"
    exit 1
}

./test_msxhid
rc=$?

if [ $rc -eq 0 ]; then
    echo "PASS: todos los casos de MsxHid"
else
    echo "FAIL: hay casos rotos (ver arriba)"
fi
exit $rc
