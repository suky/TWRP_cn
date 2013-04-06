#!/usr/bin/env python
# -*- coding: utf8 -*-
import codecs,os,gzip
from struct import *
from PIL import Image, ImageDraw, ImageFont

print u"字体文件名称"
fnt = raw_input()
print u"字体大小"
fontsize = int(raw_input())
font = ImageFont.truetype(fnt, fontsize,0,"utf-32be")
print u"字体Y轴垂直偏移(防止字体上方空白,下方的已自动切除,若无需要0即可)"
Y = int(raw_input())
print u"待生成文本(需为utf8编码)"
txt = raw_input()
print u"是否生成预览图片(生成到preview文件夹下)(y/n)"
yes = raw_input()
f = codecs.open(txt,"rb","utf-8","ignore")
ch = [ ]
while True:
        tmp = f.read(1)
        if tmp == "":break
        ch += [tmp]
ch = list(set(list(ch))-set("\n\r\t"))
f.close()
print u"正在绘制字符"
index = ""
cwidth = ""
cheight = ""
data = ""
for i in range(len(ch)):
	width,height = font.getsize(ch[i])
	im = Image.new('L', (width, height), 0)
	text = ImageDraw.Draw(im)
	index += ch[i].encode("utf-32le")
	text.text((0,-Y), ch[i], 255, font)
	for j in range(height-1):
			bottom = im.crop((0,height-1,width,height))
			if (bottom.tostring() != "\x00"*width):break
			height = height-1

	im = im.crop((0,0,width,height))
	cwidth += pack("B",width)
	cheight += pack("B",height)
	data += im.tostring()
	if yes == "y":
		if not os.path.isdir(".%spreview"%os.sep):
			os.makedirs(".%spreview"%os.sep)
		try:
			im.save(".%spreview%s%s.png"%(os.sep,os.sep,ch[i]))
		except:
			print u"特殊字符(%s)无法保存,另存为p_%d.png"%(ch[i].encode("utf8"),i)
			im.save(".%spreview%sp_%d.png"%(os.sep,os.sep,i))

g = gzip.GzipFile(filename=r'font.dat', mode='wb', compresslevel=9)
g.write(pack("I",len(ch)))
g.write(index)
g.write(cwidth)
g.write(cheight)
g.write(data)
g.close()
