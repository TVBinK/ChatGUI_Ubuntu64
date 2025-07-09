#include <gtk/gtk.h>
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
#include <pthread.h>
#include <time.h>

// Định nghĩa các hằng số
#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define MAX_USERNAME 32
#define MAX_PASSWORD 32
#define MAX_MESSAGE 256
#define MAX_ENCRYPTED 512
#define MAX_CLIENTS 10

// Cấu trúc dữ liệu cho thiết bị crypto
struct lab9_data {
    char operation;
    char input[256];
    char key[32];
    char result[512];
};

// Cấu trúc tin nhắn phát sóng từ server
#pragma pack(1)
struct broadcast_msg {
    char username[MAX_USERNAME];
    char encrypted_msg[MAX_ENCRYPTED];
};
#pragma pack()

// Cấu trúc dữ liệu cho GUI
typedef struct {
    GtkWidget *window;
    GtkWidget *login_window;
    GtkWidget *main_window;
    GtkWidget *username_entry;
    GtkWidget *password_entry;
    GtkWidget *chat_text_view;
    GtkWidget *message_entry;
    GtkWidget *send_button;
    GtkWidget *users_list;
    GtkWidget *status_label;
    GtkTextBuffer *chat_buffer;
    int sock;
    int crypto_fd;
    char username[MAX_USERNAME];
    char shared_key[17];
    char cipher_algo;
    pthread_t receive_thread;
    int connected;
    GtkWidget *current_window;
} ChatGUI;

// Biến toàn cục
ChatGUI *gui_data = NULL;

// Định nghĩa struct cho dữ liệu truyền vào g_idle_add
typedef struct {
    char username[MAX_USERNAME];
    char message[MAX_MESSAGE];
    char type[16];
} ChatMsgData;

// Prototype cho add_message_to_chat
void add_message_to_chat(const char *username, const char *message, const char *type);

// Callback cho g_idle_add
static gboolean add_message_to_chat_idle(gpointer data) {
    ChatMsgData *msg = (ChatMsgData *)data;
    add_message_to_chat(msg->username, msg->message, msg->type);
    free(msg);
    return FALSE;
}

// Hàm mã hóa tin nhắn
int encrypt_message(const char *input, char *output) {
    struct lab9_data data;
    memset(&data, 0, sizeof(data));
    data.operation = '2';
    strncpy(data.input, input, sizeof(data.input) - 1);
    memcpy(data.key, gui_data->shared_key, 16);
    data.key[16] = '\0';

    if (write(gui_data->crypto_fd, &data, sizeof(data)) != sizeof(data)) {
        return -1;
    }

    if (read(gui_data->crypto_fd, &data, sizeof(data)) != sizeof(data)) {
        return -1;
    }

    strncpy(output, data.result, sizeof(data.result) - 1);
    return 0;
}

// Hàm giải mã tin nhắn
int decrypt_message(const char *input, char *output) {
    if (!input || strlen(input) == 0) {
        return -1;
    }
    if (strlen(input) % 32 != 0) {
        return -1;
    }

    struct lab9_data data;
    memset(&data, 0, sizeof(data));
    data.operation = '4';
    strncpy(data.input, input, sizeof(data.input) - 1);
    memcpy(data.key, gui_data->shared_key, 16);
    data.key[16] = '\0';

    if (write(gui_data->crypto_fd, &data, sizeof(data)) != sizeof(data)) {
        return -1;
    }

    if (read(gui_data->crypto_fd, &data, sizeof(data)) != sizeof(data)) {
        return -1;
    }

    strncpy(output, data.result, sizeof(data.result) - 1);
    return 0;
}

// Hàm nhận đủ số byte từ socket
int recv_all(int sock, void *buffer, size_t len) {
    size_t total = 0;
    char *buf = (char *)buffer;
    while (total < len) {
        int bytes = recv(sock, buf + total, len - total, 0);
        if (bytes <= 0) return bytes;
        total += bytes;
    }
    return total;
}

// Hàm nhận dữ liệu với timeout (dùng cho các trường hợp nhận ít byte)
int receive_data(int sock, void *buffer, size_t len, int timeout_sec) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sock, &read_fds);
    struct timeval tv = { .tv_sec = timeout_sec, .tv_usec = 0 };

    int ret = select(sock + 1, &read_fds, NULL, NULL, &tv);
    if (ret <= 0) {
        return -1;
    }

    int bytes_received = recv(sock, buffer, len, 0);
    if (bytes_received <= 0) {
        return -1;
    }
    return bytes_received;
}

// Hàm nhận cấu hình thuật toán từ server
int receive_config(int sock) {
    char config_msg[1];
    int bytes_received = receive_data(sock, config_msg, sizeof(config_msg), 5);
    if (bytes_received != 1) {
        return -1;
    }
    gui_data->cipher_algo = config_msg[0];
    return 0;
}

// Hàm thêm tin nhắn vào chat buffer
void add_message_to_chat(const char *username, const char *message, const char *type) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[26];
    strftime(time_str, 26, "%H:%M:%S", tm_info);
    
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(gui_data->chat_buffer, &iter);
    
    char formatted_msg[1024];
    if (strcmp(type, "system") == 0) {
        snprintf(formatted_msg, sizeof(formatted_msg), "[%s] %s\n", time_str, message);
        gtk_text_buffer_insert(gui_data->chat_buffer, &iter, formatted_msg, -1);
    } else if (strcmp(type, "received") == 0) {
        snprintf(formatted_msg, sizeof(formatted_msg), "[%s] %s: %s\n", time_str, username, message);
        gtk_text_buffer_insert(gui_data->chat_buffer, &iter, formatted_msg, -1);
    } else if (strcmp(type, "sent") == 0) {
        snprintf(formatted_msg, sizeof(formatted_msg), "[%s] Bạn: %s\n", time_str, message);
        gtk_text_buffer_insert(gui_data->chat_buffer, &iter, formatted_msg, -1);
    }
    
    // Tự động cuộn xuống cuối
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(gui_data->chat_text_view), &iter, 0.0, FALSE, 0.0, 0.0);
}

// Thread nhận tin nhắn từ server
void* receive_messages(void *arg) {
    (void)arg;
    while (gui_data->connected) {
        struct broadcast_msg msg;
        int bytes_received = recv_all(gui_data->sock, &msg, sizeof(msg));
        if (bytes_received == sizeof(msg)) {
            char decrypted_msg[MAX_MESSAGE];
            if (decrypt_message(msg.encrypted_msg, decrypted_msg) == 0) {
                ChatMsgData *msg_data = malloc(sizeof(ChatMsgData));
                strncpy(msg_data->username, msg.username, MAX_USERNAME);
                strncpy(msg_data->message, decrypted_msg, MAX_MESSAGE);
                strcpy(msg_data->type, "received");
                g_idle_add(add_message_to_chat_idle, msg_data);
            }
        } else if (bytes_received < 0) {
            break;
        }
    }
    return NULL;
}

// Callback cho nút gửi tin nhắn
void on_send_button_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    const char *message_text = gtk_entry_get_text(GTK_ENTRY(gui_data->message_entry));
    
    if (strlen(message_text) == 0) {
        return;
    }
    
    char encrypted_msg[MAX_ENCRYPTED];
    if (encrypt_message(message_text, encrypted_msg) == 0) {
        if (send(gui_data->sock, encrypted_msg, strlen(encrypted_msg), 0) > 0) {
            ChatMsgData *msg_data = malloc(sizeof(ChatMsgData));
            strncpy(msg_data->username, gui_data->username, MAX_USERNAME);
            strncpy(msg_data->message, message_text, MAX_MESSAGE);
            strcpy(msg_data->type, "sent");
            g_idle_add(add_message_to_chat_idle, msg_data);
            gtk_entry_set_text(GTK_ENTRY(gui_data->message_entry), "");
        }
    }
}

// Callback cho phím Enter trong message entry
gboolean on_message_entry_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget; (void)data;
    if (event->keyval == GDK_KEY_Return) {
        on_send_button_clicked(NULL, NULL);
        return TRUE;
    }
    return FALSE;
}

// Hàm kết nối tới server
int connect_to_server() {
    struct sockaddr_in server_addr;
    
    gui_data->sock = socket(AF_INET, SOCK_STREAM, 0);
    if (gui_data->sock < 0) {
        return -1;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);
    
    if (connect(gui_data->sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(gui_data->sock);
        return -1;
    }
    
    if (receive_config(gui_data->sock) < 0) {
        close(gui_data->sock);
        return -1;
    }
    
    return 0;
}

// Callback cho nút đăng nhập
void on_login_button_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    const char *username = gtk_entry_get_text(GTK_ENTRY(gui_data->username_entry));
    const char *password = gtk_entry_get_text(GTK_ENTRY(gui_data->password_entry));
    
    if (strlen(username) == 0 || strlen(password) == 0) {
        gtk_label_set_text(GTK_LABEL(gui_data->status_label), "Vui lòng nhập tên người dùng và mật khẩu");
        return;
    }
    
    strncpy(gui_data->username, username, MAX_USERNAME - 1);
    gui_data->username[MAX_USERNAME - 1] = '\0';
    
    if (connect_to_server() < 0) {
        gtk_label_set_text(GTK_LABEL(gui_data->status_label), "Không thể kết nối tới server");
        return;
    }
    
    // Gửi thông tin đăng nhập
    char login_data[MAX_USERNAME + MAX_PASSWORD + 2];
    snprintf(login_data, sizeof(login_data), "%s:%s", username, password);
    
    if (send(gui_data->sock, login_data, strlen(login_data), 0) < 0) {
        gtk_label_set_text(GTK_LABEL(gui_data->status_label), "Lỗi gửi thông tin đăng nhập");
        close(gui_data->sock);
        return;
    }
    
    // Nhận phản hồi đăng nhập
    char response[MAX_MESSAGE];
    int bytes_received = receive_data(gui_data->sock, response, sizeof(response) - 1, 5);
    if (bytes_received > 0) {
        response[bytes_received] = '\0';
        
        if (strstr(response, "thành công") != NULL) {
            gui_data->connected = 1;
            
            // Ẩn cửa sổ đăng nhập và hiển thị cửa sổ chat
            gtk_widget_hide(gui_data->login_window);
            gtk_widget_show_all(gui_data->main_window);
            gui_data->current_window = gui_data->main_window;
            
            // Bắt đầu thread nhận tin nhắn
            pthread_create(&gui_data->receive_thread, NULL, receive_messages, NULL);
            
            add_message_to_chat(NULL, "Đã kết nối tới server", "system");
            gtk_label_set_text(GTK_LABEL(gui_data->status_label), "Đã kết nối");
        } else {
            gtk_label_set_text(GTK_LABEL(gui_data->status_label), response);
            close(gui_data->sock);
        }
    } else {
        gtk_label_set_text(GTK_LABEL(gui_data->status_label), "Không nhận được phản hồi từ server");
        close(gui_data->sock);
    }
}

// Callback cho nút đăng ký
void on_register_button_clicked(GtkWidget *widget, gpointer data) {
    (void)widget; (void)data;
    const char *username = gtk_entry_get_text(GTK_ENTRY(gui_data->username_entry));
    const char *password = gtk_entry_get_text(GTK_ENTRY(gui_data->password_entry));
    
    if (strlen(username) == 0 || strlen(password) == 0) {
        gtk_label_set_text(GTK_LABEL(gui_data->status_label), "Vui lòng nhập tên người dùng và mật khẩu");
        return;
    }
    
    if (connect_to_server() < 0) {
        gtk_label_set_text(GTK_LABEL(gui_data->status_label), "Không thể kết nối tới server");
        return;
    }
    
    // Gửi yêu cầu đăng ký
    char register_data[MAX_USERNAME + MAX_PASSWORD + 10];
    snprintf(register_data, sizeof(register_data), "REGISTER:%s:%s", username, password);
    
    if (send(gui_data->sock, register_data, strlen(register_data), 0) < 0) {
        gtk_label_set_text(GTK_LABEL(gui_data->status_label), "Lỗi gửi yêu cầu đăng ký");
        close(gui_data->sock);
        return;
    }
    
    // Nhận phản hồi đăng ký
    char response[MAX_MESSAGE];
    int bytes_received = receive_data(gui_data->sock, response, sizeof(response) - 1, 5);
    if (bytes_received > 0) {
        response[bytes_received] = '\0';
        gtk_label_set_text(GTK_LABEL(gui_data->status_label), response);
    } else {
        gtk_label_set_text(GTK_LABEL(gui_data->status_label), "Không nhận được phản hồi từ server");
    }
    
    close(gui_data->sock);
}

// Callback cho phím Enter trong password entry
gboolean on_password_entry_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget; (void)data;
    if (event->keyval == GDK_KEY_Return) {
        on_login_button_clicked(NULL, NULL);
        return TRUE;
    }
    return FALSE;
}

// Callback khi đóng cửa sổ
gboolean on_window_destroy(GtkWidget *widget, GdkEvent *event, gpointer data) {
    (void)widget; (void)event; (void)data;
    if (gui_data->connected) {
        gui_data->connected = 0;
        pthread_join(gui_data->receive_thread, NULL);
        close(gui_data->sock);
    }
    if (gui_data->crypto_fd >= 0) {
        close(gui_data->crypto_fd);
    }
    gtk_main_quit();
    return FALSE;
}

// Tạo cửa sổ đăng nhập
void create_login_window() {
    gui_data->login_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(gui_data->login_window), "Chat Group - Đăng nhập");
    gtk_window_set_default_size(GTK_WINDOW(gui_data->login_window), 400, 300);
    gtk_window_set_position(GTK_WINDOW(gui_data->login_window), GTK_WIN_POS_CENTER);
    gtk_widget_set_name(gui_data->login_window, "login-window");
    g_signal_connect(gui_data->login_window, "destroy", G_CALLBACK(on_window_destroy), NULL);
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(gui_data->login_window), vbox);
    gtk_widget_set_margin_start(vbox, 20);
    gtk_widget_set_margin_end(vbox, 20);
    gtk_widget_set_margin_top(vbox, 20);
    gtk_widget_set_margin_bottom(vbox, 20);
    
    // Tiêu đề
    GtkWidget *title_label = gtk_label_new("CHAT GROUP");
    gtk_widget_set_name(title_label, "title-label");
    gtk_widget_set_halign(title_label, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox), title_label, FALSE, FALSE, 10);
    
    // Username
    GtkWidget *username_label = gtk_label_new("Tên người dùng:");
    gtk_widget_set_halign(username_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), username_label, FALSE, FALSE, 5);
    
    gui_data->username_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(gui_data->username_entry), "Nhập tên người dùng");
    gtk_box_pack_start(GTK_BOX(vbox), gui_data->username_entry, FALSE, FALSE, 5);
    
    // Password
    GtkWidget *password_label = gtk_label_new("Mật khẩu:");
    gtk_widget_set_halign(password_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), password_label, FALSE, FALSE, 5);
    
    gui_data->password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(gui_data->password_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(gui_data->password_entry), "Nhập mật khẩu");
    g_signal_connect(gui_data->password_entry, "key-press-event", G_CALLBACK(on_password_entry_key_press), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), gui_data->password_entry, FALSE, FALSE, 5);
    
    // Buttons
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(button_box, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 20);
    
    GtkWidget *login_button = gtk_button_new_with_label("Đăng nhập");
    g_signal_connect(login_button, "clicked", G_CALLBACK(on_login_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), login_button, FALSE, FALSE, 5);
    
    GtkWidget *register_button = gtk_button_new_with_label("Đăng ký");
    g_signal_connect(register_button, "clicked", G_CALLBACK(on_register_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(button_box), register_button, FALSE, FALSE, 5);
    
    // Status label
    gui_data->status_label = gtk_label_new("");
    gtk_widget_set_halign(gui_data->status_label, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox), gui_data->status_label, FALSE, FALSE, 10);
    
    gtk_widget_show_all(gui_data->login_window);
    gui_data->current_window = gui_data->login_window;
}

    // Tạo cửa sổ chat chính
void create_main_window() {
    gui_data->main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(gui_data->main_window), "Chat Group");
    gtk_window_set_default_size(GTK_WINDOW(gui_data->main_window), 800, 600);
    gtk_window_set_position(GTK_WINDOW(gui_data->main_window), GTK_WIN_POS_CENTER);
    gtk_widget_set_name(gui_data->main_window, "main-window");
    g_signal_connect(gui_data->main_window, "destroy", G_CALLBACK(on_window_destroy), NULL);
    
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_add(GTK_CONTAINER(gui_data->main_window), main_box);
    
    // Panel bên trái - Chat area
    GtkWidget *chat_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_name(chat_panel, "chat-panel");
    gtk_widget_set_margin_start(chat_panel, 5);
    gtk_widget_set_margin_end(chat_panel, 5);
    gtk_widget_set_margin_top(chat_panel, 5);
    gtk_widget_set_margin_bottom(chat_panel, 5);
    gtk_box_pack_start(GTK_BOX(main_box), chat_panel, TRUE, TRUE, 0);
    
    // Chat text view
    GtkWidget *chat_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(chat_scrolled, 500, 400);
    gtk_box_pack_start(GTK_BOX(chat_panel), chat_scrolled, TRUE, TRUE, 5);
    
    gui_data->chat_text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(gui_data->chat_text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(gui_data->chat_text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(gui_data->chat_text_view), FALSE);
    gtk_container_add(GTK_CONTAINER(chat_scrolled), gui_data->chat_text_view);
    
    gui_data->chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gui_data->chat_text_view));
    
    // Message input area
    GtkWidget *input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_name(input_box, "input-area");
    gtk_box_pack_start(GTK_BOX(chat_panel), input_box, FALSE, FALSE, 5);
    
    gui_data->message_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(gui_data->message_entry), "Nhập tin nhắn...");
    g_signal_connect(gui_data->message_entry, "key-press-event", G_CALLBACK(on_message_entry_key_press), NULL);
    gtk_box_pack_start(GTK_BOX(input_box), gui_data->message_entry, TRUE, TRUE, 0);
    
    gui_data->send_button = gtk_button_new_with_label("Gửi");
    gtk_widget_set_name(gui_data->send_button, "send-button");
    g_signal_connect(gui_data->send_button, "clicked", G_CALLBACK(on_send_button_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(input_box), gui_data->send_button, FALSE, FALSE, 0);
    
    // Panel bên phải - Users list
    GtkWidget *users_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_name(users_panel, "users-panel");
    gtk_widget_set_margin_start(users_panel, 5);
    gtk_widget_set_margin_end(users_panel, 5);
    gtk_widget_set_margin_top(users_panel, 5);
    gtk_widget_set_margin_bottom(users_panel, 5);
    gtk_widget_set_size_request(users_panel, 200, -1);
    gtk_box_pack_start(GTK_BOX(main_box), users_panel, FALSE, FALSE, 0);
    
    GtkWidget *users_label = gtk_label_new("Người dùng online");
    gtk_widget_set_halign(users_label, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(users_panel), users_label, FALSE, FALSE, 5);
    
    GtkWidget *users_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(users_panel), users_scrolled, TRUE, TRUE, 5);
    
    gui_data->users_list = gtk_list_box_new();
    gtk_container_add(GTK_CONTAINER(users_scrolled), gui_data->users_list);
    
    // Status bar
    GtkWidget *status_bar = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(users_panel), status_bar, FALSE, FALSE, 5);
    gtk_statusbar_push(GTK_STATUSBAR(status_bar), 0, "Sẵn sàng");
    
    gtk_widget_hide(gui_data->main_window);
}

// Hàm chính
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    
    // Load CSS style
    GtkCssProvider *provider = gtk_css_provider_new();
    GtkStyleContext *context = gtk_style_context_new();
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                             GTK_STYLE_PROVIDER(provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    GError *error = NULL;
    if (!gtk_css_provider_load_from_file(provider, g_file_new_for_path("style.css"), &error)) {
        g_warning("Không thể load CSS file: %s", error->message);
        g_error_free(error);
    }
    g_object_unref(provider);
    
    // Khởi tạo dữ liệu GUI
    gui_data = g_malloc0(sizeof(ChatGUI));
    gui_data->sock = -1;
    gui_data->crypto_fd = -1;
    gui_data->connected = 0;
    strcpy(gui_data->shared_key, "key123456789abcd");
    
    // Mở thiết bị crypto
    gui_data->crypto_fd = open("/dev/lab9_crypto", O_RDWR);
    if (gui_data->crypto_fd < 0) {
        fprintf(stderr, "Không thể mở /dev/lab9_crypto: %s\n", strerror(errno));
        return 1;
    }
    
    // Tạo các cửa sổ
    create_login_window();
    create_main_window();
    
    // Chạy main loop
    gtk_main();
    
    // Dọn dẹp
    if (gui_data) {
        g_free(gui_data);
    }
    
    return 0;
} 