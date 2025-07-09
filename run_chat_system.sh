#!/bin/bash

# Script để chạy toàn bộ hệ thống Chat Group
# Tác giả: Chat Group Development Team

set -e  # Dừng script nếu có lỗi

# Colors cho output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function để in thông báo với màu
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function kiểm tra command có tồn tại không
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Function kiểm tra file có tồn tại không
file_exists() {
    [ -f "$1" ]
}

# Function kiểm tra process có đang chạy không
is_process_running() {
    pgrep -f "$1" >/dev/null 2>&1
}

# Function dừng process
stop_process() {
    local process_name="$1"
    if is_process_running "$process_name"; then
        print_status "Dừng $process_name..."
        pkill -f "$process_name" || true
        sleep 2
    fi
}

# Function kiểm tra dependencies
check_dependencies() {
    print_status "Kiểm tra dependencies..."
    
    # Kiểm tra GCC
    if ! command_exists gcc; then
        print_error "GCC không được cài đặt. Vui lòng cài đặt: sudo apt-get install build-essential"
        exit 1
    fi
    
    # Kiểm tra pkg-config
    if ! command_exists pkg-config; then
        print_error "pkg-config không được cài đặt. Vui lòng cài đặt: sudo apt-get install pkg-config"
        exit 1
    fi
    
    # Kiểm tra GTK+3
    if ! pkg-config --exists gtk+-3.0; then
        print_error "GTK+3 không được cài đặt. Vui lòng cài đặt: sudo apt-get install libgtk-3-dev"
        exit 1
    fi
    
    # Kiểm tra make
    if ! command_exists make; then
        print_error "Make không được cài đặt. Vui lòng cài đặt: sudo apt-get install make"
        exit 1
    fi
    
    print_success "Tất cả dependencies đã sẵn sàng!"
}

# Function cài đặt kernel module
setup_kernel_module() {
    print_status "Thiết lập kernel module..."
    
    # Kiểm tra module đã load chưa
    if lsmod | grep -q lab9; then
        print_success "Kernel module lab9 đã được load"
    else
        # Biên dịch kernel module
        if file_exists "Makefile"; then
            print_status "Biên dịch kernel module..."
            make -f Makefile
        else
            print_warning "Không tìm thấy Makefile cho kernel module"
            return 1
        fi
        
        # Load module
        if file_exists "lab9.ko"; then
            print_status "Load kernel module..."
            sudo insmod lab9.ko || print_warning "Không thể load module (có thể đã load rồi)"
        else
            print_warning "Không tìm thấy lab9.ko"
            return 1
        fi
    fi
    
    # Tạo device file nếu chưa có
    if [ ! -e "/dev/lab9_crypto" ]; then
        print_status "Tạo device file..."
        sudo mknod /dev/lab9_crypto c 240 0
        sudo chmod 666 /dev/lab9_crypto
        print_success "Đã tạo /dev/lab9_crypto"
    else
        print_success "Device file /dev/lab9_crypto đã tồn tại"
    fi
}

# Function biên dịch ứng dụng
build_applications() {
    print_status "Biên dịch ứng dụng..."
    
    # Biên dịch server
    if file_exists "chat_server.c"; then
        print_status "Biên dịch chat server..."
        gcc -Wall -o chat_server chat_server.c -lpthread
        print_success "Đã biên dịch chat_server"
    fi
    
    # Biên dịch GUI client
    if file_exists "chat_gui.c"; then
        print_status "Biên dịch GUI client..."
        make -f Makefile.gui
        print_success "Đã biên dịch chat_gui"
    fi
    
    # Biên dịch console client
    if file_exists "chat_client.c"; then
        print_status "Biên dịch console client..."
        gcc -Wall -o chat_client chat_client.c -lpthread
        print_success "Đã biên dịch chat_client"
    fi
}

# Function chạy server
run_server() {
    print_status "Khởi động chat server..."
    
    if ! file_exists "chat_server"; then
        print_error "Không tìm thấy chat_server. Vui lòng biên dịch trước."
        return 1
    fi
    
    # Dừng server cũ nếu có
    stop_process "chat_server"
    
    # Chạy server trong background
    ./chat_server &
    SERVER_PID=$!
    
    # Đợi server khởi động
    sleep 2
    
    if is_process_running "chat_server"; then
        print_success "Server đã khởi động với PID: $SERVER_PID"
        echo $SERVER_PID > .server.pid
    else
        print_error "Không thể khởi động server"
        return 1
    fi
}

# Function chạy GUI client
run_gui_client() {
    print_status "Khởi động GUI client..."
    
    if ! file_exists "chat_gui"; then
        print_error "Không tìm thấy chat_gui. Vui lòng biên dịch trước."
        return 1
    fi
    
    # Dừng GUI client cũ nếu có
    stop_process "chat_gui"
    
    # Chạy GUI client
    ./chat_gui &
    GUI_PID=$!
    
    print_success "GUI client đã khởi động với PID: $GUI_PID"
    echo $GUI_PID > .gui.pid
}

# Function chạy console client
run_console_client() {
    print_status "Khởi động console client..."
    
    if ! file_exists "chat_client"; then
        print_error "Không tìm thấy chat_client. Vui lòng biên dịch trước."
        return 1
    fi
    
    # Dừng console client cũ nếu có
    stop_process "chat_client"
    
    # Chạy console client
    ./chat_client &
    CONSOLE_PID=$!
    
    print_success "Console client đã khởi động với PID: $CONSOLE_PID"
    echo $CONSOLE_PID > .console.pid
}

# Function dừng tất cả
stop_all() {
    print_status "Dừng tất cả processes..."
    
    # Dừng các processes
    stop_process "chat_server"
    stop_process "chat_gui"
    stop_process "chat_client"
    
    # Xóa PID files
    rm -f .server.pid .gui.pid .console.pid
    
    print_success "Đã dừng tất cả processes"
}

# Function hiển thị trạng thái
show_status() {
    print_status "Trạng thái hệ thống:"
    
    if is_process_running "chat_server"; then
        print_success "✓ Server đang chạy"
    else
        print_error "✗ Server không chạy"
    fi
    
    if is_process_running "chat_gui"; then
        print_success "✓ GUI client đang chạy"
    else
        print_error "✗ GUI client không chạy"
    fi
    
    if is_process_running "chat_client"; then
        print_success "✓ Console client đang chạy"
    else
        print_error "✗ Console client không chạy"
    fi
    
    if [ -e "/dev/lab9_crypto" ]; then
        print_success "✓ Kernel module đã sẵn sàng"
    else
        print_error "✗ Kernel module chưa sẵn sàng"
    fi
}

# Function hiển thị help
show_help() {
    echo "Chat Group System Management Script"
    echo ""
    echo "Usage: $0 [COMMAND]"
    echo ""
    echo "Commands:"
    echo "  start       - Khởi động toàn bộ hệ thống (server + GUI client)"
    echo "  server      - Chỉ khởi động server"
    echo "  gui         - Chỉ khởi động GUI client"
    echo "  console     - Chỉ khởi động console client"
    echo "  stop        - Dừng tất cả processes"
    echo "  status      - Hiển thị trạng thái hệ thống"
    echo "  build       - Biên dịch tất cả ứng dụng"
    echo "  setup       - Thiết lập kernel module"
    echo "  check       - Kiểm tra dependencies"
    echo "  help        - Hiển thị help này"
    echo ""
    echo "Examples:"
    echo "  $0 start    # Khởi động toàn bộ hệ thống"
    echo "  $0 stop     # Dừng tất cả"
    echo "  $0 status   # Xem trạng thái"
}

# Main script
case "${1:-help}" in
    "start")
        check_dependencies
        setup_kernel_module
        build_applications
        run_server
        sleep 1
        run_gui_client
        print_success "Hệ thống đã khởi động thành công!"
        print_status "Sử dụng '$0 status' để kiểm tra trạng thái"
        print_status "Sử dụng '$0 stop' để dừng hệ thống"
        ;;
    "server")
        check_dependencies
        setup_kernel_module
        build_applications
        run_server
        ;;
    "gui")
        check_dependencies
        build_applications
        run_gui_client
        ;;
    "console")
        check_dependencies
        setup_kernel_module
        build_applications
        run_console_client
        ;;
    "stop")
        stop_all
        ;;
    "status")
        show_status
        ;;
    "build")
        build_applications
        ;;
    "setup")
        setup_kernel_module
        ;;
    "check")
        check_dependencies
        ;;
    "help"|*)
        show_help
        ;;
esac 