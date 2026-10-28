"""Gộp bootloader + bảng partition + app thành một ảnh flash duy nhất.

Wokwi (và mọi công cụ nạp một-file) cần ảnh gộp chứ không phải riêng
firmware.bin. Chạy tự động sau mỗi lần build.
"""
import os
import subprocess

Import("env")  # noqa: F821


def merge(source, target, env):  # noqa: ARG001
    build_dir = env.subst("$BUILD_DIR")
    out = os.path.join(build_dir, "merged.bin")

    parts = [
        ("0x0",     os.path.join(build_dir, "bootloader.bin")),
        ("0x8000",  os.path.join(build_dir, "partitions.bin")),
        ("0xf000",  os.path.join(build_dir, "ota_data_initial.bin")),
        (os.environ.get("SG_APP_OFFSET","0x20000"), os.path.join(build_dir, "firmware.bin")),
    ]
    parts = [(a, p) for a, p in parts if os.path.exists(p)]

    cmd = [
        env.subst("$PYTHONEXE"),
        os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"),
                     "esptool.py"),
        "--chip", "esp32c3", "merge_bin",
        "-o", out, "--flash_mode", "dio", "--flash_size", "4MB",
        # Đệm đầy tới hết dung lượng flash. Trình mô phỏng nạp thẳng file này
        # làm toàn bộ flash; ảnh ngắn thì phần còn lại không xác định.
        "--fill-flash-size", "4MB",
    ]
    for addr, path in parts:
        cmd += [addr, path]

    try:
        subprocess.run(cmd, check=True, capture_output=True)
        print("Ảnh gộp cho Wokwi: %s (%d KB)"
              % (out, os.path.getsize(out) // 1024))
    except Exception as e:  # noqa: BLE001
        print("Không gộp được ảnh flash: %s" % e)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)  # noqa: F821
