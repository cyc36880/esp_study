#include <stdio.h>
#include "nvs_flash.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_err.h"
#include <dirent.h> // DIR, dirent

static const char *TAG = "littlefs";

static void littlefs_init(void);
static void list_dir(void);
static void file_read(void);

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    littlefs_init();
    list_dir();
    file_read();
}

static void littlefs_init(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = NULL,
        // .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    // 初始化 LittleFS 文件系统
    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
}

// 列出 LittleFS 文件系统中的所有文件
static void list_dir(void)
{
    DIR *dir = opendir("/littlefs");
    if (dir == NULL)
    {
        ESP_LOGE(TAG, "Failed to open directory: %s", "/littlefs");
        return;
    }

    ESP_LOGI(TAG, "Listing files in: %s", "/littlefs");
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        // 打印文件名
        ESP_LOGI(TAG, "  %s", entry->d_name);
    }

    closedir(dir);
}

// 读取 LittleFS 文件系统中的文件内容
static void file_read(void)
{
    ESP_LOGI(TAG, "Reading file");
    FILE *f = fopen("/littlefs/test.txt", "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }
    char line[64];
    fgets(line, sizeof(line), f);
    ESP_LOGI(TAG, "Read line: %s", line);
    fclose(f);
}
