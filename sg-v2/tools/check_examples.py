#!/usr/bin/env python3
"""Compile every nhtlib example to prove it still builds.

    python3 tools/check_examples.py            # all examples
    python3 tools/check_examples.py valve net  # just these

Each example is an ordinary ESP-IDF project (components/nhtlib/examples/<name>).
This script builds them with PlatformIO in a scratch project, reusing one build
directory so the ESP-IDF itself is compiled only once.
"""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NHTLIB = os.path.join(HERE, "components", "nhtlib")
EXAMPLES = os.path.join(NHTLIB, "examples")
PIO = os.path.expanduser("~/.platformio/penv/bin/pio")


def main():
    names = sys.argv[1:] or sorted(
        d for d in os.listdir(EXAMPLES)
        if os.path.isfile(os.path.join(EXAMPLES, d, "CMakeLists.txt")))
    work = os.path.join(tempfile.gettempdir(), "nhtlib_examples")
    os.makedirs(work, exist_ok=True)

    with open(os.path.join(work, "CMakeLists.txt"), "w") as f:
        f.write("cmake_minimum_required(VERSION 3.16.0)\n"
                "list(APPEND EXTRA_COMPONENT_DIRS %s)\n"
                "include($ENV{IDF_PATH}/tools/cmake/project.cmake)\n"
                "project(nhtlib_example)\n" % NHTLIB)
    shutil.copy(os.path.join(HERE, "sdkconfig.defaults"), work)

    failed = []
    for n in names:
        src = os.path.join(EXAMPLES, n, "main")
        with open(os.path.join(work, "platformio.ini"), "w") as f:
            f.write("[platformio]\nsrc_dir = %s\n\n"
                    "[env:ex]\nplatform = espressif32@6.13.0\n"
                    "board = esp32-c3-devkitm-1\nframework = espidf\n" % src)
        print("== building example '%s'" % n, flush=True)
        r = subprocess.run([PIO, "run", "-d", work], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
        ok = r.returncode == 0
        print("   %s" % ("OK" if ok else "FAILED"), flush=True)
        if not ok:
            failed.append(n)
            print("\n".join(r.stdout.splitlines()[-25:]))
    print("\nexamples: %d built, %d failed %s" % (
        len(names) - len(failed), len(failed), failed or ""))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
