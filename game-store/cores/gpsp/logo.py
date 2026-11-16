"""Original purple handheld icon in little-endian RGB565."""
import struct,sys
from pathlib import Path
p=[0]*2304
def rect(x,y,w,h,c):
    r,g,b=(c>>16)&255,(c>>8)&255,c&255
    v=((r>>3)<<11)|((g>>2)<<5)|(b>>3)
    for yy in range(y,y+h):
        for xx in range(x,x+w):
            p[yy*48+xx]=v
rect(0,0,48,48,0x25253b)
rect(3,12,42,24,0x7761c7)
rect(4,15,40,18,0x8b77da)
rect(13,15,22,17,0x242738)
rect(15,17,18,13,0x8fad87)
rect(7,19,3,9,0x32313e)
rect(4,22,9,3,0x32313e)
rect(37,20,4,4,0xb9b3cf)
rect(41,25,3,3,0xb9b3cf)
Path(sys.argv[1]).write_bytes(struct.pack('<2304H',*p))
