#!/usr/bin/env python3
"""
make_logo_msx.py - Genera LogoMsx.h: el logotipo MSX del arranque, como
MASCARA DE 1 BIT para la pantalla del C6 (240x240).

POR QUE UNA MASCARA Y NO UN BITMAP: el logo es blanco sobre negro, no tiene
medias tintas. Guardarlo como RGB565 en crudo costaba 115 KB (LogoMsximus.h,
que era 240x240x2); como mascara son 1,3 KB. Y ademas hace falta asi: la
animacion de arranque pinta el logo DOS VECES con desplazamientos distintos y
colorea el solape, cosa que con un bitmap fijo no se puede hacer.

Uso:  python tools/make_logo_msx.py [imagen] [--ancho N] [--preview p.png]
      por defecto lee tools/logo_msx_src.png

La imagen fuente debe ser el logotipo en BLANCO sobre fondo negro, ya recortado
a su caja (sin margenes): el script solo escala y umbraliza.
"""
import sys, os

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    ops  = {a.split('=')[0]: (a.split('=')[1] if '=' in a else True)
            for a in sys.argv[1:] if a.startswith('--')}
    aqui = os.path.dirname(os.path.abspath(__file__))
    src  = args[0] if args else os.path.join(aqui, 'logo_msx_src.png')
    W    = int(ops.get('--ancho', 192))

    from PIL import Image
    im = Image.open(src).convert('L')
    H = max(1, round(W * im.size[1] / im.size[0]))
    im = im.resize((W, H), Image.LANCZOS)
    px = im.load()

    # umbral por encima del medio: los bordes suavizados del reescalado caen del
    # lado que toque sin dejar pixeles sueltos
    filas, tinta = [], 0
    for y in range(H):
        fila = bytearray((W + 7) // 8)
        for x in range(W):
            if px[x, y] > 110:
                fila[x >> 3] |= 0x80 >> (x & 7)
                tinta += 1
        filas.append(fila)

    bpr = (W + 7) // 8
    out = os.path.join(os.path.dirname(aqui), 'LogoMsx.h')
    with open(out, 'w', newline='\n') as f:
        f.write('// LogoMsx.h - GENERADO por tools/make_logo_msx.py, no editar a mano.\n')
        f.write('// Logotipo MSX como mascara de 1 bit, bit7 = pixel mas a la izquierda.\n')
        f.write('// Fuente: %s\n\n' % os.path.basename(src))
        f.write('#ifndef LOGOMSX_H\n#define LOGOMSX_H\n\n')
        f.write('#define LOGO_MSX_W     %d\n' % W)
        f.write('#define LOGO_MSX_H     %d\n' % H)
        f.write('#define LOGO_MSX_BPR   %d   // bytes por fila\n\n' % bpr)
        f.write('static const uint8_t LOGO_MSX[LOGO_MSX_H * LOGO_MSX_BPR] = {\n')
        for y, fila in enumerate(filas):
            f.write('  ' + ','.join('0x%02X' % b for b in fila) + ',\n')
        f.write('};\n\n#endif // LOGOMSX_H\n')

    print('%s: %dx%d, %d bytes de mascara, %.1f%% de tinta'
          % (os.path.basename(out), W, H, H * bpr, 100.0 * tinta / (W * H)))

    if '--preview' in ops:
        p = Image.new('RGB', (W, H), (0, 0, 0))
        q = p.load()
        for y in range(H):
            for x in range(W):
                if filas[y][x >> 3] & (0x80 >> (x & 7)):
                    q[x, y] = (248, 248, 248)
        p.save(ops['--preview'])
        print('preview:', ops['--preview'])

if __name__ == '__main__':
    main()
