# -*- coding: utf8 -*-
import codecs
from struct import *
from PIL import Image, ImageDraw, ImageFont

fontsize = 30
font = ImageFont.truetype('msyh.ttf', fontsize)

f = codecs.open("UTF8.txt","rb","utf-8","ignore")
ch = list(set(f.read()))
f.close()

im = Image.new('P', (1, 1), 0)
text = ImageDraw.Draw(im)
index = ""
cwidth = ""
cheight = ""
data = ""
for i in range(len(ch)):
	text_size = text.textsize(ch[i],font)
	im = Image.new('P', (text_size[0], text_size[1]), 0)
	text = ImageDraw.Draw(im)
	index += ch[i].encode("utf-32le")
	cwidth += pack("B",text_size[0])
	cheight += pack("B",text_size[1])
	text.text((0,0), ch[i], 255, font)
	data += im.tostring()

import gzip

g = gzip.GzipFile(filename=r'font.dat', mode='wb', compresslevel=9)
g.write(pack("I",len(ch)))
g.write(index)
g.write(cwidth)
g.write(cheight)
g.write(data)
g.close()
