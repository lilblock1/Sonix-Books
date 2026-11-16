"""Original NES-inspired controller icon, no third-party marks."""
import struct,sys
from pathlib import Path
p=[0]*2304
def rect(x,y,w,h,c):
    r,g,b=((c>>16)&255,(c>>8)&255,c&255)
    v=((r>>3)<<11)|((g>>2)<<5)|(b>>3)
    for yy in range(y,y+h):
        for xx in range(x,x+w):
            p[yy*48+xx]=v
rect(0,0,48,48,0x323740)
rect(3,11,42,26,0xd5d4cb)
rect(6,14,36,20,0x272a30)
rect(12,17,5,15,0xb6bcc5)
rect(7,22,15,5,0xb6bcc5)
rect(25,24,4,3,0xa9a9a2)
rect(31,18,6,6,0xc6303b)
rect(37,25,6,6,0xc6303b)
Path(sys.argv[1]).write_bytes(struct.pack("<2304H",*p))
