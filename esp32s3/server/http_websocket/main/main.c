#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WIFI_SSID "DESK"
#define WIFI_PASSWORD "12345678"

// 需要修改html关于按键发送数据的代码
#define BUTTON_GET_OR_POST 1 // 1 post 0 get

static const char *TAG = "esp_websocket";
static httpd_handle_t websocket_handle;
static int websocket_fd = -1;

// 声明一个外部常量，类型为uint8_t，表示index.html文件的起始位置、结束位置
extern const uint8_t _binary_index_html_start[];
extern const uint8_t _binary_index_html_end[];

extern const uint8_t _binary_index_html_gz_start[];
extern const uint8_t _binary_index_html_gz_end[];

// 声明外部常量，表示man.jpeg文件的起始和结束位置
extern const uint8_t _binary_man_jpeg_start[];
extern const uint8_t _binary_man_jpeg_end[];


/*********************************************************************************
 *                                  HTTP
 ********************************************************************************/

// 1. 握手阶段（Handshake
//  当客户端首次建立连接时发送HTTP GET请求（WebSocket握手）
// 此阶段只需返回ESP_OK确认握手成功
esp_err_t ws_handler(httpd_req_t *req)
{
    // 检查请求方法是否为GET
    if (req->method == HTTP_GET)
    {
        websocket_fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "Handshake done, new connection");
        return ESP_OK;
    }
    // 初始化WebSocket帧，2. 接收WebSocket帧
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    // 获取帧长度
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get frame length: %s", esp_err_to_name(ret));
        return ret;
    }

    // 如果帧长度大于0，则接收帧数据3. 处理有效载荷
    if (ws_pkt.len > 0)
    {
        // 分配内存
        uint8_t *buf = malloc(ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to allocate memory for WebSocket payload");
            return ESP_ERR_NO_MEM;
        }

        ws_pkt.payload = buf;
        // 接收帧数据
        // 从HTTP请求中接收WebSocket帧
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        // 如果接收帧失败
        if (ret != ESP_OK)
        {
            // 打印错误信息
            ESP_LOGE(TAG, "Failed to receive frame: %s", esp_err_to_name(ret));
            // 释放缓冲区
            free(buf);
            // 返回错误码
            return ret;
        }

        buf[ws_pkt.len] = 0; // null terminate
        ESP_LOGI(TAG, "Received WebSocket message: %s", (char *)buf);

        // Echo back//回显帧数据
        // 定义一个httpd_ws_frame_t类型的变量response
        httpd_ws_frame_t response = {
            // 设置response的type为HTTPD_WS_TYPE_TEXT
            .type = HTTPD_WS_TYPE_TEXT,
            // 设置response的payload为buf
            .payload = buf,
            // 设置response的len为ws_pkt.len
            .len = ws_pkt.len,
            // 设置response的final为true
            .final = true};
        ret = httpd_ws_send_frame(req, &response); // 发送回显帧
        free(buf);
        return ret;
    }

    return ESP_OK;
}

// 处理 /button 的GET请求,从URL参数解析按钮标签并返回响应文本
esp_err_t button_get_handler(httpd_req_t *req)
{
#if BUTTON_GET_OR_POST
    // 获取URL查询字符串长度
    size_t query_len = httpd_req_get_url_query_len(req) + 1;
    if (query_len <= 1)
    {
        const char *default_resp = "No button param";
        httpd_resp_send(req, default_resp, strlen(default_resp));
        return ESP_OK;
    }

    char query[64] = {0};
    httpd_req_get_url_query_str(req, query, query_len > sizeof(query) ? sizeof(query) : query_len);
    ESP_LOGI(TAG, "Button GET query: %s", query);

    // 从query中提取btn参数的值
    char btn_val[8] = {0};
    httpd_query_key_value(query, "btn", btn_val, sizeof(btn_val));

    const char *response;
    if (strcmp(btn_val, "A") == 0 || strcmp(btn_val, "a") == 0)
    {
        response = "Button A pressed - Hello from ESP32!";
    }
    else if (strcmp(btn_val, "B") == 0 || strcmp(btn_val, "b") == 0)
    {
        response = "Button B pressed - Greetings from ESP32!";
    }
    else
    {
        response = "Unknown button";
    }

    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
#else
    char buf[64] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0)
    { // 没有收到有效数据,返回默认提示
        const char *default_resp = "No button data received";
        httpd_resp_send(req, default_resp, strlen(default_resp));
        return ESP_OK;
    }
    buf[ret] = 0;
    ESP_LOGI(TAG, "Button POST body: %s", buf); // 解析 btn=A 或 btn=B
    const char *response;
    if (strstr(buf, "btn=A") || strstr(buf, "btn=a"))
    {
        response = "Button A pressed - Hello from ESP32!";
    }
    else if (strstr(buf, "btn=B") || strstr(buf, "btn=b"))
    {
        response = "Button B pressed - Greetings from ESP32!";
    }
    else
    {
        response = "Unknown button";
    }
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
#endif
}

// 处理根路径的GET请求,发送html文件
esp_err_t index_get_handler(httpd_req_t *req)
{
    // return ESP_OK;
    // 设置响应类型为text/html
    httpd_resp_set_type(req, "text/html");

    // 发送index.html文件内容
    // return httpd_resp_send(req, (const char *)_binary_index_html_start, _binary_index_html_end - _binary_index_html_start);

    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)_binary_index_html_gz_start, _binary_index_html_gz_end - _binary_index_html_gz_start);
}

// 处理 /man.jpeg 的GET请求,发送图片文件
esp_err_t jpeg_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "image/jpeg");
    return httpd_resp_send(req, (const char *)_binary_man_jpeg_start, _binary_man_jpeg_end - _binary_man_jpeg_start);
}

// 启动WebSocket服务器
static httpd_handle_t start_websocket_server(void)
{
    // 声明一个httpd_handle_t类型的变量server，用于存储httpd_start函数返回的句柄
    httpd_handle_t server = NULL;
    // 声明一个httpd_config_t类型的变量config，用于存储httpd_start函数需要的配置参数
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // 设置uri匹配函数为httpd_uri_match_wildcard，用于资源请求路径的匹配。NULL必须完全匹配，esp提供的允许通配符
    config.uri_match_fn = httpd_uri_match_wildcard;
    // 设置最大uri处理器的数量为16
    // config.max_uri_handlers = 16;
    // 设置最大响应头的数量为16
    // config.max_resp_headers = 16;
    // 设置最大打开的socket数量为4
    config.max_open_sockets = 4;

    // 调用httpd_start函数启动http服务器，并将返回的句柄存储在server变量中
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // 声明一个httpd_uri_t类型的变量ws_uri，用于存储websocket的uri信息
        httpd_uri_t ws_uri = {
            .uri = "/ws",
            .method = HTTP_GET,
            .handler = ws_handler, // 握手阶段,成功后升级通信协议为WebSocket
            .user_ctx = NULL,
            .is_websocket = true};
        httpd_register_uri_handler(server, &ws_uri);

        httpd_uri_t index_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_get_handler, //// 处理根路径的GET请求,发送html文件
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &index_uri);

        httpd_uri_t jpeg_uri = {
            .uri = "/man.jpeg",
            .method = HTTP_GET,
            .handler = jpeg_get_handler, // 处理 /man.jpeg 的GET请求,发送图片文件
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &jpeg_uri);

#if BUTTON_GET_OR_POST
        httpd_uri_t button_uri = {
            .uri = "/button",
            .method = HTTP_GET,
            .handler = button_get_handler, // 处理 /button?btn=A/B 的GET请求
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &button_uri);
#else
    httpd_uri_t button_uri = {
            .uri = "/button",
            .method = HTTP_POST,
            .handler = button_get_handler, // 处理 /button?btn=A/B 的POST请求
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &button_uri);
#endif
    }
    return server;
}

/*********************************************************************************
 *                                  WIFI
 ********************************************************************************/

// WiFi 连接事件处理
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect(); // WiFi 启动后尝试连接
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "WiFi 断开，尝试重连...");
        esp_wifi_connect(); // 断开后自动重连
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "获取 IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init_soft_sta(void)
{
#if 1
    esp_netif_create_default_wifi_sta(); // 创建默认 STA 接口

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // 注册 WiFi 和 IP 事件回调
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // 配置 WiFi 参数（从 menuconfig 获取）
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // 默认 WPA2
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

#else

    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_init(&cfg)); // 初始化WiFi

    // 配置WiFi热点
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,             // 设置WiFi热点名称
            .ssid_len = strlen(WIFI_SSID), // 设置WiFi热点名称长度
            .password = WIFI_PASSWORD,     // 设置WiFi热点密码
            .max_connection = 1,           // 设置最大连接数
            .authmode = WIFI_AUTH_OPEN     // 设置认证模式为开放
        },
    };
    // 设置WiFi模式为热点模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    // 设置WiFi热点配置
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    // 启动WiFi热点
    ESP_ERROR_CHECK(esp_wifi_start());

    // 打印WiFi热点已启动的信息
    ESP_LOGI(TAG, "WiFi AP started. SSID:%s", EXAMPLE_WIFI_SSID);

#endif
}

static void websocket_send_task(void *pvParameters)
{
    while (1)
    {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        if (websocket_handle != NULL && websocket_fd < 0)
            continue;

        char *test = "hello\n";
        httpd_ws_frame_t websocket_frame = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)test,
            .len = strlen(test),
        };
        // 判断 fd 是否有效
        if (httpd_ws_get_fd_info(websocket_handle, websocket_fd) == HTTPD_WS_CLIENT_WEBSOCKET)
            httpd_ws_send_data(websocket_handle, websocket_fd, &websocket_frame);
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

    wifi_init_soft_sta();
    websocket_handle = start_websocket_server();

    xTaskCreate(websocket_send_task,
                "websocket_send_task",
                1024 * 10,
                NULL,
                4,
                NULL);
}
