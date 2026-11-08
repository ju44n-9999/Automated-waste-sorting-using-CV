# EcoSort B — Hướng dẫn chạy máy

Firmware đã được nạp sẵn trên camera và trên board điều khiển servo. Không cài Arduino, không build và không nạp lại.

Máy không cần máy tính khi vận hành. Điện thoại hoặc máy tính bảng chỉ dùng để mở trang điều khiển.

## Bật máy

1. Cấp nguồn 5 V cho board camera và cho board điều khiển hai servo. Mass của servo phải chung với mass của board điều khiển.
2. Đợi khoảng 20 giây cho camera khởi động.
3. Đưa một vật vào giữa khung hình, gần camera, chỉ một vật mỗi lần.

Khi vừa bật, hai cánh ở vị trí giữa (kim loại).

## Mở trang điều khiển

Camera phát mạng riêng tên **WasteCamera**, mật khẩu **wastecam123**. Mạng này chỉ có sóng 2.4 GHz.

1. Trên điện thoại, kết nối Wi-Fi **WasteCamera**.
2. Mở trình duyệt và vào `http://192.168.4.1/`.
3. Trang hiện **Đang kết nối tới server** trong giây lát, rồi cho phép chụp.

Nếu camera đã được lưu vào Wi-Fi nhà:

1. Vẫn nối **WasteCamera** và mở `http://192.168.4.1/`.
2. Bấm **Wi-Fi**. Dòng trạng thái ghi địa chỉ dạng `192.168.x.x`.
3. Ngắt **WasteCamera**, nối điện thoại vào cùng Wi-Fi nhà, rồi mở `http://192.168.x.x/`.

Wi-Fi nhà phải có sóng **2.4 GHz**. Sóng chỉ 5 GHz thì camera không vào được. Khi đó vẫn dùng được mạng **WasteCamera**.

Đèn server ở góc trang:

- **Sáng:** máy gọi được Groq. Cần Wi-Fi nhà có internet.
- **Tắt:** máy dùng mô hình gắn sẵn trên camera. Vẫn phân loại và vẫn quay cánh.

## Chụp thủ công

1. Chọn **Thủ công**.
2. Đưa vật vào giữa hình đang xem trực tiếp.
3. Bấm **Chụp và phân loại**.
4. Trang hiện **Nhựa**, **Kim loại** hoặc **Giấy**. Hai cánh gạt quay theo ngăn đó.

Muốn chụp món tiếp theo mà vẫn gọi Groq, đợi ít nhất 30 giây. Bấm sớm hơn thì lần đó dùng mô hình trên máy.

## Chụp tự động

1. Chọn **Tự động**. Hình camera vẫn chạy, không bị đóng băng.
2. Đặt vật vào khung, rồi bấm **Sẵn sàng, bắt đầu**.
3. Máy chụp một lần, hiện tên ngăn, rồi đếm ngược 30 giây.
4. Ba giây cuối hiện **Sẵn sàng chụp lại sau … giây**. Hết giờ, máy tự chụp tiếp.
5. Bấm **Dừng** khi muốn ngừng.

Trong 30 giây chờ, thay vật trong khung để lần sau phân loại đúng món mới.

## Cánh gạt

Hai servo quay cùng lúc. Cánh phải lắp đối xứng nên góc của nó là góc đối diện của cánh trái.

| Kết quả trên trang | Cánh trái | Cánh phải | Vị trí |
| --- | --- | --- | --- |
| Nhựa | 30° | 150° | Một phía |
| Kim loại | 90° | 90° | Giữa |
| Giấy | 150° | 30° | Phía còn lại |

Kết quả luôn là một trong ba ngăn. Nếu Groq không chọn được nhựa, kim loại hoặc giấy, máy dùng mô hình trên camera để chọn một trong ba.

## Khi gặp sự cố

- Không thấy **WasteCamera:** đưa điện thoại sát camera, tắt ưu tiên sóng 5 GHz, đợi thêm 20 giây rồi quét lại.
- Vào được mạng nhưng không mở trang: kiểm tra địa chỉ `http://192.168.4.1/` khi đang nối **WasteCamera**. Ở Wi-Fi nhà thì dùng địa chỉ ghi trong mục Wi-Fi của trang.
- Đèn server không sáng: máy vẫn chạy bằng mô hình trên camera. Muốn đèn sáng thì camera phải vào Wi-Fi 2.4 GHz có internet.
- Cánh không quay: kiểm tra nguồn 5 V của servo và mass chung với board điều khiển. Board điều khiển phải đang bật. Camera và board này cần ở gần nhau vì nhãn được gửi bằng ESP-NOW, không qua router.
