"""Sinh include/version.h - build number tự tăng. Port từ versioning.py của v1."""
import os

Import("env")  # noqa: F821  (PlatformIO inject)

PROJ = env.subst("$PROJECT_DIR")  # noqa: F821
BUILDNO_FILE = os.path.join(PROJ, "versioning")
VERSION_H = os.path.join(PROJ, "include", "version.h")
VERSION = "2.0."

try:
    with open(BUILDNO_FILE) as f:
        build_no = int(f.readline()) + 1
except Exception:
    build_no = 1

with open(BUILDNO_FILE, "w") as f:
    f.write(str(build_no))

content = (
    "/* File này do tools/versioning.py sinh ra. Đừng sửa tay. */\n"
    "#ifndef SG_VERSION_H\n"
    "#define SG_VERSION_H\n"
    "#define FIRMWARE_BUILD {}\n"
    '#define FIRMWARE_VERSION "{}"\n'
    "#endif\n"
).format(build_no, VERSION + str(build_no))

old = ""
if os.path.exists(VERSION_H):
    with open(VERSION_H) as f:
        old = f.read()
if old != content:
    with open(VERSION_H, "w") as f:
        f.write(content)

print("Smart Garden v2 build #{} ({}{})".format(build_no, VERSION, build_no))
