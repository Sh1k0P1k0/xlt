#!/usr/bin/env python3
"""Runs build/test_fsname on sample names: XLT's names must be XLT Tool's (tools/xltlink.py)."""
import os, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import xltlink

names = ["plain.mp3", "Καλημέρα κόσμε.mp3", "Ψυχή ϊ ΰ ά έ ή ί ό ύ ώ", "Café del Mar – Beyoncé — “Halo”",
         "日本語のファイル ソ表", "Привет мир", "Straße Æsir Øl œuvre", "ﾊﾝｶｸ half-width", "♪ ★ ① Ⅳ",
         "€ ™ © ® … · •", "Ŝkodа č ř ž ő ű", "😀 emoji", "tab\there"]
exe = sys.argv[1]
p = subprocess.run([exe], input="\n".join(names) + "\n", capture_output=True, text=True)
print(p.stderr.strip())
bad = p.returncode != 0
for name, line in zip(names, p.stdout.splitlines()):
    hexs, changed, r, back = line.split("\t")
    want = xltlink.psp_name(name)
    got = bytes.fromhex(hexs).decode("cp932", errors="replace")
    ok = got == want or (name == "tab\there" and got == "tab_here")
    if not ok or back != got:
        bad = True
    print(f"{'OK  ' if ok and back == got else 'BAD '} {name!r} -> {got!r}{'' if ok else f' (XLT Tool: {want!r})'}"
          f"{'' if back == got else f' read back as {back!r}'}")
sys.exit(1 if bad else 0)
