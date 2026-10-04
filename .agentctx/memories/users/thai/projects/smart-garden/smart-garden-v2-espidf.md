---
name: smart-garden-v2-espidf
description: Smart Garden v2 được viết lại từ đầu trên ESP-IDF thuần ở thư mục sg-v2, tách thành các lib nht_* dùng lại được
type: project
author: thai
created: 2026-10-01
updated: 2026-10-01
---

Từ 2026-08-26, Smart Garden v2 sống ở `sg-v2/` (ESP32-C3 Super Mini,
**ESP-IDF thuần**, không Arduino). Thư mục `smart-garden/` là v1 đang chạy
production, `smart-garden-v2/` là bản nháp cũ đã bỏ — cả hai giữ nguyên.

**Why:** user chọn ESP-IDF dù phải viết lại toàn bộ thư viện Arduino, để
kiểm soát được sdkconfig (watchdog, coredump, mbedtls) và tiết kiệm ~400KB
flash. Ba lỗi của v1 (tưới sai thời lượng do echo loop Firebase, leak báo ảo,
treo phải cắt nguồn) được sửa ở tầng kiến trúc chứ không vá.

**How to apply:** code mới phải viết dưới dạng component `nht_*` không dính
domain, phần tưới cây nằm ở `src/app_*`. Logic thuần thì nhận `now_ms` qua
tham số và ghi GPIO qua hook, để test được trên máy (`pio test -e native`).
Xem [[nht-lib-conventions]] và [[sg-v2-toolchain]].
