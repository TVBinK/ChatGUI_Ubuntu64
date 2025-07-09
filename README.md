# Chat Group GUI Application

Ứng dụng chat nhóm với giao diện đồ họa sử dụng GTK+ cho Ubuntu/Linux.

## Tính năng

- 🎨 **Giao diện đồ họa đẹp mắt** với GTK+3
- 🔐 **Mã hóa tin nhắn** bằng AES
- 👥 **Chat nhóm** với nhiều người dùng
- 👤 **Hệ thống đăng nhập/đăng ký**
- 📱 **Giao diện responsive** và thân thiện người dùng
- ⏰ **Hiển thị thời gian** cho mỗi tin nhắn
- 🔄 **Tự động cuộn** tin nhắn mới
- 📋 **Danh sách người dùng online**

## Yêu cầu hệ thống

- Ubuntu 18.04 trở lên hoặc Linux distribution tương tự
- GTK+3 development libraries
- GCC compiler
- Kernel module lab9_crypto (cho mã hóa)

## Cài đặt

### 1. Cài đặt dependencies

```bash
# Cài đặt các package cần thiết
sudo apt-get update
sudo apt-get install -y build-essential libgtk-3-dev pkg-config

# Hoặc sử dụng Makefile
make install-deps
```

### 2. Biên dịch ứng dụng

```bash
# Kiểm tra dependencies
make check-deps

# Biên dịch ứng dụng
make

# Hoặc biên dịch và chạy ngay
make run
```

### 3. Cài đặt kernel module (nếu chưa có)

```bash
# Biên dịch kernel module
make -f Makefile

# Load module
sudo insmod lab9.ko

# Tạo device file
sudo mknod /dev/lab9_crypto c 240 0
sudo chmod 666 /dev/lab9_crypto
```

## Sử dụng

### Chạy ứng dụng

```bash
# Chạy trực tiếp
./chat_gui

# Hoặc nếu đã cài đặt
chat_gui
```

### Hướng dẫn sử dụng

1. **Đăng ký tài khoản mới**:
   - Nhập tên người dùng và mật khẩu
   - Nhấn nút "Đăng ký"

2. **Đăng nhập**:
   - Nhập tên người dùng và mật khẩu đã đăng ký
   - Nhấn nút "Đăng nhập" hoặc Enter

3. **Gửi tin nhắn**:
   - Nhập tin nhắn vào ô nhập liệu
   - Nhấn nút "Gửi" hoặc Enter

4. **Xem tin nhắn**:
   - Tin nhắn sẽ hiển thị trong khung chat
   - Tự động cuộn xuống tin nhắn mới nhất

## Cấu trúc dự án

```
ubuntu64/
├── chat_gui.c          # Ứng dụng GUI chính
├── chat_client.c       # Client console (cũ)
├── chat_server.c       # Server
├── kernel.c            # Kernel module
├── style.css           # CSS styling cho GUI
├── Makefile.gui        # Makefile cho GUI app
├── Makefile            # Makefile cho kernel module
└── README.md           # Hướng dẫn này
```

## Tính năng kỹ thuật

### Mã hóa
- Sử dụng AES encryption cho tin nhắn
- Khóa chia sẻ 16-byte
- Tích hợp với kernel module `/dev/lab9_crypto`

### Giao diện
- GTK+3 với CSS styling
- Responsive design
- Dark/Light theme support
- Custom styling cho buttons, entries, và text views

### Network
- TCP socket communication
- Multi-threaded message handling
- Timeout handling cho network operations

## Troubleshooting

### Lỗi "Không thể mở /dev/lab9_crypto"
```bash
# Kiểm tra module đã load chưa
lsmod | grep lab9

# Load module nếu chưa có
sudo insmod lab9.ko

# Tạo device file
sudo mknod /dev/lab9_crypto c 240 0
sudo chmod 666 /dev/lab9_crypto
```

### Lỗi "GTK+3 không được cài đặt"
```bash
sudo apt-get install libgtk-3-dev pkg-config
```

### Lỗi kết nối server
- Đảm bảo server đang chạy: `./chat_server`
- Kiểm tra port 8080 không bị block
- Kiểm tra firewall settings

## Phát triển

### Thêm tính năng mới

1. **Thêm emoji support**:
   - Tích hợp emoji picker
   - Unicode emoji rendering

2. **File sharing**:
   - Upload/download files
   - Progress bar cho transfer

3. **Voice chat**:
   - Audio streaming
   - Voice recording

4. **Group management**:
   - Tạo/xóa groups
   - Invite/remove members

### Customization

#### Thay đổi theme
Chỉnh sửa file `style.css`:

```css
/* Dark theme */
window {
    background-color: #2c3e50;
    color: #ecf0f1;
}
```

#### Thay đổi port
Chỉnh sửa trong `chat_gui.c`:

```c
#define PORT 8080  // Thay đổi port ở đây
```

## License

Dự án này được phát triển cho mục đích học tập và nghiên cứu.

## Contributing

1. Fork dự án
2. Tạo feature branch
3. Commit changes
4. Push to branch
5. Tạo Pull Request

## Support

Nếu gặp vấn đề, vui lòng:
1. Kiểm tra phần Troubleshooting
2. Tạo issue trên GitHub
3. Liên hệ developer

---

**Lưu ý**: Đảm bảo server `chat_server` đang chạy trước khi sử dụng client GUI. 