# Waste Classifier VerLLM — Groq / OpenAI

Firmware thứ hai sao chép từ `firmware/arduino/WasteClassifier`. Giữ nguyên camera,
GPIO, Wi-Fi portal/NVS và ESP-NOW; thay TFLite bằng JPEG → FastAPI → Vision API.
Không sửa/xóa firmware gốc hoặc code bo servo `Esp32Core`.

## Điền key Groq và chạy backend

Tại thư mục `waste_classifier_verllm`, sao chép `.env.example` thành `.env` nếu chưa
có file này, rồi điền:

```dotenv
LLM_PROVIDER=groq
GROQ_API_KEY=gsk_key_cua_ban
GROQ_MODEL=qwen/qwen3.8-27b
MOCK_MODE=false
GROQ_MIN_INTERVAL_SECONDS=30
GROQ_MAX_CALLS_24H=75
```

Key chỉ nằm ở backend, không điền vào firmware. Khởi động lại sau khi đổi `.env`.

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-dev.txt
.\.venv\Scripts\python.exe -m uvicorn backend.app:app --host 0.0.0.0 --port 8001
```

Chạy một worker, một backend cho thiết bị demo. Backend dùng OpenAI Python SDK
với base URL `https://api.groq.com/openai/v1`, Chat Completions, JSON mode;
Pydantic kiểm tra nhãn plastic/metal/paper/unknown. Không gửi strict schema của
OpenAI cho Groq. Thinking tắt, output tối đa 100 token. `IMAGE_DETAIL` chỉ áp dụng OpenAI.

Để thử không cần key: đặt `MOCK_MODE=true`, `MOCK_LABEL=unknown` (không gửi lệnh servo).
Mock plastic/metal/paper giúp thử tuyến servo, nhưng luôn trả nhãn cố định.

## Nhịp phân loại cho gói miễn phí

Mặc định **30 giây/lần**, tối đa khoảng 2 lần/phút. Giao diện tự động chờ 30 giây
sau kết quả, firmware chặn cả thao tác thủ công quá nhanh. Preview `/capture`
vẫn chạy liên tục; preview không gọi Groq và không tiêu quota API.

Tài liệu Groq kiểm tra ngày 07/11/2026: Qwen 3.8 27B hỗ trợ ảnh, mỗi ảnh tính
2.048 input token. Bảng giới hạn công bố 30 RPM, 1.000 RPD, 8.000 TPM, 200.000 TPD;
giới hạn chính xác phải xem trong tài khoản. Model chỉ nhận text không dùng được cho ảnh.

Ước tính ảnh + prompt + output khoảng 2.300–2.500 token/lượt: 2 lượt/phút khoảng
4.600–5.000 token/phút. Mặc định chặn bảo thủ **75 lượt trong 24 giờ trượt**
(khoảng 172.500–187.500 token ước tính). Đây không phải cam kết quota, chưa đo usage
bằng key thật; ứng dụng khác trong cùng tổ chức cũng dùng chung quota Groq.

- Chỉ phân loại khi có vật, ưu tiên chụp thủ công cho demo.
- 30 giây/lần phù hợp demo ngắn, không chạy liên tục cả ngày: 75 lượt hết trong
  khoảng 38 phút hoặc lâu hơn tùy thời gian API.
- Muốn phân bố đều cả ngày: chọn **20 phút/lần**, khoảng 72 lượt/ngày.
  Đổi `LLM_CAPTURE_INTERVAL_MS=1200000` trong firmware và
  `GROQ_MIN_INTERVAL_SECONDS=1200` trong `.env`, rồi build/nạp lại và restart backend.
- Bộ đếm lưu ở `runtime/groq_usage.sqlite3`, còn sau restart. Không xóa file khi
  deploy nếu muốn giữ quota. Mỗi attempt gọi upstream, kể cả lỗi, tính một lượt;
  mock/preview không tính. Hết quota trả 429 và `Retry-After`, không gọi Groq.
- Groq trả 429: backend/firmware đợi ít nhất 60 giây hoặc lâu hơn theo header;
  giao diện dừng tự động và hiện thời gian chờ. Bấm sẵn sàng lại sau đó.
- Không tự retry ảnh cũ. Unknown/lỗi không gửi lệnh servo.

Nguồn: [Vision](https://console.groq.com/docs/vision),
[rate limits](https://console.groq.com/docs/rate-limits),
[Qwen 3.8](https://console.groq.com/docs/model/qwen/qwen3.8-27b),
[giới hạn tài khoản](https://console.groq.com/settings/limits).

## Nạp firmware

1. Sửa `firmware/WasteClassifierVerLLM/llm_config.h`: `LLM_BACKEND_URL` là
   `http://<IPv4-PC>:8001/classify`, không dùng localhost.
2. PC và ESP32 cùng LAN tin cậy; cho phép TCP 8001 qua firewall mạng Private.
   ESP32 → backend dùng HTTP LAN; backend → Groq dùng HTTPS.
3. Nếu đặt `DEVICE_TOKEN` trong `.env`, đặt cùng giá trị ở `LLM_DEVICE_TOKEN`.
   Đây là token LAN riêng, không phải key Groq.
4. Arduino IDE: mở `firmware/WasteClassifierVerLLM/WasteClassifierVerLLM.ino`,
   ESP32 core 3.2.1, ArduinoJson 7.4.2, ESP32S3 Dev Module, flash 16MB, OPI PSRAM,
   USB CDC On Boot Disabled. Giữ các cấu hình upload như bản cũ.
5. Hoặc từ thư mục này:

   ```powershell
   pio run -d firmware -e waste_s3_verllm
   pio run -d firmware -e waste_s3_verllm -t upload
   ```

6. Wi-Fi NVS `waste-wifi` đọc lại thông tin cũ; hoặc kết nối hotspot WasteCamera,
   mở `http://192.168.4.1/wifi`, cấu hình Wi-Fi rồi mở IP LAN camera để phân loại.

Giữ nguyên Esp32Core: servo GPIO 18/19; góc trái nhựa/kim loại/giấy 30/90/150,
góc phải 180 trừ trái. Hai bo cần cùng kênh Wi-Fi. Nhãn paper đổi thành
paper_cardboard cho giao diện/receiver cũ. HTTP trả score:null và scores:null;
packet nhị phân dùng -1 là sentinel không có confidence, không phải xác suất.

## API và kiểm tra

```powershell
curl.exe http://127.0.0.1:8001/health
curl.exe -X POST http://127.0.0.1:8001/classify -H "Content-Type: image/jpeg" --data-binary "@C:\path\image.jpg"
.\.venv\Scripts\python.exe -m pytest -c pytest.ini tests -q
```

Request là JPEG nhị phân (không phải JSON/multipart); response backend:
`{"label":"plastic"}`, `{"label":"metal"}`, `{"label":"paper"}`, `{"label":"unknown"}`.
Lỗi có dạng `{"detail":"..."}`: 400 ảnh sai, 401 token sai, 408 upload timeout,
413 ảnh lớn, 415 MIME sai, 429 busy/quota/rate limit, 502 upstream/output sai,
504 API timeout. Không log key/ảnh. Một request xử lý tại một thời điểm mỗi process.

30 tests passed bằng client giả: ảnh, payload Groq/OpenAI, output sai, timeout,
quota và backoff còn sau restart. Chưa thử key Groq thật hoặc camera/servo thực.
Artifact build ở `../storage/arduino-build-verllm/`; không tự upload lên thiết bị.
Firmware Groq/cooldown đã build thành công với ESP32 core 3.2.1, ArduinoJson 7.4.2:
1.052.290 byte chương trình, 51.488 byte RAM tĩnh. Đã đối chiếu lại cả 32 hash
firmware gốc, tất cả không đổi.

## File và giới hạn

- Firmware bản sao: thay inference bằng HTTP, thêm cooldown và khoảng chờ giao diện.
  Camera, Wi-Fi, ESP-NOW giữ nguyên; wifi_config.h/partitions.csv sao chép nguyên byte.
- `backend/app.py`: provider Groq/OpenAI, mock, validation và xử lý lỗi.
- `backend/quota.py`: quota rolling 24 giờ và cooldown lưu SQLite.
- `.env.example`, `requirements*.txt`: cấu hình và dependencies riêng.
- `tests/`: kiểm tra backend không tiêu token.
- `original_firmware_sha256.json`: đối chiếu 32 file firmware gốc không đổi.

Không thêm HC-SR04 hoặc state machine lấy vật ra. Lấy vật ra/chờ servo hoàn tất
trước lần tiếp theo. ESP-NOW không có ACK chuyển động. HTTP đồng bộ nên UI có thể
chờ lúc gọi API. `model_ready` chỉ kiểm tra URL cấu hình, không đảm bảo provider online.
Response backend phải có Content-Length và không dài quá 256 byte.

API timeout mặc định là 45 giây (`OPENAI_TIMEOUT_SECONDS`, áp dụng cả Groq).
Firmware đợi HTTP tối đa 60 giây để chừa thời gian upload. Nếu test trả 504 đúng
ngưỡng timeout thì upstream chưa trả lời kịp; tăng timeout không đảm bảo API sẽ
thành công. Không tự retry vì request đã gửi có thể vẫn được provider tính quota.
Sau khi đổi `.env` cần restart backend; smoke_test tự đọc lại mỗi lần chạy.

Muốn dùng lại OpenAI: `LLM_PROVIDER=openai`, điền `OPENAI_API_KEY`,
`OPENAI_MODEL=gpt-4.1-mini`, `MOCK_MODE=false`. Nhánh Responses API/strict schema giữ
nguyên. Quota Groq không áp dụng OpenAI; nhịp 30 giây của firmware vẫn có hiệu lực.
