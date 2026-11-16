"""Generated original minimal ARM GBA homebrew. No official BIOS/logo or game data."""
import struct,sys
from pathlib import Path
rom=bytearray(2*1024*1024)
def put(off,value): struct.pack_into('<I',rom,off,value)
put(0,0xea00002e) # ARM entry branch to 0xc0
rom[0xa0:0xac]=b'SONIX SMOKE '
rom[0xac:0xb0]=b'SNXE'
rom[0xb0:0xb2]=b'00'
rom[0xb2]=0x96
rom[0xbd]=(-sum(rom[0xa0:0xbd])-0x19)&255
# Set LCD mode 3 plus BG2; set first VRAM pixel red; spin.
# LDR R0,[PC,#24], LDR R1,[PC,#24], STRH R1,[R0],
# LDR R0,[PC,#20], MOV R1,#31, STRH R1,[R0], B .
code=[0xe59f0018,0xe59f1018,0xe1c010b0,0xe59f0014,0xe3a0101f,0xe1c010b0,0xeafffffe,0xe1a00000,0x04000000,0x00000403,0x06000000]
for i,v in enumerate(code): put(0xc0+i*4,v)
if len(sys.argv)>2 and sys.argv[2]=='thumb':
    # Enter Thumb code via BX; test both the ARM and Thumb translators.
    put(0xc0,0xe59f0000)
    put(0xc4,0xe12fff10)
    put(0xc8,0x080000d1)
    for i,v in enumerate([0x4803,0x4904,0x8001,0x4804,0x211f,0x8001,0xe7fe,0x46c0]):
        struct.pack_into('<H',rom,0xd0+i*2,v)
    put(0xe0,0x04000000)
    put(0xe4,0x00000403)
    put(0xe8,0x06000000)
Path(sys.argv[1]).write_bytes(rom)
