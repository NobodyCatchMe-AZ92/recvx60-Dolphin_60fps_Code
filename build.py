"""Build cvx60.c into a blob at 0x817B7000 and emit the Action Replay code (raw, unencrypted)."""
import struct, subprocess, sys, re, os, zlib
LAB = "--lab" in sys.argv
USA = "--usa" in sys.argv
import json
REG = {k: int(v[1 if USA else 0], 16) for k, v in json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "regions.json"))).items()}
# free arena above the game's heaps (ArenaLo after heap setup .. ArenaHi), measured in game
ARENA = (0x817F34C0, 0x817FF800) if USA else (0x817B6BE0, 0x817FF800)
TAG = ("usa" if USA else "pal") + ("_lab" if LAB else "")
BUILD_ID = zlib.crc32(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "cvx60.c"), "rb").read() + TAG.encode()) | 1
HERE = os.path.dirname(os.path.abspath(__file__))
PY = sys.executable
ZIG = [PY, "-m", "ziglang"]
BASE = (ARENA[0] + 0xFF) & ~0xFF

def run(cmd):
    r = subprocess.run(cmd, cwd=HERE, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr); sys.exit(1)
    return r.stdout

run(ZIG + ["cc", "-target", "powerpc-freestanding-eabihf", "-mcpu=750", "-O2", "-ffreestanding", "-fno-builtin",
           "-fno-pic", "-fno-stack-protector", "-fno-asynchronous-unwind-tables", "-fno-unwind-tables", "-g0", "-Wall"] + (["-DCVX_LAB"] if LAB else []) + (["-DCVX_USA", "-Os"] if USA else []) + [f"-DCVX_BUILD_ID={BUILD_ID:#010x}u"] + ["-c", "cvx60.c", "-o", "cvx60.o"])
open(os.path.join(HERE, "link_gen.ld"), "w").write(open(os.path.join(HERE, "link.ld")).read().replace("0x817B7000", hex(BASE)))
run(ZIG + ["ld.lld", "-m", "elf32ppc", "-T", "link_gen.ld", "-e", "cvx_frame_end", "cvx60.o", "-o", "cvx60.elf"])
from elftools.elf.elffile import ELFFile
_elf = ELFFile(open(os.path.join(HERE, "cvx60.elf"), "rb"))
_syms = {s.name: s["st_value"] for s in _elf.get_section_by_name(".symtab").iter_symbols()}
_secs = {s.name: s for s in _elf.iter_sections()}

def sym(name):
    if name not in _syms:
        sys.exit(f"symbol {name} missing")
    return _syms[name]

blob = bytearray(sym("__blob_end") - BASE)
for sec in _elf.iter_sections():
    if sec["sh_type"] == "SHT_PROGBITS" and sec["sh_flags"] & 2 and sec["sh_size"]:  # SHF_ALLOC
        off = sec["sh_addr"] - BASE
        if off < 0 or off + sec["sh_size"] > len(blob):
            sys.exit(f"section {sec.name} outside blob")
        blob[off:off + sec["sh_size"]] = sec.data()
blob = bytes(blob) + b"\0" * (-len(blob) % 4)
open(os.path.join(HERE, "cvx60.bin"), "wb").write(blob)
bss_end = sym("__bss_end")

for n in (".data", ".sdata"):
    if n in _secs and _secs[n]["sh_size"]:
        sys.exit(f"{n} is not empty - mutable state must be in .bss (AR rewrites the blob every frame)")
for n in _secs:
    if n.startswith(".rela"):
        sys.exit(f"relocations left in output: {n}")

def bl(site, target, link=True):
    off = target - site
    assert -0x2000000 <= off < 0x2000000
    return 0x48000000 | (off & 0x03FFFFFC) | (1 if link else 0)

HOOKS = [  # (site, our function, link)
    ("A_SITE_MAIN_WAITVSYNC", "cvx_frame_end", True),      # main: njWaitVSync
    ("A_SITE_MS_CONTROLLIGHT", "cvx_after_logic", True),   # bhMainSequence: bhControlLight
    ("A_SITE_MS_ALLDRAW", "cvx_alldraw", True),            # bhAllDrawModel
    ("A_SITE_MS_MIRRORDRAW", "cvx_mirrordraw", True),      # bhEtcMirrorDrawModel
    ("A_SITE_MS_EASYDRAW", "cvx_easydraw", True),          # bhAllEasyDrawModel
    ("A_bhPutModel", "cvx_putmodel", False),               # bhPutModel entry: b cvx_putmodel
    ("A_SITE_DOOR_CONTROLDOOR", "cvx_controldoor", True),  # bhSysCallDoordemo: bl bhControlDoor
]
hooks = [(REG[s], bl(REG[s], sym(f), l)) for s, f, l in HOOKS]
arena_lo = (bss_end + 0x1F) & ~0x1F

def ar(addr, val):
    return f"{(0x04000000 | (addr & 0x01FFFFFF)):08X} {val:08X}"

lines = [ar(BASE + i, struct.unpack(">I", blob[i:i + 4])[0]) for i in range(0, len(blob), 4)]
# Don't touch ArenaLo: AR codes are applied from the first frames of boot, before the game builds its
# heaps from [ArenaLo, ArenaHi); moving ArenaLo up there leaves no room and the game halts on a black
# screen. The heaps end at 0x817B6BE0 and nothing uses the space above them, so the blob just lives there.
if not (ARENA[0] <= BASE and bss_end <= ARENA[1]):
    sys.exit(f"blob/bss {BASE:#x}-{bss_end:#x} outside the free arena {ARENA[0]:#x}-{ARENA[1]:#x}")
if LAB:
    lines.append(ar(REG["A_TASK_TABLE"] + 7 * 4, sym("cvx_syscallgame")))  # task table slot 7: bhSysCallGame -> harness wrapper
lines += [ar(a, v) for a, v in hooks]
open(os.path.join(HERE, {"pal": "cvx60_ar.txt", "pal_lab": "cvx60_lab_ar.txt", "usa": "cvx60_usa_ar.txt", "usa_lab": "cvx60_usa_lab_ar.txt"}[TAG]), "w").write("\n".join(lines) + "\n")
print(TAG.upper() + f" base {BASE:#x} free-left {ARENA[1] - bss_end:#x} blob {len(blob):#x} bytes, bss to {bss_end:#x}, {len(lines)} AR lines")
for n in ("cvx_frame_end", "cvx_after_logic", "cvx_putmodel", "PutModelOrig"):
    print(f"  {n} {sym(n):#x}")
