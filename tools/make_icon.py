# Generates resources/icon.png (1024x1024), the app icon. Requires Pillow.
from PIL import Image, ImageDraw, ImageFilter
import math
S=2048; c=S/2
img=Image.new("RGBA",(S,S),(0,0,0,0))
# rounded-square background with vertical gradient (macOS icon grid: 824/1024 body)
body=int(S*824/1024); off=(S-body)//2; rad=int(body*0.225)
grad=Image.new("RGBA",(S,S))
gd=ImageDraw.Draw(grad)
for y in range(S):
    t=y/S; a=(46,44,48); b=(18,17,20)
    gd.line([(0,y),(S,y)],fill=tuple(int(a[i]+(b[i]-a[i])*t) for i in range(3))+(255,))
mask=Image.new("L",(S,S),0); ImageDraw.Draw(mask).rounded_rectangle([off,off,off+body,off+body],rad,fill=255)
shadow=Image.new("RGBA",(S,S),(0,0,0,0)); ImageDraw.Draw(shadow).rounded_rectangle([off,off+24,off+body,off+body+24],rad,fill=(0,0,0,150))
img=Image.alpha_composite(img,shadow.filter(ImageFilter.GaussianBlur(30)))
img.paste(grad,(0,0),mask)
d=ImageDraw.Draw(img)
d.rounded_rectangle([off,off,off+body,off+body],rad,outline=(232,168,60,110),width=10)
gold=(240,170,50,255)
R=body*0.36
# tick marks
for i in range(11):
    ang=math.radians(135+i*27)
    r1,r2=R*1.12,R*1.22
    d.line([(c+r1*math.cos(ang),c+r1*math.sin(ang)),(c+r2*math.cos(ang),c+r2*math.sin(ang))],fill=(150,145,150,255),width=16)
# track and value arc (angles in PIL: degrees clockwise from 3 o'clock)
box=[c-R,c-R,c+R,c+R]
d.arc(box,135,405,fill=(60,58,64,255),width=58)
d.arc(box,135,135+270*0.78,fill=gold,width=58)
# knob body
kr=R*0.72
knob=Image.new("RGBA",(S,S),(0,0,0,0)); kd=ImageDraw.Draw(knob)
for i in range(int(kr),0,-2):
    t=i/kr; col=int(28+38*(1-t)**0.7)
    kd.ellipse([c-i,c-i-kr*0.08*(1-t),c+i,c+i-kr*0.08*(1-t)],fill=(col,col,col+4,255))
ks=Image.new("RGBA",(S,S),(0,0,0,0)); ImageDraw.Draw(ks).ellipse([c-kr,c-kr+30,c+kr,c+kr+30],fill=(0,0,0,170))
img=Image.alpha_composite(img,ks.filter(ImageFilter.GaussianBlur(25)))
img=Image.alpha_composite(img,knob)
d=ImageDraw.Draw(img)
d.ellipse([c-kr,c-kr,c+kr,c+kr],outline=(90,88,95,255),width=8)
# pointer at the arc's end
ang=math.radians(135+270*0.78)
d.line([(c+kr*0.25*math.cos(ang),c+kr*0.25*math.sin(ang)),(c+kr*0.88*math.cos(ang),c+kr*0.88*math.sin(ang))],fill=gold,width=40)
d.ellipse([c+kr*0.88*math.cos(ang)-20,c+kr*0.88*math.sin(ang)-20,c+kr*0.88*math.cos(ang)+20,c+kr*0.88*math.sin(ang)+20],fill=gold)
img=img.resize((1024,1024),Image.LANCZOS)
img.save("resources/icon.png")  # run from the repository root: python3 tools/make_icon.py
