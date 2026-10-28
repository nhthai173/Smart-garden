"""Ép hierarchy của dự án lúc build. Chạy tay: python3 tools/check_layers.py

Ba tầng, phụ thuộc chỉ đi từ trên xuống:

    main (src/)                 ráp mọi thứ, không chứa logic
      sg_net                    adapter mạng của sản phẩm (web, Firebase)
        sg_core                 lõi tưới + an toàn, KHÔNG biết mạng
          nht_*                 thư viện dùng lại, KHÔNG biết sản phẩm

Quy tắc cứng:
  1. Mỗi component chỉ được REQUIRES những gì bảng ALLOWED cho phép.
  2. Mọi #include chéo component phải được khai báo trong REQUIRES.
  3. nht_* không bao giờ chạm sg_* hay main.
  4. sg_core không bao giờ chạm nht_net / nht_webkit / nht_fbrtdb / sg_net.
  5. Component mới chưa có trong bảng là lỗi - phải xếp tầng cho nó.
"""
import os
import re
import sys

try:
    Import("env")  # noqa: F821
    PROJ = env.subst("$PROJECT_DIR")  # noqa: F821
except Exception:
    PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Chỉ tính phụ thuộc giữa các component của dự án (nht_*, sg_*, main).
ALLOWED = {
    # --- thư viện tái sử dụng: độc lập, trừ nht_valve đứng trên nht_actuator
    "nht_actuator": set(),
    "nht_dinput":   set(),
    "nht_sched":    set(),
    "nht_store":    set(),
    "nht_sensor":   set(),
    "nht_net":      set(),
    "nht_webkit":   set(),
    "nht_fbrtdb":   set(),
    "nht_valve":    {"nht_actuator"},
    # --- sản phẩm
    "sg_core": {"nht_store", "nht_actuator", "nht_valve", "nht_sched",
                "nht_dinput", "nht_sensor"},
    "sg_net":  {"sg_core", "nht_net", "nht_webkit", "nht_fbrtdb"},
    "main":    {"sg_core", "sg_net", "nht_net", "nht_sensor", "nht_sched"},
}
NETWORK = {"nht_net", "nht_webkit", "nht_fbrtdb", "sg_net"}


def components():
    out = {}
    cdir = os.path.join(PROJ, "components")
    for n in sorted(os.listdir(cdir)):
        if os.path.isdir(os.path.join(cdir, n)):
            out[n] = os.path.join(cdir, n)
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
            errs.append(f"{name}: chưa được xếp tầng trong tools/check_layers.py")

    for name, path in comps.items():
        if name not in ALLOWED:
            continue
        req = requires(path)
        used = {owner[h] for h in includes(path) if h in owner} - {name}

        for r in sorted(req - ALLOWED[name]):
            errs.append(f"{name}: REQUIRES '{r}' vi phạm tầng "
                        f"(chỉ được: {sorted(ALLOWED[name]) or 'không gì cả'})")
        for u in sorted(used - req):
            errs.append(f"{name}: #include header của '{u}' nhưng không "
                        f"khai báo REQUIRES")
        for u in sorted(used - ALLOWED[name]):
            errs.append(f"{name}: dùng '{u}' vi phạm tầng")
        if name == "sg_core" and (req | used) & NETWORK:
            errs.append("sg_core đụng tới tầng mạng: "
                        f"{sorted((req | used) & NETWORK)}")
        if name.startswith("nht_") and (req | used) & {"sg_core", "sg_net", "main"}:
            errs.append(f"{name}: thư viện tái sử dụng không được biết sản phẩm")

    # Chu trình (bảng đã là DAG nhưng chặn người sửa bảng).
    state = {}

    def visit(n, stack):
        if state.get(n) == 2:
            return
        if state.get(n) == 1:
            errs.append("chu trình phụ thuộc: " + " -> ".join(stack + [n]))
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
        print("\n".join("[layers] LỖI " + e for e in errs), file=sys.stderr)
        sys.exit(1)
    print("[layers] OK - hierarchy hợp lệ (%d component)" % len(components()))


main()
