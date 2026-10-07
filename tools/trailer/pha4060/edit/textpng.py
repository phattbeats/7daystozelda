# text overlays as transparent PNGs (ffmpeg here has no drawtext)
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import sys
def text_png(path, W, H, lines):
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    for text, size, color, y in lines:
        f = ImageFont.truetype('/tmp/z4060/edit/Cinzel.ttf', size)
        w = d.textbbox((0, 0), text, font=f)[2]
        x = (W - w) // 2
        sh = Image.new('RGBA', (W, H), (0, 0, 0, 0)); ds = ImageDraw.Draw(sh)
        ds.text((x, y), text, font=f, fill=(0, 0, 0, 230), stroke_width=max(2, size // 14), stroke_fill=(0, 0, 0, 230))
        im = Image.alpha_composite(im, sh.filter(ImageFilter.GaussianBlur(2)))
        d = ImageDraw.Draw(im); d.text((x, y), text, font=f, fill=color)
    im.save(path)
if __name__ == '__main__':
    text_png('tag16x9.png', 1920, 1080, [('Ocarina of Time, but co-op zombie survival.', 62, (255, 255, 255, 255), 700)])
    text_png('url16x9.png', 1920, 1080, [('zelda.phatt.vip', 46, (242, 193, 78, 255), 805)])
