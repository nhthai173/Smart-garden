"""
Biên dịch web/*.html -> src/web_assets.h (mảng byte đã gzip).

Viết HTML/CSS/JS cho người đọc trong web/, script này lo phần tối ưu:
  minify (bỏ comment + khoảng trắng thừa) -> gzip mức 9 -> mảng C.

Nhờ vậy không cần SPIFFS/LittleFS: trang nằm thẳng trong .rodata của app.
Chỉ dùng thư viện chuẩn Python, không cài thêm gì.
"""
import os
import re
import gzip

try:
    Import("env")  # noqa: F821
    PROJ = env.subst("$PROJECT_DIR")  # noqa: F821
except Exception:
    PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

WEB_DIR = os.path.join(PROJ, "web")
OUT = os.path.join(PROJ, "components", "sg_net", "web_assets.h")


def minify_html(src: str) -> str:
    # Tách <script>/<style> ra xử lý riêng để không phá nội dung JS/CSS.
    blocks = []

    def stash(m):
        blocks.append(m.group(0))
        return "\x00%d\x00" % (len(blocks) - 1)

    src = re.sub(r"<(script|style)\b[^>]*>.*?</\1>", stash, src,
                 flags=re.S | re.I)

    src = re.sub(r"<!--(?!\[if).*?-->", "", src, flags=re.S)   # bỏ comment HTML
    src = re.sub(r">\s+<", "><", src)                          # khoảng trắng giữa thẻ
    src = re.sub(r"\s{2,}", " ", src)
    src = src.strip()

    def unstash(m):
        b = blocks[int(m.group(1))]
        if b[:7].lower() == "<script":
            b = re.sub(r"/\*.*?\*/", "", b, flags=re.S)
            b = re.sub(r"(?<![:'\"/\\])//[^\n]*", "", b)       # comment dòng
        else:
            b = re.sub(r"/\*.*?\*/", "", b, flags=re.S)
        b = re.sub(r"\n\s*", "\n", b)
        b = re.sub(r"\n{2,}", "\n", b)
        b = re.sub(r"\s*([{}:;,>])\s*", r"\1", b)
        return b

    return re.sub(r"\x00(\d+)\x00", unstash, src)


def c_array(name: str, data: bytes) -> str:
    rows = []
    for i in range(0, len(data), 16):
        rows.append("  " + ",".join("0x%02x" % b for b in data[i:i + 16]))
    return ("static const unsigned char %s[] = {\n%s\n};\n"
            "static const unsigned int %s_len = %d;\n"
            % (name, ",\n".join(rows), name, len(data)))


def main():
    if not os.path.isdir(WEB_DIR):
        return
    pages = sorted(f for f in os.listdir(WEB_DIR) if f.endswith(".html"))
    if not pages:
        return

    out = ["/* Sinh bởi tools/build_web.py - đừng sửa tay. */\n",
           "#ifndef SG_WEB_ASSETS_H\n#define SG_WEB_ASSETS_H\n\n"]
    total_raw = total_gz = 0
    for page in pages:
        with open(os.path.join(WEB_DIR, page), encoding="utf-8") as f:
            raw = f.read()
        mini = minify_html(raw).encode("utf-8")
        gz = gzip.compress(mini, 9)
        sym = "web_" + re.sub(r"[^a-z0-9]", "_", page[:-5].lower()) + "_gz"
        out.append(c_array(sym, gz))
        out.append("\n")
        total_raw += len(raw.encode("utf-8"))
        total_gz += len(gz)
        print("  web: %-16s %6d -> %5d B gzip" % (page, len(raw), len(gz)))
    out.append("#endif /* SG_WEB_ASSETS_H */\n")

    new = "".join(out)
    old = ""
    if os.path.exists(OUT):
        with open(OUT) as f:
            old = f.read()
    if old != new:
        with open(OUT, "w") as f:
            f.write(new)
    print("  web: tổng %d B -> %d B gzip (%.0f%%)"
          % (total_raw, total_gz, 100.0 * total_gz / max(total_raw, 1)))


main()
