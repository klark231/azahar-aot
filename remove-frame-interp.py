#!/usr/bin/env python3
"""Removes the frame interpolation analysis (patches 0007 and 0008, its tests, its docs, and
the settings toggle) from the repo. Run from the repo root:

    python3 remove-frame-interp.py

It edits your current files in place, and writes nothing unless every edit it makes leaves
config.cpp's settings template consistent. Safe to run twice."""
import glob
import os
import re
import shutil
import sys

ROOT = os.getcwd()
if not os.path.isdir(os.path.join(ROOT, "mh4u")) or not os.path.isdir(os.path.join(ROOT, "patches")):
    sys.exit("run this from the repo root (the folder with mh4u/ and patches/)")

CONFIG_H = "mh4u/port/app/config.h"
CONFIG_CPP = "mh4u/port/app/config.cpp"
SETTINGS_KT = "mh4u/port/android/app/src/main/java/org/mh4u/port/ui/SettingsPages.kt"

REMOVE_PATHS = [
    "patches/0007-frame-interpolation-analysis.patch",
    "patches/0008-frame-interp-cpu-draw-causes.patch",
    "tools/frame-interp-test",
    "docs/FRAME_INTERP.md",
]
PATCH_NAMES = ["0007-frame-interpolation-analysis.patch", "0008-frame-interp-cpu-draw-causes.patch"]


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return f.read()


def count_template(src):
    """({} placeholders in the settings template, arguments of the format call after it)."""
    start = src.index("vk_flush_draws = {}")
    a = src.rfind('R"(', 0, start)
    b = src.index(')",', start)
    n = src[a + 3:b].count("{}")
    rest = src[b + 3:]
    depth, args, cur, instr, i = 0, [], "", False, 0
    while i < len(rest):
        ch = rest[i]
        if instr:
            cur += ch
            if ch == "\\":
                cur += rest[i + 1]
                i += 1
            elif ch == '"':
                instr = False
        elif ch == '"':
            instr = True
            cur += ch
        elif ch in "([{":
            depth += 1
            cur += ch
        elif ch in ")]}":
            if depth == 0:
                args.append(cur.strip())
                break
            depth -= 1
            cur += ch
        elif ch == "," and depth == 0:
            args.append(cur.strip())
            cur = ""
        else:
            cur += ch
        i += 1
    return n, len([x for x in args if x])


changes = {}   # path -> new text
report = []


def edit(path, steps):
    """steps: (label, regex, flags); every regex that matches is removed."""
    if not os.path.exists(os.path.join(ROOT, path)):
        report.append(f"  {path}: not found, skipped")
        return
    text = read(path)
    original = text
    for label, pattern, flags in steps:
        text, n = re.subn(pattern, "", text, flags=flags)
        report.append(f"  {path}: {label}: {'removed' if n else 'not there'}")
    if text != original:
        changes[path] = text


edit(CONFIG_H, [
    ("frame_interp_stats field",
     r"[ \t]*/// Frame interpolation, stage A[^\n]*\n(?:[ \t]*///[^\n]*\n)*?[ \t]*bool frame_interp_stats = false;\n", 0),
])
edit(CONFIG_CPP, [
    ("load line", r"[ \t]*c\.frame_interp_stats = flag\([^\n]*\n", 0),
    ("template entry", r"# frame interpolation analysis[^\n]*\n(?:#[^\n]*\n)*?frame_interp_stats = \{\}\n", 0),
    ("format argument", r"OnOff\(c\.frame_interp_stats\),[ \t]*", 0),
    ("environment variable", r"[ \t]*SyncEnv\(\"AZAHAR_FRAME_INTERP\"[^\n]*\n", 0),
])
edit(SETTINGS_KT, [
    ("settings toggle",
     r"[ \t]*var interpStats by remember[^\n]*\n[ \t]*ToggleRow\(\"Frame interpolation analysis[^\n]*\n"
     r"[ \t]*interpStats = it;[^\n]*\n[ \t]*\}\n[ \t]*RowDivider\(\)\n", 0),
])
for wf in sorted(glob.glob(os.path.join(ROOT, ".github/workflows/*.yml"))):
    rel = os.path.relpath(wf, ROOT)
    steps = [(f"{n} in the patch list", r"[ \t]*\.\./repo/patches/" + re.escape(n), 0) for n in PATCH_NAMES]
    edit(rel, steps)

# the settings template must still pair every {} with an argument
if CONFIG_CPP in changes:
    before = count_template(read(CONFIG_CPP))
    after = count_template(changes[CONFIG_CPP])
    report.append(f"  {CONFIG_CPP}: template {{}} vs arguments: {before} -> {after}")
    if after[0] != after[1]:
        print("\n".join(report))
        sys.exit("\nNOT written: the template and its arguments would no longer match. "
                 "Nothing was changed.")

for path, text in changes.items():
    with open(os.path.join(ROOT, path), "w", encoding="utf-8") as f:
        f.write(text)
for rel in REMOVE_PATHS:
    p = os.path.join(ROOT, rel)
    if os.path.isdir(p):
        shutil.rmtree(p)
        report.append(f"  {rel}/: deleted")
    elif os.path.exists(p):
        os.remove(p)
        report.append(f"  {rel}: deleted")
    else:
        report.append(f"  {rel}: not there")

print("\n".join(report))
left = [p for p in ["frame_interp", "FRAME_INTERP"] if any(
    p in read(f) for f in (CONFIG_H, CONFIG_CPP, SETTINGS_KT) if os.path.exists(os.path.join(ROOT, f)))]
print("\nleftover mentions in the app files:", left if left else "none")
print("done. Review with `git status` and `git diff`, then commit.")
