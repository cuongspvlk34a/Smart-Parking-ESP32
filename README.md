# 🅿️ Smart Parking System v3.2

> Hệ thống bãi đỗ xe thông minh tự động — ESP32 · WiFi Dashboard · Telegram Bot · Tính phí · Emergency Mode

![Version](https://img.shields.io/badge/version-3.2-orange)
![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![License](https://img.shields.io/badge/license-MIT-green)

---

## 📋 Mục lục

1. [Giới thiệu](#-giới-thiệu)
2. [Tính năng](#-tính-năng)
3. [Danh sách linh kiện](#-danh-sách-linh-kiện)
4. [Sơ đồ kết nối phần cứng](#-sơ-đồ-kết-nối-phần-cứng)
5. [Cài đặt phần mềm](#-cài-đặt-phần-mềm)
6. [Tạo Telegram Bot](#-tạo-telegram-bot)
7. [Cấu hình và nạp code](#-cấu-hình-và-nạp-code)
8. [Sử dụng hệ thống](#-sử-dụng-hệ-thống)
9. [Web Dashboard](#-web-dashboard)
10. [Emergency Mode](#-emergency-mode)
11. [Biểu phí](#-biểu-phí)
12. [Kiến trúc phần mềm](#-kiến-trúc-phần-mềm)
13. [Xử lý lỗi thường gặp](#-xử-lý-lỗi-thường-gặp)

---

## 🎯 Giới thiệu

**Smart Parking System v3.2** là hệ thống quản lý bãi đỗ xe thông minh được xây dựng trên nền tảng **ESP32**, phù hợp cho các dự án học tập và nghiên cứu IoT.

Hệ thống có khả năng:
- Tự động phát hiện xe vào/ra qua cảm biến hồng ngoại IR
- Điều khiển thanh chắn cổng bằng servo SG90
- Tính phí đỗ xe tự động theo thời gian
- Hiển thị trạng thái realtime trên Web Dashboard (điện thoại/máy tính)
- Gửi thông báo tức thì qua Telegram Bot
- Xử lý tình huống khẩn cấp bằng 1 nút nhấn

---

## ✨ Tính năng

| Tính năng | Chi tiết |
|-----------|----------|
| 🚗 **Servo tự động** | Mở/đóng cổng mượt mà 0°→90°, từng bước 5°/15ms, không dùng `delay()` |
| 🔍 **Debounce IR** | Chống nhiễu điện từ servo, ổn định 50ms cho tất cả 4 cảm biến |
| 💰 **Tính phí tự động** | 15 phút miễn phí → 2.000đ/30 phút → tối thiểu 3.000đ |
| 📱 **Web Dashboard** | Realtime tại `192.168.4.1`, tự cập nhật mỗi 2.5 giây |
| 📨 **Telegram Bot** | Thông báo xe vào/ra/bãi đầy/khẩn cấp chạy Core 0 riêng biệt |
| 🚨 **Emergency Mode** | 1 nút nhấn → mở thông 2 cổng, LCD nhấp nháy cảnh báo |
| 🕐 **Đồng hồ NTP** | Đồng bộ giờ thực UTC+8, fallback millis() nếu mất mạng |
| 📺 **LCD 16x2** | Hiển thị trạng thái slot, phí khi xe ra, cảnh báo khẩn cấp |

---

## 🛒 Danh sách linh kiện

| STT | Linh kiện | Số lượng | Ghi chú |
|-----|-----------|----------|---------|
| 1 | ESP32 DevKit v1 | 1 | Loại 30 hoặc 38 chân |
| 2 | Servo SG90 | 2 | Điều khiển thanh chắn cổng |
| 3 | Cảm biến IR FC-51 | 4 | Phát hiện xe (2 cổng + 2 slot) |
| 4 | LCD 16x2 + module I2C | 1 | Đã hàn sẵn module I2C |
| 5 | Buzzer thụ động | 1 | Passive buzzer |
| 6 | Nút nhấn | 1 | Momentary push button |
| 7 | Điện trở 100Ω | 1 | Nối nối tiếp với buzzer |
| 8 | Dây jumper | 30–40 sợi | Đực-đực và đực-cái |
| 9 | Breadboard | 1 | Cắm thử linh kiện |
| 10 | Nguồn 5V ≥ 2A | 1 | Cấp cho ESP32 + 2 servo |

---

## 🔌 Sơ đồ kết nối phần cứng

### Bảng GPIO

| GPIO | Linh kiện | Ghi chú |
|------|-----------|---------|
| **19** | Servo cổng VÀO | Dây tín hiệu (cam/vàng) |
| **18** | Servo cổng RA | Dây tín hiệu (cam/vàng) |
| **32** | IR cổng VÀO | Chân OUT của cảm biến |
| **4** | IR cổng RA | Chân OUT của cảm biến |
| **14** | IR Slot 1 | Chân OUT của cảm biến |
| **27** | IR Slot 2 | Chân OUT của cảm biến |
| **15** | Buzzer (+) | Qua điện trở 100Ω |
| **21** | LCD SDA | I2C Data |
| **22** | LCD SCL | I2C Clock |
| **13** | Nút Emergency | Một đầu GPIO 13, đầu kia GND |

### Kết nối từng linh kiện

**Servo SG90 (×2):**
```
Dây đỏ   → 5V (chân VIN trên ESP32)
Dây nâu  → GND
Dây cam  → GPIO 19 (cổng vào) hoặc GPIO 18 (cổng ra)
```
> ⚠️ Servo cần 5V — KHÔNG dùng chân 3.3V, servo sẽ yếu hoặc không quay

**Cảm biến IR FC-51 (×4):**
```
VCC → 3.3V
GND → GND
OUT → GPIO tương ứng (32 / 4 / 14 / 27)
```

**LCD I2C 16x2:**
```
VCC → 5V
GND → GND
SDA → GPIO 21
SCL → GPIO 22
```
> Địa chỉ I2C mặc định: `0x27`. Nếu LCD không sáng thử đổi thành `0x3F`

**Buzzer thụ động:**
```
GPIO 15 → [điện trở 100Ω] → chân (+) buzzer → GND
```

**Nút Emergency:**
```
GPIO 13 → một đầu nút nhấn → GND
(Không cần điện trở — ESP32 dùng pull-up nội bộ)
```

### Sơ đồ tổng quát
```
                    ┌──────────────────────┐
                    │        ESP32         │
   Servo VÀO ───────│ GPIO 19        VIN   │──── 5V
   Servo RA  ───────│ GPIO 18        GND   │──── GND chung
   IR Vào    ───────│ GPIO 32        3.3V  │──── VCC các IR
   IR Ra     ───────│ GPIO  4        GPIO21│──── LCD SDA
   IR Slot 1 ───────│ GPIO 14        GPIO22│──── LCD SCL
   IR Slot 2 ───────│ GPIO 27              │
   Buzzer    ───────│ GPIO 15              │
   Emergency ───────│ GPIO 13              │
                    └──────────────────────┘
```

---

## 💻 Cài đặt phần mềm

### Bước 1 — Cài Arduino IDE

1. Truy cập [arduino.cc/en/software](https://www.arduino.cc/en/software)
2. Tải **Arduino IDE 2.x** phù hợp với hệ điều hành
3. Cài đặt bình thường theo hướng dẫn

### Bước 2 — Thêm board ESP32

1. Mở Arduino IDE → vào **File → Preferences**
2. Tìm ô **"Additional boards manager URLs"** → dán vào:
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```
3. Vào **Tools → Board → Boards Manager**
4. Tìm **"esp32"** → cài **"ESP32 by Espressif Systems"** (chờ 2–5 phút)
5. Sau khi cài xong: **Tools → Board → ESP32 Arduino → ESP32 Dev Module**

### Bước 3 — Cài thư viện

**Cài qua Library Manager** (Tools → Manage Libraries):

| Tìm kiếm | Cài thư viện |
|----------|-------------|
| `ESP32Servo` | ESP32Servo by Kevin Harrington |
| `LiquidCrystal I2C` | LiquidCrystal I2C by Frank de Brabander |

**Cài thủ công từ GitHub ZIP** (2 thư viện còn lại):

1. Tải **ESPAsyncWebServer**:
   - Vào: `github.com/me-no-dev/ESPAsyncWebServer`
   - Nhấn **Code → Download ZIP**
2. Tải **AsyncTCP**:
   - Vào: `github.com/me-no-dev/AsyncTCP`
   - Nhấn **Code → Download ZIP**
3. Arduino IDE → **Sketch → Include Library → Add .ZIP Library**
4. Chọn từng file ZIP vừa tải → cài lần lượt

---

## 📨 Tạo Telegram Bot

### Bước 1 — Tạo bot mới

1. Mở Telegram → tìm kiếm **@BotFather**
2. Nhấn **Start** → gõ `/newbot`
3. BotFather hỏi tên bot → gõ tên bất kỳ, ví dụ: `Smart Parking`
4. BotFather hỏi username → gõ username kết thúc bằng `bot`, ví dụ: `SmartParking2025_bot`
5. BotFather gửi lại **BOT_TOKEN** dạng:
```
8572713614:AAEmXgB...............
```
→ Copy lưu lại token này

### Bước 2 — Lấy CHAT_ID

1. Tìm kiếm **@userinfobot** → chọn bot có username `@userinfobot`
2. Nhấn **Start** hoặc gõ `/start`
3. Bot trả về thông tin, tìm dòng **"Id:"**:
```
Id: 663........
```
→ Copy lưu lại số ID này

### Bước 3 — Kích hoạt bot

1. Tìm kiếm username bot vừa tạo (ví dụ `@SmartParking2025_bot`)
2. Nhấn **Start**
3. Gõ bất kỳ tin nhắn nào, ví dụ: `xin chao`

> ⚠️ Bước này bắt buộc — Telegram chặn bot gửi tin nếu người dùng chưa nhắn trước

---

## ⚙️ Cấu hình và nạp code

### Bước 1 — Mở file và điền thông tin

Mở file `SmartParking_v3.2_final.ino` → tìm mục **CONFIG** ở đầu file:

```cpp
// ================================================================
//  CONFIG  ← chỉnh tại đây trước khi nạp
// ================================================================
#define WIFI_SSID    "tên_wifi_nhà_bạn"      // Tên WiFi
#define WIFI_PASS    "mật_khẩu_wifi"          // Mật khẩu WiFi
#define AP_SSID      "SmartParking"           // Giữ nguyên
#define AP_PASS      "parking123"             // Giữ nguyên

#define BOT_TOKEN    "token_từ_BotFather"     // Dán token vào đây
#define CHAT_ID      "id_từ_userinfobot"      // Dán ID vào đây
```

**Ví dụ sau khi điền:**
```cpp
#define WIFI_SSID    "NhaToiWifi"
#define WIFI_PASS    "matkhau123"
#define BOT_TOKEN    "8572713614:AAEmXgB46UPmb2Lonp8..."
#define CHAT_ID      "6633882197"
```

### Bước 2 — Chọn đúng Board và Port

1. Cắm ESP32 vào máy tính qua cáp USB
2. **Tools → Board → ESP32 Arduino → ESP32 Dev Module**
3. **Tools → Port → chọn cổng COM có ESP32**
   - Windows: thường là `COM3`, `COM4`, `COM10`...
   - Mac/Linux: thường là `/dev/ttyUSB0`

### Bước 3 — Nạp code

1. Nhấn nút **Upload** (mũi tên → trên thanh công cụ)
2. Chờ khoảng 30–60 giây
3. Thấy dòng **"Done uploading"** là thành công

### Bước 4 — Kiểm tra Serial Monitor

1. **Tools → Serial Monitor** (hoặc Ctrl+Shift+M)
2. Chọn baudrate **115200**
3. Kết quả đúng sẽ hiện:
```
[INIT] Smart Parking v3.2
[WIFI] AP: SmartParking @ 192.168.4.1
[WIFI] STA: 192.168.1.xxx
[NTP] 29/03 10:32:13
[WEB] Server started on port 80
[EVENT sys] He thong khoi dong
[INIT] Ready.
[TG] Sent OK
```

---

## 🚗 Sử dụng hệ thống

### Xe vào bãi
1. Xe đến cổng → IR cổng vào phát hiện
2. Hệ thống kiểm tra còn chỗ không:
   - **Còn chỗ** → Servo mở cổng, LCD "Xe đang vào...", Telegram thông báo
   - **Hết chỗ** → Cổng không mở, LCD "BÃI ĐẦY", còi kêu 3 tiếng
3. Xe vào đậu → IR slot ghi nhận, bắt đầu tính giờ

### Xe ra bãi
1. Xe rời chỗ đậu → IR slot phát hiện trống
2. Hệ thống tính phí → LCD hiển thị phí + thời gian đậu
3. Xe đến cổng ra → IR cổng ra phát hiện → servo mở
4. Xe ra → cổng tự đóng sau 3 giây
5. Telegram gửi thông báo: thời gian + phí

---

## 🌐 Web Dashboard

### Kết nối
1. Điện thoại/máy tính → WiFi → chọn **"SmartParking"**
2. Nhập mật khẩu: **`parking123`**
3. Mở trình duyệt → vào địa chỉ: **`http://192.168.4.1`**

### Giao diện Dashboard
```
┌─────────────────────────────────┐
│  SMART PARKING          ● LIVE  │
│  10:32:15                       │
├────────────────┬────────────────┤
│  Trong bãi     │  Trạng thái   │
│    1/2         │   CÓ CHỖ      │
├────────────────┴────────────────┤
│ SLOT 01: CÓ XE                 │
│ Phí: 4,000đ (đang chạy)        │
│                                 │
│ SLOT 02: TRỐNG                 │
│ Sẵn sàng                       │
├─────────────────────────────────┤
│ SỰ KIỆN GẦN ĐÂY                │
│ ● 10:15  Xe vào S1             │
│ ● 09:55  Xe ra S2 | 2,000đ    │
│ ● 08:00  Hệ thống khởi động   │
└─────────────────────────────────┘
```

Dashboard tự cập nhật mỗi **2.5 giây** — phí tăng realtime theo từng giây.

> 💡 Dashboard hoạt động ngay cả khi mất internet — chỉ cần kết nối WiFi SmartParking

---

## 🚨 Emergency Mode

Dùng khi có tình huống khẩn cấp (cháy, tai nạn...) cần mở thông toàn bộ cổng ngay lập tức.

### Kết nối nút
```
GPIO 13 ──── [NÚT NHẤN] ──── GND
(không cần điện trở)
```

### Cách hoạt động

**Nhấn nút lần 1 → BẬT khẩn cấp:**
- Cả 2 servo quay lên 90° ngay lập tức
- Cổng giữ mở, không tự đóng
- LCD nhấp nháy xen kẽ giữa `!!! KHAN CAP !!!` và `THOAT HIEM NGAY`
- Telegram gửi: *"CHẾ ĐỘ KHẨN CẤP — CẢ 2 CỔNG MỞ THÔNG"*
- Dashboard hiển thị banner cam cảnh báo
- Toàn bộ cảm biến IR bị bỏ qua — xe ra vào tự do

**Nhấn nút lần 2 → TẮT khẩn cấp:**
- 2 cổng từ từ đóng lại qua state machine
- Hệ thống trở về hoạt động bình thường
- Telegram gửi: *"Chế độ khẩn cấp đã TẮT"*

---

## 💰 Biểu phí

| Thời gian đậu | Phí |
|---------------|-----|
| ≤ 15 phút | **MIỄN PHÍ** |
| 16–45 phút | **3.000đ** (tối thiểu) |
| 46–75 phút | **4.000đ** |
| 76–105 phút | **6.000đ** |
| Mỗi 30 phút tiếp | +**2.000đ** |

**Công thức tính:**
```
Nếu thời gian ≤ 15 phút  →  MIỄN PHÍ
Nếu thời gian > 15 phút  →  MAX(3.000đ,  ⌈(phút − 15) ÷ 30⌉ × 2.000đ)
```

> Chỉnh biểu phí trong file code tại mục CONFIG:
> ```cpp
> #define FREE_MINUTES    15    // phút miễn phí
> #define RATE_PER_BLOCK  2000  // đồng/30 phút
> #define MIN_FEE         3000  // phí tối thiểu
> #define BLOCK_MINUTES   30    // phút/block
> ```

---

## 🏗️ Kiến trúc phần mềm

```
┌─────────────────────────────────────────┐
│         CORE 1 — loop() chính           │
│  (non-blocking hoàn toàn, 0 delay())    │
│                                         │
│  updateEmergency()  ← ưu tiên #1        │
│  updateSlots()      ← IR slot debounce  │
│  updateGate(In/Out) ← state machine     │
│  debounceRead()     ← IR cổng          │
│  updateBuzzer()     ← non-blocking      │
│  refreshLCD()       ← non-blocking      │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│    CORE 0 — telegramTask() FreeRTOS     │
│                                         │
│  Poll mỗi 300ms                         │
│  WiFiClientSecure HTTPS TLS             │
│  → api.telegram.org:443                 │
│  Mutex bảo vệ shared data               │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│      Async Web Server — Port 80         │
│                                         │
│  GET /            → Dashboard HTML      │
│  GET /api/status  → JSON realtime       │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│        Gate State Machine               │
│                                         │
│  CLOSED → TRIGGERED (800ms)             │
│        → OPENING (5°/15ms → 90°)        │
│        → OPEN (giữ 3 giây)             │
│        → CLOSING (5°/15ms → 0°)         │
│        → CLOSED                         │
└─────────────────────────────────────────┘
```

### WiFi Dual Mode
```
AP Mode  → "SmartParking" luôn bật → Dashboard 192.168.4.1
STA Mode → Kết nối WiFi nhà → Telegram + NTP
(Mất internet → AP vẫn hoạt động bình thường)
```

---

## 🔧 Xử lý lỗi thường gặp

| Lỗi | Nguyên nhân | Cách sửa |
|-----|-------------|----------|
| **Lỗi Upload** | Sai Board hoặc Port | Chọn đúng `ESP32 Dev Module` và cổng COM |
| **LCD không sáng** | Sai địa chỉ I2C | Thử đổi `0x27` thành `0x3F` trong code |
| **Servo không quay** | Thiếu nguồn 5V | Kiểm tra dây đỏ servo nối VIN, không dùng 3.3V |
| **Servo quay ngược** | Lắp đối xứng | Hoán đổi `PIN_SERVO_IN` và `PIN_SERVO_OUT` |
| **Telegram không gửi** | Sai token/ID | Kiểm tra BOT_TOKEN và CHAT_ID, nhớ nhắn tin cho bot trước |
| **Dashboard trắng** | Lỗi JSON | Nhấn F12 xem Console, thử refresh |
| **Không vào Dashboard** | Sai WiFi | Kiểm tra kết nối WiFi SmartParking, IP 192.168.4.1 |
| **Giờ sai** | Sai múi giờ | Đổi số `8` trong `configTime(8 * 3600,...)` theo múi giờ của bạn |

### Tra cứu múi giờ phổ biến
| Khu vực | Giờ UTC | Giá trị configTime |
|---------|---------|-------------------|
| Việt Nam | UTC+7 | `configTime(7 * 3600, ...)` |
| Đài Loan / Singapore | UTC+8 | `configTime(8 * 3600, ...)` |
| Nhật / Hàn | UTC+9 | `configTime(9 * 3600, ...)` |

---

## 📁 Cấu trúc thư mục

```
Smart-Parking-ESP32/
│
├── SmartParking_v3.2_final.ino   # File code chính
├── SmartParking_BaoCao.pptx      # Báo cáo PowerPoint
└── README.md                     # Hướng dẫn này
```

---

## 📄 License

MIT License — Tự do sử dụng, chỉnh sửa và chia sẻ cho mục đích học tập.

---

## 👨‍💻 Tác giả

**Nguyễn Văn Cường**
- Telegram Bot: [@SmartParkingNVC_bot](https://t.me/SmartParkingNVC_bot)
- Dự án học tập — 2025
