# -*- coding: utf-8 -*-
"""生成 CN-WoW Patcher LOGO v3：12 几乎填充整个正方形"""
from PIL import Image, ImageDraw, ImageFont

SIZE = 1024

img = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

top = (0x14, 0x2E, 0x5C)
bot = (0x00, 0x74, 0xE0)
for y in range(SIZE):
    t = y / SIZE
    r = int(top[0] + (bot[0] - top[0]) * t)
    g = int(top[1] + (bot[1] - top[1]) * t)
    b = int(top[2] + (bot[2] - top[2]) * t)
    d.rectangle([0, y, SIZE, y], fill=(r, g, b, 255))

mask = Image.new('L', (SIZE, SIZE), 0)
md = ImageDraw.Draw(mask)
md.rounded_rectangle([0, 0, SIZE - 1, SIZE - 1], radius=SIZE // 7, fill=255)
img.putalpha(mask)

hl = Image.new('RGBA', (SIZE, SIZE), (0, 0, 0, 0))
hd = ImageDraw.Draw(hl)
hd.rounded_rectangle([0, 0, SIZE - 1, SIZE - 1], radius=SIZE // 7, fill=(255, 255, 255, 26))
img = Image.alpha_composite(img, hl)

d = ImageDraw.Draw(img)

# 数字 12：几乎填充（0.85），阴影 + 主体
font = ImageFont.truetype(r'C:\Windows\Fonts\segoeuib.ttf', int(SIZE * 0.85))
bbox = d.textbbox((0, 0), '12', font=font)
tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
x = (SIZE - tw) // 2 - bbox[0]
y = (SIZE - th) // 2 - bbox[1]
d.text((x + SIZE // 60, y + SIZE // 60), '12', font=font, fill=(0, 0, 0, 110))
d.text((x, y), '12', font=font, fill=(255, 255, 255, 255))

out_dir = r'C:\Users\Adavak\Documents\GitHub\CNWoW_Patcher'
img.save(out_dir + r'\logo_1024.png')
img.resize((512, 512), Image.LANCZOS).save(out_dir + r'\logo_512.png')
img.resize((256, 256), Image.LANCZOS).save(
    out_dir + r'\CNWoW.ico',
    sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print('LOGO v3 已生成（12 填满）')
