# Máy phân loại rác tự động bằng camera

Hướng dẫn này giúp nhóm em lắp ráp, nạp chương trình và chạy máy phân loại rác từ đầu đến cuối. Em làm lần lượt từng bước, xong bước nào thì đánh dấu bước đó. Gặp lỗi thì xem mục [Xử lý sự cố](#11-xử-lý-sự-cố).

---

## Mục lục

1. [Máy hoạt động như thế nào?](#1-máy-hoạt-động-như-thế-nào)
2. [Chuẩn bị linh kiện](#2-chuẩn-bị-linh-kiện)
3. [Cài phần mềm trên máy tính](#3-cài-phần-mềm-trên-máy-tính)
4. [Tải mã nguồn](#4-tải-mã-nguồn)
5. [Đặt tên riêng cho nhóm](#5-đặt-tên-riêng-cho-nhóm)
6. [Lấy khóa Groq (AI trên mạng)](#6-lấy-khóa-groq-ai-trên-mạng)
7. [Lắp mạch](#7-lắp-mạch)
8. [Nạp chương trình cho board servo](#8-nạp-chương-trình-cho-board-servo)
9. [Nạp chương trình cho board camera](#9-nạp-chương-trình-cho-board-camera)
10. [Chạy thử cả hệ thống](#10-chạy-thử-cả-hệ-thống)
11. [Xử lý sự cố](#11-xử-lý-sự-cố)
12. [Tinh chỉnh](#12-tinh-chỉnh)
13. [Các thư mục trong dự án](#13-các-thư-mục-trong-dự-án)

---

## 1. Máy hoạt động như thế nào?

Máy có **2 board ESP32-S3** nói chuyện với nhau qua sóng **ESP-NOW**. ESP-NOW là cách hai board ESP32 gửi tin nhắn trực tiếp cho nhau, không cần router WiFi.

```
  ┌──────────────────────┐   ESP-NOW (không dây)   ┌──────────────────────┐
  │   BOARD CAMERA       │ ──────────────────────► │   BOARD SERVO        │
  │                      │   "metal" / "plastic" / │                      │
  │ - Chụp ảnh rác       │   "paper_cardboard"     │ - Nhận tên loại rác  │
  │ - Đoán loại rác      │                         │ - Quay 2 servo tới   │
  │ - Nút bấm + đèn LED  │                         │   góc của loại đó    │
  │ - Trang web điều     │                         │ - Giữ 1,5 giây rồi   │
  │   khiển              │                         │   quay về            │
  └──────────────────────┘                         └──────────────────────┘
```

Board camera có **hai cách** đoán loại rác:

- **AI trên mạng (Groq):** dùng khi board có internet. Đoán chính xác hơn, nhưng chỉ được hỏi 1 lần mỗi 30 giây.
- **Model trên board (TFLite):** chạy ngay trên chip, không cần mạng. Board dùng cách này khi không có internet, hoặc khi chưa hết 30 giây chờ của Groq.

Có 3 loại rác, mỗi loại ứng với một góc quay của servo:

| Loại rác | Tên board camera gửi | Góc servo 1 | Góc servo 2 |
|---|---|---|---|
| Kim loại | `metal` | 20° | 160° |
| Nhựa | `plastic` | 45° | 135° |
| Giấy, bìa | `paper_cardboard` | 90° | 90° |

Hai servo đặt **đối diện nhau**, nên servo 2 luôn quay góc `180 − góc servo 1` để hai bên cùng nghiêng về một phía.

---

## 2. Chuẩn bị linh kiện

| Linh kiện | Số lượng | Ghi chú |
|---|---|---|
| Board ESP32-S3 có camera OV2640, **flash 16 MB, PSRAM 8 MB** | 1 | Sơ đồ chân camera trong code giống loại *ESP32-S3-WROOM CAM*. Nếu board của em khác loại, xem [mục 11](#11-xử-lý-sự-cố). |
| Board ESP32-S3-DevKitC-1 | 1 | Dùng làm board servo |
| Servo **180°** (SG90 hoặc MG90S loại thường) | 2 | ⚠️ Servo **360° không dùng được**, vì loại đó quay liên tục chứ không đứng ở một góc. |
| Nút nhấn 2 chân | 1 | Dùng để chụp ảnh |
| Module LED RGB WS2812B (ví dụ MKE-M22) | 1 | Đèn báo trạng thái |
| Nguồn **5 V, từ 2 A trở lên** | 1 | Riêng cho servo |
| Dây cáp USB có truyền dữ liệu | 2 | Một số cáp chỉ sạc được mà không truyền dữ liệu, máy tính sẽ không nhận board |
| Dây cắm, breadboard | — | |

---

## 3. Cài phần mềm trên máy tính

1. Cài **Visual Studio Code**: <https://code.visualstudio.com>
2. Mở VS Code, bấm vào biểu tượng **Extensions** (4 ô vuông) ở thanh bên trái, tìm **PlatformIO IDE** rồi bấm **Install**. Đợi cài xong và khởi động lại VS Code. Sau đó sẽ có biểu tượng đầu kiến 🐜 ở thanh bên trái.
3. Cài **Git**: <https://git-scm.com/downloads>
4. **Driver USB** (chỉ cần trên Windows): nếu cắm board mà *Device Manager* không hiện cổng `COM`, cài driver theo tên chip USB ghi trên board, thường là **CH340/CH343** hoặc **CP210x**.

---

## 4. Tải mã nguồn

Mở **Terminal** (Windows: *Git Bash* hoặc *PowerShell*) và gõ:

```bash
git clone https://github.com/ju44n-9999/Automated-waste-sorting-using-CV.git
cd Automated-waste-sorting-using-CV
```

---

## 5. Đặt tên riêng cho nhóm

> ⚠️ **Bước này bắt buộc nếu lớp có nhiều nhóm.** Nếu hai nhóm dùng trùng tên, board servo của nhóm này có thể nghe nhầm board camera của nhóm kia.

Board camera phát một mạng WiFi riêng (gọi là *hotspot*). Board servo dựa vào **tên** mạng đó để tìm đúng camera của nhóm mình. Ví dụ nhóm 3 đặt tên `WasteCamera-03`.

**a) Board camera:** mở [waste_classifier_verllm/firmware/WasteClassifierVerLLM/wifi_config.h](waste_classifier_verllm/firmware/WasteClassifierVerLLM/wifi_config.h) và sửa 2 dòng:

```cpp
static const char *AP_SSID = "WasteCamera-03";     // tên hotspot của nhóm
static const char *AP_PASSWORD = "matkhau-nhom3";  // ít nhất 8 ký tự
```

**b) Board servo:** mở [devicesControl/include/config.h](devicesControl/include/config.h) và ghi **đúng y hệt** tên đó:

```cpp
#define CAMERA_AP_SSID      "WasteCamera-03"
```

> Hai tên phải giống nhau từng ký tự, kể cả chữ hoa, chữ thường và dấu gạch.

---

## 6. Lấy khóa Groq (AI trên mạng)

Bước này **không bắt buộc**. Nếu bỏ qua, máy vẫn chạy bằng model trên board.

1. Vào <https://console.groq.com>, đăng ký tài khoản miễn phí.
2. Chọn **API Keys** → **Create API Key**, rồi sao chép khóa vừa tạo (bắt đầu bằng `gsk_...`).
3. Mở [waste_classifier_verllm/firmware/WasteClassifierVerLLM/llm_config.h](waste_classifier_verllm/firmware/WasteClassifierVerLLM/llm_config.h) và dán khóa vào:

   ```cpp
   #define GROQ_API_KEY "gsk_xxxxxxxxxxxxxxxx"
   ```

> 🔒 **Giữ khóa bí mật.** Ai có khóa này đều dùng được tài khoản Groq của em. **Đừng commit hay push file `llm_config.h` lên GitHub sau khi đã dán khóa.** Để Git bỏ qua thay đổi của file này, chạy lệnh:
>
> ```bash
> git update-index --skip-worktree waste_classifier_verllm/firmware/WasteClassifierVerLLM/llm_config.h
> ```

---

## 7. Lắp mạch

> ⚡ **Luôn rút điện trước khi cắm hay rút dây.**

### 7.1 Board camera

```
      Board camera                  Nút nhấn
   ┌──────────────┐               ┌─────────┐
   │      GPIO 14 ├───────────────┤ chân 1  │
   │          GND ├───────────────┤ chân 2  │
   │              │               └─────────┘
   │              │               Module LED WS2812B
   │       GPIO 3 ├───────────────► DIN (IN)
   │           5V ├───────────────► VCC (5V)
   │          GND ├───────────────► GND
   └──────────────┘
```

- **Nút nhấn** chỉ cần 2 dây: GPIO 14 và GND. Board đã bật sẵn điện trở kéo lên (pull-up) bên trong nên không cần thêm điện trở.
- **Module LED**: nối vào chân **DIN/IN**, không phải chân DOUT/OUT.
- ⛔ **Không dùng các chân 4–13 và 15–18** cho linh kiện khác, vì camera đang dùng các chân này.

### 7.2 Board servo

```
   Nguồn 5V riêng (≥ 2A)
     (+) ───────┬──────────────► dây ĐỎ của servo 1
                └──────────────► dây ĐỎ của servo 2
     (−) ───────┬──────────────► dây NÂU/ĐEN của servo 1
                ├──────────────► dây NÂU/ĐEN của servo 2
                └──────────────► GND của board servo   ← BẮT BUỘC nối chung GND

   Board servo
     GPIO 4 ───────────────────► dây CAM/VÀNG (tín hiệu) của servo 1
     GPIO 5 ───────────────────► dây CAM/VÀNG (tín hiệu) của servo 2
```

- ⚠️ **Không cấp điện cho servo từ chân 5V của board.** Servo hút nhiều dòng điện làm board bị sụt áp và tự khởi động lại liên tục. Trên Serial Monitor sẽ thấy dòng `BROWNOUT_RST`.
- ⚠️ **Phải nối GND của nguồn với GND của board.** Nếu thiếu dây này, servo không hiểu tín hiệu và sẽ quay lung tung.
- **Lắp cơ khí:** đặt hai servo **đối diện nhau**. Ở vị trí nghỉ (0°), cửa phân loại đang đóng.

---

## 8. Nạp chương trình cho board servo

Board servo dễ kiểm tra hơn, nên làm board này trước.

1. Trong VS Code: **File → Open Folder…**, chọn thư mục **`devicesControl`**.
   > Phải mở đúng thư mục `devicesControl`, không mở thư mục gốc của dự án, thì PlatformIO mới nhận ra project.
2. Cắm board servo vào máy tính bằng cáp USB.
3. Ở thanh dưới cùng của VS Code, bấm nút **→ (Upload)**. Lần đầu PlatformIO phải tải bộ công cụ nên mất vài phút.
   - Thấy dòng `[SUCCESS]` là nạp xong.
   - Nếu báo lỗi `Failed to connect`: giữ nút **BOOT** trên board, nhấn rồi thả nút **RESET**, sau đó thả **BOOT** và bấm Upload lại.
4. Bấm nút **🔌 (Serial Monitor)** ở thanh dưới, rồi nhấn **RESET** trên board. Em sẽ thấy:

   ```
   [INIT] Waste Sorting Servo Controller
   [INIT] Servo 1 on GPIO 4 (Home: 0°)
   [SCAN] Looking for camera hotspot "WasteCamera-03"...
   [SCAN] Camera not found. Staying on channel 6, accepting any sender. Retry in 10 s.
   ```

   Dòng `Camera not found` là **bình thường** vì camera chưa bật.
5. **Thử servo bằng tay:** gõ vào ô nhập của Serial Monitor rồi nhấn Enter:

   | Gõ | Kết quả mong đợi |
   |---|---|
   | `metal` | Servo quay tới 20°, giữ 1,5 giây, quay về 0° |
   | `plastic` | Quay tới 45° |
   | `paper` | Quay tới 90° |

   ✅ Nếu servo quay đúng như bảng thì board servo đã sẵn sàng.

> Nhớ **đóng Serial Monitor** (bấm vào ô terminal rồi nhấn `Ctrl+C`, hoặc đóng tab đó) trước khi nạp board khác. Nếu không sẽ gặp lỗi `port is busy`.

---

## 9. Nạp chương trình cho board camera

1. Trong VS Code: **File → Open Folder…**, chọn thư mục **`waste_classifier_verllm/firmware`**.
2. Cắm board camera vào máy tính (rút board servo ra cho khỏi nhầm cổng).
3. Bấm **→ (Upload)**. Lần đầu build mất khoảng **4–5 phút**, vì model AI nặng khoảng 9 MB. Đây là chuyện bình thường.
4. Mở **Serial Monitor** và nhấn **RESET**. Em sẽ thấy:

   ```
   Hotspot: WasteCamera-03
   Setup page: http://192.168.4.1/wifi
   ESP-NOW ready on channel 6, MAC ...
   PSRAM total=8388608 ...
   Camera init: ESP_OK
   Web server ready
   ```

   Nếu thấy `Enable OPI PSRAM` hoặc `Camera init:` kèm một lỗi, xem [mục 11](#11-xử-lý-sự-cố).

### Ý nghĩa đèn LED trên board camera

| Đèn | Ý nghĩa |
|---|---|
| 🔴 nhấp nháy **chậm** | Đang khởi động |
| 🔴 sáng **liền** | Chưa có WiFi internet. Máy vẫn chụp được nhưng chỉ dùng model trên board |
| 🟢 xanh | Sẵn sàng, có thể bấm nút chụp |
| 🟡 vàng | Đang chụp và phân tích |
| 🔴 nhấp nháy **nhanh** | Lần phân tích vừa rồi bị lỗi (nháy 2 giây). Nếu nháy mãi không dừng thì camera hoặc model bị hỏng |

---

## 10. Chạy thử cả hệ thống

### 10.1 Mở trang web điều khiển

1. Dùng điện thoại hoặc laptop kết nối vào WiFi **`WasteCamera-03`** (tên nhóm em đặt ở bước 5), nhập mật khẩu nhóm.
2. Mở trình duyệt, vào **<http://192.168.4.1>**.
3. Trang **Phân loại rác** hiện ra với 2 chế độ:
   - **Thủ công:** bấm **Chụp và phân loại** để chụp một lần.
   - **Tự động:** bấm **Sẵn sàng, bắt đầu**, máy sẽ tự chụp khoảng 30 giây một lần. Bấm **Dừng** để tắt.

### 10.2 Cho camera vào WiFi có internet (để dùng Groq)

1. Trên trang web, bấm nút **Wi-Fi** ở góc phải.
2. Chọn mạng nhà hoặc mạng trường, nhập mật khẩu, bấm **Lưu và kết nối**. Board sẽ tự khởi động lại.
3. Khi kết nối được, đèn chuyển sang 🟢 và Serial Monitor in ra dòng `Wi-Fi connected. Camera URL: http://192.168.x.x`. Từ giờ em có hai cách mở trang điều khiển:
   - Vẫn vào qua hotspot `WasteCamera-03` tại `192.168.4.1`, hoặc
   - Nối điện thoại vào cùng mạng WiFi với camera rồi vào địa chỉ `192.168.x.x` vừa in ra.

> Nếu không vào được WiFi (sai mật khẩu, mạng ở xa…), board vẫn giữ hotspot và đèn sáng 🔴 liền. Muốn thử lại thì nhấn **RESET** board camera.

### 10.3 Kiểm tra hai board đã "thấy" nhau chưa

1. Bật **board camera trước**, đợi đèn hết nhấp nháy.
2. Bật board servo và mở Serial Monitor của nó. Trong vòng 10 giây phải thấy:

   ```
   [SCAN] Camera 7C:DF:A1:xx:xx:xx found on channel 6. Listening only to it.
   ```

   Số kênh có thể khác 6 nếu camera đang nối router. Board servo tự chuyển theo nên em không cần chỉnh gì.
3. Đặt rác trước camera rồi **bấm nút chụp**. Trên Serial Monitor của board servo sẽ thấy:

   ```
   [ESP-NOW] Received packet #5 with tag: "plastic"
   [SORT] Detected "plastic" -> Moving to 45° (Servo1=45°, Servo2=135°)
   ```

   ✅ **Servo quay theo đúng loại rác nghĩa là máy đã hoạt động hoàn chỉnh!**

> 💡 Bật board servo trước cũng được, vì cứ 10 giây nó lại tự tìm camera một lần. Nếu đổi WiFi cho camera trong lúc máy đang chạy, board servo sẽ tự tìm lại trong khoảng 30 giây. Muốn tìm ngay thì gõ `SCAN` vào Serial Monitor của board servo.

> Kết quả của lần chụp bằng **nút bấm** không hiện trên trang web. Muốn xem kết quả thì nhìn Serial Monitor của board camera.

---

## 11. Xử lý sự cố

| Hiện tượng | Nguyên nhân thường gặp | Cách sửa |
|---|---|---|
| Upload báo `port is busy` | Serial Monitor còn đang mở | Đóng Serial Monitor rồi upload lại |
| Upload báo `Failed to connect` | Board chưa vào chế độ nạp | Giữ **BOOT**, nhấn và thả **RESET**, thả **BOOT**, rồi upload lại |
| Máy tính không thấy cổng COM | Cáp chỉ sạc, hoặc thiếu driver | Đổi cáp khác. Cài driver CH340/CH343/CP210x |
| Serial Monitor không hiện gì | Cắm nhầm cổng USB (board có 2 cổng) | Cắm sang cổng USB còn lại, mở lại Serial Monitor |
| Board servo khởi động lại liên tục, có dòng `BROWNOUT_RST` | Servo đang lấy điện từ board | Dùng nguồn 5 V riêng (xem [mục 7.2](#72-board-servo)) |
| Servo rung hoặc quay lung tung | Thiếu dây GND chung | Nối GND nguồn với GND board |
| Servo quay mãi không dừng | Đang dùng servo 360° | Thay bằng servo 180° |
| Camera in `Enable OPI PSRAM` | Board không có PSRAM 8 MB loại OPI | Dùng board đúng loại ở [mục 2](#2-chuẩn-bị-linh-kiện) |
| Camera in `Camera init:` kèm lỗi | Cáp camera lỏng, hoặc board khác sơ đồ chân | Cắm lại cáp camera. Nếu board khác loại, sửa các chân `pin_...` trong hàm `initCamera()` của file `WasteClassifierVerLLM.ino` theo tài liệu của board |
| Đèn LED không sáng | Nối vào DOUT, hoặc thiếu GND | Nối vào chân **DIN**, kiểm tra lại GND |
| Board servo báo `Camera not found` mãi | Tên ở bước 5 không khớp, hoặc camera chưa bật | So lại `AP_SSID` và `CAMERA_AP_SSID` từng ký tự. Nạp lại cả hai board |
| Camera đã gửi (`ESP-NOW plastic to broadcast: ESP_OK`) nhưng servo không quay | Board servo đang khóa vào camera khác hoặc sai kênh | Gõ `SCAN` vào Serial Monitor của board servo |
| Servo nhận nhầm lệnh của nhóm khác | Hai nhóm trùng tên hotspot | Đặt tên riêng như [bước 5](#5-đặt-tên-riêng-cho-nhóm) |
| Board servo báo `Unknown waste tag` | Tên loại rác lạ | Chỉ gõ `metal`, `plastic`, `paper` (hoặc `paper_cardboard`) |
| Kết quả luôn là `"source":"local"` | Không có internet, hoặc thiếu khóa Groq, hoặc đang trong 30 giây chờ | Làm [bước 6](#6-lấy-khóa-groq-ai-trên-mạng) và [10.2](#102-cho-camera-vào-wifi-có-internet-để-dùng-groq) |

---

## 12. Tinh chỉnh

Mọi thông số của board servo nằm trong [devicesControl/include/config.h](devicesControl/include/config.h). Sửa xong thì **nạp lại** board servo.

| Thông số | Mặc định | Ý nghĩa |
|---|---|---|
| `ANGLE_METAL` | 20 | Góc quay cho kim loại |
| `ANGLE_PLASTICS` | 45 | Góc quay cho nhựa |
| `ANGLE_PAPER` | 90 | Góc quay cho giấy |
| `SERVO_HOME_ANGLE` | 0 | Vị trí nghỉ |
| `SERVO_HOLD_MS` | 1500 | Thời gian giữ ở vị trí phân loại (mili giây) |
| `SERVO_STEP_MS` | 10 | Tăng lên thì servo quay chậm và êm hơn |

Ở board camera, `LED_LEVEL` (đầu file `WasteClassifierVerLLM.ino`) chỉnh độ sáng đèn, từ 0 đến 255.

---

## 13. Các thư mục trong dự án

```
Automated-waste-sorting-using-CV/
├── devicesControl/               ← BOARD SERVO
│   ├── include/config.h            thông số: góc, chân, tên camera
│   ├── lib/espNowComms/            nhận tin nhắn ESP-NOW, tự tìm kênh của camera
│   ├── lib/servoControl/           điều khiển 2 servo
│   ├── lib/wasteClassifier/        đổi tên loại rác thành góc quay
│   ├── src/main.cpp                điểm bắt đầu chương trình
│   └── test/                       kiểm thử chạy trên máy tính
│
└── waste_classifier_verllm/
    ├── firmware/                 ← BOARD CAMERA
    │   └── WasteClassifierVerLLM/
    │       ├── WasteClassifierVerLLM.ino   chương trình chính
    │       ├── wifi_config.h               tên và mật khẩu hotspot
    │       ├── llm_config.h                khóa Groq
    │       ├── model_data.cpp              model AI chạy trên board
    │       └── web_page.h                  trang web điều khiển
    ├── backend/                  máy chủ Python (tùy chọn, firmware hiện chưa dùng)
    └── tests/                    kiểm thử cho backend
```

### Dành cho bạn muốn tìm hiểu thêm

- **Kiểm thử logic phân loại trên máy tính** (cần trình biên dịch `g++`):

  ```bash
  cd devicesControl
  g++ -std=c++17 -Iinclude test/test_waste_classifier.cpp lib/wasteClassifier/wasteClassifier.cpp -o test_wc && ./test_wc
  ```

- **Backend Python:** cài [Miniconda](https://docs.conda.io/en/latest/miniconda.html), rồi chạy:

  ```bash
  conda env create -f environment.yml
  conda activate waste-sorting
  cd waste_classifier_verllm && python -m pytest tests -q
  ```
