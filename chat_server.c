
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
#include <sys/select.h>

// Định nghĩa các hằng số
#define PORT 8080
#define MAX_CLIENTS 10
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

// Cấu trúc lưu thông tin người dùng
struct user {
    char username[MAX_USERNAME];
    char password_hash[33];
};

// Cấu trúc tin nhắn phát sóng
#pragma pack(1)
struct broadcast_msg {
    char username[MAX_USERNAME];
    char encrypted_msg[MAX_ENCRYPTED];
};
#pragma pack()

// Biến toàn cục
char cipher_algo = '2';
char hash_algo = '5';
char shared_key[17] = "key123456789abcd";
int crypto_fd;

// Hàm băm mật khẩu bằng MD5
int hash_password(const char *password, char *hash) {
    struct lab9_data data;
    memset(&data, 0, sizeof(data));
    data.operation = '5';
    strncpy(data.input, password, sizeof(data.input) - 1);

    printf("Debug: Băm mật khẩu, operation=%c, input=[HIDDEN]\n", data.operation);

    if (write(crypto_fd, &data, sizeof(data)) != sizeof(data)) {
        fprintf(stderr, "Không thể ghi vào thiết bị crypto: %s\n", strerror(errno));
        return -1;
    }

    if (read(crypto_fd, &data, sizeof(data)) != sizeof(data)) {
        fprintf(stderr, "Không thể đọc từ thiết bị crypto: %s\n", strerror(errno));
        return -1;
    }

    strncpy(hash, data.result, 32);
    hash[32] = '\0';
    return 0;
}

// Hàm lưu thông tin người dùng vào file
int save_user(const char *username, const char *password) {
    char hash[33];
    if (hash_password(password, hash) < 0) {
        fprintf(stderr, "Không thể băm mật khẩu cho người dùng %s\n", username);
        return -1;
    }

    FILE *fp = fopen("users.txt", "a");
    if (!fp) {
        fprintf(stderr, "Không thể mở users.txt: %s\n", strerror(errno));
        return -1;
    }

    fprintf(fp, "%s:%s\n", username, hash);
    fclose(fp);
    printf("Đã lưu người dùng %s với hash %s\n", username, hash);
    return 0;
}

// Hàm xác thực người dùng
int verify_user(const char *username, const char *password, char *error_msg) {
    FILE *fp = fopen("users.txt", "r");
    if (!fp) {
        snprintf(error_msg, MAX_MESSAGE, "Lỗi server: Không thể mở users.txt (%s)", strerror(errno));
        fprintf(stderr, "%s\n", error_msg);
        return -1;
    }

    char line[256], file_username[MAX_USERNAME], file_hash[33];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        if (sscanf(line, "%31[^:]:%32s", file_username, file_hash) != 2) {
            snprintf(error_msg, MAX_MESSAGE, "Lỗi server: Định dạng không hợp lệ trong users.txt");
            fprintf(stderr, "%s\n", error_msg);
            fclose(fp);
            return -1;
        }

        if (strcmp(username, file_username) == 0) {
            char input_hash[33];
            if (hash_password(password, input_hash) < 0) {
                snprintf(error_msg, MAX_MESSAGE, "Lỗi server: Không thể băm mật khẩu");
                fprintf(stderr, "%s\n", error_msg);
                fclose(fp);
                return -1;
            }
            fclose(fp);
            if (strcmp(input_hash, file_hash) == 0) {
                snprintf(error_msg, MAX_MESSAGE, "Đăng nhập thành công");
                printf("Người dùng %s đăng nhập thành công\n", username);
                return 0;
            } else {
                snprintf(error_msg, MAX_MESSAGE, "Mật khẩu không đúng");
                printf("Người dùng %s đăng nhập thất bại: mật khẩu không đúng\n", username);
                return -2;
            }
        }
    }

    fclose(fp);
    snprintf(error_msg, MAX_MESSAGE, "Tên người dùng không tồn tại");
    printf("Người dùng %s đăng nhập thất bại: tên người dùng không tồn tại\n", username);
    return -1;
}

// Hàm tạo người dùng mới
void create_user() {
    char username[MAX_USERNAME], password[MAX_PASSWORD];
    printf("Nhập tên người dùng: ");
    fflush(stdout);
    if (fgets(username, sizeof(username), stdin) == NULL) {
        printf("Lỗi khi đọc tên người dùng\n");
        return;
    }
    username[strcspn(username, "\n")] = '\0';

    printf("Nhập mật khẩu: ");
    fflush(stdout);
    if (fgets(password, sizeof(password), stdin) == NULL) {
        printf("Lỗi khi đọc mật khẩu\n");
        return;
    }
    password[strcspn(password, "\n")] = '\0';

    if (strlen(username) == 0 || strlen(password) == 0) {
        printf("Tên người dùng và mật khẩu không được rỗng\n");
        return;
    }

    if (save_user(username, password) == 0) {
        printf("Người dùng %s được tạo thành công\n", username);
    } else {
        printf("Không thể tạo người dùng %s\n", username);
    }
}

// Hàm chính
int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_sockets[MAX_CLIENTS];
    char client_users[MAX_CLIENTS][MAX_USERNAME];
    int client_count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_users[i][0] = '\0';
    }

    crypto_fd = open("/dev/lab9_crypto", O_RDWR);
    if (crypto_fd < 0) {
        fprintf(stderr, "Không thể mở /dev/lab9_crypto: %s\n", strerror(errno));
        return 1;
    }

    printf("Debug: shared_key=[HIDDEN], key_len=%zu\n", strlen(shared_key));

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        fprintf(stderr, "Tạo socket thất bại: %s\n", strerror(errno));
        close(crypto_fd);
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        fprintf(stderr, "Setsockopt thất bại: %s\n", strerror(errno));
        close(server_fd);
        close(crypto_fd);
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        fprintf(stderr, "Bind thất bại: %s\n", strerror(errno));
        close(server_fd);
        close(crypto_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        fprintf(stderr, "Listen thất bại: %s\n", strerror(errno));
        close(server_fd);
        close(crypto_fd);
        return 1;
    }

    printf("Server đã khởi động trên cổng %d\n", PORT);

    while (1) {
        char choice[10];
        printf("\nMenu:\n1. Tạo người dùng\n2. Bắt đầu chat\n3. Thoát\nLựa chọn: ");
        fflush(stdout);
        if (fgets(choice, sizeof(choice), stdin) == NULL) {
            printf("Lỗi khi đọc lựa chọn menu\n");
            continue;
        }
        choice[strcspn(choice, "\n")] = '\0';

        if (choice[0] == '1') {
            create_user();
        } else if (choice[0] == '2') {
            printf("Đang chờ client...\n");
            while (client_count < MAX_CLIENTS) {
                fd_set read_fds;
                FD_ZERO(&read_fds);
                FD_SET(server_fd, &read_fds);
                int max_fd = server_fd;
                for (int i = 0; i < client_count; i++) {
                    FD_SET(client_sockets[i], &read_fds);
                    if (client_sockets[i] > max_fd) max_fd = client_sockets[i];
                }

                struct timeval timeout = { .tv_sec = 5, .tv_usec = 0 };
                int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
                if (activity < 0) {
                    fprintf(stderr, "Lỗi select: %s\n", strerror(errno));
                    continue;
                }

                if (FD_ISSET(server_fd, &read_fds)) {
                    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                    if (client_fd < 0) {
                        fprintf(stderr, "Accept thất bại: %s\n", strerror(errno));
                        continue;
                    }

                    if (send(client_fd, &cipher_algo, 1, 0) <= 0) {
                        printf("Không thể gửi thuật toán mã hóa: %s\n", strerror(errno));
                        close(client_fd);
                        continue;
                    }
                    printf("Debug: Đã gửi cipher_algo %c\n", cipher_algo);

                    char login_data[MAX_USERNAME + MAX_PASSWORD + 2];
                    int bytes_received = recv(client_fd, login_data, sizeof(login_data) - 1, 0);
                    if (bytes_received <= 0) {
                        printf("Không thể nhận thông tin đăng nhập từ client, bytes: %d (%s)\n", bytes_received, strerror(errno));
                        close(client_fd);
                        continue;
                    }
                    login_data[bytes_received] = '\0';

                    char username[MAX_USERNAME], password[MAX_PASSWORD];
                    char *sep = strchr(login_data, ':');
                    if (sep) {
                        *sep = '\0';
                        strncpy(username, login_data, MAX_USERNAME - 1);
                        username[MAX_USERNAME - 1] = '\0';
                        strncpy(password, sep + 1, MAX_PASSWORD - 1);
                        password[MAX_PASSWORD - 1] = '\0';
                    } else {
                        printf("Dữ liệu đăng nhập không đúng định dạng: '%s'\n", login_data);
                        close(client_fd);
                        continue;
                    }
                    printf("Nhận username: '%s', password: [HIDDEN]\n", username);

                    int user_exists = 0;
                    for (int i = 0; i < client_count; i++) {
                        if (client_users[i][0] == '\0') continue;
                        if (strcmp(client_users[i], username) == 0) {
                            user_exists = 1;
                            break;
                        }
                    }

                    char error_msg[MAX_MESSAGE];
                    int login_result = verify_user(username, password, error_msg);
                    printf("Kết quả đăng nhập cho %s: %d, Thông báo: %s\n", username, login_result, error_msg);

                    if (user_exists) {
                        snprintf(error_msg, MAX_MESSAGE, "Tên người dùng %s đã đăng nhập", username);
                        login_result = -3;
                        printf("Người dùng %s bị từ chối: đã đăng nhập\n", username);
                    }

                    if (send(client_fd, error_msg, strlen(error_msg) + 1, 0) <= 0) {
                        printf("Không thể gửi thông báo lỗi tới client %s: %s\n", username, strerror(errno));
                        close(client_fd);
                        continue;
                    }
                    printf("Debug: Đã gửi error_msg '%s'\n", error_msg);

                    if (login_result == 0) {
                        client_sockets[client_count] = client_fd;
                        strncpy(client_users[client_count], username, MAX_USERNAME - 1);
                        client_users[client_count][MAX_USERNAME - 1] = '\0';
                        client_count++;
                        printf("Người dùng %s đã kết nối (tổng client: %d)\n", username, client_count);
                    } else {
                        printf("Đóng kết nối cho %s do đăng nhập thất bại: %s\n", username, error_msg);
                        close(client_fd);
                    }
                }

                for (int i = 0; i < client_count; i++) {
                    if (FD_ISSET(client_sockets[i], &read_fds)) {
                        char encrypted_msg[MAX_ENCRYPTED];
                        int len = recv(client_sockets[i], encrypted_msg, sizeof(encrypted_msg) - 1, 0);
                        if (len <= 0) {
                            printf("Người dùng %s đã ngắt kết nối (recv trả về %d: %s)\n", 
                                   client_users[i], len, strerror(errno));
                            close(client_sockets[i]);
                            for (int j = i; j < client_count - 1; j++) {
                                client_sockets[j] = client_sockets[j + 1];
                                strcpy(client_users[j], client_users[j + 1]);
                            }
                            client_count--;
                            continue;
                        }

                        encrypted_msg[len] = '\0';
                        if (strlen(encrypted_msg) == 0 || strlen(encrypted_msg) % 32 != 0) {
                            printf("Lỗi: Tin nhắn mã hóa từ %s không hợp lệ (rỗng hoặc độ dài %zu không chia hết cho 32)\n",
                                   client_users[i], strlen(encrypted_msg));
                            continue;
                        }

                        printf("Debug: Nhận tin nhắn mã hóa từ %s: %s (len=%d)\n", 
                               client_users[i], encrypted_msg, len);

                        struct broadcast_msg msg;
                        strncpy(msg.username, client_users[i], MAX_USERNAME - 1);
                        msg.username[MAX_USERNAME - 1] = '\0';
                        strncpy(msg.encrypted_msg, encrypted_msg, MAX_ENCRYPTED - 1);
                        msg.encrypted_msg[MAX_ENCRYPTED - 1] = '\0';

                        for (int j = 0; j < client_count; j++) {
                            if (j != i) {
                                if (send(client_sockets[j], &msg, sizeof(msg), 0) <= 0) {
                                    printf("Không thể gửi tin nhắn tới client %s: %s\n", 
                                           client_users[j], strerror(errno));
                                }
                            }
                        }
                        printf("Đã chuyển tiếp tin nhắn mã hóa từ %s đến các client khác\n", client_users[i]);
                    }
                }
            }
        } else if (choice[0] == '3') {
            break;
        } else {
            printf("Lựa chọn không hợp lệ\n");
        }
    }

    for (int i = 0; i < client_count; i++) {
        close(client_sockets[i]);
    }
    close(server_fd);
    close(crypto_fd);
    printf("Server đã kết thúc\n");
    return 0;
}
