# EcoSort B — Kiến thức và câu lệnh đã dùng

Dự án dùng tin học và lập trình để nối bước nhận diện rác với bước điều khiển cánh gạt. Phần dưới đối chiếu đúng chương trình đã nạp trong máy. Khách không cần cài lại hay sửa các lệnh này.

Máy có hai board, đều đã nạp sẵn:

- **Camera ESP32-S3** chụp ảnh, nhận diện và gửi nhãn.
- **ESP32 điều khiển** nhận nhãn và quay hai servo cánh gạt.

## Ngôn ngữ, môi trường, thư viện và mô hình

| Hạng mục | Trong EcoSort B |
| --- | --- |
| Ngôn ngữ | C++ theo kiểu Arduino trên cả hai board. Trang điều khiển là HTML, CSS và JavaScript, nhúng sẵn trong firmware camera. |
| Môi trường đã dùng khi nạp | Arduino IDE, board ESP32S3 Dev Module, gói ESP32 3.2.1, flash 16 MB, phân vùng Custom, PSRAM OPI. Firmware đã nằm trên máy. |
| Thư viện camera | `WiFi`, `WebServer`, `HTTPClient`, `WiFiClientSecure`, `Preferences`, `esp_camera`, `esp_now`, ArduinoJson, Chirale TensorFlow Lite, `mbedtls/base64`. |
| Thư viện board servo | `WiFi`, `WebServer`, `esp_now`, và `ledcAttach` / `ledcWrite` của ESP32 để phát xung servo. |
| Mô hình trên máy | MobileNetV2, ảnh 128×128, số nguyên 8 bit. Ba nhãn: `plastic`, `metal`, `paper_cardboard`. |
| Mô hình qua mạng | Groq, model `qwen/qwen3.8-27b`. Chỉ gọi khi camera đã có Wi-Fi internet. Nếu không gọi được, máy dùng mô hình trên board. |

## Kiến thức và cách vận dụng

| Kiến thức / câu lệnh | Cách vận dụng trong EcoSort B |
| --- | --- |
| Cấu trúc tuần tự | `setup()` chạy lần lượt: bật Wi-Fi, kiểm tra PSRAM, `initCamera()`, `initModel()`, rồi mở web server. `loop()` liên tục gọi `server.handleClient()` để phục vụ trang và nhận lệnh chụp. |
| Cấu trúc rẽ nhánh `if` / `else` | `poseForLabel()` chọn góc theo nhãn: `plastic` 30°, `metal` 90°, `paper_cardboard` 150°. Nhãn khác trả về -1 và không quay servo. `runClassify()` ưu tiên Groq và chỉ nhận `plastic`, `metal` hoặc `paper`. Nếu Groq trả `unknown` hay nhãn lạ, máy bỏ kết quả đó và `runLocal()` chọn một trong ba nhãn. |
| Biến và kiểu dữ liệu | `label` là chuỗi nhãn. `score` là số thực, chỉ có khi dùng mô hình trên máy. `api_link` là trạng thái Groq. `capture_id` là số thứ tự lần chụp. `gateLeft` và `gateRight` là góc hai cánh. |
| Hàm / chương trình con | `initCamera()` mở camera. `initModel()` nạp mô hình trên máy. `runClassify()` và `runLocal()` nhận diện. `sendEspNow()` gửi nhãn sang board servo. `moveGate()` quay hai cánh cùng lúc. `poseForLabel()` đổi nhãn thành góc. |
| Tín hiệu vào – ra | Vào: ảnh JPEG từ camera. Ra: xung servo chân GPIO 18 (cánh trái) và GPIO 19 (cánh phải), tần số 50 Hz. Trang web trên điện thoại là màn hình của máy. Đèn server trên trang sáng khi Groq dùng được. Máy không gắn màn hình LCD hay đèn báo riêng. |
| Kiểm tra điều kiện và xử lý ngoại lệ | Không có ảnh, hoặc Groq không trả đúng một trong ba nhãn: máy dùng mô hình trên board, vốn chỉ có nhựa, kim loại và giấy. Camera hoặc mô hình chưa sẵn sàng thì nút chụp bị tắt. Hết lượt hoặc mất mạng thì cũng chuyển sang mô hình trên máy. |

## Câu lệnh thực tế trong chương trình

Camera, file `WasteClassifierVerLLM.ino`:

- `WiFi.softAP("WasteCamera", ...)` phát mạng cài đặt của máy.
- `WiFi.begin(...)` nối Wi-Fi nhà đã lưu, chỉ sóng 2.4 GHz.
- `esp_camera_fb_get()` lấy một khung hình.
- `interpreter->Invoke()` chạy mô hình trên máy.
- `http.POST(...)` gửi ảnh sang Groq khi đủ điều kiện.
- `strcmp(label, "plastic")` và các nhãn còn lại kiểm tra kết quả.
- `sendEspNow(wireLabel, ...)` phát nhãn. Nhãn `paper` từ Groq được đổi thành `paper_cardboard` trước khi gửi.

Board servo, file `Esp32Core.ino`:

- `ledcAttach(18, 50, 14)` và `ledcAttach(19, 50, 14)` tạo xung hai servo.
- `poseForLabel()` dùng `if` và `strcmp` để chọn 30, 90 hoặc 150 độ.
- `moveGate(leftAngle)` đặt cánh phải bằng `180 - leftAngle`, nên hai cánh quay cùng một phía dù lắp đối xứng.
- `onEspNow()` chỉ nhận gói có mã `WASTE`. Gói lạ bị bỏ.

Trang web trong firmware dùng `fetch("/classify")` để chụp. Chế độ tự động đếm 30 giây rồi mới chụp lần tiếp theo, cùng nhịp với giới hạn gọi Groq.
