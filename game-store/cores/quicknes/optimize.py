"""Apply the R1 v1.1.0 patch to the exact pinned QuickNES source.

Keep audio rate, CPU/PPU timing, pixel format and libretro state ABI unchanged.
"""
import sys
from pathlib import Path
p=Path(sys.argv[1])/"libretro/libretro.cpp"
s=p.read_text()
old="""	   for (int y = 0; y < Nes_Emu::image_height; y++)
	   {
		   uint16_t *out_scanline = video_buffer + Nes_Emu::image_width * y;
		   uint8_t *in_scanline = frame.pixels + videoBufferWidth * y;
		   for (int x = 0; x < Nes_Emu::image_width; x++)
			   out_scanline[x] = retro_palette[in_scanline[x]];
	   }

	   video_cb(video_buffer + (use_overscan_v ? (use_overscan_h ? 0 : 8) : ((use_overscan_h ? 0 : 8) + 256 * 8)),
		   Nes_Emu::image_width - (use_overscan_h ? 0 : 16),
		   Nes_Emu::image_height - (use_overscan_v ? 0 : 16),
		   Nes_Emu::image_width * sizeof(uint16_t));"""
new="""	   // Convert only pixels the frontend will display. Both widths are
	   // multiples of four; unroll the lookup loop for the scalar MIPS CPU.
	   const int x0 = use_overscan_h ? 0 : 8;
	   const int y0 = use_overscan_v ? 0 : 8;
	   const int width = Nes_Emu::image_width - 2*x0;
	   const int height = Nes_Emu::image_height - 2*y0;
	   for (int y = 0; y < height; ++y)
	   {
		   uint16_t *out = video_buffer + Nes_Emu::image_width * (y+y0) + x0;
		   const uint8_t *in = frame.pixels + videoBufferWidth * (y+y0) + x0;
		   for (int x = 0; x < width; x += 4)
		   {
			   out[x]   = retro_palette[in[x]];
			   out[x+1] = retro_palette[in[x+1]];
			   out[x+2] = retro_palette[in[x+2]];
			   out[x+3] = retro_palette[in[x+3]];
		   }
	   }
	   video_cb(video_buffer + y0*Nes_Emu::image_width + x0,
		   width, height, Nes_Emu::image_width * sizeof(uint16_t));"""
assert s.count(old)==1,"Pinned RGB565 loop changed"
s=s.replace(old,new)
# Unroll mono expansion; never alter stereo effects, sample count or sample rate.
old="""			for (long i = 0; i < read_samples; i++)
				out_samples[(i << 1)] = out_samples[(i << 1) + 1] = samples[i];"""
new="""			long i = 0;
			for (; i + 3 < read_samples; i += 4)
			{
				out_samples[2*i]   = out_samples[2*i+1] = samples[i];
				out_samples[2*i+2] = out_samples[2*i+3] = samples[i+1];
				out_samples[2*i+4] = out_samples[2*i+5] = samples[i+2];
				out_samples[2*i+6] = out_samples[2*i+7] = samples[i+3];
			}
			for (; i < read_samples; ++i)
				out_samples[2*i] = out_samples[2*i+1] = samples[i];"""
assert s.count(old)==1,"Pinned mono loop changed"
p.write_text(s.replace(old,new))
