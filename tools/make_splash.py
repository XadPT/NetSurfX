#!/usr/bin/env python3
"""Build res/splash.png (640x480) for NetSurfX.

    python3 tools/make_splash.py <netsurf tree> <fonts dir> <version> <out.png>

The NetSurf logo comes from NetSurf's own Amiga splash screen
(frontends/amiga/resources/splash.png, GPLv2 like the rest of NetSurf).
Its background is a horizontal gradient that is the same on every row, so
the logo is lifted with its background and placed on that gradient,
rebuilt at full size: there are no visible seams. Below it goes the
edition line; the version goes in a corner.
"""
import os, sys
from PIL import Image, ImageDraw, ImageFont

W, H = 640, 480
SCALE = 1.6
LOGO_ROWS = (62, 134)        # ball + "NetSurf" in the Amiga splash
BG_ROW = 175                 # a row with nothing on it but the gradient


def main():
    ns, fonts, version, out = sys.argv[1:5]
    src = Image.open(os.path.join(ns, 'frontends/amiga/resources/splash.png')).convert('RGB')
    sw, sh = src.size

    # gradient, one colour per column, stretched to the new width
    # (columns 0-1 and the last two are a frame line in the original)
    grad_row = src.crop((3, BG_ROW, sw - 3, BG_ROW + 1)).resize((W, 1), Image.LANCZOS)
    img = grad_row.resize((W, H), Image.NEAREST)

    # lift the logo off its background: the background under each logo pixel
    # is estimated from the clear rows just above and below it, and pixels
    # close to it become transparent (soft edge keeps the anti-aliasing)
    y0, y1 = LOGO_ROWS
    above, below = y0 - 3, y1 + 1
    logo = Image.new('RGBA', (sw, y1 - y0))
    lp, sp = logo.load(), src.load()
    for y in range(y0, y1):
        t = (y - above) / float(below - above)
        for x in range(sw):
            a, b = sp[x, above], sp[x, below]
            bg = [a[i] + (b[i] - a[i]) * t for i in range(3)]
            px = sp[x, y]
            dist = sum(abs(px[i] - bg[i]) for i in range(3))
            alpha = max(0, min(255, int((dist - 12) * 255 / 40)))
            lp[x, y - y0] = (px[0], px[1], px[2], alpha)
    lw, lh = int(sw * SCALE), int((y1 - y0) * SCALE)
    logo = logo.resize((lw, lh), Image.LANCZOS)
    top = 120
    img.paste(logo, ((W - lw) // 2, top), logo)

    d = ImageDraw.Draw(img)
    f_ed = ImageFont.truetype(os.path.join(fonts, 'DejaVuSerif-Bold.ttf'), 30)
    f_small = ImageFont.truetype(os.path.join(fonts, 'DejaVuSans.ttf'), 14)

    text = 'Original Xbox Edition'
    tw = d.textlength(text, font=f_ed)
    ty = top + lh + 22
    d.text(((W - tw) / 2, ty), text, font=f_ed, fill=(0, 0, 0))

    # a thin rule between logo and text, like the Amiga splash spacing
    rule_y = ty - 10
    d.line(((W - tw) / 2, rule_y, (W + tw) / 2, rule_y), fill=(40, 60, 120), width=1)

    ver = 'NetSurfX %s' % version
    vw = d.textlength(ver, font=f_small)
    d.text((W - vw - 28, H - 40), ver, font=f_small, fill=(235, 240, 255))
    d.text((28, H - 40), 'netsurf-browser.org', font=f_small, fill=(235, 240, 255))

    img.save(out, optimize=True)
    print('wrote', out, img.size)


if __name__ == '__main__':
    main()
