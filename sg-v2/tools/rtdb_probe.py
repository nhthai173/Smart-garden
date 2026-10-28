#!/usr/bin/env python3
"""
Dò Firebase RTDB bằng ĐÚNG các endpoint mà firmware dùng.

Mục đích không phải để quản trị dữ liệu, mà để kiểm chứng những giả định mà
code C trong components/nhtlib/nht_fbrtdb dựa vào:

  - signInWithPassword trả về idToken/refreshToken/localId
  - REST PUT/PATCH/GET trên <db>/<path>.json?auth=<idToken>
  - stream SSE thực sự trả 307 và chuyển sang host khác  <-- điểm mấu chốt
  - định dạng khung sự kiện khớp với bộ tách nht_sse

Dùng:
    python3 tools/rtdb_probe.py auth
    python3 tools/rtdb_probe.py tree
    python3 tools/rtdb_probe.py get  /reported
    python3 tools/rtdb_probe.py open 900          # ra lệnh tưới 15 phút
    python3 tools/rtdb_probe.py close
    python3 tools/rtdb_probe.py watch             # nghe stream như thiết bị
    python3 tools/rtdb_probe.py selftest          # kiểm chứng toàn bộ giả định
"""
import argparse
import http.client
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SECRET = os.path.join(HERE, "include", "secret.h")


def load_secret():
    if not os.path.exists(SECRET):
        sys.exit("Không thấy %s" % SECRET)
    v = {}
    for line in open(SECRET):
        m = re.match(r'\s*#define\s+(\w+)\s+"([^"]*)"', line)
        if m:
            v[m.group(1)] = m.group(2)
    for k in ("SECRET_FB_API_KEY", "SECRET_FB_DB_URL",
              "SECRET_FB_EMAIL", "SECRET_FB_PASS"):
        if not v.get(k):
            sys.exit("secret.h thiếu %s" % k)
    return v


def post_json(url, obj):
    req = urllib.request.Request(
        url, data=json.dumps(obj).encode(),
        headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.load(r)


def post_form(url, fields):
    req = urllib.request.Request(
        url, data=urllib.parse.urlencode(fields).encode(),
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        method="POST")
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.load(r)


class RTDB:
    def __init__(self, s):
        self.api = s["SECRET_FB_API_KEY"]
        self.db = s["SECRET_FB_DB_URL"].rstrip("/")
        self.email = s["SECRET_FB_EMAIL"]
        self.password = s["SECRET_FB_PASS"]
        self.device = s.get("SECRET_FB_DEVICE_ID", "sg-v2-dev")
        self.token = None
        self.refresh = None
        self.uid = None
        self.expires = 0

    # --- auth (giống hệt nht_fbrtdb.c: sign_in / refresh_token) ---

    def sign_in(self):
        d = post_json(
            "https://identitytoolkit.googleapis.com"
            "/v1/accounts:signInWithPassword?key=" + self.api,
            {"email": self.email, "password": self.password,
             "returnSecureToken": True})
        self.token = d["idToken"]
        self.refresh = d["refreshToken"]
        self.uid = d["localId"]
        self.expires = time.time() + int(d["expiresIn"])
        return d

    def refresh_token(self):
        d = post_form("https://securetoken.googleapis.com/v1/token?key=" + self.api,
                      {"grant_type": "refresh_token",
                       "refresh_token": self.refresh})
        self.token = d["id_token"]
        self.refresh = d["refresh_token"]
        self.uid = d["user_id"]
        self.expires = time.time() + int(d["expires_in"])
        return d

    def ensure(self):
        if not self.token or time.time() > self.expires - 300:
            self.sign_in() if not self.refresh else self.refresh_token()

    @property
    def base(self):
        return "/%s/%s" % (self.uid, self.device)

    def url(self, path):
        return "%s%s.json?auth=%s" % (self.db, path, self.token)

    # --- REST ---

    def req(self, method, path, obj=None):
        self.ensure()
        data = json.dumps(obj).encode() if obj is not None else None
        r = urllib.request.Request(
            self.url(path), data=data,
            headers={"Content-Type": "application/json"}, method=method)
        with urllib.request.urlopen(r, timeout=20) as resp:
            body = resp.read().decode()
            return json.loads(body) if body else None

    def get(self, path):    return self.req("GET", path)
    def put(self, path, o): return self.req("PUT", path, o)
    def patch(self, path, o): return self.req("PATCH", path, o)
    def delete(self, path): return self.req("DELETE", path)

    # --- stream SSE, kể cả chuyển hướng 307 ---

    def open_stream(self, path, max_hops=3):
        """Trả về (conn, response, số lần chuyển hướng, host cuối)."""
        self.ensure()
        url = self.url(path)
        hops = 0
        while hops <= max_hops:
            u = urllib.parse.urlsplit(url)
            conn = http.client.HTTPSConnection(u.netloc, timeout=60)
            conn.request("GET", u.path + ("?" + u.query if u.query else ""),
                         headers={"Accept": "text/event-stream"})
            resp = conn.getresponse()
            if resp.status == 200:
                return conn, resp, hops, u.netloc
            if resp.status in (301, 302, 307, 308):
                loc = resp.getheader("Location")
                conn.close()
                if not loc:
                    raise RuntimeError("%d nhưng không có Location" % resp.status)
                url = loc
                hops += 1
                continue
            raise RuntimeError("HTTP %d: %s" % (resp.status, resp.read()[:200]))
        raise RuntimeError("quá nhiều lần chuyển hướng")


def cmd_auth(db, a):
    d = db.sign_in()
    print("Đăng nhập OK")
    print("  uid       :", db.uid)
    print("  idToken   : %d byte" % len(db.token))
    print("  hết hạn   : %s giây" % d["expiresIn"])
    print("  nhánh     :", db.base)
    db.refresh_token()
    print("Làm mới token OK, idToken mới %d byte" % len(db.token))


def cmd_tree(db, a):
    db.sign_in()
    print("Nhánh:", db.base)
    print(json.dumps(db.get(db.base), indent=2, ensure_ascii=False)[:3000])


def cmd_get(db, a):
    db.sign_in()
    print(json.dumps(db.get(db.base + a.path), indent=2, ensure_ascii=False))


def cmd_open(db, a):
    db.sign_in()
    cmd = {"cmd": "open", "duration_s": a.seconds, "level": 100,
           "id": int(time.time())}
    db.put(db.base + "/desired/valve", cmd)
    print("Đã ghi lệnh:", json.dumps(cmd))


def cmd_close(db, a):
    db.sign_in()
    cmd = {"cmd": "close", "id": int(time.time())}
    db.put(db.base + "/desired/valve", cmd)
    print("Đã ghi lệnh:", json.dumps(cmd))


def cmd_watch(db, a):
    db.sign_in()
    path = db.base + "/desired"
    conn, resp, hops, host = db.open_stream(path)
    print("Đang nghe %s (qua %d lần chuyển hướng, host: %s)" % (path, hops, host))
    print("Ctrl-C để dừng.\n")
    try:
        event = None
        for raw in resp:
            line = raw.decode("utf-8", "replace").rstrip("\r\n")
            if not line:
                event = None
                continue
            if line.startswith("event:"):
                event = line[6:].strip()
            elif line.startswith("data:"):
                print("[%s] %s" % (event, line[5:].strip()[:200]))
    except KeyboardInterrupt:
        pass
    finally:
        conn.close()


def cmd_selftest(db, a):
    """Kiểm chứng từng giả định mà code C dựa vào."""
    ok = fail = 0

    def check(cond, label, extra=""):
        nonlocal ok, fail
        if cond:
            ok += 1
            print("  ĐẠT  %s %s" % (label, extra))
        else:
            fail += 1
            print("  HỎNG %s %s" % (label, extra))

    print("\n[1] Đăng nhập bằng email/mật khẩu")
    d = db.sign_in()
    check(bool(db.token), "nhận được idToken", "(%d byte)" % len(db.token))
    check(len(db.token) < 1400, "idToken vừa bộ đệm TOKEN_MAX=1400 của firmware")
    check(bool(db.refresh), "nhận được refreshToken",
          "(%d byte)" % len(db.refresh))
    check(len(db.refresh) < 600, "refreshToken vừa REFRESH_MAX=600")
    check(bool(db.uid), "nhận được uid", db.uid or "")
    check(int(d["expiresIn"]) > 600, "token sống > 10 phút",
          "(%ss)" % d["expiresIn"])

    print("\n[2] Làm mới token")
    old = db.token
    db.refresh_token()
    # Google trả lại CHÍNH token cũ nếu nó còn hạn - không phải lỗi.
    check(bool(db.token), "securetoken trả về idToken hợp lệ",
          "(giống token cũ)" if db.token == old else "(token mới)")
    check(bool(db.uid), "uid giữ nguyên qua endpoint securetoken", db.uid or "")

    print("\n[3] REST ghi/đọc trên /reported (thiết bị ghi, app đọc)")
    probe = {"probe_ts": int(time.time()), "probe": "rtdb_probe"}
    db.patch(db.base + "/reported", probe)
    back = db.get(db.base + "/reported")
    check(back is not None and back.get("probe") == "rtdb_probe",
          "PATCH rồi GET lại đúng giá trị")

    print("\n[4] REST ghi/đọc trên /desired (app ghi, thiết bị đọc)")
    cmd = {"cmd": "close", "id": 1}
    db.put(db.base + "/desired/valve", cmd)
    back = db.get(db.base + "/desired/valve")
    check(back == cmd, "PUT rồi GET lại khớp", json.dumps(back))

    print("\n[5] Stream SSE và chuyển hướng 307  <-- chỗ dễ hỏng nhất")
    conn, resp, hops, host = db.open_stream(db.base + "/desired")
    check(resp.status == 200, "stream mở được", "(HTTP %d)" % resp.status)
    # Database đời cũ (*.firebaseio.com) trả 307 chuyển stream sang một máy
    # chủ khác; database vùng mới (*.firebasedatabase.app) phục vụ thẳng.
    # Firmware phải chịu được CẢ HAI, nên ở đây chỉ ghi nhận đã đi mấy chặng.
    check(hops <= 3, "số lần chuyển hướng nằm trong giới hạn của firmware",
          "(%d lần -> %s)" % (hops, host))
    print("       %s" % ("Database này KHÔNG chuyển hướng (miền vùng mới)"
                         if hops == 0 else
                         "Database này CÓ chuyển hướng %d lần" % hops))
    ctype = resp.getheader("Content-Type") or ""
    check("text/event-stream" in ctype, "Content-Type đúng", "(%s)" % ctype)

    print("\n[6] Định dạng khung sự kiện khớp bộ tách nht_sse")
    got = {}
    deadline = time.time() + 25
    buf = b""
    fired = False
    while time.time() < deadline and len(got) < 2:
        chunk = resp.read1(256)
        if not chunk:
            break
        buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.decode("utf-8", "replace").rstrip("\r")
            if line.startswith("event:"):
                got["event"] = line[6:].strip()
            elif line.startswith("data:"):
                got.setdefault("data", line[5:].strip())
                if got.get("event") == "put" and not fired:
                    got["put"] = got["data"]
        if got.get("event") == "put" and "put" in got and not fired:
            fired = True
            # kích một thay đổi để chắc chắn thấy sự kiện thứ hai
            import threading
            threading.Thread(
                target=lambda: db.put(db.base + "/desired/valve",
                                      {"cmd": "close", "id": 2}),
                daemon=True).start()

    check(got.get("event") in ("put", "patch", "keep-alive"),
          "sự kiện đầu tiên là put/patch/keep-alive",
          "(%s)" % got.get("event"))
    if "put" in got:
        try:
            j = json.loads(got["put"])
            check("path" in j and "data" in j,
                  'data của put có dạng {"path":..,"data":..}',
                  json.dumps(j)[:120])
        except Exception as e:
            check(False, "parse được data của put", str(e))
    conn.close()

    print("\n[7] Dọn dẹp")
    db.delete(db.base + "/reported/probe")
    db.delete(db.base + "/reported/probe_ts")
    check(True, "đã xoá dữ liệu dò")

    print("\n" + "=" * 46)
    print("KẾT QUẢ: %d đạt, %d hỏng" % (ok, fail))
    print("=" * 46)
    return 1 if fail else 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("auth")
    sub.add_parser("tree")
    g = sub.add_parser("get");   g.add_argument("path", default="", nargs="?")
    o = sub.add_parser("open");  o.add_argument("seconds", type=int, default=900,
                                                nargs="?")
    sub.add_parser("close")
    sub.add_parser("watch")
    sub.add_parser("selftest")
    a = ap.parse_args()

    db = RTDB(load_secret())
    fn = globals()["cmd_" + a.cmd]
    rc = fn(db, a)
    sys.exit(rc or 0)


if __name__ == "__main__":
    main()
