// Bao gồm các header cần thiết cho module kernel
#include <linux/module.h>       // Hỗ trợ module kernel
#include <linux/kernel.h>       // Các macro và hàm kernel
#include <linux/fs.h>           // Hỗ trợ hệ thống file
#include <linux/cdev.h>         // Hỗ trợ character device
#include <linux/uaccess.h>      // Hỗ trợ truy cập user space
#include <linux/slab.h>         // Hỗ trợ cấp phát bộ nhớ
#include <linux/device.h>       // Hỗ trợ quản lý thiết bị
#include <linux/init.h>         // Hỗ trợ khởi tạo và dọn dẹp module
#include <linux/crypto.h>       // Hỗ trợ các hàm mã hóa
#include <crypto/skcipher.h>    // Hỗ trợ mã hóa đối xứng
#include <crypto/hash.h>        // Hỗ trợ hàm băm
#include <linux/scatterlist.h>  // Hỗ trợ scatter-gather I/O
#include <linux/err.h>          // Hỗ trợ xử lý lỗi
#include <linux/string.h>       // Hỗ trợ thao tác chuỗi

// Định nghĩa tên thiết bị và lớp thiết bị
#define DEVICE_NAME "lab9_crypto"   // Tên thiết bị
#define CLASS_NAME "lab9_class"     // Tên lớp thiết bị

// Biến toàn cục
static int major_number;            // Số major của thiết bị
static struct cdev lab9_cdev;       // Cấu trúc character device
static struct class *lab_class = NULL;  // Con trỏ tới lớp thiết bị
static struct device *lab9_device = NULL; // Con trỏ tới thiết bị

// Cấu trúc lưu trữ dữ liệu cho các thao tác mã hóa/băm
struct lab9_data {
    char operation;     // Loại thao tác: '1': DES mã hóa, '2': AES mã hóa, '3': DES giải mã, 
                        // '4': AES giải mã, '5': MD5, '6': SHA1, '7': SHA256
    char input[256];    // Dữ liệu đầu vào
    char key[32];       // Khóa (8 byte cho DES, 16 byte cho AES, bỏ qua cho băm)
    char result[512];   // Kết quả (dạng hex cho mã hóa/giải mã, đầu ra cho băm)
};

// Chuyển đổi chuỗi hex thành binary
static int crypto_hex_to_bin(const char *hex, u8 *bin, size_t bin_len) {
    size_t hex_len = strlen(hex);
    if (hex_len != bin_len * 2) {   // Kiểm tra độ dài chuỗi hex
        printk(KERN_ERR "Độ dài chuỗi hex không hợp lệ\n");
        return -EINVAL;
    }
    for (size_t i = 0; i < bin_len; i++) {
        if (sscanf(hex + i * 2, "%2hhx", &bin[i]) != 1) { // Chuyển từng cặp hex thành byte
            printk(KERN_ERR "Lỗi khi chuyển hex sang binary\n");
            return -EINVAL;
        }
    }
    return 0;
}

// Thực hiện hàm băm (MD5, SHA1, SHA256)
static int crypto_hash(struct lab9_data *data, const char *algo_name) {
    struct crypto_shash *tfm = NULL;    // Cấu trúc cho hàm băm
    u8 *hash = NULL;                    // Bộ nhớ lưu kết quả băm
    int ret = 0;

    tfm = crypto_alloc_shash(algo_name, 0, 0); // Khởi tạo hàm băm
    if (IS_ERR(tfm)) {
        printk(KERN_ERR "Không thể khởi tạo hàm băm %s: %ld\n", algo_name, PTR_ERR(tfm));
        return PTR_ERR(tfm);
    }

    hash = kmalloc(crypto_shash_digestsize(tfm), GFP_KERNEL); // Cấp phát bộ nhớ cho kết quả
    if (!hash) {
        ret = -ENOMEM;
        goto out;
    }

    // Tính toán giá trị băm
    ret = crypto_shash_tfm_digest(tfm, data->input, strlen(data->input), hash);
    if (ret) {
        printk(KERN_ERR "Tính toán băm thất bại: %d\n", ret);
        goto out;
    }

    // Chuyển kết quả băm thành chuỗi hex
    for (size_t i = 0; i < crypto_shash_digestsize(tfm); i++) {
        sprintf(data->result + i * 2, "%02x", hash[i]);
    }
    data->result[crypto_shash_digestsize(tfm) * 2] = '\0';

    printk(KERN_INFO "Kết quả băm %s: %s\n", algo_name, data->result);

out:
    kfree(hash); // Giải phóng bộ nhớ
    if (tfm)
        crypto_free_shash(tfm); // Giải phóng hàm băm
    return ret;
}

// Thêm padding theo chuẩn PKCS#5/PKCS#7
static void add_pkcs_padding(u8 *input, size_t *len, size_t block_size) {
    size_t pad_len = block_size - (*len % block_size); // Tính độ dài padding
    if (pad_len == block_size) pad_len = block_size; // Thêm một khối nếu đã đủ
    memset(input + *len, pad_len, pad_len); // Thêm padding
    *len += pad_len; // Cập nhật độ dài
}

// Bỏ padding theo chuẩn PKCS#5/PKCS#7
static int remove_pkcs_padding(u8 *input, size_t *len, size_t block_size) {
    if (*len < block_size || *len % block_size != 0) { // Kiểm tra độ dài hợp lệ
        printk(KERN_ERR "Độ dài dữ liệu padding không hợp lệ: %zu\n", *len);
        return -EINVAL;
    }
    u8 pad_value = input[*len - 1]; // Lấy giá trị padding
    if (pad_value == 0 || pad_value > block_size) { // Kiểm tra giá trị padding
        printk(KERN_ERR "Giá trị padding không hợp lệ: %u\n", pad_value);
        return -EINVAL;
    }
    for (size_t i = *len - pad_value; i < *len; i++) { // Kiểm tra các byte padding
        if (input[i] != pad_value) {
            printk(KERN_ERR "Byte padding không hợp lệ tại %zu\n", i);
            return -EINVAL;
        }
    }
    *len -= pad_value; // Bỏ padding
    return 0;
}

// Thực hiện mã hóa/giải mã (DES, AES)
static int crypto_cipher(struct lab9_data *data) {
    struct crypto_skcipher *tfm = NULL; // Cấu trúc cho mã hóa đối xứng
    struct skcipher_request *req = NULL; // Yêu cầu mã hóa
    char *algo_name; // Tên thuật toán
    int key_len; // Độ dài khóa
    int ret = 0;
    u8 iv[16]; // Vector khởi tạo (IV)

    // Xác định thuật toán, độ dài khóa và IV
    switch (data->operation) {
        case '1': case '3': // DES
            algo_name = "cbc(des)";
            key_len = 8;
            memcpy(iv, "12345678", 8); // IV 8 byte
            break;
        case '2': case '4': // AES
            algo_name = "cbc(aes)";
            key_len = 16;
            memcpy(iv, "1234567890abcdef", 16); // IV 16 byte
            break;
        default:
            printk(KERN_ERR "Thao tác mã hóa không hợp lệ: %c\n", data->operation);
            return -EINVAL;
    }

    // Kiểm tra đầu vào
    if (data->input[0] == '\0') {
        printk(KERN_ERR "Chuỗi đầu vào rỗng\n");
        return -EINVAL;
    }

    // Kiểm tra độ dài khóa
    size_t key_actual_len = strnlen(data->key, sizeof(data->key));
    if (key_actual_len < key_len) {
        printk(KERN_ERR "Độ dài khóa không hợp lệ cho %s, cần %d byte, nhận %zu\n", 
               algo_name, key_len, key_actual_len);
        return -EINVAL;
    }

    // Khởi tạo skcipher
    tfm = crypto_alloc_skcipher(algo_name, 0, 0);
    if (IS_ERR(tfm)) {
        printk(KERN_ERR "Không thể khởi tạo mã hóa %s: %ld\n", algo_name, PTR_ERR(tfm));
        return PTR_ERR(tfm);
    }

    // Thiết lập khóa
    ret = crypto_skcipher_setkey(tfm, data->key, key_len);
    if (ret) {
        printk(KERN_ERR "Thiết lập khóa thất bại cho %s: %d\n", algo_name, ret);
        goto out;
    }

    // Cấp phát yêu cầu mã hóa
    req = skcipher_request_alloc(tfm, GFP_KERNEL);
    if (!req) {
        printk(KERN_ERR "Không thể cấp phát yêu cầu skcipher\n");
        ret = -ENOMEM;
        goto out;
    }

    size_t len = strlen(data->input);
    size_t block_size = crypto_skcipher_blocksize(tfm);
    u8 *bin_input = NULL;

    if (data->operation == '3' || data->operation == '4') { // Giải mã
        // Chuyển hex thành binary
        len = strlen(data->input) / 2;
        if (len % block_size != 0 || len == 0) {
            printk(KERN_ERR "Độ dài đầu vào không hợp lệ cho giải mã: %zu\n", len);
            ret = -EINVAL;
            goto out;
        }
        bin_input = kmalloc(len, GFP_KERNEL);
        if (!bin_input) {
            ret = -ENOMEM;
            goto out;
        }
        ret = crypto_hex_to_bin(data->input, bin_input, len);
        if (ret) {
            kfree(bin_input);
            goto out;
        }

        // Thực hiện giải mã
        struct scatterlist sg[1];
        sg_init_one(&sg[0], bin_input, len);
        skcipher_request_set_crypt(req, sg, sg, len, iv);
        ret = crypto_skcipher_decrypt(req);
        if (ret) {
            printk(KERN_ERR "Giải mã thất bại cho %s: %d\n", algo_name, ret);
            kfree(bin_input);
            goto out;
        }

        // Bỏ padding
        ret = remove_pkcs_padding(bin_input, &len, block_size);
        if (ret) {
            kfree(bin_input);
            goto out;
        }
        strncpy(data->result, bin_input, len);
        data->result[len] = '\0';
    } else { // Mã hóa
        // Chuẩn bị đầu vào và thêm padding
        size_t padded_len = ((len + block_size - 1) / block_size) * block_size;
        bin_input = kzalloc(padded_len, GFP_KERNEL);
        if (!bin_input) {
            ret = -ENOMEM;
            goto out;
        }
        memcpy(bin_input, data->input, len);
        add_pkcs_padding(bin_input, &len, block_size);

        // Thực hiện mã hóa
        struct scatterlist sg[1];//du lieu dau vao
        sg_init_one(&sg[0], bin_input, len);
        skcipher_request_set_crypt(req, sg, sg, len, iv);
        ret = crypto_skcipher_encrypt(req);
        if (ret) {
            printk(KERN_ERR "Mã hóa thất bại cho %s: %d\n", algo_name, ret);
            kfree(bin_input);
            goto out;
        }

        // Chuyển kết quả thành hex
        if (len * 2 >= sizeof(data->result)) {
            printk(KERN_ERR "Bộ đệm kết quả quá nhỏ cho đầu ra hex\n");
            ret = -ENOMEM;
            kfree(bin_input);
            goto out;
        }
        for (size_t i = 0; i < len; i++) {
            sprintf(data->result + i * 2, "%02x", bin_input[i]);
        }
        data->result[len * 2] = '\0';
    }

    printk(KERN_INFO "Kết quả mã hóa cho %s: %s\n", algo_name, data->result);

out:
    kfree(bin_input); // Giải phóng bộ nhớ
    if (req)
        skcipher_request_free(req); // Giải phóng yêu cầu
    if (tfm)
        crypto_free_skcipher(tfm); // Giải phóng skcipher
    return ret;
}

// Xử lý thao tác mã hóa/băm dựa trên operation
static int crypto_process(struct lab9_data *data) {
    switch (data->operation) {
        case '1': case '2': case '3': case '4': // Mã hóa/giải mã
            return crypto_cipher(data);
        case '5': // MD5
            return crypto_hash(data, "md5");
        case '6': // SHA1
            return crypto_hash(data, "sha1");
        case '7': // SHA256
            return crypto_hash(data, "sha256");
        default:
            printk(KERN_ERR "Thao tác không hợp lệ: %c\n", data->operation);
            return -EINVAL;
    }
}

// Hàm xử lý ghi dữ liệu từ user space
static ssize_t lab9_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset) {
    struct lab9_data *data;
    int ret;

    if (len < sizeof(struct lab9_data)) { // Kiểm tra kích thước dữ liệu
        printk(KERN_ERR "Kích thước dữ liệu không hợp lệ\n");
        return -EINVAL;
    }

    data = kmalloc(sizeof(*data), GFP_KERNEL); // Cấp phát bộ nhớ
    if (!data) {
        printk(KERN_ERR "Cấp phát bộ nhớ thất bại\n");
        return -ENOMEM;
    }

    // Sao chép dữ liệu từ user space
    if (copy_from_user(data, buffer, sizeof(*data))) {
        printk(KERN_ERR "Sao chép từ user space thất bại\n");
        kfree(data);
        return -EFAULT;
    }

    // Đảm bảo chuỗi kết thúc bằng null
    data->input[sizeof(data->input) - 1] = '\0';
    data->key[sizeof(data->key) - 1] = '\0';
    data->result[sizeof(data->result) - 1] = '\0';

    printk(KERN_DEBUG "Nhận: operation=%c, input=%s, key=%s\n", data->operation, data->input, data->key);

    // Giải phóng dữ liệu cũ nếu có
    if (file->private_data) {
        kfree(file->private_data);
    }
    file->private_data = NULL;

    // Xử lý thao tác mã hóa/băm
    ret = crypto_process(data);
    if (!ret) {
        file->private_data = data; // Lưu dữ liệu nếu thành công
    } else {
        kfree(data); // Giải phóng nếu thất bại
    }

    return ret ? ret : len; // Trả về lỗi hoặc độ dài dữ liệu
}

// Hàm xử lý đọc dữ liệu về user space
static ssize_t lab9_read(struct file *file, char __user *buffer, size_t len, loff_t *offset) {
    struct lab9_data *data = file->private_data;

    if (!data) { // Kiểm tra dữ liệu
        printk(KERN_ERR "Không có dữ liệu để đọc\n");
        return -EINVAL;
    }

    if (len < sizeof(*data)) { // Kiểm tra kích thước buffer
        printk(KERN_ERR "Buffer người dùng quá nhỏ\n");
        return -EINVAL;
    }

    // Sao chép dữ liệu tới user space
    if (copy_to_user(buffer, data, sizeof(*data))) {
        printk(KERN_ERR "Sao chép tới user space thất bại\n");
        return -EFAULT;
    }

    printk(KERN_DEBUG "Kết quả đọc: %s\n", data->result);
    return sizeof(*data); // Trả về kích thước dữ liệu
}

// Hàm giải phóng tài nguyên khi đóng file
static int lab9_release(struct inode *inode, struct file *file) {
    if (file->private_data) {
        kfree(file->private_data); // Giải phóng dữ liệu
        file->private_data = NULL;
    }
    return 0;
}

// Định nghĩa các thao tác trên file
static const struct file_operations lab9_fops = {
    .owner = THIS_MODULE,
    .write = lab9_write,
    .read = lab9_read,
    .release = lab9_release,
};

// Hàm khởi tạo module
static int __init lab9_init(void) {
    dev_t dev;
    int ret;

    // Cấp phát số major/minor
    ret = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
    if (ret) {
        printk(KERN_ERR "Không thể cấp phát chrdev\n");
        return ret;
    }
    major_number = MAJOR(dev);

    // Khởi tạo character device
    cdev_init(&lab9_cdev, &lab9_fops);
    ret = cdev_add(&lab9_cdev, dev, 1);
    if (ret) {
        printk(KERN_ERR "Không thể thêm cdev\n");
        goto unregister_chrdev;
    }

    // Tạo lớp thiết bị
    lab_class = class_create(CLASS_NAME);
    if (IS_ERR(lab_class)) {
        printk(KERN_ERR "Không thể tạo lớp\n");
        ret = PTR_ERR(lab_class);
        goto delete_cdev;
    }

    // Tạo thiết bị
    lab9_device = device_create(lab_class, NULL, dev, NULL, DEVICE_NAME);
    if (IS_ERR(lab9_device)) {
        printk(KERN_ERR "Không thể tạo thiết bị\n");
        ret = PTR_ERR(lab9_device);
        goto destroy_class;
    }

    printk(KERN_INFO "Module lab9_crypto được khởi tạo\n");
    return 0;

destroy_class:
    class_destroy(lab_class);
delete_cdev:
    cdev_del(&lab9_cdev);
unregister_chrdev:
    unregister_chrdev_region(dev, 1);
    return ret;
}

// Hàm dọn dẹp module
static void __exit lab9_exit(void) {
    device_destroy(lab_class, MKDEV(major_number, 0)); // Xóa thiết bị
    class_destroy(lab_class); // Xóa lớp
    cdev_del(&lab9_cdev); // Xóa character device
    unregister_chrdev_region(MKDEV(major_number, 0), 1); // Giải phóng số major/minor
    printk(KERN_INFO "Module lab9_crypto đã được gỡ bỏ\n");
}

// Đăng ký hàm khởi tạo và dọn dẹp
module_init(lab9_init);
module_exit(lab9_exit);

// Thông tin module
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Trinh Van Binh");
MODULE_DESCRIPTION("Character driver cho AES, DES, MD5, SHA1, SHA256");