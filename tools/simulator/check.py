import sys, glob, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw
W,H=410,502; R=int(sys.argv[1]) if len(sys.argv)>1 else 80
bgs={(0x0B,0x0F,0x14)}
def load(p):
    data=open(p,'rb').read(); im=Image.new('RGB',(W,H)); px=im.load()
    for i in range(W*H):
        v=data[2*i]|(data[2*i+1]<<8); r=(v>>11)&31; g=(v>>5)&63; b=v&31
        px[i%W,i//W]=((r*255)//31,(g*255)//63,(b*255)//31)
    return im
def outside(x,y):
    cx = R if x<R else (W-1-R if x>W-1-R else x); cy = R if y<R else (H-1-R if y>H-1-R else y)
    return (x-cx)**2+(y-cy)**2 > R*R
os.makedirs('png',exist_ok=True)
for p in sorted(glob.glob('shots/*.raw')):
    im=load(p); px=im.load(); bad=0; bg=px[W//2,H-3]
    for y in list(range(0,R))+list(range(H-R,H)):
        for x in list(range(0,R))+list(range(W-R,W)):
            if outside(x,y):
                c=px[x,y]
                if max(abs(c[i]-bg[i]) for i in range(3))>24: bad+=1
    edge=sum(1 for x in range(W) for y in (0,1,H-2,H-1) if max(abs(px[x,y][i]-bg[i]) for i in range(3))>24)
    edge+=sum(1 for y in range(H) for x in (0,1,W-2,W-1) if max(abs(px[x,y][i]-bg[i]) for i in range(3))>24)
    name=os.path.basename(p)[:-4]
    print(f"{name:28s} corner-clipped px: {bad:5d}   edge px: {edge}")
    # device-like render: black outside the rounded panel
    m=Image.new('L',(W,H),0); ImageDraw.Draw(m).rounded_rectangle([0,0,W-1,H-1],radius=R,fill=255)
    dev=Image.new('RGB',(W,H),(0,0,0)); dev.paste(im,(0,0),m); dev.save(f'png/{name}.png')
