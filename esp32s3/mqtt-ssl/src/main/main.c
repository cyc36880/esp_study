#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "string.h"

static const char* TAG = "MQTT_Demo";
extern const char emqx_ca_pem_start[] asm("_binary_emqxsl_ca_crt_start");
extern const char emqx_ca_pem_end[]   asm("_binary_emqxsl_ca_crt_end");

#define WIFI_SSID "DESK"
#define WIFI_PASSWORD "12345678"

#define MQTT_BROKER_URL "mqtts://je73adf3.ala.cn-shenzhen.emqxsl.cn"     //MQTT连接地址
#define MQTT_USERNAME "zishen"                     //MQTT用户名
#define MQTT_PASSWORD "12345678"                  //MQTT密码


#define MQTT_PUBLIC_TOPIC      "topic/1"       //测试用的,推送消息主题
#define MQTT_SUBSCRIBE_TOPIC    "topic/2"      //测试用的,需要订阅的主题


// MQTT 客户端句柄（全局变量，方便回调函数访问）
static esp_mqtt_client_handle_t mqtt_client = NULL;

// WiFi 连接事件处理
static void wifi_event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data) 
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        esp_wifi_connect();  // WiFi 启动后尝试连接
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        ESP_LOGW(TAG, "WiFi 断开，尝试重连...");
        esp_wifi_connect();  // 断开后自动重连
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "获取 IP: " IPSTR, IP2STR(&event->ip_info.ip));
        // WiFi 连接成功后启动 MQTT
        esp_mqtt_client_start(mqtt_client);
    }
}

// MQTT 事件处理（核心回调函数）
static void mqtt_event_handler(void* handler_args, esp_event_base_t base,int32_t event_id, void* event_data) 
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) 
    {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT 连接成功！");
            // 连接成功后订阅主题（示例：订阅 MQTT_SUBSCRIBE_TOPIC）
            int msg_id = esp_mqtt_client_subscribe(mqtt_client, MQTT_SUBSCRIBE_TOPIC, 1);  // QoS=1
            ESP_LOGI(TAG, "订阅主题，消息 ID: %d", msg_id);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT 连接断开");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "订阅成功，消息 ID: %d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "取消订阅，消息 ID: %d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "消息发布成功，消息 ID: %d", event->msg_id);
            break;

        case MQTT_EVENT_DATA: 
            // 收到订阅的消息（打印主题和内容）
            ESP_LOGI(TAG, "收到消息:");
            ESP_LOGI(TAG, "主题: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "内容: %.*s", event->data_len, event->data);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT 错误！");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) 
            {
                ESP_LOGE(TAG, "TCP 传输错误: 0x%x", event->error_handle->esp_tls_last_esp_err);
            }
            break;

        default:
            ESP_LOGI(TAG, "其他 MQTT 事件 ID: %d", event->event_id);
            break;
    }
}

// 初始化 WiFi
static void wifi_init(void) 
{
    ESP_ERROR_CHECK(esp_netif_init());  // 初始化网络接口
    ESP_ERROR_CHECK(esp_event_loop_create_default());  // 创建默认事件循环
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
}
/*
client_id ,  // 可选，若不设置会自动生成随机 ID
keepalive = 60,                      // 心跳间隔 60 秒
disable_clean_session = 0,           // 清除会话（断开后不保留状态）
lwt_topic = "topic/last_will",       // 遗嘱主题（可选）
lwt_msg = "ESP32-S3 断开连接",        // 遗嘱内容（可选）
lwt_qos = 1,                         // 遗嘱消息 QoS
lwt_retain = 1,                      // 遗嘱消息保留标志

*/
// 初始化 MQTT 客户端
static void mqtt_init(void) 
{
    // 配置 MQTT 参数

    esp_mqtt_client_config_t mqtt_cfg = 
    {
        .broker.address.uri = MQTT_BROKER_URL,
        .broker.address.port = 8883,
        // .broker.address.transport = MQTT_TRANSPORT_OVER_SSL,
        .broker.verification = {
            // .skip_cert_common_name_check = true,  // 跳过证书通用名称验证
            .certificate = emqx_ca_pem_start,
        },
        .credentials.username=MQTT_USERNAME,
        .credentials.authentication.password=MQTT_PASSWORD,
        .credentials.client_id="ESP32-S3_MQTT_Client",  // 可选，若不设置会自动生成随机 ID
        .session.keepalive=120,// 心跳间隔 60 秒
        .session.last_will.qos = 0,//// 遗嘱消息 QoS
    };

    // 创建 MQTT 客户端实例
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    // 注册 MQTT 事件回调
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,mqtt_event_handler, NULL));
}

// 定时发布消息任务（示例：每 5 秒发布一次）
static void publish_task(void* arg) 
{
    int msg_count = 0;
    while (1) 
    {
        vTaskDelay(pdMS_TO_TICKS(5000));  // 5 秒间隔
        if (mqtt_client == NULL) 
        {
            continue;
        }
        // 构造消息内容（JSON 格式示例）
        char payload[64];
        snprintf(payload, sizeof(payload), "{\"count\": %d, \"device\": \"ESP32-S3\"}", msg_count++);
        // 发布消息到 MQTT_PUBLIC_TOPIC，QoS=1，不保留
        int msg_id = esp_mqtt_client_publish(mqtt_client, MQTT_PUBLIC_TOPIC, payload, 0, 1, 0);
        ESP_LOGI(TAG, "发布消息，内容: %s，消息 ID: %d", payload, msg_id);
    }
}

void app_main(void) 
{
    // 初始化 NVS（用于存储 WiFi 配置等）
    ESP_ERROR_CHECK(nvs_flash_init());

    // 初始化 WiFi 和 MQTT
    wifi_init();
    mqtt_init();

    // 启动发布任务
    xTaskCreate(publish_task, "publish_task", 4096, NULL, 5, NULL);
}
