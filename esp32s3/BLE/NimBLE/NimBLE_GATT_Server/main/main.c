/**
 * GAP:  蓝牙广播与连接
 * GATT: 蓝牙服务与特征
 * app_main()
    │
    ├── nvs_flash_init()              # 必须第一个初始化
    ├── nimble_port_init()            # BLE 控制器 + HCI + Host 全部初始化
    ├── ble_svc_gap_init()            # 自动创建 0x1800 服务
    ├── ble_svc_gatt_init()           # 自动创建 0x1801 服务
    ├── ble_svc_gap_device_name_set() # 设备名
    ├── ble_sensor_init()             # 注册你自己的 GATT 服务
    ├── ble_hs_cfg.sync_cb = ...      # BLE 就绪回调（在这里开始广播）
    ├── init_connections()            # 初始化连接管理
    └── nimble_port_freertos_init()   # 启动 NimBLE 主循环任务

 */

#include "common.h"
#include "gap.h"
#include "gatt.h"

static void on_stack_reset(int reason) 
{
    /* On reset, print reset reason to console */
    ESP_LOGI(TAG, "nimble stack reset, reset reason: %d", reason);
}

static void on_stack_sync(void) 
{
    /* On stack sync, do advertising initialization */
    adv_init();
}

static void nimble_host_config_init(void) 
{
    /* Set host callbacks */
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
}

static void nimble_host_task(void *param) 
{
    /* Task entry log */
    ESP_LOGI(TAG, "nimble host task has been started!");

    /* This function won't return until nimble_port_stop() is executed */
    nimble_port_run();

    /* Clean up at exit */
    vTaskDelete(NULL);
}

void app_main(void)
{
    /**
     * ble 需要使用nvs存储数据
     */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nimble_port_init();
    gap_init();
    gatt_init();
    nimble_host_config_init();
    nimble_port_freertos_init(nimble_host_task);
}

