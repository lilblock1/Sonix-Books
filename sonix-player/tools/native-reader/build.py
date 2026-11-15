"""Build the isolated readers for Linux hosts or the existing R1 cross toolchain."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCES = json.loads((HERE / "sources.json").read_text())


def run(*args, cwd=None, env=None):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, env=env, check=True)


def checkout(name, spec, work):
    source = work / "src" / (name + "-" + spec["commit"][:12])
    if not (source / ".git").exists():
        source.mkdir(parents=True, exist_ok=True)
        run("git", "init", source)
        run("git", "-C", source, "remote", "add", "origin", spec["url"])
        run("git", "-C", source, "fetch", "--depth=1", "origin", spec["commit"])
        run("git", "-C", source, "checkout", "--detach", "FETCH_HEAD")
    actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != spec["commit"]:
        raise ValueError("native source pin mismatch: " + name)
    if spec.get("recursive"):
        run("git", "-C", source, "submodule", "update", "--init", "--recursive", "--depth=1")
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("host", "r1"), default="host")
    parser.add_argument("--work", type=Path, default=ROOT / "native-sdk")
    parser.add_argument("--output", type=Path, default=ROOT / "native-reader-pack")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    work, output = args.work.resolve(), args.output.resolve()
    build = work / args.target
    prefix = build / "prefix"
    prefix.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    cpu_flags = []
    if args.target == "r1":
        compiler = ROOT / "rockbox-toolchain/bin/mipsel-rockbox-linux-gnu-"
        cc, cxx, ar, strip, ranlib = (str(compiler) + tool for tool in ("gcc", "g++", "ar", "strip", "ranlib"))
        if not Path(cc).is_file():
            raise ValueError("restore/build the existing player cross toolchain first")
        env["PATH"] = str(Path(cc).parent) + os.pathsep + env.get("PATH", "")
        cpu_flags = ["-march=mips32r2", "-mno-madd4", "-mplt", "-include", str(ROOT / "src/system/core/glibc_compat.h")]
    else:
        cc, cxx, ar, strip, ranlib = "gcc", "g++", "ar", "strip", "ranlib"
    flags = " ".join(["-Os", "-ffunction-sections", "-fdata-sections"] + cpu_flags)
    sources = {name: checkout(name, spec, work) for name, spec in SOURCES.items()}

    archive_build = build / "archive"
    options = ["OPENSSL", "MBEDTLS", "NETTLE", "CNG", "MD", "ZLIB", "BZip2", "LIBB2", "LZMA", "ZSTD", "LZ4", "LZO", "LIBXML2", "EXPAT", "ICONV", "ACL", "XATTR", "TEST", "TAR", "CPIO", "CAT", "UNZIP"]
    cmake = ["cmake", "-S", sources["libarchive"], "-B", archive_build,
             "-DCMAKE_BUILD_TYPE=MinSizeRel", "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
             "-DBUILD_SHARED_LIBS=OFF", "-DCMAKE_C_COMPILER=" + cc, "-DCMAKE_C_FLAGS=" + flags]
    cmake += ["-DENABLE_" + name + "=OFF" for name in options]
    if args.target == "r1":
        # Link feature probes with the target compiler. Static-only probes can
        # incorrectly report missing libc functions and host libraries present.
        (archive_build / "CMakeCache.txt").unlink(missing_ok=True)
        cmake += ["-DCMAKE_SYSTEM_NAME=Linux", "-DCMAKE_SYSTEM_PROCESSOR=mipsel",
                  "-DCMAKE_FIND_ROOT_PATH=" + str(prefix) + ";" + str(ROOT / "rockbox-toolchain/mipsel-rockbox-linux-gnu/sysroot"),
                  "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY", "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY",
                  "-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY"]
    run(*cmake, env=env)
    run("cmake", "--build", archive_build, "--parallel", args.jobs, env=env)
    run("cmake", "--install", archive_build, env=env)

    mobi = sources["libmobi"]
    if not (mobi / "configure").exists():
        run("autoreconf", "-fi", cwd=mobi, env=env)
    mobi_build = build / "mobi"
    mobi_build.mkdir(parents=True, exist_ok=True)
    mobi_env = dict(env, CC=cc, AR=ar, RANLIB=ranlib, CFLAGS=flags)
    if not (mobi_build / "Makefile").exists():
        options = [str(mobi / "configure"), "--prefix=" + str(prefix), "--disable-shared", "--enable-static",
                   "--enable-tools-static", "--disable-encryption", "--with-zlib=no", "--with-libxml2=no"]
        if args.target == "r1":
            options.append("--host=mipsel-rockbox-linux-gnu")
        run(*options, cwd=mobi_build, env=mobi_env)
    run("make", "-j" + str(args.jobs), cwd=mobi_build, env=mobi_env)
    run("make", "install", cwd=mobi_build, env=mobi_env)

    mupdf = sources["mupdf"]
    common = ["build=small", "build_prefix=sonix-" + args.target + "-", "tofu=yes", "tofu_cjk=yes",
              "mujs=no", "brotli=no", "extract=no", "archive=yes", "HAVE_X11=no", "HAVE_GLUT=no", "HAVE_CURL=no", "HAVE_LIBCRYPTO=no",
              "USE_SYSTEM_LIBS=no", "HAVE_SYS_LIBARCHIVE=yes", "SYS_LIBARCHIVE_CFLAGS=-I" + str(prefix / "include"),
              "SYS_LIBARCHIVE_LIBS=" + str(prefix / "lib/libarchive.a")]
    if args.target == "r1":
        # MuPDF otherwise uses the build host's ld to wrap raw font files.
        # Compile generated C resources with the target compiler instead.
        common.append("HAVE_OBJCOPY=no")
        marker = build / "portable-font-objects-v1"
        if not marker.exists():
            for old in (mupdf / "build").glob("sonix-r1-*"):
                for pattern in ("**/*.cff.o", "**/*.otf.o", "**/*.ttf.o", "**/*.ttc.o", "**/*.zip.o"):
                    for obj in old.glob(pattern):
                        obj.unlink()
                for name in ("libmupdf.a", "source/fitz/noto.o", "source/fitz/hyphen.o"):
                    (old / name).unlink(missing_ok=True)
            marker.write_text("C resource objects use the R1 compiler.\n")
    run("make", "generate", "-j" + str(args.jobs), *common, "CC=gcc", "CXX=g++", "AR=ar", "RANLIB=ranlib", cwd=mupdf, env=env)
    extra = flags + " -DFZ_ENABLE_ICC=0 -DFZ_ENABLE_SPOT_RENDERING=0 -DFZ_PLOTTERS_CMYK=0 -DFZ_ENABLE_OCR_OUTPUT=0 -DFZ_ENABLE_ODT_OUTPUT=0"
    run("make", "libs", "-j" + str(args.jobs), *common, "CC=" + cc, "CXX=" + cxx, "AR=" + ar, "RANLIB=" + ranlib,
        "XCFLAGS=" + extra, "XCXXFLAGS=" + extra, cwd=mupdf, env=env)
    mupdf_lib = mupdf / ("build/sonix-" + args.target + "-small-tofu-tofu_cjk-archive")
    if not (mupdf_lib / "libmupdf.a").exists():
        matches = list((mupdf / "build").glob("sonix-" + args.target + "-*/libmupdf.a"))
        if len(matches) != 1:
            raise ValueError("could not identify pinned MuPDF output")
        mupdf_lib = matches[0].parent
    tools = output / "Ebook/Tools"
    tools.mkdir(parents=True, exist_ok=True)
    obj = build / "booktool.o"
    run(cc, "-std=c11", "-Wall", "-Wextra", "-Werror", *flags.split(), "-I" + str(ROOT), "-I" + str(mupdf / "include"),
        "-I" + str(prefix / "include"), "-c", HERE / "booktool.c", "-o", obj, env=env)
    run(cxx, "-Wl,--gc-sections", "-static-libstdc++", "-static-libgcc", *cpu_flags, obj,
        mupdf_lib / "libmupdf.a", mupdf_lib / "libmupdf-third.a", prefix / "lib/libarchive.a", prefix / "lib/libmobi.a",
        "-lm", "-lpthread", "-ldl", "-o", tools / "sonix_booktool", env=env)
    shutil.copy2(prefix / "bin/mobitool", tools / "mobitool")
    run(strip, tools / "sonix_booktool", tools / "mobitool", env=env)
    for name, source in sources.items():
        for filename in source.rglob("*"):
            relative = filename.relative_to(source)
            if any(p in {".git", "build"} for p in relative.parts) or not filename.is_file():
                continue
            if filename.name.upper().startswith(("COPYING", "LICENSE", "NOTICE", "OFL")):
                destination = tools / "licenses" / name / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(filename, destination)
    metadata = {"protocol": "BK01", "target": args.target, "sources": SOURCES, "binaries": {}}
    for filename in (tools / "sonix_booktool", tools / "mobitool"):
        metadata["binaries"][filename.name] = {"sha256": hashlib.sha256(filename.read_bytes()).hexdigest(), "bytes": filename.stat().st_size}
    (tools / "native-reader-sources.json").write_text(json.dumps(metadata, indent=2) + "\n")
    if args.target == "r1":
        import re
        reports = []
        for binary in (tools / "sonix_booktool", tools / "mobitool"):
            report = subprocess.check_output([str(compiler) + "readelf", "-h", "-l", "-V", str(binary)], text=True)
            if "MIPS" not in report or "little endian" not in report:
                raise ValueError("not a little-endian MIPS reader: " + binary.name)
            for version in re.findall(r"GLIBC_(\d+)\.(\d+)", report):
                if tuple(map(int, version)) > (2, 19):
                    raise ValueError("reader requires newer glibc: " + ".".join(version))
            reports.append(binary.name + "\n" + report)
        (tools / "native-reader-elf.txt").write_text("\n".join(reports))
    package = output / ("Sonix-Books-Native-Readers-" + args.target + ".zip")
    with zipfile.ZipFile(package, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for filename in sorted((output / "Ebook").rglob("*")):
            if filename.is_file():
                info = zipfile.ZipInfo(filename.relative_to(output).as_posix(), (1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = (0o100755 if filename.name in {"sonix_booktool", "mobitool"} else 0o100644) << 16
                archive.writestr(info, filename.read_bytes())
    (output / (package.name + ".sha256")).write_text(hashlib.sha256(package.read_bytes()).hexdigest() + "  " + package.name + "\n")
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
