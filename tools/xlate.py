"""Translate CVX60's PAL (GCDP08) addresses to USA (GCDE08) by code-pattern alignment, and verify.

Every address is found by matching the surrounding PAL code (relocations masked) in the USA DOL near the
expected position; data/SDA references are read from the aligned instruction. Prints a table + JSON."""
import json, struct, sys
from dol import Dol  # needs ../../dol.py (DOL loader) and both main.dol files extracted

PAL = Dol("disc1/sys/main.dol")
USA = Dol("usa1/sys/main.dol")


def sda_base(d):
    regs = {}
    for a in range(0x800032B0, 0x80003340, 4):
        w = d.u32(a); op, rt, ra, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        if op == 15 and ra == 0: regs[rt] = imm << 16
        if op == 24 and rt == ra and rt in regs: regs[rt] |= imm
    return regs[2], regs[13]


def mask(w):
    op = w >> 26
    if op == 18: return w & 0xFC000003                      # b/bl: target differs
    if op == 16: return w & 0xFFFF0003                      # bc: keep cond, drop disp
    if op in (14, 15, 24, 25): return w & 0xFFFF0000        # addi/addis/ori/oris: imm may be an address half
    if 32 <= op <= 55:                                      # D-form load/store
        ra = (w >> 16) & 31
        return w & 0xFFFF0000 if ra in (2, 13) or True else w
    return w


def words(d, a, n):
    out = []
    for i in range(n):
        b = d.read(a + 4 * i, 4)
        out.append(struct.unpack(">I", b)[0] if b else None)
    return out


def find(addr, guess_delta, before=6, after=10, span=0x4000):
    """Find the USA address of PAL `addr` by matching masked code around it near addr+guess_delta."""
    pat = [mask(w) if w is not None else None for w in words(PAL, addr - 4 * before, before + after)]
    hits = []
    base = addr + guess_delta
    for d in range(-span, span + 4, 4):
        cand = base + d
        ws = words(USA, cand - 4 * before, before + after)
        if None in ws: continue
        if all(p is None or p == mask(w) for p, w in zip(pat, ws)):
            hits.append(cand)
    return hits


P2, P13 = sda_base(PAL)
U2, U13 = sda_base(USA)

# rough per-region delta from the symbol map (PAL addr -> USA addr), nearest mapped function
SYM = []
for l in open("pal_symbols.txt"):
    n, pa, sz, ua = l.split()
    SYM.append((int(pa, 16), int(ua, 16), n))
SYM.sort()


def guess(addr):
    best = min(SYM, key=lambda s: abs(s[0] - addr))
    return best[1] - best[0]


def xcode(addr, before=6, after=10):
    hits = find(addr, guess(addr), before, after)
    if len(hits) != 1:
        hits = find(addr, guess(addr), before + 6, after + 10, span=0x8000)
    return hits


def ref_sites(target, kind):
    """PAL instruction(s) that build `target` (kind 'abs': lis+addi/lwz pair) or use r13 offset (kind 'r13')."""
    sites = []
    prev = []
    for a, w in PAL.iter_text():
        op, rt, ra, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if kind == "r13" and ra == 13 and (op in (14,) or 32 <= op <= 55) and simm == target:
            sites.append((a, None))
        if kind == "abs" and (op in (14, 24) or 32 <= op <= 55) and ra != 0:
            for pa, pw in prev:
                if pw >> 26 == 15 and (pw >> 21) & 31 == ra and (pw >> 16) & 31 == 0:
                    hi = pw & 0xFFFF
                    val = ((hi << 16) | imm) if op == 24 else ((hi << 16) + simm) & 0xFFFFFFFF
                    if val == target:
                        sites.append((a, pa))
                    break
        prev = (prev + [(a, w)])[-12:]
    return sites


def usa_value(site, lis_site, kind):
    """Decode the referenced value at the aligned USA site."""
    hits = xcode(site)
    if len(hits) != 1: return None
    ua = hits[0]
    w = USA.u32(ua); op, imm = w >> 26, w & 0xFFFF
    simm = imm - 0x10000 if imm & 0x8000 else imm
    if kind == "r13":
        return simm
    lw = USA.u32(ua + (lis_site - site))
    hi = lw & 0xFFFF
    return ((hi << 16) | imm) if op == 24 else ((hi << 16) + simm) & 0xFFFFFFFF


if __name__ == "__main__":
    spec = json.load(open(sys.argv[1]))
    out, bad = {}, []
    for name, (kind, val) in spec.items():
        v = int(val, 16) if isinstance(val, str) else val
        if kind == "code":
            h = xcode(v)
            res = h[0] if len(h) == 1 else None
            note = f"{len(h)} match(es)"
        elif kind in ("abs", "r13"):
            sites = ref_sites(v if kind == "abs" else -v, kind)
            vals = []
            for s, ls in sites[:6]:
                u = usa_value(s, ls, kind)
                if u is not None: vals.append(u if kind == "abs" else -u)
            uniq = sorted(set(vals))
            res = uniq[0] if len(uniq) == 1 else None
            note = f"{len(sites)} PAL refs, USA values {[hex(x) for x in uniq]}"
        out[name] = res
        flag = "" if res is not None else "   <-- UNRESOLVED"
        if res is None: bad.append(name)
        print(f"{name:28} {kind:4} PAL {v:#010x} -> USA {res:#010x}  ({note}){flag}" if res is not None else
              f"{name:28} {kind:4} PAL {v:#010x} -> ?  ({note}){flag}")
    print(f"\nSDA: PAL r2={P2:#x} r13={P13:#x} | USA r2={U2:#x} r13={U13:#x}")
    json.dump({k: (hex(v) if v is not None else None) for k, v in out.items()} | {"R13": hex(U13), "R2": hex(U2)},
              open("usa_addrs.json", "w"), indent=1)
    sys.exit(1 if bad else 0)
