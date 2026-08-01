/**
 * 麦克风使用INMP441, 扬声器使用MAX98357A
 * INMP441： 32位单声道（技术手册规定）、飞利浦格式、单声道（硬件L/R接口接地）,数据格式PCM
 * MAX98357A
 *
 *
 * - 音频录制，需要先存储到RAM中，全部录制完成后再写入文件系统中。flash写入速度慢，不能实时写入，否则会丢失数据。
 * - 为了根据录制时长提前申请内存，需要根据时间提前计算需要的内存大小，计算公式：采样率(16000) * 声道数(1) * 数据位宽(32位 -> 4字节) * 录制时长（秒）
 * - 关于I2S的通讯频率，一般指的是WS的翻转频率（即使是单声道不反转），所以实际频率应该是 采样率(16000) * 声道数(1) * 数据位宽(32位 -> 4字节) ，INMP441对
 *   通讯频率有要求，范围在 0.5MHz - 3.2MHz，当前设置的频率是 采样率(16000) * 声道数(1) * 数据位宽(32) = 51200，刚好在范围内。
 * - 在i2s数据传输中，采用 8、16、24、32位数据宽度，但是实际传输存储按照标准宽度存储如1、2、4字节存储。以24位位例子：
 *   若在麦克风中，有一个数据为 0x112233 的三字节（24位）数据，传输前麦克风模块会按照 0x11223300 的格式补全（左对齐）为 4 字节（32位），然后按照从高到底的方式传输
 *   （因为最高位是符号位，所以先发送最高位），由于esp（接收端）配置的是32位数据宽度，实际按照 11 22 33 00 的顺序接收，然后I2S外设会把这个数据 解释/组合 为
 *   一个正确的32位数据，然后再写到RAM中，即 0x11223300，然后由于ESP是小端存储，所以在内存中存储的顺序是 00 33 22 11，但是不妨碍直接按照 int32_t 的格式直接强制类型转换，去
 *   读取数据进行处理。
 */

#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include <dirent.h>

#define TAG "recorder"

#define DATA_BIT_WIDTH I2S_DATA_BIT_WIDTH_32BIT
#define DATA_HZ 16000

#define PCM_CLK_IO GPIO_NUM_15
#define PCM_WS_IO GPIO_NUM_16
#define PCM_DOUT_IO GPIO_NUM_7
#define PCM_I2S_PORT I2S_NUM_1

// 定义引脚 (根据你的实际接线修改)
#define MIC_BCLK_IO GPIO_NUM_5
#define MIC_WS_IO GPIO_NUM_4
#define MIC_DIN_IO GPIO_NUM_6
#define PDM_I2S_PORT I2S_NUM_0 // ESP32-S3 的 PDM 功能在 I2S0 上支持最好 [citation:1]

static void spiffs_init(void);
static void setup_microphone(void);
static void record_audio(const char *save_file_path, size_t sec);
static void setup_pcm_player(void);
static esp_err_t play_audio_file(const char *file_path);

i2s_chan_handle_t rx_handle; // 接收通道句柄
i2s_chan_handle_t tx_handle; // 发送通道句柄

void app_main(void)
{
    nvs_flash_init();
    spiffs_init();
    setup_microphone();
    setup_pcm_player();
    record_audio("/spiffs/recording.pcm", 5);
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    play_audio_file("/spiffs/recording.pcm");
}

static void spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 10,
        .format_if_mount_failed = true};

    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));

    size_t total = 0, used = 0;
    esp_err_t ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s). Formatting...", esp_err_to_name(ret));
        esp_spiffs_format(conf.partition_label);
    }
    else
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
}

static void setup_microphone(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(PDM_I2S_PORT, I2S_ROLE_MASTER);

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(DATA_HZ),
        // 采用Philips （飞利浦模式）格式 ， 32bit 单声道
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_BCLK_IO,
            .ws = MIC_WS_IO,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_DIN_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; // 只使用左声道

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}

static void setup_pcm_player(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(PCM_I2S_PORT, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(DATA_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PCM_CLK_IO,
            .ws = PCM_WS_IO,
            .dout = PCM_DOUT_IO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; // 只使用左声道

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    // ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
}

/**
 * @brief 录制音频
 * @param save_file_path 保存的文件路径
 * @param sec 录制时长，单位秒
 */
static void record_audio(const char *save_file_path, size_t sec)
{
    uint32_t ready_to_read = DATA_HZ * 1 * 4 * sec;
    // 检查是否有该文件，有则删除重新创建，没有则创建
    FILE *fp = fopen(save_file_path, "wb");
    if (fp == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file %s for writing", save_file_path);
        return;
    }
    // 读取音频数据
    uint8_t *audio_data = malloc(ready_to_read);
    if (audio_data == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for audio data");
        fclose(fp);
        return;
    }

    ESP_LOGI(TAG, "Recording audio...");

    size_t read_bytes = 0;
    int64_t temp;
    if (i2s_channel_read(rx_handle, audio_data, ready_to_read, &read_bytes, portMAX_DELAY) == ESP_OK)
    {
        ESP_LOGI(TAG, "Read %d bytes of audio data, start processing...", read_bytes);
        int sample_count = read_bytes / sizeof(int32_t);
        for (int i = 0; i < sample_count; i++)
        {
            temp = ((int32_t *)audio_data)[i] * 20; // 放大音量
            if (temp > INT32_MAX) temp = INT32_MAX;
            if (temp < INT32_MIN) temp = INT32_MIN;
            ((int32_t *)audio_data)[i] = (int32_t)temp;
        }
        
        ESP_LOGI(TAG, "Finished processing, start writing audio data to file: %s", save_file_path);
        uint32_t write_count = 0;
        while (write_count < read_bytes)
        {
            uint32_t write_bytes = read_bytes - write_count > 10000 ? 10000 : read_bytes - write_count;
            if (fwrite(audio_data + write_count, 1, write_bytes, fp) != write_bytes)  
            {
                ESP_LOGE(TAG, "Failed to write audio data to file");
                break;
            }
            else
            {
                write_count += write_bytes;
            }
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to read audio data");
    }
    ESP_LOGI(TAG, "Write audio data to file done");
    free(audio_data);
    fclose(fp);
}

static esp_err_t play_audio_file(const char *file_path)
{
    FILE *fp = fopen(file_path, "rb");
    if (!fp)
    {
        ESP_LOGE(TAG, "Failed to open %s", file_path);
        return ESP_FAIL;
    }
    const size_t buf_size = 4096;
    uint8_t *buffer = malloc(buf_size);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Buffer alloc failed");
        fclose(fp);
        return ESP_FAIL;
    }
    i2s_channel_enable(tx_handle);

    ESP_LOGI(TAG, "Playing audio file: %s", file_path);
    while (1)
    {
        size_t to_read = buf_size;
        size_t n = fread(buffer, 1, to_read, fp);
        if (n == 0)
            break;

        size_t written = 0;
        esp_err_t err = i2s_channel_write(tx_handle, buffer, n, &written, portMAX_DELAY);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "I2S write error: %s", esp_err_to_name(err));
            break;
        }
    }

    free(buffer);
    fclose(fp);
    i2s_channel_disable(tx_handle);
    return ESP_OK;
}
