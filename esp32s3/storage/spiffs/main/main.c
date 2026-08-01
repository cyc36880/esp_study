/**
 * 可以在cmakelists中配置 spiffs_create_partition_image 以构建 spiffs 分区的镜像文件
 */



#include <stdio.h>
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include <dirent.h> // 注意包含正确的头文件
#include "esp_log.h"

static const char *TAG = "spiffs";

static void spiffs_init(void);
static void list_dir(void);
static void file_read(void);


void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    spiffs_init();
    list_dir();
    file_read();
}


static void spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    // 挂载、初始化 spiffs
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));    

    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s). Formatting...", esp_err_to_name(ret));
        esp_spiffs_format(conf.partition_label);
        return;
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
}

/**
 * 列出 spiffs 分区中的所有文件
 * 由于spiffs是扁平化的文件系统，所有文件都在根目录下，没有目录结构，
 * 所以看似会自动递归列出所有文件，但实际上只是列出根目录下的文件，在
 * littlefs等文件分区系统中，每个文件都有自己的目录，所以不能递归列出所有文件。
 */
static void list_dir(void)
{
    DIR *dir = opendir("/spiffs");
    if (dir == NULL)
    {
        ESP_LOGE(TAG, "Failed to open directory: %s", "/spiffs");
        return;
    }

    ESP_LOGI(TAG, "Listing files in: %s", "/spiffs");
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        // 打印文件名
        ESP_LOGI(TAG, "  %s", entry->d_name);
    }

    closedir(dir);
}

// 读取文件内容
static void file_read(void)
{
    ESP_LOGI(TAG, "Reading file");
    FILE *f = fopen("/spiffs/test.txt", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }
    char line[64];
    fgets(line, sizeof(line), f);
    ESP_LOGI(TAG, "Read line: %s", line);
    fclose(f);
}
