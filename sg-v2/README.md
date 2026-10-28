# Smart Garden v2 — ESP32-C3 / ESP-IDF

Hệ tưới cây tự động, viết lại từ đầu trên ESP-IDF cho board **ESP32-C3 Super
Mini**. Kế thừa kinh nghiệm từ bản v1 chạy Arduino, và sửa tận gốc ba lỗi
dai dẳng của nó.

```bash
pio run -e esp32c3              # build firmware thật
pio test -e native              # test logic trên máy, không cần board
pio run -e esp32c3_sim          # bản chạy trong QEMU, có self-test
python3 tools/ota_upload.py --scan   # nạp firmware qua LAN
```

---

## Ba lỗi của v1 và cách v2 xử lý

| Triệu chứng ở v1 | Nguyên nhân gốc | Cách chặn ở v2 | Test hồi quy |
|---|---|---|---|
| Chỉnh thời gian tưới không ăn, luôn về 60 giây | Thiết bị vừa ghi trạng thái vào `/data` vừa nghe realtime chính `/data` → tự nhận lại lệnh của mình → `on()` xoá thời lượng một-lần rồi rơi về mặc định | Tách `/desired` (app ghi) khỏi `/reported` (thiết bị ghi) — hai đường không gặp nhau. Thêm `nht_act_on_for()` bất biến với lệnh trùng | `test_repeated_on_for_is_idempotent`, `test_duplicate_open_keeps_duration` |
| Báo ngập/hết ngập qua lại suốt hàng giờ | Chỉ có debounce đối xứng quanh MỘT ngưỡng; cảm biến ẩm dao động quanh đúng ngưỡng đó | Schmitt trigger hai ngưỡng + trễ bất đối xứng (vào 5s, ra 10 phút) + khoá van khi đang ngập | `test_flapping_around_threshold_gives_one_transition` |
| Thỉnh thoảng treo, phải cắt nguồn | Không có watchdog, không có coredump, nên chưa bao giờ biết nó treo ở đâu | Task WDT bật cho mọi task + coredump ghi vào flash + log lý do reset lên cloud | `esp_reset_reason()` báo cáo trong `/reported/last_reset` |

Ngoài ra `nht_sched` sửa hai lỗi nữa của scheduler cũ: lịch bị bỏ khi vòng
lặp trễ quá một phút (nay có **cửa sổ bù giờ**), và `update()` báo thành công
mà không sửa gì (lặp bằng bản sao theo value).

---

## Kiến trúc

Chiều phụ thuộc chỉ đi một hướng, và **bị ép lúc build** bằng
`tools/check_layers.py` (chạy trước mọi lần biên dịch; vi phạm là build hỏng):

```
 main (src/)        ráp mọi thứ, không chứa logic
   sg_net           adapter mạng của sản phẩm: web server, Firebase
     sg_core        lõi tưới + an toàn + cấu hình. KHÔNG biết mạng tồn tại
       nht_*        thư viện dùng lại, KHÔNG biết sản phẩm tồn tại
```

| Tầng | Quy tắc cứng |
|---|---|
| `nht_*` | Không bao giờ `REQUIRES`/`#include` `sg_*` hay `main`. Độc lập nhau, trừ `nht_valve → nht_actuator`. |
| `sg_core` | Không được chạm `nht_net`, `nht_webkit`, `nht_fbrtdb`, `sg_net`. |
| `sg_net` | Chỉ đứng trên `sg_core` + các lib mạng. Không có chiều ngược lại. |
| Mọi nơi | `#include` chéo component mà không khai báo `REQUIRES` cũng là lỗi. Component mới chưa xếp tầng là lỗi. |

Rút mạng ra thì cả `sg_net` im lặng, lịch tưới vẫn chạy. Đây là chỗ v1 làm
ngược: danh sách lịch chỉ được nạp trong callback "đã kết nối Firebase lần
đầu", nên reboot lúc mất mạng là không tưới gì cả.

### Vòng điều khiển theo sự kiện

`ctl_task` không quay đều theo nhịp. Nó ngủ trong `xTaskNotifyWait` và chỉ
thức dậy khi có một trong:

- **lệnh / đổi cấu hình** (`EV_CMD`) từ bất kỳ task nào,
- **tới hạn của van** — `nht_valve_next_wake_ms()` cho biết bao lâu nữa có việc
  (hết hành trình, hết thời lượng, hết trễ bơm, hết nghỉ đảo chiều),
- **ba timer `esp_timer`** lấy mẫu: cảm biến ngập (200 ms), nguồn (1 s), lịch (1 s).

Trần ngủ 1 s để còn nuôi watchdog. Mọi lib giữ nguyên tính thuần (thời gian
truyền qua tham số) nên vẫn test được trên máy.

### An toàn: sự cố → đóng van ngay

`eval_faults()` gộp mọi nguồn sự cố về một chỗ. Có bit nào bật là **đóng van
ngay trong cùng lần gọi** (không đợi chu kỳ sau) và khoá mở cho tới khi hết:

| Bit | Nguồn | Lọc nhiễu |
|---|---|---|
| `WF_LEAK` | cảm biến ngập | Schmitt hai ngưỡng; vào 5 s, ra 10 phút |
| `WF_LEAK_SENSOR` | ADC cảm biến ngập đọc hỏng | 3 lần hỏng liên tiếp; gỡ sau 5 lần tốt |
| `WF_VOLTAGE` | nguồn dưới `volt_min` hoặc không đo được | 3 mẫu liên tiếp (3 s); gỡ sau 5 mẫu tốt + trễ 0,3 V |

Cảm biến hỏng **không** được im lặng bỏ qua (bản đầu của v2 làm vậy: không đọc
được thì cứ coi như khô). Task treo thì task watchdog (10 s) reset chip, và
`nht_valve_init()` luôn đóng van lúc lên lại.

Cloud: bản chụp `/desired` nhận được mỗi lần stream (mở lại) **chỉ đồng bộ,
không thi hành** lệnh nào — lệnh mở viết lúc thiết bị mất mạng, hay `restart`
còn nằm trong DB, không được chạy khi nối lại.

### Thư viện tách riêng (`components/`)

Tất cả đều không dính gì tới tưới cây, dùng lại được ở dự án khác. Mỗi cái có
`idf_component.yml` riêng theo SemVer.

| Component | Việc | Tương ứng bên Arduino |
|---|---|---|
| `nht_actuator` | Đầu ra có trạng thái: tự tắt, trễ bật, chống đảo nhanh, khoá an toàn | `GenericOutput` / `VirtualOutput` |
| `nht_valve` | Van bi động cơ + van điện từ NC, sau một API chung | (mới) |
| `nht_dinput` | Đầu vào chống nhiễu: Schmitt + trễ bất đối xứng | `GenericInput` |
| `nht_sched` | Lịch giờ-trong-ngày, có bù giờ, payload tuỳ dự án | `Scheduler` |
| `nht_store` | Khoá-giá trị trên NVS | `ENVFile` |
| `nht_sensor` | ADC one-shot có hiệu chuẩn; đếm xung | `VoltageReader` |
| `nht_net` | WiFi STA tự nối lại + SoftAP + captive portal + SNTP | (mới) |
| `nht_webkit` | HTTP + WebSocket + OTA qua LAN | `WebOTA` |
| `nht_fbrtdb` | Firebase RTDB: auth, REST, stream SSE | `FirebaseHelper` |

Hai component của sản phẩm (không tái sử dụng): `sg_core` (cấu hình, logic
tưới, an toàn, lý do reset) và `sg_net` (web + Firebase adapter).

Bốn thư viện đầu là **logic thuần**: thời gian truyền vào qua tham số, ghi
GPIO qua hook. Nhờ vậy biên dịch được trên máy tính và test được kịch bản ba
tiếng trong vài mili giây.

---

## Hai phát hiện về phần cứng

**ESP32-C3 không có PCNT.** Chip C3 (và C2) thiếu hẳn ngoại vi đếm xung mà
ESP32/S3/C6 đều có. `nht_pulse` tự chuyển sang đếm bằng ngắt GPIO — với lưu
lượng kế thì thừa sức (YF-S201 ở 30 L/phút chỉ khoảng 225 Hz).

**ESP-IDF chỉ cho một handle trên mỗi bộ ADC.** Hai cảm biến analog cùng nằm
trên ADC1 (bắt buộc, vì ADC2 không dùng được khi WiFi bật) thì cái thứ hai bị
từ chối với `adc1 is already in use`. `nht_adc` tự chia sẻ handle và đếm tham
chiếu. Lỗi này do chạy QEMU mới lộ ra.

---

## Pin map

`include/board_config.h` — **hiện là bản placeholder**, sửa theo PCB v2 thật.

Ràng buộc của C3 (khác hẳn ESP32 ở v1, các chân 16/18/19/32/34/35 không tồn tại):

- GPIO khả dụng: `0..10, 20, 21`
- ADC1: `0,1,2,3,4` — cảm biến analog **bắt buộc** nằm trong đây
- ADC2 (`5`): không dùng được khi WiFi bật
- Strapping: `2, 8, 9` — không treo tải kéo mức lúc boot
- `8` = LED onboard, `9` = nút BOOT, `20/21` = UART0

---

## Cấu hình

Mọi thông số vật lý nằm trên NVS, chỉnh qua web, **không hardcode**. Ngưỡng
cảm biến và thời lượng có hiệu lực ngay; đổi loại van hoặc chế độ cảm biến
thì cần khởi động lại.

Lần đầu bật, thiết bị phát SoftAP `SmartGarden-<id>` kèm captive portal để
nhập WiFi và thông tin Firebase.

Partition 4MB: app 1856K × 2 (OTA), NVS riêng cho cấu hình, 64K coredump.
Cấp rộng tay ngay từ đầu — đổi kích thước sau sẽ phá OTA của thiết bị đang chạy.

---

## Firebase

```
/<uid>/<device_id>/
  desired/     app GHI, thiết bị chỉ ĐỌC   (thiết bị nghe stream ở đây)
    valve      {"cmd":"open","duration_s":900,"level":100,"id":17}
    schedules  [{"hour":6,"minute":0,"repeat":127,"duration_s":900,...}]
    cmd        "restart"
  reported/    thiết bị GHI, app chỉ ĐỌC   (thiết bị không bao giờ đọc lại)
  info/
```

`firebase-rules.json` khoá luôn ở tầng máy chủ. Trường `id` trong lệnh van
chống thi hành lại lệnh cũ mỗi khi stream mở lại.

Khởi động lại thì **bỏ qua** lệnh van đang có trong `desired` — thiết bị vừa
reboot có thể vì đã treo giữa lúc tưới, không nên mở nước lại theo lệnh cũ.

---

## Kiểm thử — đã chạy được tới đâu

| Tầng | Công cụ | Kết quả |
|---|---|---|
| Logic thuần | `pio test -e native` | **68/68 đạt** |
| Trên chip (NVS, timer, GPIO, task) | QEMU + self-test | **35/35 đạt** |
| Giao thức Firebase RTDB | `tools/rtdb_probe.py selftest` (server thật) | **16/16 đạt** |
| WiFi + HTTP + Firebase client chạy trên chip | Wokwi | **chưa chạy được** |

### Về Wokwi

`wokwi.toml` và `diagram.json` đã sẵn sàng và hợp lệ (`wokwi-cli lint` sạch),
nhưng CLI khởi động được mô phỏng rồi **không xuất ra serial nào**. Đã loại
trừ: kiểu board (thử 4 loại), bảng partition (thử cả factory lẫn OTA), kích
thước ảnh flash (thử cả ngắn lẫn đệm đầy 4MB), và `net.forward`.

Phép thử quyết định: một firmware ESP-IDF "hello world" tối giản **cũng im
lặng y hệt**. Vậy nguyên nhân nằm ở phía Wokwi (gói tài khoản / phiên bản
CLI so với API), không phải ở firmware này. Cách đi tiếp: chạy Wokwi qua
extension VS Code (dùng `~/.wokwi/user.tok`, có giao diện), hoặc nạp lên
board thật.

### Về Firebase RTDB

`tools/rtdb_probe.py` nói chuyện với Firebase bằng **đúng những endpoint mà
`nht_fbrtdb.c` dùng**, nên nó kiểm chứng được từng giả định của code C:

```bash
python3 tools/rtdb_probe.py selftest   # kiểm chứng 16 giả định
python3 tools/rtdb_probe.py watch      # nghe stream y như thiết bị
python3 tools/rtdb_probe.py open 900   # ra lệnh tưới 15 phút
python3 tools/rtdb_probe.py tree       # xem toàn bộ nhánh
```

Kết quả đo được trên database thật:

- idToken **916 byte** (bộ đệm firmware 1400 — vừa, còn dư)
- refreshToken **226 byte** (bộ đệm 600 — vừa)
- token sống 3600s; firmware làm mới trước hạn 300s
- Khung sự kiện đúng dạng `{"path":..,"data":..}` mà `nht_sse` mong đợi
- **Database này KHÔNG chuyển hướng 307.** Nó nằm trên miền vùng mới
  (`*.firebasedatabase.app`) và phục vụ stream thẳng. Chỉ database đời cũ
  (`*.firebaseio.com`) mới trả 307. Firmware chịu được cả hai, nhưng đừng
  bỏ nhánh xử lý 307 đi — nó là thứ duy nhất cứu bạn nếu sau này dùng một
  project Firebase cũ hơn.

## Kiểm thử

**Trên máy** (`pio test -e native`) — 54 test, phủ toàn bộ logic thuần:

- `test_sched` — cửa sổ bù giờ, không chạy đúp, `update` sửa thật
- `test_dinput` — 3 tiếng dao động quanh ngưỡng chỉ được sinh 1 chuyển trạng thái
- `test_actuator` — lệnh trùng không mất thời lượng, `sync` có chốt chặn, tràn 32-bit
- `test_valve` — trình tự van bi/solenoid, trần thời gian cứng, khoá an toàn
- `test_sse` — parser SSE bất biến với mọi cách cắt khối 1..40 byte, sự kiện quá dài bị bỏ chứ không cắt cụt

**Trên chip** (`pio run -e esp32c3_sim` rồi chạy QEMU) — 35 test tích hợp:
NVS thật, esp_timer thật, GPIO thật, task chen nhau thật.

```bash
python3 -c "d=open('.pio/build/esp32c3_sim/merged.bin','rb').read(); \
  open('/tmp/flash4m.bin','wb').write(d+b'\xff'*(4*1024*1024-len(d)))"
qemu-system-riscv32 -nographic -machine esp32c3 \
  -drive file=/tmp/flash4m.bin,if=mtd,format=raw
```

QEMU không mô phỏng ADC (nên bản sim dùng `NHT_ADC_FAKE`) và không có sóng
WiFi (nên bản sim bỏ hẳn tầng mạng — hoá ra lại là phép thử tốt: nó chứng
minh việc tưới chạy đủ khi không có mạng nào cả).

Phần WiFi/HTTP/Firebase chạy trên chip thì xem mục "Về Wokwi" ở trên.
