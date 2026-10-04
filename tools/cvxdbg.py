"""Live diagnostics for CVX60 in a running Dolphin. Every address comes from the ELF symbol table, so
struct changes in cvx60.c can't silently shift what we read or write.

  cvxdbg.py stats [secs]          rates: ticks, VI, extra/late/vanilla frames, camera decisions
  cvxdbg.py on | off              force the vanilla 30fps path off/on (A/B testing)
  cvxdbg.py blank 0|1             1 = leave the extra frame blank (presentation test)
  cvxdbg.py pad MASK              OR buttons into sys->pad_on (0x1 fwd, 0x2 back, 0x4/0x8 turn, 0x400 run)
  cvxdbg.py trace SECS            collect the per-frame trace -> shots/trace.csv + smoothness summary
  cvxdbg.py camlog SECS           collect per-tick camera decisions -> shots/camlog.csv + summary
"""
import math, os, struct, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
from dmem import Mem  # noqa: E402
from elftools.elf.elffile import ELFFile  # noqa: E402

R13 = 0x804F50A0
_elf = ELFFile(open(os.path.join(HERE, "..", "cvx60.elf"), "rb"))
SYM = {s.name: s["st_value"] for s in _elf.get_section_by_name(".symtab").iter_symbols() if s.name}
D = SYM["cvx_D"]
D_FIELDS = ["off", "dbg", "fake_on", "stat_extra", "stat_late", "stat_vanilla",
            "stat_camok", "stat_camcut", "stat_camfar", "stat_bskip", "fake_ps", "why_last", "why_or", "act_last", "act_or", "stat_poserej"]


def dfield(name):
    return D + 4 * D_FIELDS.index(name)


def snapshot(m):
    v = dict(zip(D_FIELDS, m.u32s(D, len(D_FIELDS))))
    v["ticks"] = m.u32(R13 - 0x730C)
    v["vi"] = m.u32(R13 - 0x6F4C)
    return v


def stats(m, secs):
    a = snapshot(m); t0 = time.time(); time.sleep(secs); b = snapshot(m); dt = time.time() - t0
    for k in ("ticks", "vi", "stat_extra", "stat_late", "stat_vanilla", "stat_camok", "stat_camcut",
              "stat_camfar", "stat_bskip", "stat_poserej"):
        print(f"{k:13} {(b[k] - a[k]) / dt:7.2f}/s")
    print("off", b["off"], "blank", b["dbg"], "pad", hex(b["fake_on"]))
    print("blocked-by (last/ever):", hex(b["why_last"]), hex(b["why_or"]), " tasks (last/ever):", hex(b["act_last"]), hex(b["act_or"]))


def collect(m, base, count, size, fmt, secs, key):
    rows = {}; t0 = time.time()
    while time.time() - t0 < secs:
        raw = m.read(base, count * size)
        for k in range(count):
            r = struct.unpack_from(fmt, raw, k * size)
            if r[0]:
                rows[key(r)] = r
        time.sleep(0.5)
    return sorted(rows.values(), key=key)


def trace(m, secs):
    rows = collect(m, SYM["cvx_tr"], 256, 36, ">3I6f", secs, lambda r: (r[2], r[1], r[0]))
    os.makedirs(os.path.join(ROOT, "shots"), exist_ok=True)
    with open(os.path.join(ROOT, "shots", "trace.csv"), "w") as f:
        f.write("\n".join(",".join(map(str, r)) for r in rows))
    print(f"{len(rows)} presented frames; phases " +
          ", ".join(f"{p}:{sum(1 for r in rows if r[1] == p)}" for p in (0, 1, 2)))
    for name, sl in (("camera", slice(3, 6)), ("player", slice(6, 9))):
        st = [math.dist(a[sl], b[sl]) for a, b in zip(rows, rows[1:]) if b[2] - a[2] == 1]
        mv = [x for x in st if x > 1e-4]
        r = [b / a for a, b in zip(mv, mv[1:]) if a > 1e-3]
        bad = sum(1 for x in r if x < 0.5 or x > 2.0)
        print(f"{name}: moving steps {len(mv)}, still {len(st) - len(mv)}, uneven {bad} ({bad / max(1, len(r)) * 100:.0f}%)")


def camlog(m, secs):
    rows = collect(m, SYM["cvx_cl"], 128, 24, ">IiffIi", secs, lambda r: r[0])
    with open(os.path.join(ROOT, "shots", "camlog.csv"), "w") as f:
        f.write("\n".join(",".join(map(str, r)) for r in rows))
    names = {0: "off", 1: "cut(zone)", 2: "cut(jump)", 3: "blend"}
    print("decisions:", {names[d]: sum(1 for r in rows if r[4] == d) for d in names})
    print("cuts:", [(r[0], round(r[2], 1), round(r[3], 2)) for r in rows if r[4] in (1, 2)])
    print("max blended step:", max([r[2] for r in rows if r[4] == 3] or [0]))


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "stats"
    arg = sys.argv[2] if len(sys.argv) > 2 else None
    m = Mem(write=cmd in ("on", "off", "blank", "pad", "press"))
    if cmd == "stats":
        stats(m, float(arg or 3))
    elif cmd in ("on", "off"):
        m.write(dfield("off"), struct.pack(">I", 0 if cmd == "on" else 1)); print("mod", cmd)
    elif cmd == "blank":
        m.write(dfield("dbg"), struct.pack(">I", int(arg))); print("blank", arg)
    elif cmd == "pad":
        m.write(dfield("fake_on"), struct.pack(">I", int(arg, 0)))
    elif cmd == "press":
        m.write(dfield("fake_ps"), struct.pack(">I", int(arg, 0)))
    elif cmd == "trace":
        trace(m, float(arg or 20))
    elif cmd == "camlog":
        camlog(m, float(arg or 20))
    else:
        print(__doc__)
