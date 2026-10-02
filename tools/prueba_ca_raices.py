#!/usr/bin/env python3
"""prueba_ca_raices.py - comprueba en el PC que CaRaices.h basta para validar un servidor (TLS 1.2 y 1.3).

    python tools/prueba_ca_raices.py [host ...]          (por defecto msx.barcelona)
Valida con TODAS las raices de CaRaices.h y, para ver que no hay una sola de la que todo dependa, con cada raiz por
separado; dice cual de ellas valida al servidor.
"""
import os
import re
import socket
import ssl
import sys
import tempfile

AQUI = os.path.dirname(os.path.abspath(__file__))


def raices():
    h = open(os.path.join(AQUI, "..", "CaRaices.h"), encoding="ascii").read()
    pem = "\n".join(re.findall(r'^  "(.*)\\n"$', h, re.M)) + "\n"
    certs = re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", pem, re.S)
    nombres = re.findall(r"^// (.+?)(?:  \(hasta .*\))?$", h.split("#ifndef")[0], re.M)[2:]
    return pem, list(zip(nombres, certs))


def valida(host, pem, tls):
    f = tempfile.NamedTemporaryFile("w", delete=False, suffix=".pem")
    f.write(pem)
    f.close()
    c = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    c.load_verify_locations(f.name)
    c.minimum_version = c.maximum_version = tls
    try:
        with socket.create_connection((host, 443), timeout=15) as s, c.wrap_socket(s, server_hostname=host):
            return True
    except ssl.SSLError:
        return False
    finally:
        os.unlink(f.name)


def main():
    pem, lista = raices()
    bien = True
    for host in sys.argv[1:] or ["msx.barcelona"]:
        for tls in (ssl.TLSVersion.TLSv1_2, ssl.TLSVersion.TLSv1_3):
            ok = valida(host, pem, tls)
            bien &= ok
            cuales = [n for n, c in lista if valida(host, c + "\n", tls)] if ok else []
            print("%s %s: %s%s" % (host, tls.name, "VALIDA" if ok else "NO VALIDA",
                                   " (con %s)" % ", ".join(cuales) if cuales else ""))
    sys.exit(0 if bien else 1)


if __name__ == "__main__":
    main()
