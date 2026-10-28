"""Enforce the project hierarchy at build time. Run by hand: python3 tools/check_layers.py

Three layers, dependencies only go downwards:

    main (src/)                 wires everything together, holds no logic
      sg_net                    the product's network adapters (web, Firebase)
        sg_core                 watering + safety core, knows NOTHING about the network
          nhtlib (nht_*)        reusable libraries, know NOTHING about the product

Hard rules:
  1. A component may only REQUIRE what the ALLOWED table permits.
  2. Every cross-component #include must be declared in REQUIRES.
  3. nht_* never touches sg_* or main.
  4. sg_core never touches nht_net / nht_webkit / nht_fbrtdb / sg_net.
  5. A new component missing from the table is an error - it must be given a layer.
"""
import os
import re
import sys

try:
    Import("env")  # noqa: F821
    PROJ = env.subst("$PROJECT_DIR")  # noqa: F821
except Exception:
    PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Only dependencies between the project's own components count (nht_*, sg_*, main).
ALLOWED = {
    # --- reusable libraries: independent, except nht_valve sits on nht_actuator
    "nht_actuator": set(),
    "nht_dinput":   set(),
    "nht_sched":    set(),
    "nht_store":    set(),
    "nht_sensor":   set(),
    "nht_net":      set(),
    "nht_webkit":   set(),
    "nht_fbrtdb":   set(),
    "nht_valve":    {"nht_actuator"},
    # --- the product
    "sg_core": {"nht_store", "nht_actuator", "nht_valve", "nht_sched",
                "nht_dinput", "nht_sensor"},
    "sg_net":  {"sg_core", "nht_net", "nht_webkit", "nht_fbrtdb"},
    "main":    {"sg_core", "sg_net", "nht_net", "nht_sensor", "nht_sched"},
}
NETWORK = {"nht_net", "nht_webkit", "nht_fbrtdb", "sg_net"}


def components():
    out = {}
    # Product components and the reusable nhtlib components live in two roots.
    for root in (os.path.join(PROJ, "components"),
                 os.path.join(PROJ, "components", "nhtlib")):
        for n in sorted(os.listdir(root)):
            p = os.path.join(root, n)
            # A component is a directory with its own CMakeLists.txt; this also
            # skips "nhtlib" itself (a bundle) and its "examples" folder.
            if os.path.isfile(os.path.join(p, "CMakeLists.txt")):
                out[n] = p
    out["main"] = os.path.join(PROJ, "src")
    return out


def requires(path):
    txt = open(os.path.join(path, "CMakeLists.txt"), encoding="utf-8").read()
    txt = re.sub(r"#.*", "", txt)
    got = set()
    for m in re.finditer(r"\b(?:PRIV_)?REQUIRES\b([^)]*?)(?=\b[A-Z_]{4,}\b\s|\))", txt, re.S):
        got |= set(m.group(1).split())
    return {r for r in got if r.startswith(("nht_", "sg_"))}


def header_owner(comps):
    owner = {}
    for name, path in comps.items():
        for root, _, files in os.walk(path):
            for f in files:
                if f.endswith(".h"):
                    owner.setdefault(f, name)
    return owner


def includes(path):
    got = set()
    for root, _, files in os.walk(path):
        for f in files:
            if f.endswith((".c", ".h")):
                txt = open(os.path.join(root, f), encoding="utf-8",
                           errors="ignore").read()
                got |= set(re.findall(r'#\s*include\s+"([^"]+\.h)"', txt))
    return got


def check():
    errs = []
    comps = components()
    owner = header_owner(comps)

    for name in comps:
        if name not in ALLOWED:
            errs.append(f"{name}: has not been given a layer in tools/check_layers.py")

    for name, path in comps.items():
        if name not in ALLOWED:
            continue
        req = requires(path)
        used = {owner[h] for h in includes(path) if h in owner} - {name}

        for r in sorted(req - ALLOWED[name]):
            errs.append(f"{name}: REQUIRES '{r}' violates the layering "
                        f"(allowed: {sorted(ALLOWED[name]) or 'nothing'})")
        for u in sorted(used - req):
            errs.append(f"{name}: includes a header of '{u}' but does not "
                        f"declare it in REQUIRES")
        for u in sorted(used - ALLOWED[name]):
            errs.append(f"{name}: uses '{u}', violating the layering")
        if name == "sg_core" and (req | used) & NETWORK:
            errs.append("sg_core touches the network layer: "
                        f"{sorted((req | used) & NETWORK)}")
        if name.startswith("nht_") and (req | used) & {"sg_core", "sg_net", "main"}:
            errs.append(f"{name}: a reusable library must not know the product")

    # Cycles (the table is already a DAG, but this guards whoever edits it).
    state = {}

    def visit(n, stack):
        if state.get(n) == 2:
            return
        if state.get(n) == 1:
            errs.append("dependency cycle: " + " -> ".join(stack + [n]))
            return
        state[n] = 1
        for d in ALLOWED.get(n, ()):
            visit(d, stack + [n])
        state[n] = 2

    for n in ALLOWED:
        visit(n, [])
    return errs


def main():
    errs = check()
    if errs:
        print("\n".join("[layers] ERROR " + e for e in errs), file=sys.stderr)
        sys.exit(1)
    print("[layers] OK - hierarchy valid (%d components)" % len(components()))


main()
