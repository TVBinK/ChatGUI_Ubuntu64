
// Bao gồm các header cần thiết
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>

// Định nghĩa các hằng số
#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define MAX_USERNAME 32
#define MAX_PASSWORD 32
#define MAX_MESSAGE 256
#define MAX_ENCRYPTED 512

// Cấu trúc dữ liệu cho thiết bị crypto
struct lab9_data {
    char operation;         // '2': AES mã hóa, '4': AES giải mã, '5': MD5
    char input[256];
    char key[32];
    char result[512];
};

// Cấu trúc tin nhắn phát sóng từ server
struct broadcast_msg {
    char username[MAX_USERNAME];
    char encrypted_msg[MAX_ENCRYPTED];
};

// Biến toàn cục
char shared_key[17] = "key123456789abcd"; // Khóa 16 byte cho AES
int crypto_fd = -1;
char cipher_algo = '2'; // Thuật toán mã hóa: AES

// Hàm mã hóa tin nhắn
int encrypt_message(const char *input, char *output) {
    struct lab9_data data;
    memset(&data, 0, sizeof(data));
    data.operation = '2';
    strncpy(data.input, input, sizeof(data.input) - 1);
    memcpy(data.key, shared_key, 16);
    data.key[16] = '\0';

    printf("Debug: operation=%c, input='%s', key=[HIDDEN], key_len=%zu\n", 
           data.operation, data.input, strlen(data.key));

    if (write(crypto_fd, &data, sizeof(data)) != sizeof(data)) {
        fprintf(stderr, "Không thể mã hóa tin nhắn: %s\n", strerror(errno));
        return -1;
    }

    if (read(crypto_fd, &data, sizeof(data)) != sizeof(data)) {
        fprintf(stderr, "Không thể đọc tin nhắn mã hóa: %s\n", strerror(errno));
        return -1;
    }

    strncpy(output, data.result, sizeof(data.result) - 1);
    return 0;
}

// Hàm giải mã tin nhắn
int decrypt_message(const char *input, char *output) {
    // Kiểm tra đầu vào
    if (!input || strlen(input) == 0) {
        fprintf(stderr, "Lỗi: Tin nhắn mã hóa rỗng hoặc không hợp lệ\n");
        return -1;
    }
    if (strlen(input) % 32 != 0) { // Chuỗi hex phải chia hết cho 32 (16 byte binary)
        fprintf(stderr, "Lỗi: Độ dài tin nhắn mã hóa không hợp lệ (%zu)\n", strlen(input));
        return -1;
    }

    struct lab9_data data;
    memset(&data, 0, sizeof(data));
    data.operation = '4';
    strncpy(data.input, input, sizeof(data.input) - 1);
    memcpy(data.key, shared_key, 16);
    data.key[16] = '\0';

    printf("Debug: operation=%c, input='%s', key=[HIDDEN], key_len=%zu\n", 
           data.operation, data.input, strlen(data.key));

    if (write(crypto_fd, &data, sizeof(data)) != sizeof(data)) {
        fprintf(stderr, "Không thể giải mã tin nhắn: %s\n", strerror(errno));
        return -1;
    }

    if (read(crypto_fd, &data, sizeof(data)) != sizeof(data)) {
        fprintf(stderr, "Không thể đọc tin nhắn giải mã: %s\n", strerror(errno));
        return -1;
    }

    strncpy(output, data.result, sizeof(data.result) - 1);
    return 0;
}

// Hàm nhận dữ liệu với timeout
int receive_data(int sock, void *buffer, size_t len, int timeout_sec) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);
    struct timeval tv = { .tv_sec = timeout_sec, .tv_usec = 0 };

    int ret = select(sock + 1, &read_fds, NULL, NULL, &tv);
    if (ret < 0) {
        fprintf(stderr, "Lỗi select: %s\n", strerror(errno));
        return -1;
    }
    if (ret == 0) {
        fprintf(stderr, "Hết thời gian chờ nhận dữ liệu\n");
        return -1;
    }

    int bytes_received = recv(sock, buffer, len, 0);
    if (bytes_received < 0) {
        fprintf(stderr, "Không thể nhận dữ liệu: %s\n", strerror(errno));
        return -1;
    }
    if (bytes_received == 0) {
        fprintf(stderr, "Server đã ngắt kết nối\n");
        return -1;
    }
    return bytes_received;
}

// Hàm nhận cấu hình thuật toán từ server
int receive_config(int sock) {
    char config_msg[1];
    int bytes_received = receive_data(sock, config_msg, sizeof(config_msg), 5);
    if (bytes_received != 1) {
        fprintf(stderr, "Độ dài thông báo cấu hình không hợp lệ: %d\n", bytes_received);
        return -1;
    }
    cipher_algo = config_msg[0];
    printf("Nhận thuật toán mã hóa: %c (%s)\n", cipher_algo, cipher_algo == '2' ? "AES" : "Unknown");
    if (cipher_algo != '2') {
        fprintf(stderr, "Lỗi: Server yêu cầu thuật toán không phải AES\n");
        return -1;
    }
    return 0;
}

// Hàm chính
int main() {
    int sock = -1;
    struct sockaddr_in server_addr;

    setbuf(stdin, NULL);

    // Mở thiết bị crypto
    crypto_fd = open("/dev/lab9_crypto", O_RDWR);
    if (crypto_fd < 0) {
        fprintf(stderr, "Không thể mở /dev/lab9_crypto: %s\n", strerror(errno));
        return 1;
    }

    printf("Debug: shared_key=[HIDDEN], key_len=%zu\n", strlen(shared_key));

    // Tạo socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "Tạo socket thất bại: %s\n", strerror(errno));
        close(crypto_fd);
        return 1;
    }

    // Cấu hình địa chỉ server
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    // Kết nối tới server
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Kết nối thất bại: %s\n", strerror(errno));
        close(sock);
        close(crypto_fd);
        return 1;
    }
    printf("Kết nối tới server trên cổng %d\n", PORT);

    // Nhận cấu hình thuật toán từ server
    if (receive_config(sock) < 0) {
        close(sock);
        close(crypto_fd);
        return 1;
    }

    // Nhập tên người dùng
    char username[MAX_USERNAME], password[MAX_PASSWORD];
    printf("Nhập tên người dùng: ");
    fflush(stdout);
    if (fgets(username, sizeof(username), stdin) == NULL) {
        fprintf(stderr, "Lỗi khi đọc tên người dùng\n");
        close(sock);
        close(crypto_fd);
        return 1;
    }
    username[strcspn(username, "\n")] = '\0';
    if (strlen(username) == 0) {
        fprintf(stderr, "Tên người dùng không được rỗng\n");
        close(sock);
        close(crypto_fd);
        return 1;
    }
    printf("Gửi tên người dùng: '%s'\n", username);

    // Nhập mật khẩu
    printf("Nhập mật khẩu: ");
    fflush(stdout);
    if (fgets(password, sizeof(password), stdin) == NULL) {
        fprintf(stderr, "Lỗi khi đọc mật khẩu\n");
        close(sock);
        close(crypto_fd);
        return 1;
    }
    password[strcspn(password, "\n")] = '\0';
    if (strlen(password) == 0) {
        fprintf(stderr, "Mật khẩu không được rỗng\n");
        close(sock);
        close(crypto_fd);
        return 1;
    }
    printf("Gửi mật khẩu: [HIDDEN]\n");

    // Gửi tên người dùng và mật khẩu
    char username_buf[MAX_USERNAME] = {0};
    char password_buf[MAX_PASSWORD] = {0};
    strncpy(username_buf, username, MAX_USERNAME - 1);
    strncpy(password_buf, password, MAX_PASSWORD - 1);

    if (send(sock, username_buf, MAX_USERNAME, 0) != MAX_USERNAME) {
        fprintf(stderr, "Không thể gửi tên người dùng: %s\n", strerror(errno));
        close(sock);
        close(crypto_fd);
        return 1;
    }
    printf("Debug: Gửi %d byte cho tên người dùng\n", MAX_USERNAME);

    if (send(sock, password_buf, MAX_PASSWORD, 0) != MAX_PASSWORD) {
        fprintf(stderr, "Không thể gửi mật khẩu: %s\n", strerror(errno));
        close(sock);
        close(crypto_fd);
        return 1;
    }
    printf("Debug: Gửi %d byte cho mật khẩu\n", MAX_PASSWORD);

    // Nhận kết quả đăng nhập
    int login_result;
    int bytes_received = receive_data(sock, &login_result, sizeof(int), 5);
    if (bytes_received != sizeof(int)) {
        fprintf(stderr, "Kích thước kết quả đăng nhập không hợp lệ: %d\n", bytes_received);
        close(sock);
        close(crypto_fd);
        return 1;
    }
    printf("Nhận kết quả đăng nhập: %d\n", login_result);

    // Nhận thông báo lỗi
    char error_msg[MAX_MESSAGE];
    bytes_received = receive_data(sock, error_msg, MAX_MESSAGE, 5);
    if (bytes_received <= 0) {
        fprintf(stderr, "Không thể nhận thông báo lỗi: %s\n", bytes_received < 0 ? strerror(errno) : "Server ngắt kết nối");
        close(sock);
        close(crypto_fd);
        return 1;
    }
    error_msg[bytes_received < MAX_MESSAGE ? bytes_received - 1 : MAX_MESSAGE - 1] = '\0';
    printf("Nhận thông báo lỗi: '%s'\n", error_msg);

    // Kiểm tra kết quả đăng nhập
    if (login_result == 0) {
        printf("Đăng nhập thành công cho người dùng %s: %s\n", username, error_msg);
        printf("Bắt đầu chat (gõ 'exit' để thoát):\n");
    } else {
        printf("Đăng nhập thất bại cho người dùng %s: %s\n", username, error_msg);
        close(sock);
        close(crypto_fd);
        return 1;
    }

    // Vòng lặp xử lý chat
    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);
        int max_fd = sock;

        struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if (activity < 0) {
            fprintf(stderr, "Lỗi select: %s\n", strerror(errno));
            continue;
        }

        // Xử lý nhập từ bàn phím
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char message[MAX_MESSAGE];
            if (fgets(message, sizeof(message), stdin) == NULL) {
                printf("Lỗi khi đọc tin nhắn\n");
                break;
            }
            message[strcspn(message, "\n")] = '\0';

            if (strcmp(message, "exit") == 0) {
                break;
            }

            if (strlen(message) == 0) {
                printf("Tin nhắn rỗng, không gửi\n");
                continue;
            }

            char encrypted_msg[MAX_ENCRYPTED];
            if (encrypt_message(message, encrypted_msg) == 0) {
                if (send(sock, encrypted_msg, strlen(encrypted_msg) + 1, 0) <= 0) {
                    fprintf(stderr, "Không thể gửi tin nhắn: %s\n", strerror(errno));
                    break;
                }
                printf("Gửi tin nhắn mã hóa: %s\n", encrypted_msg);
            } else {
                fprintf(stderr, "Không thể mã hóa tin nhắn\n");
                continue;
            }
        }

        // Xử lý phản hồi từ server
        if (FD_ISSET(sock, &read_fds)) {
            struct broadcast_msg msg;
            int len = recv(sock, &msg, sizeof(msg), 0);
            if (len <= 0) {
                printf("Server đã ngắt kết nối hoặc lỗi: %s\n", len < 0 ? strerror(errno) : "Kết nối đóng");
                break;
            }
            if (len != sizeof(msg)) {
                fprintf(stderr, "Kích thước tin nhắn không hợp lệ: %d (mong đợi %zu)\n", len, sizeof(msg));
                continue;
            }

            msg.username[MAX_USERNAME - 1] = '\0';
            msg.encrypted_msg[MAX_ENCRYPTED - 1] = '\0';

            printf("Debug: Nhận từ server, username='%s', encrypted_msg='%s'\n", 
                   msg.username, msg.encrypted_msg);

            char decrypted_reply[MAX_MESSAGE];
            if (decrypt_message(msg.encrypted_msg, decrypted_reply) == 0) {
                printf("%s: %s\n", msg.username, decrypted_reply);
            } else {
                fprintf(stderr, "Không thể giải mã tin nhắn từ %s\n", msg.username);
            }
        }
    }

    // Đóng socket và thiết bị crypto
    if (sock >= 0) close(sock);
    if (crypto_fd >= 0) close(crypto_fd);
    printf("Client đã kết thúc\n");
    return 0;
}