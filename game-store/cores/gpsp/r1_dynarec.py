"""R1 v1.1.0 patches for the exact pinned upstream gpSP."""
import sys
from pathlib import Path
root=Path(sys.argv[1])
def edit(path,old,new):
    p=root/path
    s=p.read_text()
    assert s.count(old)==1, f"Unexpected upstream source: {path}"
    p.write_text(s.replace(old,new))
# MIPS mapping macro calls this with two arguments, unlike generic validator.
edit("memmap.c",
     "bool validate_addr_section_mips(void *ptr, unsigned size, unsigned max_offset_mb) {",
     "bool validate_addr_section_mips(void *ptr, unsigned size) {")
# Keep the JIT's direct jumps inside a single 256 MiB region. Also avoid
# unsigned overflow when the end of the block approaches address-space end.
edit("memmap.c",
     """\tuintptr_t end_addr = start_addr + size;
\tconst uintptr_t msk = ~0xFFFFFFFU;  // 256MB block""",
     """\tif (!size || start_addr > UINTPTR_MAX - (size - 1))
\t\treturn false;
\tuintptr_t end_addr = start_addr + size - 1;
\tconst uintptr_t msk = ~((uintptr_t)0xFFFFFFFU);  // 256MB block""")
# The generated fast EWRAM stack helpers rely on the documented 64 KiB
# layout; guarantee this even when the core is loaded as a shared library.
edit("mips/mips_stub.S",
     "# We place them at 128KB offset to ensure so.\ndefsymbl(iwram)",
     "# We place them at 128KB offset to ensure so.\n.balign 65536\ndefsymbl(iwram)")
# Diagnostic entry point for the MIPS smoke test. Existing frontend ignores it.
edit("libretro/libretro.c",
     "unsigned retro_api_version(void)\n{",
     "/* Sonix diagnostic: prove the JIT is actually active during testing. */\nunsigned retro_sonix_jit_active(void) { return dynarec_enable != 0; }\n\nunsigned retro_api_version(void)\n{")
