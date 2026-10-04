---
name: sg-v2-toolchain
description: Đường dẫn pio/qemu và khoảng trống token Wokwi khi build-test sg-v2
type: reference
author: thai
created: 2026-10-01
updated: 2026-10-01
---

- PlatformIO core **không có trên PATH**: dùng `~/.platformio/penv/bin/pio`.
  Đã cài core 6.1.19 + `intelhex` vào venv đó ngày 2026-08-26 (trước đó venv
  rỗng, chỉ có packages cũ từ VS Code).
- QEMU RISC-V của Espressif: `~/.local/qemu-esp/qemu/bin/qemu-system-riscv32`.
  Chạy: `-machine esp32c3 -drive file=<ảnh 4MB>,if=mtd,format=raw`.
  QEMU **không mô phỏng ADC** (đọc thật sẽ quay vô hạn trong HAL rồi nổ
  interrupt WDT) và **không có sóng WiFi** (`esp_phy_enable` assert).
- Wokwi CLI: `~/.wokwi/bin/wokwi-cli` (0.26.1). `~/.wokwi/user.tok` KHÔNG
  phải token CI — API trả Unauthorized. Token CI lấy ở wokwi.com/dashboard/ci
  rồi đặt vào `WOKWI_CLI_TOKEN`. Chỉ Wokwi mới mô phỏng được WiFi/Firebase.

Liên quan: [[smart-garden-v2-espidf]]
