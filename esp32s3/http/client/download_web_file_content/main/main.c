#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

static const char *TAG = "client";

#define WIFI_SSID "NULL"
#define WIFI_PASSWORD "12345678"
#define MAX_HTTP_OUTPUT_BUFFER 2048

// gitee的一个仓库文件，使用 raw
#define FILE_URL "https://raw.giteeusercontent.com/cyc36880/esp32-ota/raw/master/version.json?metadata=eyJyIjoibWFzdGVyIiwiZnAiOiJ2ZXJzaW9uLmpzb24iLCJ1aWQiOjg3MTU1NTcsInBpZCI6NDkzMzk3MjcsInN0byI6ImdpdC1zaGFyZGluZy1zdG8tNDJ0LTAzMCIsInJwIjoicmVwb3MvNTYvNzIvNTY3MmQ0NWFlZDRiNDIyMzlhNWQ2ZDVmZDU5ZTRkNjcyMTExZGU0M2E2NzQ2YzJhY2RjYzg4ZmU3YjdmZDdmMC5naXQiLCJpc3AiOnRydWUsImV4cGlyZV9hdCI6MTc4NDk4OTIwMH0&signature=ZHDe9nFlAVkLwsd_a6BppOARKfWGJ4ZTJN9ElvT3Fkw"

static volatile uint8_t wifi_connect = 0;

/* ========== WiFi 初始化（保持不变）========== */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
        wifi_connect = 0;
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "WiFi 断开，尝试重连...");
        esp_wifi_connect();
        wifi_connect = 0;
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "获取 IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connect = 1;
    }
}

static void wifi_init(void)
{
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE);
}

/* ========== 下载上下文结构体 ========== */
typedef struct
{
    char *buffer;          // 接收缓冲区
    size_t buf_len;        // 缓冲区总大小
    size_t received;       // 已接收字节数
    int64_t total;         // 文件总大小（-1 表示 chunked/未知）
    uint8_t header_parsed; // 标记：是否已获取过 Content-Length
} download_ctx_t;

/* ========== 带进度显示的 HTTP 事件处理器 ========== */
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    download_ctx_t *ctx = (download_ctx_t *)evt->user_data;

    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP 已连接，开始接收数据...");
        break;

    case HTTP_EVENT_ON_HEADER:    //接收头事件
        // ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;

    case HTTP_EVENT_ON_DATA:
        if (!ctx)
            break;

        /* 第一次收到数据时，获取 Content-Length（此时 Header 已解析完毕） */
        if (!ctx->header_parsed)
        {
            ctx->header_parsed = 1;
            ctx->total = esp_http_client_get_content_length(evt->client);

            if (ctx->total > 0)
            {
                ESP_LOGI(TAG, "文件总大小: %lld 字节", (long long)ctx->total);
            }
            else if (ctx->total == -1)
            {
                ESP_LOGI(TAG, "服务器使用 chunked 传输，总大小未知");
            }
        }

        /* 将数据追加写入缓冲区（带边界保护） */
        if (ctx->buffer && ctx->buf_len > 0)
        {
            size_t remaining = ctx->buf_len - ctx->received - 1;
            size_t copy_len = (evt->data_len < remaining) ? evt->data_len : remaining;
            if (copy_len > 0)
            {
                memcpy(ctx->buffer + ctx->received, evt->data, copy_len);
                ctx->received += copy_len;
            }
        }

        /* 打印实时进度 */
        if (ctx->total > 0)
        {
            int percent = (int)((ctx->received * 100) / ctx->total);
            ESP_LOGI(TAG, "下载进度: %u/%lld 字节 (%d%%)",
                     (unsigned)ctx->received, (long long)ctx->total, percent);
        }
        else
        {
            ESP_LOGI(TAG, "已接收: %u 字节 (chunked/未知总大小)", (unsigned)ctx->received);
        }
        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP 传输完成，共接收 %u 字节",
                 ctx ? (unsigned)ctx->received : 0);
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP 连接断开");
        break;

    case HTTP_EVENT_ERROR:
        ESP_LOGE(TAG, "HTTP 发生错误");
        break;

    default:
        break;
    }
    return ESP_OK;
}

/* ========== 获取文件内容（带进度）========== */
static esp_err_t fetch_file_content(char *buffer, size_t buf_len)
{
    if (buffer == NULL || buf_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(buffer, 0, buf_len);

    download_ctx_t ctx = {
        .buffer = buffer,
        .buf_len = buf_len,
        .received = 0,
        .total = 0,
        .header_parsed = 0,
    };

    esp_http_client_config_t config = {
        .method = HTTP_METHOD_GET,
        .url = FILE_URL,
        .event_handler = _http_event_handler,
        .user_data = &ctx, // 传入上下文结构体地址
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE(TAG, "HTTP client init failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "开始下载: %s", FILE_URL);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Status = %d", status);

        if (status == 200)
        {
            ESP_LOGI(TAG, "文件内容:\n%s", buffer);
        }
        else
        {
            ESP_LOGW(TAG, "请求失败，HTTP 状态码: %d", status);
            err = ESP_FAIL;
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

/* ========== 主函数 ========== */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init();

    ESP_LOGI(TAG, "等待 WiFi 连接...");
    while (wifi_connect == 0)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 避免消耗栈内存
    static char response_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
    esp_err_t ret = fetch_file_content(response_buffer, sizeof(response_buffer));
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "获取成功，内容长度: %zu", strlen(response_buffer));
    }
    else
    {
        ESP_LOGE(TAG, "获取文件失败");
    }
}
