#!/usr/bin/env python3
"""make_ca_raices.py - genera CaRaices.h: las raices de confianza que el firmware lleva de serie (02/10/2026).

El TLS con validacion (TCP_OPEN con "verificar certificado", lo que pide MXUPDATE para msx.barcelona) necesita un
CA. Si en la FFat no hay /ca.pem ni /certs.bin, el firmware usa estas. Son RAICES, no el certificado de la web: el
de msx.barcelona (Sectigo, renovado cada ano) cuelga de "Sectigo Public Server Authentication Root R46", que vale
hasta 2046; las demas cubren un cambio de autoridad de IONOS (Let's Encrypt, DigiCert, GlobalSign, Google, Amazon,
GoDaddy/Starfield, Cloudflare). Salen del almacen de Mozilla que trae el paquete certifi de Python.

    python tools/make_ca_raices.py            (deja CaRaices.h al lado del .ino)
"""
import datetime
import os
import re
import ssl
import sys

import certifi

RAICES = [
    "Sectigo Public Server Authentication Root R46",     # la de msx.barcelona hoy (IONOS)
    "Sectigo Public Server Authentication Root E46",
    "USERTrust RSA Certification Authority",             # Sectigo de antes (y su firma cruzada de la R46)
    "USERTrust ECC Certification Authority",
    "ISRG Root X1",                                      # Let's Encrypt
    "ISRG Root X2",
    "DigiCert Global Root G2",
    "GlobalSign Root CA - R3",
    "GlobalSign Root CA - R6",
    "GTS Root R1",                                       # Google
    "GTS Root R4",                                       # Google ECC: Cloudflare (tsx.eslamejor.com)
    "SSL.com TLS RSA Root CA 2022",                      # Cloudflare tambien firma con SSL.com
    "SSL.com TLS ECC Root CA 2022",
    "COMODO RSA Certification Authority",                # Sectigo de antes
    "Amazon Root CA 1",
    "Starfield Root Certificate Authority - G2",         # GoDaddy
]

AQUI = os.path.dirname(os.path.abspath(__file__))
SALIDA = os.path.join(AQUI, "..", "CaRaices.h")


def main():
    t = open(certifi.where(), encoding="utf-8").read()
    pems = dict((lab, pem) for lab, pem in re.findall(
        r'# Label: "(.*?)"\n.*?(-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----)', t, re.S))
    faltan = [r for r in RAICES if r not in pems]
    if faltan:
        sys.exit("no estan en certifi: %s" % ", ".join(faltan))
    lineas = []
    for r in RAICES:
        der = ssl.PEM_cert_to_DER_cert(pems[r])
        fin = ""
        try:
            from cryptography import x509
            c = x509.load_der_x509_certificate(der)
            fin = getattr(c, "not_valid_after_utc", None) or c.not_valid_after
            fin = fin.strftime("%Y-%m-%d")
        except Exception:
            pass
        lineas.append("// %s%s" % (r, "  (hasta %s)" % fin if fin else ""))
    cuerpo = "\n".join(pems[r] for r in RAICES) + "\n"
    with open(SALIDA, "w", encoding="ascii", newline="\n") as f:
        f.write("// CaRaices.h - GENERADO por tools/make_ca_raices.py (%s), certifi %s. No editar a mano.\n"
                % (datetime.date.today().isoformat(), certifi.__version__))
        f.write("// Raices de confianza de serie para el TLS con validacion cuando la FFat no trae /ca.pem ni /certs.bin:\n")
        f.write("\n".join(lineas) + "\n")
        f.write("#ifndef _CA_RAICES_H\n#define _CA_RAICES_H\n")
        f.write("static const char CA_RAICES[] =\n")
        for l in cuerpo.splitlines():
            f.write('  "%s\\n"\n' % l)
        f.write("  ;\n#endif\n")
    print("%s: %d raices, %d bytes de PEM" % (os.path.normpath(SALIDA), len(RAICES), len(cuerpo)))


if __name__ == "__main__":
    main()
