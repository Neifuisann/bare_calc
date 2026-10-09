"""P17 manual logical contracts for Statistics and Distribution.

Regenerates test/reference_cases/apps-statistics.json and
test/reference_cases/apps-distribution.json (run from the repo root with the
project's Python; output is committed). Physical raster is
deliberately pending: expectations assert screens, menus, grids, result
texts and error titles only.

Key identities reuse the shared reference-runner semantic map (OK, EXE,
arrows, TOOLS, CATALOG, VARIABLE, DEL, BACK, HOME, DOT, SUBTRACT, ANS,
CLOSE, digits) already established by the P15/P16 ledgers; no new keycap
identity is inferred here. The Vietnamese manual text names keys by function
(HOME/Statistics icon, 1-Variable/2-Variable, OK, EXE, TOOLS > Frequency /
Edit / Sort, CATALOG > Statistics), matching those runner names.

Expected numerics come from the host-only mpmath oracle
(p17_oracle.py, not included); the firmware
embeds no mpmath/scipy. Kernel accuracy (~1e-25 relative) is asserted
independently in test/calculator_apps_p17_test.h. LIKELY-marked fits are
exact-model datasets verified at the kernel level; their reference rows lock
the menu flow plus the fitted values.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REF = ROOT / 'test' / 'reference_cases'

STAT_SRC = ("../../../991CN-X-CW-Decompilation/research/downloads/manual-880/"
            "using_calculator_apps__statistical_calculations.html.txt")
DIST_SRC = ("../../../991CN-X-CW-Decompilation/research/downloads/manual-880/"
            "using_calculator_apps__distribution_calculations.html.txt")
CW_SRC = ("CW panel capture: user-provided fx-991 CN CW Statistics photos "
          "(16 frames: editor, TOOLS/Frequency, results pages, Clear Data?)")
SETTINGS = {"inputOutput": "MathMath", "angle": "Degree", "number": "Norm1"}
SCOPE = {"reference_model": "fx-880BTG PLUS", "target_applicability": "STRONG"}


def D(s):
    return ["DOT" if c == "." else c for c in s]


def commit(s):
    return D(s) + ["EXE"]


def freq_on():
    return ["TOOLS", "OK", "UP", "OK"]


def tools_row(n):
    return ["TOOLS"] + ["DOWN"] * n + ["OK"]


def home_launch(idx):
    return ["HOME"] + ["LEFT"] * 14 + ["RIGHT"] * idx + ["OK"]


def K(cap):
    return {"capture": cap}


def P(cid, cap, expect, conf="STRONG", ev="880-MAN"):
    return {"id": cid, "capture": cap, "expect": expect, "native": "pass",
            "evidence": ev, "confidence": conf, "owner": "P17"}


def pend(cid, cap, reason, resolution):
    return {"id": cid, "capture": cap, "expect": None, "native": "pending",
            "evidence": "PENDING", "confidence": "UNKNOWN", "owner": "P17",
            "reason": reason, "resolution": resolution}


def stat_case(cid, title, surface, section, keys, checks, conf="STRONG",
              srckind="880-MAN", src=None):
    return {
        "id": cid, "title": title, "app": "Statistics", "surface": surface,
        "scope": dict(SCOPE),
        "sources": [{"kind": srckind, "evidence": src or STAT_SRC, "section": section}],
        "initial": {"app": "Statistics", "settings": dict(SETTINGS),
                    "memory": "cleared", "history": []},
        "keys": keys, "checks": checks, "confidence": conf,
        "native": ("pending" if all(q["native"] == "pending" for q in checks) else "pass"),
        "owner": "P17", "gap_ids": [],
        "notes": "880-MAN logical behavior; kernels and controller additionally covered in "
                 "calculator_apps_p17_test.h. Raster is not a target pass.",
    }


def dist_case(cid, title, surface, section, keys, checks, conf="STRONG"):
    c = stat_case(cid, title, surface, section, keys, checks, conf)
    c["app"] = "Distribution"
    c["initial"]["app"] = "Distribution"
    c["sources"] = [{"kind": "880-MAN", "evidence": DIST_SRC, "section": section}]
    return c


X10 = ["1.0", "1.2", "1.5", "1.6", "1.9", "2.1", "2.4", "2.5", "2.7", "3.0"]
Y10 = ["1.0", "1.1", "1.2", "1.3", "1.4", "1.5", "1.6", "1.7", "1.8", "2.0"]
FR = ["1", "2", "1", "2", "2", "2", "3", "4", "2", "1"]
TYPES = ["Binomial PD", "Binomial CD", "Normal PD", "Normal CD", "Inverse Normal",
         "Poisson PD", "Poisson CD"]


def stat2_dataset():
    k = []
    for v in X10:
        k += commit(v)
    k += ["UP"] * 10 + ["RIGHT"]
    for v in Y10:
        k += commit(v)
    return k


def stat1_freq_dataset():
    k = []
    for i in range(1, 11):
        k += commit(str(i))
    # Frequency On with committed data asks Clear Data? first; Yes clears,
    # then x is re-entered exactly as the manual's Freq workflow does.
    k += freq_on() + ["OK"]
    for i in range(1, 11):
        k += commit(str(i))
    k += ["UP"] * 10 + ["RIGHT"]
    for v in FR:
        k += commit(v)
    return k


STAT1_LABELS = ["xbar=5.95", "sumX=119", "sumX2=837", "popVarX=6.4475", "popSdX=2.539192785",
                "smpVarX=6.786842105", "smpSdX=2.605156829", "n=20", "minX=1", "Q1=4",
                "Med=6.5", "Q3=8", "maxX=10"]
STAT2_LABELS = ["xbar=1.99", "sumX=19.9", "sumX2=43.57", "popVarX=0.3969", "popSdX=0.63",
                "smpVarX=0.441", "smpSdX=0.6640783086", "n=10", "ybar=1.46", "sumY=14.6",
                "sumY2=22.24", "popVarY=0.0924", "popSdY=0.3039736831", "smpVarY=0.1026666667",
                "smpSdY=0.3204163958", "sumXY=30.96", "sumX3=102.451", "sumX2Y=71.244",
                "sumX4=253.5541", "minX=1", "maxX=3", "minY=1", "maxY=2"]


def build_statistics():
    cases = []
    rel = ["HOME"] + ["LEFT"] * 14 + ["RIGHT", "OK"]
    cases.append(stat_case("STAT-01", "Statistics launch type selector", "launch",
                           "Statistical calculations: general procedure steps 1-2",
                           [K("final")],
                           [P("STAT-01-1", "final",
                               {"screen": {"id": "StatisticsType"},
                                "menu": {"labels": ["1-Variable", "2-Variable"]}})]))
    cases.append(stat_case("STAT-02", "1-variable editor", "input",
                           "Entering data with the Statistics editor",
                           ["OK"] + commit("1") + commit("2") + [K("final")],
                           [P("STAT-02-1", "final",
                               {"screen": {"id": "Statistics"},
                                "grid": {"rows": 2, "cols": 1, "cells": ["1", "2"]}})]))
    cases.append(stat_case("STAT-03", "2-variable editor", "input",
                           "Entering data with the Statistics editor; Example 1",
                           ["DOWN", "OK"] + commit("170") + commit("179") + [K("final")],
                           [P("STAT-03-1", "final",
                               {"screen": {"id": "Statistics"},
                                "grid": {"rows": 2, "cols": 2, "cells": ["170", "", "179", ""]}})]))
    cases.append(stat_case("STAT-04", "Frequency On adds Freq", "menu",
                           "Freq column: showing the Freq column",
                           ["OK"] + commit("7") + freq_on() + ["OK", K("final")],
                           [P("STAT-04-1", "final",
                               {"screen": {"id": "Statistics"}, "grid": {"rows": 0, "cols": 2}})]))
    cases.append(stat_case("STAT-05", "Frequency Off removes Freq", "menu",
                           "Freq column: hiding the Freq column",
                           ["OK"] + commit("7") + freq_on() + ["OK", "TOOLS", "OK", "DOWN", "OK"] +
                           [K("final")],
                           [P("STAT-05-1", "final",
                               {"screen": {"id": "Statistics"}, "grid": {"rows": 0, "cols": 1}})]))
    k160 = ["OK"]
    for _ in range(160):
        k160 += commit("1")
    k160 += [K("final")]
    cases.append(stat_case("STAT-06", "capacity 160 rows one column", "input",
                           "Data row capacity: 160 rows for one column", k160,
                           [P("STAT-06-1", "final", {"grid": {"rows": 160, "cols": 1}})]))
    k80 = ["DOWN", "OK"]
    for _ in range(80):
        k80 += commit("1")
    k80 += [K("final")]
    cases.append(stat_case("STAT-07", "capacity 80 rows two columns", "input",
                           "Data row capacity: 80 rows for two columns", k80,
                           [P("STAT-07-1", "final", {"grid": {"rows": 80, "cols": 2}})]))
    k53 = ["DOWN", "OK"] + freq_on()
    for _ in range(53):
        k53 += commit("1")
    k53 += [K("final")]
    cases.append(stat_case("STAT-08", "capacity 53 rows three columns", "input",
                           "Data row capacity: 53 rows for three columns", k53,
                           [P("STAT-08-1", "final", {"grid": {"rows": 53, "cols": 3}})]))
    cases.append(stat_case("STAT-09", "mode switch clears data", "retention",
                           "Important: switching 1-variable/2-variable clears entered data",
                           ["OK"] + commit("4") + rel + ["DOWN", "OK", "OK", K("final")],
                           [P("STAT-09-1", "final",
                               {"screen": {"id": "Statistics"}, "grid": {"rows": 0, "cols": 2}})]))
    cases.append(stat_case("STAT-10", "Frequency change clears data", "retention",
                           "Important: changing Frequency clears entered data",
                           ["OK"] + commit("4") + freq_on() + ["OK", K("final")],
                           [P("STAT-10-1", "final",
                               {"screen": {"id": "Statistics"}, "grid": {"rows": 0, "cols": 2}})]))
    cases.append(stat_case("STAT-11", "replace a cell", "input",
                           "Editing sample data: replacing a cell",
                           ["OK"] + commit("1") + commit("2") + ["UP", "UP"] + D("5") +
                           ["EXE", K("final")],
                           [P("STAT-11-1", "final", {"grid": {"cells": ["5", "2"]}})]))
    cases.append(stat_case("STAT-12", "delete a row", "input",
                           "Editing sample data: deleting a row",
                           ["OK"] + commit("1") + commit("2") + ["UP", "DEL", K("final")],
                           [P("STAT-12-1", "final", {"grid": {"rows": 1, "cells": ["1"]}})]))
    cases.append(stat_case("STAT-13", "insert a row", "input",
                           "Editing sample data: inserting a row",
                           ["OK"] + commit("1") + tools_row(1) + ["OK", K("final")],
                           [P("STAT-13-1", "final", {"grid": {"rows": 2}})]))
    cases.append(stat_case("STAT-14", "Delete All", "input",
                           "Editing sample data: deleting all editor contents",
                           ["OK"] + commit("1") + tools_row(1) + ["DOWN", "OK", "OK",
                                                                 K("final")],
                           [P("STAT-14-1", "final", {"grid": {"rows": 0}})]))
    st1 = ["DOWN", "OK"]
    for v in ["170", "179", "173"]:
        st1 += commit(v)
    st1 += ["UP"] * 3 + ["RIGHT"]
    for v in ["66", "75", "68"]:
        st1 += commit(v)
    cases.append(stat_case("STAT-15", "sort x ascending keeps tuples", "menu",
                           "Sorting data; Example 2", st1 + tools_row(2) + ["OK", K("final")],
                           [P("STAT-15-1", "final",
                               {"grid": {"cells": ["170", "66", "173", "68", "179", "75"]}})]))
    cases.append(stat_case("STAT-16", "sort x descending", "menu",
                           "Sorting data ascending/descending",
                           st1 + tools_row(2) + ["DOWN", "OK", K("final")],
                           [P("STAT-16-1", "final",
                               {"grid": {"cells": ["179", "75", "173", "68", "170", "66"]}})]))
    cases.append(stat_case("STAT-17", "sort y descending", "menu",
                           "Sorting data on y/Freq",
                           st1 + tools_row(2) + ["DOWN", "DOWN", "DOWN", "OK", K("final")],
                           [P("STAT-17-1", "final",
                               {"grid": {"cells": ["179", "75", "173", "68", "170", "66"]}})]))
    sf = (["OK"] + freq_on() + commit("2") + commit("1") + ["UP", "UP", "RIGHT"] + commit("1") +
          commit("2") + tools_row(2) + ["DOWN", "DOWN", "OK", K("final")])
    cases.append(stat_case("STAT-18", "sort Freq ascending", "menu",
                           "Sorting data on y/Freq",
                           sf, [P("STAT-18-1", "final",
                                   {"grid": {"cells": ["2", "1", "1", "2"]}})]))
    cases.append(stat_case("STAT-19", "store a statistics cell", "menu",
                           "Statistics editor: storing a cell value in a variable",
                           ["OK"] + commit("5") +
                           ["UP", "VARIABLE", "UP", "UP", "UP", "UP", "UP", "LEFT",
                            "OK", "DOWN", "OK", "BACK", K("final")],
                           [P("STAT-19-1", "final", {"screen": {"id": "Statistics"}})]))
    cases.append(stat_case("STAT-20", "manual 1-Var Results dataset", "result",
                           "Displaying 1-variable results; Example 3",
                           stat1_freq_dataset() + ["OK", "OK", K("final")],
                           [P("STAT-20-1", "final",
                               {"screen": {"id": "StatisticsResults"},
                                "menu": {"labels": STAT1_LABELS}})]))
    cases.append(stat_case("STAT-21", "manual 2-Var Results dataset", "result",
                           "Displaying 2-variable results; Example 4",
                           ["DOWN", "OK"] + stat2_dataset() + ["OK", "OK", K("final")],
                           [P("STAT-21-1", "final",
                               {"screen": {"id": "StatisticsResults"},
                                "menu": {"labels": STAT2_LABELS}})]))
    cases.append(stat_case("STAT-22", "linear regression a/b/r", "result",
                           "Displaying regression results; Example 5 (linear)",
                           ["DOWN", "OK"] + stat2_dataset() + ["OK", "DOWN", "OK", "OK", K("final")],
                           [P("STAT-22-1", "final",
                               {"screen": {"id": "StatisticsResults"},
                                "menu": {"labels": ["a=0.5043587805", "b=0.4802217183",
                                                    "r=0.9952824846"]}})]))
    cases.append(stat_case("STAT-23", "quadratic regression a/b/c", "result",
                           "Displaying regression results; Example 5 (quadratic)",
                           ["DOWN", "OK"] + stat2_dataset() +
                           ["OK", "DOWN", "OK", "DOWN", "OK", K("final")],
                           [P("STAT-23-1", "final",
                               {"screen": {"id": "StatisticsResults"},
                                "menu": {"labels": ["a=0.7028598638", "b=0.2576384379",
                                                    "c=0.05610274153"]}})]))
    cases.append(stat_case("STAT-24", "logarithmic regression domain", "error",
                           "Supported regression types: logarithmic y=a+b*ln(x)",
                           ["DOWN", "OK"] + ["SUBTRACT", "1", "EXE"] + commit("2") + commit("3") +
                           ["UP", "UP", "UP", "RIGHT"] + commit("1") + commit("2") + commit("3") +
                           ["OK", "DOWN", "OK", "DOWN", "DOWN", "OK", K("final")],
                           [P("STAT-24-1", "final",
                               {"screen": {"id": "ErrorDialog"}, "error": {"title": "Math ERROR"}})],
                           conf="LIKELY"))
    cases.append(stat_case("STAT-25", "e exponential regression domain", "error",
                           "Supported regression types: exponential y=a*e^(bx)",
                           ["DOWN", "OK"] + commit("1") + commit("2") + ["UP", "UP", "RIGHT"] +
                           ["SUBTRACT", "1", "EXE"] + commit("2") +
                           ["OK", "DOWN", "OK", "DOWN", "DOWN", "DOWN", "OK", K("final")],
                           [P("STAT-25-1", "final",
                               {"screen": {"id": "ErrorDialog"}, "error": {"title": "Math ERROR"}})],
                           conf="LIKELY"))
    k26 = ["DOWN", "OK"] + commit("0") + commit("1") + commit("2") + ["UP"] * 3 + ["RIGHT"]
    k26 += commit("2") + commit("3") + commit("4.5")
    k26 += ["OK", "DOWN", "OK", "DOWN", "DOWN", "DOWN", "DOWN", "OK", K("final")]
    cases.append(stat_case("STAT-26", "ab exponential exact fit", "result",
                           "Supported regression types: y=a*b^x", k26,
                           [P("STAT-26-1", "final",
                               {"menu": {"labels": ["a=2", "b=1.5", "r=1"]}})], conf="LIKELY"))
    k27 = ["DOWN", "OK"] + commit("1") + commit("4") + commit("9") + ["UP"] * 3 + ["RIGHT"]
    k27 += commit("2") + commit("16") + commit("54")
    k27 += ["OK", "DOWN", "OK", "DOWN", "DOWN", "DOWN", "DOWN", "DOWN", "OK", K("final")]
    cases.append(stat_case("STAT-27", "power exact fit", "result",
                           "Supported regression types: y=a*x^b", k27,
                           [P("STAT-27-1", "final",
                               {"menu": {"labels": ["a=2", "b=1.5", "r=1"]}})], conf="LIKELY"))
    k28 = ["DOWN", "OK"] + commit("1") + commit("2") + commit("3") + ["UP"] * 3 + ["RIGHT"]
    k28 += commit("5") + commit("3.5") + commit("3")
    k28 += ["OK", "DOWN", "OK", "DOWN", "DOWN", "DOWN", "DOWN", "DOWN", "DOWN", "OK", K("final")]
    cases.append(stat_case("STAT-28", "inverse exact fit", "result",
                           "Supported regression types: y=a+b/x", k28,
                           [P("STAT-28-1", "final",
                               {"menu": {"labels": ["a=2", "b=3", "r=1"]}})], conf="LIKELY"))
    k29 = (["DOWN", "OK"] + stat2_dataset() + ["OK", "DOWN", "OK", "OK", "BACK",
           "OK", "DOWN", "DOWN", "OK", "CATALOG", "DOWN", "DOWN", "DOWN", "OK",
           "DOWN", "DOWN", "OK", "EXE", K("final")])
    cases.append(stat_case("STAT-29", "correlation through Statistics Calc", "result",
                           "Statistics Calc; Example 7 (a, b, r)", k29,
                           [P("STAT-29-1", "final",
                               {"screen": {"id": "StatisticsCalcResult"},
                                "result": {"text": "0.9952824846"}})]))
    k30 = (stat1_freq_dataset() + ["OK", "DOWN", "OK", "CATALOG", "OK", "OK", "EXE", K("mid"),
           "BACK", "AC", "CATALOG", "DOWN", "OK", "OK", "EXE", K("final")])
    cases.append(stat_case("STAT-30", "Statistics Calc summation and mean", "result",
                           "Statistics Calc; Example 6 (sum x, mean x)", k30,
                           [P("STAT-30-1", "mid", {"result": {"text": "119"}}),
                            P("STAT-30-2", "final",
                              {"screen": {"id": "StatisticsCalcResult"},
                               "result": {"text": "5.95"}})]))
    cases.append(stat_case("STAT-31", "Statistics CATALOG groups", "menu",
                           "Statistical variables and functions via CATALOG",
                           ["OK", "OK", "DOWN", "OK", "CATALOG", K("final")],
                           [P("STAT-31-1", "final",
                               {"screen": {"id": "StatisticsCatalog"},
                                "menu": {"labels": ["Summation", "Mean/Var/Dev...",
                                                    "Min/Max/Quartile", "Norm Dist"]}})]))
    k32 = (["DOWN", "OK"] + stat2_dataset() + ["OK", "DOWN", "OK", "OK", "BACK",
           "OK", "DOWN", "DOWN", "OK"] + D("5.5") +
           ["CATALOG", "DOWN", "DOWN", "DOWN", "OK",
            "DOWN", "DOWN", "DOWN", "DOWN", "OK", "EXE", K("final")])
    cases.append(stat_case("STAT-32", "estimated y value", "result",
                           "Estimated values; Example 9 (yHat 5.5)", k32,
                           [P("STAT-32-1", "final",
                               {"screen": {"id": "StatisticsCalcResult"},
                                "result": {"text": "3.145578231"}})]))
    k33 = (["DOWN", "OK"] + stat2_dataset() + ["OK", "DOWN", "OK", "OK", "BACK",
           "OK", "DOWN", "DOWN", "OK"] + D("2") +
           ["CATALOG", "DOWN", "DOWN", "DOWN", "OK",
            "DOWN", "DOWN", "DOWN", "OK", "EXE", K("final")])
    cases.append(stat_case("STAT-33", "estimated x value", "result",
                           "Estimated values: xHat for an entered y", k33,
                           [P("STAT-33-1", "final",
                               {"screen": {"id": "StatisticsCalcResult"},
                                "result": {"text": "3.114480588"}})]))
    k34 = (stat1_freq_dataset() + ["OK", "DOWN", "OK", "AC"] + D("2") +
           ["CATALOG", "DOWN", "DOWN", "DOWN", "OK", "DOWN", "DOWN", "DOWN", "OK", "EXE", K("mid"),
            "BACK", "AC", "CATALOG", "DOWN", "DOWN", "DOWN", "OK", "OK", "ANS", "CLOSE",
            "EXE", K("final")])
    cases.append(stat_case("STAT-34", "standardized t and P(t)", "result",
                           "Normal distribution with 1-variable data; Example 8", k34,
                           [P("STAT-34-1", "mid", {"result": {"text": "-1.555612486"}}),
                            P("STAT-34-2", "final", {"result": {"text": "0.05990013396"}})]))
    cases.append(stat_case("STAT-35", "stale statistics result", "stale job",
                           "Statistics ownership snapshot",
                           ["OK", "OK", "OK", K("final")],
                           [pend("STAT-35-1", "final", "reference runner drains after every key; no "
                                  "no-drain interleaving possible",
                                  "native coverage in calculator_apps_p17_test.h")]))
    cases.append(stat_case("STAT-36", "HOME reopen preserves dataset", "retention",
                           "Retention beyond type/Frequency clears is target-unresolved",
                           ["OK"] + commit("4") + rel + ["OK", "OK", K("final")],
                           [pend("STAT-36-1", "final", "Statistics retention beyond the documented "
                                  "clears is provisional (O-1)",
                                  "physical retention observation")]))
    cases.append(stat_case("STAT-37", "result and menu raster", "render reference",
                           "Manual screenshots",
                           ["OK", K("final")],
                           [pend("STAT-37-1", "final", "exact raster needs hardware captures",
                                  "P20 panel captures")]))
    cases.append(stat_case("STAT-38", "Insert Row at capacity 1 column", "error",
                           "Data row capacity: 160 rows for one column",
                           ["OK"] + sum([commit("1") for _ in range(160)], []) + tools_row(1) +
                           ["OK"] + [K("final")],
                           [pend("STAT-38-1", "final", "manual names no over-capacity error; "
                                  "provisional Range ERROR",
                                  "target error evidence")]))
    cases.append(stat_case("STAT-39", "Insert Row at capacity 2 columns", "error",
                           "Data row capacity: 80 rows for two columns",
                           ["DOWN", "OK"] + sum([commit("1") for _ in range(80)], []) + tools_row(1) +
                           ["OK"] + [K("final")],
                           [pend("STAT-39-1", "final", "manual names no over-capacity error",
                                  "target error evidence")]))
    cases.append(stat_case("STAT-40", "Insert Row at capacity 3 columns", "error",
                           "Data row capacity: 53 rows for three columns",
                           ["DOWN", "OK"] + freq_on() + sum([commit("1") for _ in range(53)], []) +
                           tools_row(1) + ["OK"] + [K("final")],
                           [pend("STAT-40-1", "final", "manual names no over-capacity error",
                                  "target error evidence")]))
    cases.append(stat_case("STAT-41", "Frequency default state", "menu",
                           "Freq column default",
                           ["OK", K("final")],
                           [pend("STAT-41-1", "final", "manual states no default Frequency; "
                                  "provisional Off", "target default evidence")]))
    cases.append(stat_case("STAT-42", "single observation sample variance", "result",
                           "1-variable results with n = 1",
                           ["OK"] + commit("4") + ["OK", "OK", K("final")],
                           [pend("STAT-42-1", "final", "undefined-quantity presentation is provisional",
                                  "target single-row observation")]))
    # y = 1 + 2x + 0.5x^2 on (1,3.5),(2,7),(3,11.5); xHat1 for y = 8 is 2.242640687.
    k43 = (["DOWN", "OK"] + commit("1") + commit("2") + commit("3") + ["UP"] * 3 + ["RIGHT"] +
           commit("3.5") + commit("7") + commit("11.5") +
           ["OK", "DOWN", "OK", "DOWN", "OK", "BACK", "OK", "DOWN", "DOWN", "OK"] + D("8") +
           ["CATALOG", "DOWN", "DOWN", "DOWN", "OK", "DOWN", "DOWN", "DOWN", "OK", "EXE",
            K("final")])
    cases.append(stat_case("STAT-43", "quadratic estimated x pair", "result",
                           "Estimated values: xHat1/xHat2 for quadratic", k43,
                           [P("STAT-43-1", "final",
                               {"screen": {"id": "StatisticsCalcResult"},
                                "result": {"text": "2.242640687"}})], conf="LIKELY"))
    # CW capture: entering x with Frequency On seeds Freq=1 on that row
    # (2-var editor and scrolled 1-var editor frames).
    cases.append(stat_case("STAT-44", "Frequency seeds Freq=1 on x entry", "input",
                           "Editor with the Freq column: entered rows carry Freq 1",
                           ["OK"] + freq_on() + commit("5") + [K("final")],
                           [P("STAT-44-1", "final",
                               {"screen": {"id": "Statistics"},
                                "grid": {"rows": 1, "cols": 2, "cells": ["5", "1"]}})],
                           srckind="880-HW", src=CW_SRC))
    # CW capture: destructive clears ask "Clear Data?" with Yes (selected)
    # first and Cancel second. Cancel preserves the dataset.
    k45 = (["OK"] + commit("1") + tools_row(1) + ["DOWN", "OK", K("mid"),
           "DOWN", "OK", K("cancelled")] + tools_row(1) + ["DOWN", "OK", "OK",
                                                          K("final")])
    cases.append(stat_case("STAT-45", "Clear Data confirm on Delete All", "input",
                           "Delete All asks Clear Data? Yes/Cancel; Cancel preserves data",
                           k45,
                           [P("STAT-45-1", "mid",
                               {"screen": {"id": "StatisticsConfirm"},
                                "menu": {"labels": ["Yes", "Cancel"]}}),
                            P("STAT-45-2", "cancelled",
                               {"screen": {"id": "Statistics"}, "grid": {"rows": 1}}),
                            P("STAT-45-3", "final", {"grid": {"rows": 0}})],
                           srckind="880-HW", src=CW_SRC))
    # CW capture: typed digits echo in the bottom band while the grid stays
    # on screen; entry never takes over the display.
    cases.append(stat_case("STAT-46", "typing echoes on the grid", "input",
                           "Editor typing echo with the grid visible",
                           ["OK", "1", K("final")],
                           [P("STAT-46-1", "final",
                               {"screen": {"id": "Statistics"},
                                "grid": {"rows": 0, "editing": True}})],
                           srckind="880-HW", src=CW_SRC))
    # CW capture: the mode switch is a destructive clear, so it asks first.
    k47 = (["OK"] + commit("4") + rel + ["DOWN", "OK", K("mid"), "OK", K("final")])
    cases.append(stat_case("STAT-47", "Clear Data confirm on mode switch", "retention",
                           "Switching 1-variable/2-variable asks Clear Data? first",
                           k47,
                           [P("STAT-47-1", "mid",
                               {"screen": {"id": "StatisticsConfirm"},
                                "menu": {"labels": ["Yes", "Cancel"]}}),
                            P("STAT-47-2", "final",
                               {"screen": {"id": "Statistics"}, "grid": {"rows": 0, "cols": 2}})],
                           srckind="880-HW", src=CW_SRC))
    return cases


def build_distribution():
    cases = []
    cases.append(dist_case("DIST-01", "Distribution type menu", "launch",
                           "Distribution calculation types",
                           [K("final")],
                           [P("DIST-01-1", "final",
                               {"screen": {"id": "DistributionType"}, "menu": {"labels": TYPES}})]))
    cases.append(dist_case("DIST-02", "Binomial PD variable", "input",
                           "General procedure; List/Variable input method",
                           ["OK", "DOWN", "OK", "OK"] + commit("2") + commit("5") + commit("0.5") +
                           ["OK", K("final")],
                           [P("DIST-02-1", "final",
                               {"screen": {"id": "DistributionResult"},
                                "result": {"text": "0.3125"}})]))
    cases.append(dist_case("DIST-03", "Binomial PD list", "input",
                           "List screen: up to 45 x items with P results",
                           ["OK", "OK"] + commit("2") + commit("3") + ["OK"] + commit("5") +
                           commit("0.5") + ["OK", K("final")],
                           [P("DIST-03-1", "final",
                               {"grid": {"rows": 2, "cells": ["2", "0.3125", "3", "0.3125"]}})]))
    cases.append(dist_case("DIST-04", "Binomial CD variable", "input",
                           "General procedure with Binomial CD",
                           ["DOWN", "OK", "DOWN", "OK", "OK"] + commit("4") + commit("5") +
                           commit("0.5") + ["OK", K("final")],
                           [P("DIST-04-1", "final", {"result": {"text": "0.96875"}})]))
    cases.append(dist_case("DIST-05", "manual Binomial CD list example", "input",
                           "General procedure example: x {2,3,4,5}, N=5, p=0.5",
                           ["DOWN", "OK", "OK"] + commit("2") + commit("3") + commit("4") +
                           commit("5") + ["OK"] + commit("5") + commit("0.5") + ["OK", K("final")],
                           [P("DIST-05-1", "final",
                               {"grid": {"rows": 4,
                                         "cells": ["2", "0.5", "3", "0.8125", "4", "0.96875",
                                                   "5", "1"]}})]))
    cases.append(dist_case("DIST-06", "Normal PD manual example", "input",
                           "Calculation example: Normal PD x=36, mu=35, sigma=2",
                           ["DOWN", "DOWN", "OK"] + commit("36") + commit("35") + commit("2") +
                           ["OK", K("final")],
                           [P("DIST-06-1", "final", {"result": {"text": "0.1760326634"}})]))
    cases.append(dist_case("DIST-07", "Normal CD symmetric bounds", "input",
                           "Parameter list: Normal CD Lower/Upper/mu/sigma",
                           ["DOWN", "DOWN", "DOWN", "OK"] + ["SUBTRACT", "1", "EXE"] + commit("1") +
                           commit("0") + commit("1") + ["OK", K("final")],
                           [P("DIST-07-1", "final", {"result": {"text": "0.6826894921"}})]))
    cases.append(dist_case("DIST-08", "Inverse Normal value", "input",
                           "Inverse Normal returns xInv from Area",
                           ["DOWN"] * 4 + ["OK"] + commit("0.975") + ["DOWN", "DOWN", "OK",
                                                                      K("final")],
                           [P("DIST-08-1", "final", {"result": {"text": "1.959963985"}})]))
    cases.append(dist_case("DIST-09", "Poisson PD variable", "input",
                           "Parameter list: Poisson PD x/lambda",
                           ["DOWN"] * 5 + ["OK", "DOWN", "OK", "OK"] + commit("5") + commit("4") +
                           ["OK", K("final")],
                           [P("DIST-09-1", "final", {"result": {"text": "0.1562934519"}})]))
    cases.append(dist_case("DIST-10", "Poisson PD list", "input",
                           "List screen with Poisson PD",
                           ["DOWN"] * 5 + ["OK", "OK"] + commit("0") + commit("1") + commit("2") +
                           ["OK"] + commit("4") + ["OK", K("final")],
                           [P("DIST-10-1", "final",
                               {"grid": {"cells": ["0", "0.01831563889", "1", "0.07326255555",
                                                   "2", "0.1465251111"]}})]))
    cases.append(dist_case("DIST-11", "Poisson CD variable", "input",
                           "Parameter list: Poisson CD",
                           ["DOWN"] * 6 + ["OK", "DOWN", "OK", "OK"] + commit("4") + commit("4") +
                           ["OK", K("final")],
                           [P("DIST-11-1", "final", {"result": {"text": "0.6288369352"}})]))
    cases.append(dist_case("DIST-12", "Poisson CD list", "input",
                           "List screen with Poisson CD",
                           ["DOWN"] * 6 + ["OK", "OK"] + commit("0") + commit("4") + ["OK"] +
                           commit("4") + ["OK", K("final")],
                           [P("DIST-12-1", "final",
                               {"grid": {"cells": ["0", "0.01831563889", "4", "0.6288369352"]}})]))
    k13 = ["DOWN", "OK", "OK"]
    for _ in range(45):
        k13 += commit("1")
    k13 += [K("final")]
    cases.append(dist_case("DIST-13", "list capacity 45", "input", "List screen: up to 45 x items",
                           k13, [P("DIST-13-1", "final", {"grid": {"rows": 45}})]))
    cases.append(dist_case("DIST-14", "edit a list row", "input",
                           "Editing the list screen: changing x",
                           ["OK", "OK"] + commit("2") + commit("3") + ["UP", "UP"] + D("5") +
                           ["EXE", K("final")],
                           [P("DIST-14-1", "final", {"grid": {"cells": ["5", "", "3", ""]}})]))
    cases.append(dist_case("DIST-15", "delete a list row", "input",
                           "Editing the list screen: deleting a row",
                           ["OK", "OK"] + commit("2") + commit("3") + ["UP", "DEL", K("final")],
                           [P("DIST-15-1", "final", {"grid": {"rows": 1, "cells": ["2", ""]}})]))
    cases.append(dist_case("DIST-16", "insert row defaults 0", "input",
                           "Editing the list screen: inserting a row defaults to 0",
                           ["OK", "OK"] + commit("2") + ["TOOLS", "OK", K("final")],
                           [P("DIST-16-1", "final", {"grid": {"cells": ["2", "", "0", ""]}})]))
    cases.append(dist_case("DIST-17", "Delete All list", "input", "Deleting all list contents",
                           ["OK", "OK"] + commit("2") + ["TOOLS", "DOWN", "OK", "OK",
                                                         K("final")],
                           [P("DIST-17-1", "final", {"grid": {"rows": 0}})]))
    k18 = (["DOWN", "OK", "OK"] + commit("2") + commit("3") + ["OK"] + commit("5") + commit("0.5") +
           ["OK", "UP", "UP"] + D("4") + ["EXE", K("final")])
    cases.append(dist_case("DIST-18", "edit after results clears P", "input",
                           "Editing the result list clears all calculation results",
                           k18,
                           [P("DIST-18-1", "final", {"grid": {"cells": ["4", "", "3", ""]}})]))
    k19 = (["DOWN", "OK", "OK"] + commit("6") + commit("3") + ["OK"] + commit("5") + commit("0.5") +
           ["OK", K("final")])
    cases.append(dist_case("DIST-19", "per-row ERROR for bad x", "error",
                           "ERROR appears in P when an input value is outside the domain",
                           k19,
                           [P("DIST-19-1", "final",
                               {"grid": {"cells": ["6", "ERROR", "3", "0.8125"]}})]))
    cases.append(dist_case("DIST-20", "variable result presentation", "result",
                           "Variable input stores the result in Ans",
                           ["OK", "DOWN", "OK", "OK"] + commit("2") + commit("5") + commit("0.5") +
                           ["OK", K("final")],
                           [P("DIST-20-1", "final",
                               {"screen": {"id": "DistributionResult"},
                                "result": {"text": "0.3125"}})]))
    cases.append(dist_case("DIST-21", "list results and Ans", "result",
                           "List-mode P rows versus Ans",
                           ["DOWN", "OK", "OK"] + commit("2") + ["OK"] + commit("5") + commit("0.5") +
                           ["OK", K("final")],
                           [pend("DIST-21-1", "final",
                                  "Ans is not snapshot-observable; target is silent",
                                  "memory observation or target evidence")]))
    k22 = (["OK", "DOWN", "OK", "DOWN"] + commit("7") + home_launch(2) + ["DOWN", "OK", "OK",
                                                                         K("final")])
    cases.append(dist_case("DIST-22", "parameter N shared PD to CD", "retention",
                           "Parameter values persist by name across screens",
                           k22,
                           [P("DIST-22-1", "final",
                               {"menu": {"labels": ["x=0", "N=7", "p=0.5", "Execute"]}})]))
    k23 = (["DOWN", "OK", "OK"] + commit("3") + home_launch(0) + home_launch(2) +
           ["DOWN", "OK", "OK", K("final")])
    cases.append(dist_case("DIST-23", "other-app launch clears the list", "retention",
                           "List x clears when HOME launches another calculator app",
                           k23, [P("DIST-23-1", "final", {"grid": {"rows": 0}})]))
    cases.append(dist_case("DIST-24", "reset clears parameters", "retention",
                           "Parameters persist until Settings&Data or Initialize All",
                           ["OK", K("final")],
                           [pend("DIST-24-1", "final", "reset flow is not key-stable in the runner",
                                  "native coverage in calculator_apps_p17_test.h")]))
    cases.append(dist_case("DIST-25", "store a list cell", "menu",
                           "List screen: storing a cell value in a variable",
                           ["OK", "OK"] + commit("2") +
                           ["UP", "VARIABLE", "UP", "UP", "UP", "UP", "UP",
                            "LEFT", "OK", "DOWN", "OK", "BACK", K("final")],
                           [P("DIST-25-1", "final", {"screen": {"id": "DistributionList"}})]))
    cases.append(dist_case("DIST-26", "stale list execution", "stale job",
                           "Distribution ownership snapshot",
                           ["DOWN", "OK", "OK", "OK", K("final")],
                           [pend("DIST-26-1", "final", "runner drains every key; no interleaving",
                                  "native coverage in calculator_apps_p17_test.h")]))
    cases.append(dist_case("DIST-27", "normal tail interval", "result",
                           "Distribution accuracy including tails",
                           ["DOWN", "DOWN", "DOWN", "OK"] + ["SUBTRACT", "6", "EXE"] + commit("6") +
                           commit("0") + commit("1") + ["OK", K("final")],
                           [P("DIST-27-1", "final", {"result": {"text": "0.999999998"}})]))
    cases.append(dist_case("DIST-28", "inverse normal tail", "result",
                           "Inverse Normal in the far tail",
                           ["DOWN"] * 4 + ["OK"] + commit("0.00000001") + ["DOWN", "DOWN", "OK",
                                                                           K("final")],
                           [P("DIST-28-1", "final", {"result": {"text": "-5.612001244"}})]))
    cases.append(dist_case("DIST-29", "binomial tail", "result",
                           "Binomial CD with small p and large N",
                           ["DOWN", "OK", "DOWN", "OK", "OK"] + commit("10") + commit("1000") +
                           commit("0.01") + ["OK", K("final")],
                           [P("DIST-29-1", "final", {"result": {"text": "0.5830408033"}})]))
    cases.append(dist_case("DIST-30", "poisson far tail", "result",
                           "Poisson CD far above the mean",
                           ["DOWN"] * 6 + ["OK", "DOWN", "OK", "OK"] + commit("150") + commit("100") +
                           ["OK", K("final")],
                           [P("DIST-30-1", "final", {"result": {"text": "0.9999987669"}})]))
    cases.append(dist_case("DIST-31", "type and list raster", "render reference",
                           "Distribution screenshots",
                           ["OK", K("final")],
                           [pend("DIST-31-1", "final", "exact raster needs hardware captures",
                                  "P20 panel captures")]))
    cases.append(dist_case("DIST-32", "Lower above Upper", "error",
                           "Parameter list: Normal CD Lower/Upper",
                           ["DOWN", "DOWN", "DOWN", "OK"] + commit("1") + ["SUBTRACT"] + D("1") +
                           ["EXE"] + commit("0") + commit("1") + ["OK", K("final")],
                           [pend("DIST-32-1", "final", "Lower>Upper target behavior is unresolved "
                                  "(O-2); provisional Argument ERROR",
                                  "target error evidence")]))
    cases.append(dist_case("DIST-33", "inverse endpoints", "error",
                           "Inverse Normal Area domain 0<=Area<=1",
                           ["DOWN"] * 4 + ["OK"] + commit("0") + ["DOWN", "DOWN", "OK", K("final")],
                           [pend("DIST-33-1", "final",
                                  "exact Area=0/1 target behavior is unresolved (O-2)",
                                  "target endpoint evidence")]))
    cases.append(dist_case("DIST-34", "list result Ans behavior", "result",
                           "Variable-mode result is stored in Ans",
                           ["OK", "OK"] + commit("2") + ["OK"] + commit("5") + commit("0.5") +
                           ["OK", K("final")],
                           [pend("DIST-34-1", "final", "Ans is not snapshot-observable",
                                  "memory observation")]))
    cases.append(dist_case("DIST-35", "parameter defaults", "retention",
                           "Parameter initial values",
                           ["OK", "OK", "OK", K("final")],
                           [pend("DIST-35-1", "final", "initial parameter values are provisional (O-2)",
                                  "target default evidence")]))
    return cases


def write(path, phase, cases):
    path.write_text(json.dumps({"schema": 1, "phase": phase, "cases": cases}, indent=1,
                               ensure_ascii=False) + "\n", encoding="utf-8")


def main():
    stats = build_statistics()
    dist = build_distribution()
    write(REF / "apps-statistics.json", "P17", stats)
    write(REF / "apps-distribution.json", "P17", dist)
    print("statistics cases:", len(stats))
    print("distribution cases:", len(dist))


if __name__ == "__main__":
    main()
