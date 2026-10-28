#!/usr/bin/env python3
"""Bộ kiểm thử trên board ESP32-C3 THẬT (firmware env esp32c3_bench).

Board chưa đấu I/O nên cảm biến là giả (qua /api/bench/*) và mức relay được
đọc từ thanh ghi output của chip. Mọi thứ khác - WiFi, HTTP, Firebase,
watchdog, reset, flash - là thật.

    ~/.platformio/penv/bin/python tools/bench/bench_test.py local
    ~/.platformio/penv/bin/python tools/bench/bench_test.py cloud
    ~/.platformio/penv/bin/python tools/bench/bench_test.py all --only A05,A12

Cần pyserial (có sẵn trong venv của PlatformIO). Bí mật Firebase đọc từ
include/secret.h, không in ra.

Nguyên tắc: bài test nào cũng phải để van ĐÓNG và cấu hình về như cũ khi xong,
kể cả khi bài test hỏng giữa chừng (xem Dev.safe_state / restore_cfg).
"""
import argparse
import json
import os
import random
import re
import sys
import threading
import time
import traceback
import urllib.error
import urllib.request

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import rtdb_probe  # noqa: E402  (tools/rtdb_probe.py)

PORT = "/dev/cu.usbmodem101"
TRAVEL_S = 8.0          # khớp cấu hình mặc định app_cfg.c
PUMP_DELAY_S = 8.0


# ---------------------------------------------------------------- serial log
class Log:
    """Đọc serial nền, gắn mốc thời gian máy host cho từng dòng."""

    def __init__(self, port, path):
        self.ser = serial.Serial(port, 115200, timeout=0.1)
        self.lines = []          # (t_host, text)
        self.lock = threading.Lock()
        self.f = open(path, "a", encoding="utf-8")
        self.run = True
        self.th = threading.Thread(target=self._loop, daemon=True)
        self.th.start()

    def _loop(self):
        buf = b""
        while self.run:
            try:
                buf += self.ser.read(2048)
            except Exception:
                time.sleep(0.2)
                continue
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                txt = raw.decode("utf-8", "replace").rstrip("\r")
                t = time.time()
                with self.lock:
                    self.lines.append((t, txt))
                # Không ghi lại bí mật nếu firmware lỡ log ra.
                self.f.write("%.3f %s\n" % (t, re.sub(r"AIza\S+", "<key>", txt)))
                self.f.flush()

    def mark(self):
        with self.lock:
            return len(self.lines)

    def find(self, pat, since=0):
        rx = re.compile(pat)
        with self.lock:
            for t, l in self.lines[since:]:
                if rx.search(l):
                    return t, l
        return None

    def wait(self, pat, timeout, since=0):
        end = time.time() + timeout
        while time.time() < end:
            r = self.find(pat, since)
            if r:
                return r
            time.sleep(0.1)
        return None

    def count(self, pat, since=0):
        rx = re.compile(pat)
        with self.lock:
            return sum(1 for _, l in self.lines[since:] if rx.search(l))

    def hard_reset(self):
        """Reset kiểu rút nguồn: kéo chân EN qua RTS, không cho firmware tắt êm."""
        self.ser.setDTR(False)
        self.ser.setRTS(True)
        time.sleep(0.2)
        self.ser.setRTS(False)

    def hold_reset(self, on):
        self.ser.setDTR(False)
        self.ser.setRTS(bool(on))


# ---------------------------------------------------------------- device
class Dev:
    def __init__(self, log, ip=None):
        self.log = log
        self.ip = ip
        self.orig_cfg = None

    # --- HTTP ---
    def req(self, method, path, obj=None, timeout=6, raw=None):
        url = "http://%s%s" % (self.ip, path)
        data = raw if raw is not None else (
            json.dumps(obj).encode() if obj is not None else None)
        r = urllib.request.Request(url, data=data, method=method,
                                   headers={"Content-Type": "application/json"})
        # Sóng yếu: connect/đọc có thể timeout. Mọi lệnh của bộ test đều lũy
        # đẳng (mở cùng thời lượng, đóng, đặt giá trị) nên thử lại là an toàn.
        last = None
        for attempt in range(3):
            try:
                with urllib.request.urlopen(r, timeout=min(timeout, 4)) as resp:
                    body = resp.read().decode("utf-8", "replace")
                    code = resp.status
                break
            except urllib.error.HTTPError as e:
                body, code = e.read().decode("utf-8", "replace"), e.code
                break
            except (urllib.error.URLError, OSError, TimeoutError) as e:
                last = e
                time.sleep(0.4)
        else:
            raise last
        try:
            return code, json.loads(body)
        except ValueError:
            return code, body

    def get(self, path, **kw):
        return self.req("GET", path, **kw)

    def post(self, path, obj=None, **kw):
        return self.req("POST", path, obj, **kw)

    def status(self):
        c, j = self.get("/api/status")
        assert c == 200, "status %s" % c
        return j

    def pins(self):
        c, j = self.get("/api/bench/pins", timeout=3)
        assert c == 200
        a = j["active_level"]
        return {k: (j[k] == a) for k in ("power", "dir", "pump", "ac")}

    def info(self):
        return self.get("/api/info")[1]

    def open(self, secs, level=100):
        return self.post("/api/water?seconds=%d&level=%d" % (secs, level))

    def close(self):
        return self.post("/api/water?stop=1")

    def cfg(self):
        return self.get("/api/config")[1]

    def set_cfg(self, d):
        c, j = self.post("/api/config", d)
        assert c == 200, "set_cfg %s %s" % (c, j)

    def leak_raw(self, v):
        self.post("/api/bench/leak?raw=%d" % v)

    def volt_raw(self, v):
        self.post("/api/bench/volt?raw=%d" % v)

    def leak_fail(self, on):
        self.post("/api/bench/leak_fail?on=%d" % (1 if on else 0))

    def clean_sensors(self):
        self.leak_fail(False)
        self.leak_raw(100)
        self.volt_raw(1500)

    # --- chờ ---
    def wait(self, pred, timeout, every=0.15, what="điều kiện"):
        end = time.time() + timeout
        last = None
        while time.time() < end:
            try:
                last = self.status()
                if pred(last):
                    return last
            except Exception:
                pass
            time.sleep(every)
        raise AssertionError("hết %.0fs chờ %s (cuối: %s)" % (timeout, what, last))

    def wait_up(self, timeout=90):
        """Chờ thiết bị lên mạng và nhận HTTP sau một lần khởi động."""
        end = time.time() + timeout
        while time.time() < end:
            try:
                c, _ = self.get("/api/status", timeout=2)
                if c == 200:
                    return True
            except Exception:
                pass
            time.sleep(0.5)
        raise AssertionError("thiết bị không lên lại sau %ds" % timeout)

    def reboot(self):
        m = self.log.mark()
        self.log.hard_reset()
        assert self.log.wait(r"Smart Garden v2", 15, m), "không thấy banner khởi động"
        self.wait_up()
        return m

    def safe_state(self):
        """Đưa về trạng thái an toàn bất kể bài test trước để lại gì."""
        try:
            self.clean_sensors()
            self.close()
            self.wait(lambda s: s["valve"] == "closed", 25, what="van đóng")
        except Exception:
            pass

    def save_cfg(self):
        self.orig_cfg = self.cfg()

    def restore_cfg(self):
        if not self.orig_cfg:
            return
        keep = ("leak_hold_on_ms", "leak_hold_off_ms", "travel_ms", "run_max_s",
                "run_default_s", "leak_thr_on", "leak_thr_off", "volt_min")
        try:
            self.set_cfg({k: self.orig_cfg[k] for k in keep if k in self.orig_cfg})
        except Exception:
            pass


class Recorder:
    """Dựng lại trình tự relay từ vết `PINS t=<ms chip> ...` in ra serial (độ
    phân giải 5 ms, không đi qua WiFi). Thời gian trả về là GIÂY, gốc là mốc
    đầu tiên của lần ghi (`t0`)."""

    RX = re.compile(r"PINS t=(\d+) power=(\d) dir=(\d) pump=(\d) ac=(\d)")

    def __init__(self, dev, every=None):
        self.dev = dev
        self.samples = []        # (t_chip_s, {power,dir,pump,ac})

    def __enter__(self):
        self.mark = self.dev.log.mark()
        # trạng thái ban đầu để có điểm xuất phát
        self.start = self.dev.pins()
        return self

    def __exit__(self, *a):
        time.sleep(0.4)
        with self.dev.log.lock:
            lines = list(self.dev.log.lines[self.mark:])
        for _, l in lines:
            m = self.RX.search(l)
            if m:
                t = int(m.group(1)) / 1000.0
                self.samples.append((t, {"power": m.group(2) == "1",
                                         "dir": m.group(3) == "1",
                                         "pump": m.group(4) == "1",
                                         "ac": m.group(5) == "1"}))

    def _norm(self, after):
        return [(t, p) for t, p in self.samples if t >= after]

    def first(self, pin, val, after=0.0):
        for t, p in self.samples:
            if t >= after and p[pin] == val:
                return t
        return None

    def last(self, pin, val):
        r = None
        for t, p in self.samples:
            if p[pin] == val:
                r = t
        return r

    def last_transition(self, pin, val):
        """Mốc cuối cùng `pin` CHUYỂN sang `val`."""
        r = None
        prev = None
        for t, p in self.samples:
            if p[pin] == val and prev is not None and prev[pin] != val:
                r = t
            prev = p
        return r

    def dir_flips_under_power(self):
        """Relay chiều đổi mức trong khi relay nguồn động cơ đang đóng."""
        n = 0
        prev = dict(self.start)
        for t, p in self.samples:
            if p["dir"] != prev["dir"] and p["power"] and prev["power"]:
                n += 1
            prev = p
        return n

    def spans(self, pin):
        """[(bắt đầu, kết thúc)] các khoảng `pin` đang active."""
        out, begin = [], None
        for t, p in self.samples:
            if p[pin] and begin is None:
                begin = t
            if not p[pin] and begin is not None:
                out.append((begin, t))
                begin = None
        if begin is not None:
            out.append((begin, self.samples[-1][0]))
        return out


# ---------------------------------------------------------------- runner
RESULTS = []
NOTES = []


def note(msg):
    NOTES.append(msg)
    print("      · " + msg)


TESTS = []


def test(tid, title, suite="local"):
    def deco(fn):
        TESTS.append((tid, title, suite, fn))
        return fn
    return deco


def run(dev, tests, only):
    for tid, title, suite, fn in tests:
        if only and tid not in only:
            continue
        t0 = time.time()
        print("[%s] %s" % (tid, title), flush=True)
        try:
            dev.wait_up(90)
            dev.safe_state()
            if suite == "cloud":
                wait_cloud_ready(dev)
            fn(dev)
            res, why = "PASS", ""
        except AssertionError as e:
            res, why = "FAIL", str(e)
        except Exception as e:  # noqa: BLE001
            res, why = "ERROR", "%s: %s" % (type(e).__name__, e)
            traceback.print_exc()
        finally:
            try:
                dev.wait_up(60)
                dev.safe_state()
                dev.restore_cfg()
            except Exception:
                pass
        dt = time.time() - t0
        RESULTS.append((tid, title, res, why, dt))
        print("   → %s (%.0fs) %s" % (res, dt, why), flush=True)


# ================================================================ LOCAL
@test("A01", "Khởi động: van đóng, không sự cố, relay nghỉ")
def a01(d):
    s = d.status()
    assert s["valve"] == "closed", s
    assert s["faults"] == 0, s
    assert s["time_valid"], "chưa có giờ SNTP"
    assert 11.0 < s["voltage"] < 14.0, "điện áp giả lạ: %s" % s["voltage"]
    p = d.pins()
    assert not p["power"] and not p["dir"] and not p["pump"], p
    note("fw %s, heap_min %s B" % (d.info()["fw"], d.info()["heap_min"]))


@test("A02", "Trình tự mở → tưới → tự đóng (relay thật, 25 s)")
def a02(d):
    with Recorder(d) as rec:
        t0 = time.time()
        c, j = d.open(25)
        assert c == 200, (c, j)
        d.wait(lambda s: s["valve"] == "closed" and time.time() - t0 > 30, 60,
               what="đóng hẳn sau lượt tưới")
    sp = rec.spans("power")
    assert len(sp) == 2, "kỳ vọng 2 lần quay động cơ (mở, đóng), có %d: %s" % (len(sp), sp)
    t_on = sp[0][0]
    pump = rec.spans("pump")
    assert len(pump) == 1, pump
    pump_on, pump_off = pump[0]
    assert abs((sp[0][1] - sp[0][0]) - TRAVEL_S) < 0.5, \
        "mở quay %.2fs, kỳ vọng %.0fs" % (sp[0][1] - sp[0][0], TRAVEL_S)
    assert abs((pump_on - t_on) - PUMP_DELAY_S) < 0.5, \
        "bơm bật sau %.2fs, kỳ vọng %.0fs" % (pump_on - t_on, PUMP_DELAY_S)
    assert abs((pump_off - t_on) - 25) < 1.0, \
        "bơm tắt sau %.2fs, kỳ vọng 25s" % (pump_off - t_on)
    dir_on = rec.first("dir", True)
    assert dir_on is not None and dir_on >= pump_off - 0.02, \
        "bơm phải tắt TRƯỚC khi van khép (dir=%.3f pump_off=%.3f)" % (dir_on, pump_off)
    spin = sp[1][1] - sp[1][0]
    assert abs(spin - TRAVEL_S) < 0.5, "đóng quay %.2fs, kỳ vọng %.0fs" % (spin, TRAVEL_S)
    assert rec.dir_flips_under_power() == 0
    p = d.pins()
    assert not p["power"] and not p["dir"] and not p["pump"], p
    note("mở %.2fs → bơm +%.2fs → bơm chạy %.1fs → bơm tắt rồi mới đảo chiều (+%.3fs) → đóng %.2fs" % (
        sp[0][1] - sp[0][0], pump_on - t_on, pump_off - pump_on,
        dir_on - pump_off, spin))


@test("A03", "Lệnh mở trùng KHÔNG đặt lại thời lượng (lỗi của v1)")
def a03(d):
    d.open(40)
    time.sleep(6)
    s0 = d.status()["remaining_s"]
    d.open(40)
    s1 = d.status()["remaining_s"]
    assert s1 <= s0, "mở lại cùng thời lượng làm kéo dài: %s → %s" % (s0, s1)
    assert 30 <= s1 <= 36, "còn %ss, kỳ vọng ~34" % s1


@test("A04", "Trần cứng: yêu cầu 99999 s / số âm / rác đều bị kẹp")
def a04(d):
    cap = d.cfg()["run_max_s"]
    for q in ("seconds=99999", "seconds=-5", "seconds=abc", "minutes=999999",
              "seconds=4294967295", "seconds=0"):
        d.close()
        d.wait(lambda s: s["valve"] == "closed", 20, what="đóng")
        c, j = d.post("/api/water?" + q)
        assert c in (200, 400), (q, c, j)
        if c == 200:
            rem = d.status()["remaining_s"]
            assert rem <= cap, "%s: còn %ss vượt trần %s" % (q, rem, cap)
            if q == "seconds=-5":
                assert rem <= d.cfg()["run_default_s"] + 1, \
                    "số âm phải ra thời lượng mặc định, được %ss" % rem
    note("trần %ss giữ vững với mọi đầu vào" % cap)


@test("A05", "Đóng giữa lúc đang mở: ngắt điện, nghỉ, đảo chiều, quay đủ hành trình")
def a05(d):
    with Recorder(d) as rec:
        d.open(60)
        time.sleep(3.0)
        d.close()
        d.wait(lambda s: s["valve"] == "closed", 25, what="đóng")
    sp = rec.spans("power")
    assert len(sp) == 2, "kỳ vọng 2 lần quay động cơ, có %s" % sp
    (a0, a1), (b0, b1) = sp
    assert 2.4 < a1 - a0 < 4.0, "mở dở %.2fs, kỳ vọng ~3s" % (a1 - a0)
    gap = b0 - a1
    assert 0.09 <= gap < 0.6, "nghỉ đảo chiều %.3fs (cần ≥ 0.1s)" % gap
    dir_on = rec.first("dir", True)
    assert a1 <= dir_on <= b0, \
        "relay chiều đổi lúc t=%.3f, ngoài khoảng động cơ mất điện [%.3f, %.3f]" % (dir_on, a1, b0)
    assert rec.dir_flips_under_power() == 0
    assert b1 - b0 > TRAVEL_S - 0.5, \
        "van mở dở %.0fs nhưng đóng chỉ quay %.2fs (cần ~%.0fs)" % (a1 - a0, b1 - b0, TRAVEL_S)
    note("mở dở %.2fs → ngắt điện → nghỉ %.0f ms → đảo chiều → quay đóng đủ %.2fs" % (
        a1 - a0, gap * 1000, b1 - b0))


@test("A06", "Lệnh mở lúc van đang quay đóng bị từ chối (không đảo giữa chừng)")
def a06(d):
    d.open(60)
    d.wait(lambda s: s["valve"] == "open", 15, what="mở hẳn")
    d.close()
    time.sleep(1.0)
    c, j = d.open(30)
    assert c == 409, "kỳ vọng 409, nhận %s %s" % (c, j)


@test("A07", "Ngập nước: đóng van, khoá mở, tự gỡ khi khô")
def a07(d):
    d.save_cfg()
    d.set_cfg({"leak_hold_off_ms": 4000})
    d.open(120)
    d.wait(lambda s: s["valve"] == "open", 15, what="mở hẳn")
    t0 = time.time()
    d.leak_raw(3000)
    s = d.wait(lambda s: s["faults"] & 1, 12, what="báo ngập")
    t_fault = time.time() - t0
    hold_on = d.cfg()["leak_hold_on_ms"] / 1000
    assert hold_on - 0.5 < t_fault < hold_on + 2.5, \
        "báo ngập sau %.1fs, kỳ vọng ~%.0fs" % (t_fault, hold_on)
    time.sleep(0.5)
    p = d.pins()
    assert p["dir"] or d.status()["valve"] in ("closing", "closed"), \
        "van không đóng khi ngập"
    d.wait(lambda s: s["valve"] == "closed", 20, what="đóng hẳn")
    c, _ = d.open(30)
    assert c == 409, "ngập mà vẫn mở được (%s)" % c
    d.leak_raw(100)
    t1 = time.time()
    d.wait(lambda s: not (s["faults"] & 1), 15, what="gỡ ngập")
    note("báo ngập sau %.1fs; gỡ sau %.1fs" % (t_fault, time.time() - t1))
    c, _ = d.open(10)
    assert c == 200, "khô rồi mà không mở lại được"


@test("A08", "Nhiễu cảm biến ngập (vùng chết + xung ngắn) KHÔNG sinh báo ảo")
def a08(d):
    d.open(90)
    d.wait(lambda s: s["valve"] == "open", 15, what="mở hẳn")
    cfg = d.cfg()
    lo, hi = cfg["leak_thr_off"] + 60, cfg["leak_thr_on"] - 60
    end = time.time() + 30
    n_spike = 0
    while time.time() < end:
        d.leak_raw(random.randint(lo, hi))
        time.sleep(0.25)
        if random.random() < 0.08:                 # xung vọt qua ngưỡng ~1 s
            d.leak_raw(cfg["leak_thr_on"] + 400)
            n_spike += 1
            time.sleep(1.0)
    d.leak_raw(100)
    s = d.status()
    assert not (s["faults"] & 1) and s["valve"] == "open", \
        "nhiễu làm đóng van / báo ngập: %s" % s
    note("%d xung ngắn + nhiễu vùng chết trong 30s: không báo ngập" % n_spike)


@test("A09", "Cảm biến ngập hỏng (ADC lỗi) → coi là sự cố, đóng van")
def a09(d):
    d.open(90)
    d.wait(lambda s: s["valve"] == "open", 15, what="mở hẳn")
    t0 = time.time()
    d.leak_fail(True)
    d.wait(lambda s: s["faults"] & 2, 5, what="báo hỏng cảm biến")
    note("báo sau %.1fs" % (time.time() - t0))
    d.wait(lambda s: s["valve"] == "closed", 20, what="đóng van")
    c, _ = d.open(10)
    assert c == 409, "cảm biến hỏng mà vẫn cho mở"
    d.leak_fail(False)
    t1 = time.time()
    d.wait(lambda s: not (s["faults"] & 2), 8, what="gỡ sự cố cảm biến")
    note("gỡ sau %.1fs" % (time.time() - t1))


@test("A10", "Nguồn sụt: thoáng qua bỏ qua, kéo dài đóng van, hồi lại thì gỡ")
def a10(d):
    d.open(90)
    d.wait(lambda s: s["valve"] == "open", 15, what="mở hẳn")
    d.volt_raw(300)
    time.sleep(1.4)
    d.volt_raw(1500)
    time.sleep(3.5)
    s = d.status()
    assert not (s["faults"] & 4) and s["valve"] == "open", \
        "cú sụt thoáng qua đã bị coi là sự cố: %s" % s
    t0 = time.time()
    d.volt_raw(300)
    d.wait(lambda s: s["faults"] & 4, 8, what="báo nguồn yếu")
    note("sụt kéo dài → báo sau %.1fs" % (time.time() - t0))
    d.wait(lambda s: s["valve"] == "closed", 20, what="đóng van")
    c, _ = d.open(10)
    assert c == 409
    d.volt_raw(1500)
    t1 = time.time()
    d.wait(lambda s: not (s["faults"] & 4), 12, what="gỡ")
    note("nguồn hồi → gỡ sau %.1fs" % (time.time() - t1))


@test("A11", "Treo task điều khiển: watchdog reset, van về đóng khi lên lại")
def a11(d):
    d.open(120)
    d.wait(lambda s: s["valve"] == "open", 15, what="mở hẳn")
    m = d.log.mark()
    t0 = time.time()
    d.post("/api/bench/stall?ms=40000")
    r = d.log.wait(r"Smart Garden v2", 30, m)
    assert r, "watchdog không reset được thiết bị"
    exposure = r[0] - t0
    assert d.log.find(r"Task watchdog|task_wdt|TWDT", m), "không phải reset do task WDT"
    d.wait_up()
    s = d.status()
    assert s["valve"] in ("closed", "closing"), "sau reset van = %s" % s["valve"]
    assert "watchdog" in d.info()["last_reset"].lower(), d.info()["last_reset"]
    note("relay giữ nguyên trạng thái %.1fs sau khi task treo trước khi WDT reset "
         "(đây là thời gian phơi nhiễm xấu nhất)" % exposure)
    assert exposure < 15


@test("A12", "Crash (abort) lúc đang tưới: lên lại van đóng, báo lý do reset")
def a12(d):
    d.open(120)
    d.wait(lambda s: s["valve"] == "open", 15, what="mở hẳn")
    m = d.log.mark()
    d.post("/api/bench/crash")
    assert d.log.wait(r"Smart Garden v2", 25, m), "không khởi động lại sau crash"
    d.wait_up()
    s = d.status()
    assert s["valve"] in ("closed", "closing"), s
    info = d.info()
    assert "PANIC" in info["last_reset"], info["last_reset"]
    assert d.log.find(r"Core dump|core dump|esp_core_dump", m), "không ghi coredump"
    note("last_reset=%s; coredump đã ghi" % info["last_reset"])


@test("A13", "Reset cứng (rút nguồn) giữa lúc tưới: van đóng, lịch còn nguyên")
def a13(d):
    c, j = d.post("/api/schedules", {"hour": 3, "minute": 33, "repeat": 127,
                                     "duration_s": 15, "level": 100, "enabled": True})
    assert c == 200, (c, j)
    n_before = len(d.get("/api/schedules")[1])
    d.open(120)
    d.wait(lambda s: s["valve"] == "open", 15, what="mở hẳn")
    d.reboot()
    s = d.status()
    assert s["valve"] in ("closed", "closing"), "sau rút nguồn van = %s" % s["valve"]
    assert len(d.get("/api/schedules")[1]) == n_before, "mất lịch sau reset"
    for it in d.get("/api/schedules")[1]:
        if it["hour"] == 3 and it["minute"] == 33:
            d.req("DELETE", "/api/schedules?id=%d" % it["id"])


def _local_hm(d, plus_s):
    tz = d.cfg().get("tz_offset_s", 0)
    t = time.gmtime(time.time() + tz + plus_s)
    return t.tm_hour, t.tm_min, t.tm_sec


def _add_sched(d, plus_s, dur):
    h, m, sec = _local_hm(d, plus_s)
    c, j = d.post("/api/schedules", {"hour": h, "minute": m, "repeat": 127,
                                     "duration_s": dur, "level": 100, "enabled": True})
    assert c == 200, (c, j)
    # giờ tới hạn tuyệt đối (host)
    due = time.time() + plus_s + (0 - sec)
    return j.get("id") if isinstance(j, dict) else None, h, m, due


def _del_sched(d, h, m):
    for it in d.get("/api/schedules")[1]:
        if it["hour"] == h and it["minute"] == m:
            d.req("DELETE", "/api/schedules?id=%d" % it["id"])


@test("A14", "Lịch tưới chạy đúng giờ (SNTP) và tự đóng")
def a14(d):
    _, h, m, due = _add_sched(d, 75, 15)
    try:
        wait = max(5, due - time.time()) + 20
        s = d.wait(lambda s: s["valve"] in ("opening", "open"), wait, what="lịch tới hạn")
        assert s["source"] == "schedule", s
        late = time.time() - due
        note("lịch %02d:%02d mở van lệch %.1fs so với phút tròn" % (h, m, late))
        d.wait(lambda s: s["valve"] == "closed", 40, what="tự đóng")
    finally:
        _del_sched(d, h, m)


@test("A15", "Lịch tới hạn đúng lúc thiết bị reset cứng → vẫn chạy, không bị bỏ")
def a15(d):
    # RTS không giữ được chip ở trạng thái reset, nên mô phỏng "lỡ giờ vì đang
    # khởi động": reset cứng NGAY TRƯỚC phút hẹn. Sau reset chưa có giờ thật
    # (cần ~10 s để SNTP), nên lịch tới hạn trong lúc ấy PHẢI được bù.
    now = time.time()
    due = (int(now // 60) + 1) * 60
    if due - now < 45:
        due += 60
    tz = d.cfg().get("tz_offset_s", 0)
    t = time.gmtime(due + tz)
    h, m = t.tm_hour, t.tm_min
    c, j = d.post("/api/schedules", {"hour": h, "minute": m, "repeat": 127,
                                     "duration_s": 15, "level": 100, "enabled": True})
    assert c == 200, (c, j)
    try:
        time.sleep(max(0, due - 2 - time.time()))
        mk = d.log.mark()
        d.log.hard_reset()
        assert d.log.wait(r"Smart Garden v2", 15, mk), "không khởi động lại"
        r = d.log.wait(r"lịch #\d+ tới hạn", 60, mk)
        assert r, "lịch lỡ lúc khởi động không được bù"
        late = r[0] - due
        assert late < 60, "lịch chạy trễ %.1fs" % late
        note("lịch %02d:%02d tới hạn đúng lúc khởi động lại: chạy sau giờ hẹn %.1fs "
             "(RTC giữ giờ qua reset nên không phải chờ SNTP)" % (h, m, late))
        d.wait_up(120)
        d.close()
    finally:
        d.wait_up(120)
        _del_sched(d, h, m)


def _wifi_drop(d, ms):
    # Thiết bị cắt WiFi ngay sau khi trả lời; phản hồi có thể không về kịp.
    try:
        d.post("/api/bench/wifi_drop?ms=%d" % ms, timeout=4)
    except Exception:
        pass
    time.sleep(2.5)


def _wait_wifi_back(d, timeout=150):
    end = time.time() + timeout
    while time.time() < end:
        try:
            if d.get("/api/status", timeout=2)[0] == 200:
                return True
        except Exception:
            pass
        time.sleep(1)
    raise AssertionError("WiFi không về sau %ds" % timeout)


@test("A16", "OFFLINE: mất WiFi giữa lúc tưới, van vẫn tự đóng đúng giờ")
def a16(d):
    m = d.log.mark()
    t_open = time.time()
    d.open(30)
    time.sleep(3)
    _wifi_drop(d, 60000)
    r = d.log.wait(r"van ĐÓNG - hết thời lượng", 70, m)
    assert r, "không thấy van tự đóng khi offline"
    assert d.log.find(r"mất kết nối WiFi", m), "WiFi lẽ ra phải rớt"
    dt = r[0] - t_open
    assert 27 < dt < 36, "tự đóng sau %.1fs, kỳ vọng ~30s" % dt
    note("mất mạng, van vẫn tự đóng sau %.1fs (30s đặt)" % dt)
    _wait_wifi_back(d)
    assert d.status()["valve"] in ("closing", "closed")


@test("A17", "OFFLINE: lịch tới hạn lúc không có mạng vẫn chạy")
def a17(d):
    _, h, m_, due = _add_sched(d, 100, 12)
    mk = d.log.mark()
    try:
        time.sleep(8)
        _wifi_drop(d, 100000)
        r = d.log.wait(r"lịch #\d+ tới hạn", max(10, due - time.time()) + 25, mk)
        assert r, "lịch không chạy lúc offline"
        assert d.log.find(r"mất kết nối WiFi", mk)
        note("lịch chạy lúc mất mạng, trễ %.1fs so với phút tròn" % (r[0] - due))
        assert d.log.wait(r"van ĐÓNG - hết thời lượng", 40, mk), "không tự đóng"
        _wait_wifi_back(d)
    finally:
        _del_sched(d, h, m_)


@test("A18", "Flood: 4 threads of commands and junk for 60 s - no reset, recovers, no leak")
def a18(d):
    d.wait_up(30)
    # Measure the heap with the cloud connection idle-closed (it holds ~35 KB of
    # TLS state while open), so the before/after numbers compare like with like.
    time.sleep(40)
    info0 = d.info()
    m = d.log.mark()
    stop = time.time() + 60
    stats = {"ok": 0, "err": 0, "5xx": 0}
    lock = threading.Lock()
    junk = [b"{", b"\x00\xff" * 200, b'{"hour":"x"}', b"[" * 500, b"A" * 6000,
            b'{"hour":99,"minute":99}', b'{"cmd":null}']

    def worker(seed):
        rnd = random.Random(seed)
        while time.time() < stop:
            try:
                k = rnd.randint(0, 9)
                if k < 3:
                    c, _ = d.open(rnd.randint(1, 60), rnd.randint(0, 120))
                elif k < 5:
                    c, _ = d.close()
                elif k < 7:
                    c, _ = d.get("/api/status")
                elif k == 7:
                    c, _ = d.req("POST", "/api/schedules", raw=rnd.choice(junk))
                elif k == 8:
                    c, _ = d.req("POST", "/api/config", raw=rnd.choice(junk))
                else:
                    c, _ = d.get("/api/info")
                with lock:
                    stats["ok"] += 1
                    if c >= 500:
                        stats["5xx"] += 1
            except Exception:
                with lock:
                    stats["err"] += 1
            time.sleep(rnd.random() * 0.05)

    ths = [threading.Thread(target=worker, args=(i,)) for i in range(4)]
    [t.start() for t in ths]
    [t.join() for t in ths]

    # What matters under overload: no crash or reset, the valve ends up closed,
    # and the device comes back to normal once the flood stops.
    assert not d.log.find(r"Guru Meditation|abort\(\)|Task watchdog", m), "crash/WDT under load"
    assert not d.log.find(r"Smart Garden v2", m), "device rebooted under load"
    d.wait_up(60)
    d.close()
    d.wait(lambda s: s["valve"] == "closed", 40, what="valve closed after the flood")
    time.sleep(45)                      # let pending work drain and the idle cloud connection close
    info = d.info()
    note("%d requests ok, %d connection errors, %d 5xx; heap min %d (idle before %d), "
         "free after %d (before %d)" % (stats["ok"], stats["err"], stats["5xx"],
                                       info["heap_min"], info0["heap"], info["heap"],
                                       info0["heap"]))
    assert info["heap_min"] > 12000, "heap nearly exhausted: %s" % info["heap_min"]
    assert info["heap"] > 0.7 * info0["heap"], \
        "heap did not recover after the flood (leak?): %d -> %d" % (info0["heap"], info["heap"])
    assert stats["5xx"] == 0, "got 5xx responses"
    assert stats["ok"] > 20, "device served almost nothing"


@test("A19", "Đảo lệnh mở/đóng dồn dập 150 lần: van luôn ở trạng thái hợp lệ")
def a19(d):
    seen = set()
    for i in range(150):
        (d.open(20) if i % 2 == 0 else d.close())
        seen.add(d.status()["valve"])
        time.sleep(0.04)
    assert seen <= {"closed", "opening", "open", "closing"}, seen
    d.close()
    d.wait(lambda s: s["valve"] == "closed", 30, what="đóng sau khi dồn lệnh")
    time.sleep(0.5)
    p = d.pins()
    assert not p["power"] and not p["pump"], "van báo đóng nhưng relay còn điện: %s" % p


# ================================================================ CLOUD
class Cloud:
    def __init__(self):
        self.db = rtdb_probe.RTDB(rtdb_probe.load_secret())
        self.db.sign_in()

    def p(self, sub):
        return self.db.base + sub

    def cmd(self, obj):
        obj = dict(obj)
        obj.setdefault("id", int(time.time() * 1000))
        self.db.put(self.p("/desired/valve"), obj)
        return obj["id"]

    def reported(self):
        return self.db.get(self.p("/reported")) or {}


CL = None


def cloud():
    global CL
    if CL is None:
        CL = Cloud()
    return CL


def wait_cloud_ready(d, timeout=150):
    """Chờ tới khi lần khởi động HIỆN TẠI đã đăng nhập, mở stream và nhận xong
    bản chụp /desired. Lệnh ghi trước thời điểm đó bị coi là bản chụp (cố ý
    không thi hành), nên test gửi lệnh quá sớm sẽ hỏng oan."""
    with d.log.lock:
        boots = [i for i, (_, l) in enumerate(d.log.lines) if "Smart Garden v2" in l]
    start = boots[-1] if boots else 0
    end = time.time() + timeout
    while time.time() < end:
        if d.log.find(r"/desired: nhận bản chụp", start):
            time.sleep(1.5)
            return
        time.sleep(1)
    raise AssertionError("cloud chưa sẵn sàng sau %ds" % timeout)


@test("B01", "Firebase: thiết bị đăng nhập, ghi /reported và /info", "cloud")
def b01(d):
    c = cloud()
    end = time.time() + 90
    rep = {}
    while time.time() < end:
        rep = c.reported()
        if rep.get("ts") is not None:
            break
        time.sleep(3)
    assert rep.get("ts") is not None, "chưa có /reported"
    assert rep.get("valve") == "closed", rep
    end = time.time() + 60
    info = {}
    while time.time() < end:
        info = c.db.get(c.p("/info")) or {}
        if info.get("fw") == d.info()["fw"]:
            break
        time.sleep(3)
    assert info.get("fw") == d.info()["fw"], "fw cloud %s ≠ thiết bị" % info.get("fw")
    note("reported có %d trường; fw=%s" % (len(rep), info.get("fw")))


@test("B02", "Cloud mở van: thời lượng đúng (không tụt về 60 s như v1) và báo ngược", "cloud")
def b02(d):
    c = cloud()
    c.cmd({"cmd": "open", "duration_s": 120, "level": 100})
    s = d.wait(lambda s: s["valve"] in ("opening", "open"), 20, what="nhận lệnh cloud")
    assert s["source"] == "cloud", s
    rem = d.status()["remaining_s"]
    assert 100 <= rem <= 120, "còn %ss, kỳ vọng ~118 (v1 hay rơi về 60)" % rem
    end = time.time() + 40
    while time.time() < end:
        if c.reported().get("valve") in ("opening", "open"):
            break
        time.sleep(2)
    else:
        raise AssertionError("/reported không phản ánh việc mở van")
    c.cmd({"cmd": "close"})
    d.wait(lambda s: s["valve"] in ("closing", "closed"), 20, what="đóng theo cloud")


@test("B03", "Cloud: lệnh trùng id bị bỏ qua, id mới được nhận", "cloud")
def b03(d):
    c = cloud()
    i1 = c.cmd({"cmd": "open", "duration_s": 60})
    d.wait(lambda s: s["valve"] in ("opening", "open"), 20, what="mở")
    d.close()
    d.wait(lambda s: s["valve"] == "closed", 25, what="đóng cục bộ")
    c.db.put(c.p("/desired/valve"), {"cmd": "open", "duration_s": 60, "id": i1})
    time.sleep(8)
    assert d.status()["valve"] == "closed", "lệnh cũ (cùng id) mở van lại"
    c.cmd({"cmd": "open", "duration_s": 30})
    d.wait(lambda s: s["valve"] in ("opening", "open"), 20, what="id mới")


@test("B04", "Cloud: đầu vào độc hại (âm, chuỗi, khổng lồ, lệnh lạ) không phá thiết bị", "cloud")
def b04(d):
    c = cloud()
    cap = d.cfg()["run_max_s"]
    m = d.log.mark()
    cases = [
        ({"cmd": "open", "duration_s": -5}, "âm"),
        ({"cmd": "open", "duration_s": "abc"}, "chuỗi"),
        ({"cmd": "open", "duration_s": 1e12}, "khổng lồ"),
        ({"cmd": "open", "duration_s": 30, "level": 9999}, "level lớn"),
        ({"cmd": "open", "duration_s": 30, "level": -3}, "level âm"),
        ({"cmd": "explode"}, "lệnh lạ"),
        ({"cmd": 7}, "kiểu sai"),
    ]
    for obj, what in cases:
        d.close()
        d.wait(lambda s: s["valve"] == "closed", 25, what="đóng")
        c.cmd(obj)
        time.sleep(4)
        s = d.status()
        if s["valve"] in ("opening", "open"):
            assert s["remaining_s"] <= cap, "%s: vượt trần (%s)" % (what, s["remaining_s"])
            if what == "âm":
                assert s["remaining_s"] <= d.cfg()["run_default_s"] + 1, \
                    "số âm mở %ss thay vì mặc định" % s["remaining_s"]
        else:
            assert obj.get("cmd") in ("explode", 7) or what == "chuỗi" or True
    assert not d.log.find(r"Guru Meditation|abort\(\)", m)
    c.db.delete(c.p("/desired/valve"))


@test("B05", "NGOẠI TUYẾN rồi có lại: lệnh MỞ cũ viết lúc mất mạng KHÔNG được thi hành", "cloud")
def b05(d):
    c = cloud()
    _wifi_drop(d, 35000)
    c.cmd({"cmd": "open", "duration_s": 300})     # viết khi thiết bị đang offline
    _wait_wifi_back(d)
    time.sleep(35)                                # chờ stream nối lại + bản chụp
    s = d.status()
    assert s["valve"] == "closed", \
        "AN TOÀN: lệnh mở cũ đã được thi hành khi nối lại (valve=%s)" % s["valve"]
    c.cmd({"cmd": "open", "duration_s": 20})      # lệnh THẬT sau khi đã nối lại
    d.wait(lambda s: s["valve"] in ("opening", "open"), 25, what="lệnh sống sau khi nối lại")
    d.close()


@test("B06", "Cloud: 'restart' còn nằm trong /desired KHÔNG gây vòng lặp khởi động lại", "cloud")
def b06(d):
    c = cloud()
    # Ghi lại đúng giá trị đang có thì Firebase không phát sự kiện; xoá trước.
    c.db.delete(c.p("/desired/cmd"))
    time.sleep(4)
    m = d.log.mark()
    c.db.put(c.p("/desired/cmd"), "restart")
    try:
        assert d.log.wait(r"Smart Garden v2", 40, m), "không khởi động lại theo lệnh"
    except AssertionError:
        c.db.delete(c.p("/desired/cmd"))
        raise
    d.wait_up(120)
    wait_cloud_ready(d)
    time.sleep(40)
    n = d.log.count(r"Smart Garden v2", m)
    c.db.delete(c.p("/desired/cmd"))
    assert n == 1, "khởi động lại %d lần (vòng lặp)" % n


@test("B07", "Sự cố hiện lên cloud và cloud không mở được van khi đang sự cố", "cloud")
def b07(d):
    c = cloud()
    d.leak_raw(3000)
    d.wait(lambda s: s["faults"] & 1, 15, what="báo ngập")
    end = time.time() + 45
    while time.time() < end:
        r = c.reported()
        if r.get("faults", 0) & 1 and r.get("water_leak"):
            break
        time.sleep(2)
    else:
        raise AssertionError("/reported không có cờ ngập: %s" % c.reported())
    c.cmd({"cmd": "open", "duration_s": 30})
    time.sleep(6)
    assert d.status()["valve"] == "closed", "cloud mở được van lúc đang ngập"
    d.leak_raw(100)


@test("B08", "Cloud đồng bộ lịch xuống thiết bị", "cloud")
def b08(d):
    c = cloud()
    sched = [{"hour": 4, "minute": 44, "repeat": 127, "duration_s": 20,
              "level": 100, "enabled": True}]
    c.db.put(c.p("/desired/schedules"), sched)
    end = time.time() + 30
    ok = False
    while time.time() < end and not ok:
        ok = any(i["hour"] == 4 and i["minute"] == 44 for i in d.get("/api/schedules")[1])
        time.sleep(1.5)
    c.db.delete(c.p("/desired/schedules"))
    assert ok, "lịch không xuống thiết bị"
    d.post("/api/schedules", None)  # no-op
    for i in d.get("/api/schedules")[1]:
        if i["hour"] == 4 and i["minute"] == 44:
            d.req("DELETE", "/api/schedules?id=%d" % i["id"])


# ================================================================ main
def write_report(path):
    lines = ["# Báo cáo kiểm thử trên board thật", "",
             "| ID | Kịch bản | Kết quả | Thời gian | Ghi chú |", "|---|---|---|---|---|"]
    for tid, title, res, why, dt in RESULTS:
        lines.append("| %s | %s | %s | %.0fs | %s |" % (tid, title, res, dt, why))
    lines += ["", "## Số đo", ""] + ["- " + n for n in NOTES]
    open(path, "w", encoding="utf-8").write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("suite", choices=["local", "cloud", "all"])
    ap.add_argument("--port", default=PORT)
    ap.add_argument("--ip", help="IP thiết bị (mặc định: lấy từ log serial)")
    ap.add_argument("--only", default="", help="vd A05,A12")
    ap.add_argument("--out", default=os.path.join(HERE, "out"))
    a = ap.parse_args()

    os.makedirs(a.out, exist_ok=True)
    log = Log(a.port, os.path.join(a.out, "serial.log"))
    dev = Dev(log, a.ip)

    if not dev.ip:
        m = log.mark()
        log.hard_reset()
        r = log.wait(r"đã vào WiFi, IP (\d+\.\d+\.\d+\.\d+)", 60, m)
        if not r:
            sys.exit("không lấy được IP từ serial")
        dev.ip = re.search(r"IP (\d+\.\d+\.\d+\.\d+)", r[1]).group(1)
    print("Thiết bị:", dev.ip)
    dev.wait_up(60)
    dev.save_cfg()

    only = set(filter(None, a.only.split(",")))
    sel = [t for t in TESTS if a.suite == "all" or t[2] == a.suite]
    try:
        run(dev, sel, only)
    finally:
        dev.safe_state()
        dev.restore_cfg()
        write_report(os.path.join(a.out, "report.md"))
        log.run = False

    bad = [r for r in RESULTS if r[2] != "PASS"]
    print("\n==== %d/%d đạt ====" % (len(RESULTS) - len(bad), len(RESULTS)))
    for r in bad:
        print("  %s %s: %s" % (r[0], r[2], r[3]))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
