"""Insert/update the CVX60 AR code in Dolphin's GCDP08.ini, touching nothing else.
usage: deploy.py [--usa] [--enable | --disable | --remove]"""
import os, sys

USA = "--usa" in sys.argv
INI = os.path.expanduser(r"~\Documents\Dolphin Emulator\GameSettings\%s.ini" % ("GCDE08" if USA else "GCDP08"))
NAME = "$60 FPS (interpolated) [CVX60]"
codes = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), ("cvx60_usa_ar.txt" if USA else "cvx60_ar.txt"))).read().split()
codes = [f"{codes[i]} {codes[i + 1]}" for i in range(0, len(codes), 2)]
mode = next((a for a in sys.argv[1:] if a in ("--enable", "--disable", "--remove")), "--enable")

text = open(INI, encoding="utf-8").read().replace("\r\n", "\n")
sections, order, cur = {}, [], None
for line in text.split("\n"):
    s = line.strip()
    if s.startswith("[") and s.endswith("]"):
        cur = s
        if cur not in sections:
            sections[cur] = []
            order.append(cur)
        continue
    if cur is None:
        continue
    sections[cur].append(line)

def strip_entry(lines):
    out, skip = [], False
    for l in lines:
        if l.strip().startswith("$"):
            skip = l.strip() == NAME
        if not skip:
            out.append(l)
    return out

ar = strip_entry(sections.get("[ActionReplay]", []))
while ar and not ar[-1].strip():
    ar.pop()
en = [l for l in sections.get("[ActionReplay_Enabled]", []) if l.strip() != NAME]
while en and not en[-1].strip():
    en.pop()
if mode != "--remove":
    ar += [NAME] + codes
if mode == "--enable":
    en.append(NAME)
for sec, lines in (("[ActionReplay]", ar), ("[ActionReplay_Enabled]", en)):
    if sec not in sections:
        order.append(sec)
    sections[sec] = lines

out = []
for sec in order:
    body = sections[sec]
    while body and not body[-1].strip():
        body = body[:-1]
    if sec == "[ActionReplay_Enabled]" and not body:
        continue
    out.append(sec)
    out += body
open(INI, "w", encoding="utf-8", newline="\r\n").write("\n".join(out) + "\n")
print(f"{mode}: {len(codes)} lines -> {INI}")
