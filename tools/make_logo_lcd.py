#!/usr/bin/env python3
"""
make_logo_lcd.py - Convierte el logo de MSX Barcelona a un bitmap para la
pantalla del companion S3 (ST7789, 320x170, RGB565).

Genera LogoMsxBcn.h: los datos van comprimidos con RLE de 16 bits porque el
logo es casi todo areas planas -un azul corporativo sobre blanco- y en crudo
ocuparia 108 KB de flash, que con este firmware al 80% de la particion no
sobran. El decodificador son quince lineas (ver ScreenS3.cpp).

Uso:  python tools/make_logo_lcd.py [imagen] [--preview salida.png]
      por defecto lee ../MSXnano/fpga/src/rom/logo_site.webp
"""
import sys
import os
from PIL import Image

W, H = 320, 170                 # lienzo en horizontal
BG = (255, 255, 255)            # fondo blanco, como la marca
MARGIN = 12                     # aire alrededor del logo

DEFAULT_SRC = os.path.join(os.path.dirname(__file__), "..", "..",
                           "MSXnano", "fpga", "src", "rom", "logo_site.webp")


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def build(src_path):
    im = Image.open(src_path).convert("RGBA")

    # El .webp trae mucho margen transparente: recortamos al contenido real
    # para que el logo llene la pantalla y no quede diminuto en el centro.
    bbox = im.split()[3].getbbox()
    if bbox:
        im = im.crop(bbox)

    # Encajar dentro del lienzo conservando proporcion.
    avail_w, avail_h = W - 2 * MARGIN, H - 2 * MARGIN
    scale = min(avail_w / im.width, avail_h / im.height)
    new = (max(1, int(im.width * scale)), max(1, int(im.height * scale)))
    im = im.resize(new, Image.LANCZOS)

    # Componer sobre blanco (aplana el alfa y conserva el antialias, que es lo
    # que hace que el logo no se vea dentado en un IPS a tamano real).
    canvas = Image.new("RGB", (W, H), BG)
    canvas.paste(im, ((W - im.width) // 2, (H - im.height) // 2),
                 im.split()[3])
    return canvas


def encode_rle(img):
    """RLE de 16 bits: pares (repeticiones, color). Recorre en orden de
    barrido, que es como lo consume el volcado al LCD."""
    px = list(img.convert("RGB").getdata())
    out = []
    run_col = rgb565(*px[0])
    run_len = 1
    for p in px[1:]:
        c = rgb565(*p)
        if c == run_col and run_len < 0xFFFF:
            run_len += 1
        else:
            out.append((run_len, run_col))
            run_col, run_len = c, 1
    out.append((run_len, run_col))
    return out


def main():
    # Parseo a mano: hay que saltarse tanto el flag --preview como SU VALOR,
    # o la ruta de la vista previa acabaria tomandose como imagen de origen.
    argv = sys.argv[1:]
    preview = None
    args = []
    i = 0
    while i < len(argv):
        if argv[i] == "--preview" and i + 1 < len(argv):
            preview = argv[i + 1]
            i += 2
        else:
            args.append(argv[i])
            i += 1

    src = args[0] if args else DEFAULT_SRC
    img = build(src)

    if preview:
        img.save(preview)
        print("vista previa:", preview)

    runs = encode_rle(img)
    raw = W * H * 2
    comp = len(runs) * 4
    hdr = os.path.join(os.path.dirname(__file__), "..", "LogoMsxBcn.h")

    with open(hdr, "w", newline="\n") as f:
        f.write(f"""// ============================================================================
// LogoMsxBcn.h - Logo de MSX Barcelona para la pantalla de arranque (320x170).
//
// GENERADO por tools/make_logo_lcd.py a partir del MISMO original que usa el
// logo de arranque del MSX (fpga/src/rom/logo_site.webp), asi que la pantalla
// del companion y la del propio MSX ensenan exactamente la misma marca.
//
// NO EDITAR A MANO: se regenera con
//     python tools/make_logo_lcd.py
//
// Formato: RLE de 16 bits, pares (repeticiones, color RGB565) en orden de
// barrido. El logo es casi todo areas planas, asi que comprime muchisimo:
//   crudo {raw} bytes -> {comp} bytes ({100.0*comp/raw:.1f}%)
// En crudo no cabria comodo: el firmware ya va al 80% de la particion.
// ============================================================================
#ifndef LOGO_MSX_BCN_H
#define LOGO_MSX_BCN_H

#include <stdint.h>

#define LOGO_W {W}
#define LOGO_H {H}
#define LOGO_RUNS {len(runs)}

// Cada entrada: {{ repeticiones, color RGB565 }}
static const uint16_t LOGO_RLE[LOGO_RUNS][2] = {{
""")
        for i in range(0, len(runs), 6):
            chunk = runs[i:i + 6]
            f.write("    " + " ".join(f"{{{n:5d},0x{c:04X}}}," for n, c in chunk) + "\n")
        f.write("};\n\n#endif // LOGO_MSX_BCN_H\n")

    print(f"tramos RLE: {len(runs)}")
    print(f"crudo {raw} B -> comprimido {comp} B ({100.0*comp/raw:.1f}%)")
    print("escrito:", os.path.normpath(hdr))


if __name__ == "__main__":
    main()
