#!/usr/bin/env python3
"""Ports AzaharPlus's fractional resolution scaling into the MH4U Android app. Run from the
repo root:

    python3 fractional-scaling.py

AzaharPlus counts the internal resolution in percent (100 = native, 150 = 1.5x) and its
renderer scales by any such factor; the core in your fork already does. What the app lacked
was a way to ask for one: its setting was whole multiples only. This adds:

  * a new ini key, resolution_percent (50-1800, 0 = use `resolution`), which wins when set;
    `resolution` stays as it was, in whole multiples, so older files and the PC app are
    unaffected
  * native.cpp passing the percent through to the renderer
  * the Internal resolution slider in halves (0.5x ... 6x), written to both keys

It checks every anchor first and writes nothing unless all of them match. Safe to run twice."""
import os
import sys

ROOT = os.getcwd()
CONFIG_H = "mh4u/port/app/config.h"
CONFIG_CPP = "mh4u/port/app/config.cpp"
NATIVE_CPP = "mh4u/port/android/jni/native.cpp"
SETTINGS_KT = "mh4u/port/android/app/src/main/java/org/mh4u/port/ui/SettingsPages.kt"

for p in (CONFIG_H, CONFIG_CPP, NATIVE_CPP, SETTINGS_KT):
    if not os.path.exists(os.path.join(ROOT, p)):
        sys.exit(f"{p} not found: run this from the repo root")


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return f.read()


texts = {p: read(p) for p in (CONFIG_H, CONFIG_CPP, NATIVE_CPP, SETTINGS_KT)}
already = [p for p in texts if "resolution_percent" in texts[p]]
if len(already) == len(texts):
    print("already applied, nothing to do")
    sys.exit(0)
if already:
    sys.exit("partly applied already (" + ", ".join(already) + "): not touching anything. "
             "Restore those files first (git checkout) and run this again.")

problems = []


def replace_once(path, old, new, what):
    text = texts[path]
    n = text.count(old)
    if n != 1:
        problems.append(f"{path}: {what}: found {n} times, expected once")
        return
    texts[path] = text.replace(old, new)


# ---- config.h -----------------------------------------------------------------------------
replace_once(CONFIG_H, '''    int resolution = 0;               ///< internal resolution, times the 3DS's; 0: from the window
#endif
''', '''    int resolution = 0;               ///< internal resolution, times the 3DS's; 0: from the window
#endif
    /// AzaharPlus's fractional scaling (Android): the internal resolution in percent of the
    /// 3DS's, 150 = 1.5x, in steps of 50 from 50 to 1800. 0 = use `resolution`, whole multiples.
    int resolution_percent = 0;
''', "the resolution field")

# ---- config.cpp ---------------------------------------------------------------------------
replace_once(CONFIG_CPP, '''    c.resolution = num("Display", "resolution", c.resolution, 0, 10);
''', '''    c.resolution = num("Display", "resolution", c.resolution, 0, 10);
    c.resolution_percent = num("Display", "resolution_percent", c.resolution_percent, 0, 1800);
    if (c.resolution_percent > 0 && c.resolution_percent < 50) {
        c.resolution_percent = 50; // AzaharPlus's smallest step
    }
''', "the load line")
replace_once(CONFIG_CPP, '''resolution = {}
''', '''resolution = {}
# Android: AzaharPlus's fractional scaling, the internal resolution in percent of the 3DS's (150 =
# 1.5x, 250 = 2.5x; 50 to 1800 in steps of 50). When set it is used instead of `resolution`
resolution_percent = {}
''', "the template entry")
replace_once(CONFIG_CPP, "OnOff(c.fullscreen), c.resolution,", "OnOff(c.fullscreen), c.resolution, c.resolution_percent,",
             "the format arguments")

# ---- native.cpp ---------------------------------------------------------------------------
replace_once(NATIVE_CPP, '''    // this Azahar counts the internal resolution in percent (100: native); the app's ini in
    // multiples of the 3DS's, 0 meaning from the screen
    v.resolution_factor = static_cast<u32>(config.resolution * 100);
''', '''    // this Azahar counts the internal resolution in percent (100: native) and scales by any
    // fraction (AzaharPlus). The app's ini has the percent in resolution_percent, or whole
    // multiples of the 3DS's in resolution; 0 meaning from the screen
    v.resolution_factor = static_cast<u32>(config.resolution_percent > 0 ? config.resolution_percent
                                                                         : config.resolution * 100);
''', "the resolution factor")

# ---- SettingsPages.kt ---------------------------------------------------------------------
replace_once(SETTINGS_KT, '''        var res by remember { mutableStateOf(Prefs.getInt("Display", "resolution", 2).toFloat()) }
        SliderRow("Internal resolution", "In multiples of the 3DS resolution. 0 matches the screen.", res, 0f..6f, " ×", steps = 5) {
            res = it; Prefs.setApp("Display", "resolution", it.toInt().toString()); Prefs.apply()
        }
''', '''        // AzaharPlus's fractional scaling: the resolution in percent of the 3DS's, in halves (150 =
        // 1.5×). The whole multiple is written too, for whatever reads the file the old way.
        val savedPercent = Prefs.getInt("Display", "resolution_percent", 0)
        var res by remember {
            mutableStateOf(if (savedPercent > 0) savedPercent / 100f else Prefs.getInt("Display", "resolution", 2).toFloat())
        }
        SliderRow("Internal resolution", "In multiples of the 3DS resolution, in halves (1.5×, 2.5×…). 0 matches the screen.", res, 0f..6f, " ×", steps = 11) {
            res = it
            val percent = Math.round(it * 2f) * 50
            Prefs.setApp("Display", "resolution_percent", percent.toString())
            Prefs.setApp("Display", "resolution", (if (percent == 0) 0 else Math.max(1, Math.round(percent / 100f))).toString())
            Prefs.apply()
        }
''', "the resolution slider")

if problems:
    sys.exit("NOT written, nothing was changed:\n  " + "\n  ".join(problems) +
             "\n(your files differ from what this script expects: send me the file named above)")


# the settings template must still pair every {} with an argument
def count_template(src):
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


before = count_template(read(CONFIG_CPP))
after = count_template(texts[CONFIG_CPP])
print(f"config.cpp template {{}} vs arguments: {before} -> {after}")
if after[0] != after[1] or before[0] != before[1]:
    sys.exit("NOT written: the settings template and its arguments don't match. Nothing was changed.")

for path, text in texts.items():
    with open(os.path.join(ROOT, path), "w", encoding="utf-8") as f:
        f.write(text)
    print("edited", path)
print("done. Review with `git diff`, then commit.")
