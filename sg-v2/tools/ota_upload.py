#!/usr/bin/env python3
"""
Nạp firmware qua LAN.

Thay cho cách của bản cũ: mã hoá base64 firmware rồi nhét vào Realtime
Database, thiết bị tải về và giữ trong RAM. Cách ấy làm phình dữ liệu 33%,
tốn một vòng qua đám mây, và là nguồn gây treo trên chip có heap nhỏ.

Ở đây file .bin được POST thẳng vào thiết bị trong mạng nội bộ; firmware ghi
từng khối vào partition OTA, không đệm gì cả.

    python3 tools/ota_upload.py 192.168.1.50
    python3 tools/ota_upload.py smartgarden.local -e esp32c3

Không tìm thấy IP thì tự dò trong dải mạng:
    python3 tools/ota_upload.py --scan
"""
import argparse
import json
import os
import socket
import sys
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def firmware_path(env):
    p = os.path.join(HERE, ".pio", "build", env, "firmware.bin")
    if not os.path.exists(p):
        sys.exit("Chưa có %s - chạy 'pio run -e %s' trước." % (p, env))
    return p


def get_info(host, timeout=3):
    try:
        with urllib.request.urlopen("http://%s/api/info" % host,
                                    timeout=timeout) as r:
            return json.load(r)
    except Exception:
        return None


def scan():
    """Dò các thiết bị Smart Garden trong dải /24 của máy này."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("8.8.8.8", 80))
    base = ".".join(s.getsockname()[0].split(".")[:3])
    s.close()

    print("Đang dò %s.0/24 …" % base)
    found = []
    from concurrent.futures import ThreadPoolExecutor

    def probe(i):
        host = "%s.%d" % (base, i)
        info = get_info(host, timeout=1)
        if info and "fw" in info:
            return host, info
        return None

    with ThreadPoolExecutor(max_workers=64) as ex:
        for r in ex.map(probe, range(1, 255)):
            if r:
                found.append(r)
                print("  %s  %s  fw %s" % (r[0], r[1].get("device"),
                                           r[1].get("fw")))
    if not found:
        print("  không thấy thiết bị nào")
    return found


def upload(host, path):
    size = os.path.getsize(path)
    info = get_info(host)
    if info:
        print("Thiết bị : %s" % info.get("device"))
        print("Đang chạy: fw %s (build %s)" % (info.get("fw"),
                                               info.get("build")))
    else:
        print("Cảnh báo: không đọc được /api/info, vẫn thử nạp.")

    print("Firmware : %s (%.1f KB)" % (os.path.basename(path), size / 1024))
    print("Đang nạp lên http://%s/update …" % host)

    with open(path, "rb") as f:
        req = urllib.request.Request(
            "http://%s/update" % host, data=f.read(),
            headers={"Content-Type": "application/octet-stream"},
            method="POST")
        try:
            with urllib.request.urlopen(req, timeout=180) as r:
                print(r.read().decode("utf-8", "replace"))
        except urllib.error.HTTPError as e:
            sys.exit("Nạp thất bại: %s" % e.read().decode("utf-8", "replace"))
        except Exception as e:
            sys.exit("Nạp thất bại: %s" % e)

    print("Xong. Thiết bị đang khởi động lại.")
    print("Nếu firmware mới không chạy được, bootloader tự quay về bản cũ.")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host", nargs="?", help="IP hoặc hostname của thiết bị")
    ap.add_argument("-e", "--env", default="esp32c3", help="môi trường PlatformIO")
    ap.add_argument("--scan", action="store_true", help="dò thiết bị trong LAN")
    a = ap.parse_args()

    if a.scan:
        found = scan()
        if len(found) == 1 and not a.host:
            a.host = found[0][0]
            print("\nTự chọn %s" % a.host)
        elif not a.host:
            return

    if not a.host:
        ap.error("cần IP thiết bị, hoặc dùng --scan")

    upload(a.host, firmware_path(a.env))


if __name__ == "__main__":
    main()
