/**
 * 关于从网络上下载ota文件，目前配置的是把文件放到gitee仓库
 * 可以先使用 client 中的示例文件，下载gitee中某个文件（如version.json），查看要升级固件的版本，如果大于当前版本，才进行升级，
 * 然后使用此示例再进行ota升级
 * 
 * 注意：
 * - 此示例文件本质上是与主程序文件写在一起的；
 * - 要求要升级的固件与运行的固件分区相同
 */


#include <stdio.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
// #include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"

static const char* TAG = "ota";

#define WIFI_SSID     "NULL"
#define WIFI_PASSWORD "12345678"

// #define HTTP_PATH      "http://192.168.137.1:8080/build/hello.bin"

// gitee仓库的一个文件，使用的raw模式
#define HTTP_PATH      "https://gitee.com/cyc36880/esp32-ota/raw/master/hello.bin"

static volatile uint8_t wifi_connect = 0;

static void ota(void);

// WiFi 连接事件处理
static void wifi_event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data) 
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        esp_wifi_connect();  // WiFi 启动后尝试连接
        wifi_connect = 0;
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        ESP_LOGW(TAG, "WiFi 断开，尝试重连...");
        esp_wifi_connect();  // 断开后自动重连

        wifi_connect = 0;
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "获取 IP: " IPSTR, IP2STR(&event->ip_info.ip));

        wifi_connect = 1;
    }
}


esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

static void wifi_init(void) 
{
    esp_netif_create_default_wifi_sta();  // 创建默认 STA 接口

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册 WiFi 和 IP 事件回调
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,&wifi_event_handler, NULL, NULL));

    // 配置 WiFi 参数（从 menuconfig 获取）
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,  // 默认 WPA2
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_ps(WIFI_PS_NONE); // 禁用任何省电模式
}


static void ota(void)
{
    esp_http_client_config_t config = {
        .url = HTTP_PATH,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = _http_event_handler,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    ESP_LOGI(TAG, "Attempting to download update from %s", config.url);

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) 
    {
        ESP_LOGI(TAG, "OTA Succeed, Rebooting...");
        esp_restart();
    } else 
    {
        ESP_LOGE(TAG, "Firmware upgrade failed");
    }
}



void app_main(void)
{
    // 初始化非易失性存储器
    ESP_ERROR_CHECK(nvs_flash_init());
    // 初始化网络接口
    ESP_ERROR_CHECK(esp_netif_init());
    // 创建默认的事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 初始化
    wifi_init();

    while (wifi_connect == 0) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    ota();
}
