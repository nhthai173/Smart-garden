"""Merge the bootloader + partition table + app into a single flash image.

Wokwi (and every single-file flasher) needs the merged image, not firmware.bin
alone. Runs automatically after every build.
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
        # Pad to the full flash size. The simulator loads this file as the whole
        # flash; with a short image the remainder would be undefined.
        "--fill-flash-size", "4MB",
    ]
    for addr, path in parts:
        cmd += [addr, path]

    try:
        subprocess.run(cmd, check=True, capture_output=True)
        print("Merged image for Wokwi: %s (%d KB)"
              % (out, os.path.getsize(out) // 1024))
    except Exception as e:  # noqa: BLE001
        print("Could not merge the flash image: %s" % e)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)  # noqa: F821
