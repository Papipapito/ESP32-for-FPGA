#!/usr/bin/env python3
# tsxcat_gen.py - genera tsxcat.h (catalogo compacto de tsx.eslamejor.com) para la "Cinta web".
#
# Recorre el catalogo de la web (index_back.php?page=N&idx=LETRA, el AJAX de la pagina, exige Referer)
# y saca por cinta: TOSEC.NAME (UTF-8, tal cual, es la URL de descarga), tamano, crc32 y la orden de
# carga (TSX.LOAD). El ESP lleva la tabla en flash (PROGMEM) y busca en local: la web solo ve las
# descargas. Cuando tsx.eslamejor nos de un listado plano, este script cambia y el ESP no.
#
# Uso:  python tools/tsxcat_gen.py [--out tsxcat.h] [--txt tsxcat.txt] [--letters A,B]
import argparse, json, sys, time, urllib.request, urllib.parse

HOST = "https://tsx.eslamejor.com"
LETTERS = [chr(c) for c in range(ord('A'), ord('Z') + 1)] + ["0"]   # la seccion de los digitos es idx=0 en la web
PAGE = 50

def fetch(page, idx):
    url = "%s/index_back.php?%s" % (HOST, urllib.parse.urlencode({"page": page, "idx": idx}))
    req = urllib.request.Request(url, headers={"Referer": HOST + "/", "User-Agent": "MSXimus-tsxcat/1.0",
                                               "Accept": "application/json"})
    for intento in range(3):
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return json.loads(r.read().decode("utf-8"))
        except Exception as e:
            print("  reintento %d (%s)" % (intento + 1, e), file=sys.stderr)
            time.sleep(3)
    return None

def entries_of(d):
    if isinstance(d, list):
        return d, None
    files = d.get("files") if isinstance(d, dict) else None
    if files is None and isinstance(d, dict):
        files = next((v for v in d.values() if isinstance(v, list)), [])
    total = d.get("sectionCount") if isinstance(d, dict) else None
    return files or [], total

def load_kind(s):
    u = (s or "").upper()
    if not u: return 3
    if "BLOAD" in u: return 1
    if "CLOAD" in u: return 2
    if "RUN" in u or "LOAD" in u: return 0
    return 3

def c_escape(s):
    out = []
    for b in s.encode("utf-8"):
        if b == 0x22: out.append('\\"')
        elif b == 0x5C: out.append("\\\\")
        elif 0x20 <= b < 0x7F: out.append(chr(b))
        else: out.append("\\%03o" % b)
    return "".join(out)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="tsxcat.h")
    ap.add_argument("--txt", default=None)
    ap.add_argument("--letters", default=None, help="solo estas letras, separadas por comas (prueba)")
    a = ap.parse_args()
    letters = a.letters.split(",") if a.letters else LETTERS
    cat = []      # (name, size, crc, kind, loadstr)
    seen = set()
    for idx in letters:
        page, first_page_names = 0, None
        while True:
            d = fetch(page, idx)
            if d is None:
                print("%s p%d: sin respuesta, letra abandonada" % (idx, page), file=sys.stderr); break
            files, total = entries_of(d)
            names = []
            for f in files:
                lab = f.get("labels", {}) or {}
                fi = f.get("file", {}) or {}
                name = lab.get("TOSEC.NAME") or ""
                if not name: continue
                size = int(str(fi.get("fileSize", "0")).replace(".", "").replace(",", "") or 0)
                crc = int(str(fi.get("crc32", "0") or "0"), 16)
                ld = lab.get("TSX.LOAD") or ""
                names.append(name)
                if name in seen: continue
                seen.add(name)
                cat.append((name, size, crc, load_kind(ld), ld))
            print("%s p%d: %d cintas (total letra %s)" % (idx, page, len(names), total), file=sys.stderr)
            if page == 0:
                first_page_names = names
            elif page == 1 and names == first_page_names:
                pass   # la web numera desde 1: la pagina 0 era la 1; seguimos sin duplicar (seen)
            if not names or len(names) < PAGE:
                break
            if total is not None and (page + 1) * PAGE >= int(total) + PAGE:
                break
            page += 1
            time.sleep(0.5)
    cat.sort(key=lambda e: e[0].lower())
    # ---- salida ----
    names_blob, offs = [], []
    off = 0
    for (name, size, crc, kind, ld) in cat:
        offs.append(off)
        b = name.encode("utf-8") + b"\0"
        names_blob.append(name)
        off += len(b)
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("// tsxcat.h - catalogo compacto de tsx.eslamejor.com (GENERADO por tools/tsxcat_gen.py, no editar)\n")
        f.write("// %d cintas, %d bytes de nombres (UTF-8, TOSEC.NAME = nombre del fichero .tsx en la web)\n" % (len(cat), off))
        f.write("#pragma once\n#include <stdint.h>\n#include <pgmspace.h>\n")
        f.write("#define TSXCAT_N %d\n" % len(cat))
        f.write("struct TsxCatEntry { uint32_t nameOff; uint32_t size; uint32_t crc; uint8_t load; };\n")
        f.write("static const char tsxcat_names[] PROGMEM =\n")
        for name in names_blob:
            f.write('  "%s\\0"\n' % c_escape(name))
        f.write("  ;\n")
        f.write("static const TsxCatEntry tsxcat[] PROGMEM = {\n")
        for (name, size, crc, kind, ld), o in zip(cat, offs):
            f.write("  {%d, %d, 0x%08X, %d},\n" % (o, size, crc, kind))
        f.write("};\n")
    if a.txt:
        with open(a.txt, "w", encoding="utf-8", newline="\n") as f:
            for (name, size, crc, kind, ld) in cat:
                f.write("%s|%d|%08x|%s\n" % (name, size, crc, ld))
    print("catalogo: %d cintas -> %s" % (len(cat), a.out), file=sys.stderr)

if __name__ == "__main__":
    main()
