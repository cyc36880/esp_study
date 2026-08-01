/**
 * 播放wav文件
 * - 由于初始化i2s通道后，不能再进行初始化，需要先disable，然后使用 i2s_channel_reconfig_std_xxx 类似的函数重新初始化
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

#define TAG      "music"


typedef struct
{
    char riff[4];          // "RIFF"
    uint32_t file_size;    // 文件总大小 - 8
    char wave[4];          // "WAVE"
    char fmt[4];           // "fmt "
    uint32_t fmt_size;     // 16 for PCM
    uint16_t audio_format; // 1 = PCM
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
} __attribute__((packed)) wav_header_t;

typedef struct
{
    char filename[64];
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t num_channels;
} wav_info_t;

static void i2s_init(void);
static void spiffs_init(void);
static esp_err_t parse_wav_header(FILE *fp, wav_info_t *info);
static esp_err_t i2s_configure(i2s_chan_handle_t tx_handle, uint32_t sample_rate,
                               uint16_t bits_per_sample, uint16_t num_channels);
static esp_err_t play_wav_file(const char *filepath, i2s_chan_handle_t tx_handle, float volume);
static i2s_chan_handle_t tx_handle = NULL;

// 配置i2s通道
static i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100), // 在播放前会手动去设置，这里可以屏蔽它
    .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,
        .bclk = GPIO_NUM_15,   
        .ws = GPIO_NUM_16,
        .dout = GPIO_NUM_7,
        .din = I2S_GPIO_UNUSED,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    },
};



void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    spiffs_init();
    i2s_init();

    play_wav_file("/spiffs/music.wav", tx_handle, 0.1f);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    play_wav_file("/spiffs/music.wav", tx_handle, 0.1f);

    ESP_LOGI(TAG, "All songs played!");

    // 清理 必须先失能再删除
    i2s_channel_disable(tx_handle);
    i2s_del_channel(tx_handle);
}

/* 软件音量调整函数 - 核心实现 */
static void apply_volume(uint8_t *buffer, size_t len, uint16_t bits_per_sample,
                         uint16_t num_channels, float volume)
{
    if (volume >= 1.0f)
        return;

    int bytes_per_sample = bits_per_sample / 8;
    int samples = len / bytes_per_sample;

    if (bits_per_sample == 16)
    {
        int16_t *data = (int16_t *)buffer;
        for (int i = 0; i < samples; i++)
        {
            // 使用整数运算提高性能
            data[i] = (int16_t)(data[i] * volume);
        }
    }
    else if (bits_per_sample == 24)
    {
        // 24-bit 音频处理（常见格式）
        for (int i = 0; i < samples; i++)
        {
            int offset = i * 3;
            int32_t sample = 0;

            // 读取 24-bit 有符号整数（小端）
            sample |= buffer[offset] << 0;
            sample |= buffer[offset + 1] << 8;
            sample |= buffer[offset + 2] << 16;

            // 符号扩展
            if (sample & 0x800000)
                sample |= 0xFF000000;

            // 应用音量
            sample = (int32_t)(sample * volume);

            // 写入回 buffer
            buffer[offset] = sample & 0xFF;
            buffer[offset + 1] = (sample >> 8) & 0xFF;
            buffer[offset + 2] = (sample >> 16) & 0xFF;
        }
    }
    else if (bits_per_sample == 32)
    {
        int32_t *data = (int32_t *)buffer;
        for (int i = 0; i < samples; i++)
        {
            data[i] = (int32_t)(data[i] * volume);
        }
    }
    // 8-bit 或其他格式可以类似处理
}

/* 播放单个 WAV 文件 */
static esp_err_t play_wav_file(const char *filepath, i2s_chan_handle_t tx_handle, float volume)
{
    FILE *fp = fopen(filepath, "rb");
    if (!fp)
    {
        ESP_LOGE(TAG, "Failed to open %s", filepath);
        return ESP_FAIL;
    }

    /* 解析头部 */
    wav_info_t info;
    if (parse_wav_header(fp, &info) != ESP_OK)
    {
        fclose(fp);
        return ESP_FAIL;
    }
    fseek(fp, info.data_offset, SEEK_SET);

    /* 根据当前文件的参数重新配置 I2S */
    ESP_LOGI(TAG, "Configuring I2S: %lu Hz, %u bits, %u channels",
             info.sample_rate, info.bits_per_sample, info.num_channels);

    esp_err_t ret = i2s_configure(tx_handle, info.sample_rate,
                                  info.bits_per_sample, info.num_channels);
    if (ret != ESP_OK)
    {
        fclose(fp);
        return ret;
    }

    /* 播放循环 */
    const size_t buf_size = 4096;
    uint8_t *buffer = malloc(buf_size);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Buffer alloc failed");
        fclose(fp);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Start playing: %s", filepath);
    size_t total_read = 0;

    while (total_read < info.data_size)
    {
        size_t to_read = (info.data_size - total_read > buf_size) ? buf_size : (info.data_size - total_read);
        size_t n = fread(buffer, 1, to_read, fp);
        if (n == 0)
            break;

        // 应用音量调整
        if (volume < 1.0f)
        {
            apply_volume(buffer, n, info.bits_per_sample, info.num_channels, volume);
        }

        size_t written = 0;
        esp_err_t err = i2s_channel_write(tx_handle, buffer, n, &written, portMAX_DELAY);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "I2S write error: %s", esp_err_to_name(err));
            break;
        }
        total_read += n;

        // 每 10% 打印进度
        static int last_percent = -1;
        int percent = (int)(total_read * 100 / info.data_size);
        if (percent != last_percent && percent % 10 == 0)
        {
            ESP_LOGI(TAG, "Progress: %d%%", percent);
            last_percent = percent;
        }
    }

    ESP_LOGI(TAG, "Finished: %s, total bytes: %d", filepath, total_read);

    free(buffer);
    fclose(fp);

    // 播放完后停止 I2S 流（但保持通道启用）
    i2s_channel_preload_data(tx_handle, NULL, 0, NULL);

    return ESP_OK;
}

static void i2s_init(void)
{
    /* 创建 I2S 通道 */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    // ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
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

static esp_err_t i2s_configure(i2s_chan_handle_t tx_handle, uint32_t sample_rate,
                               uint16_t bits_per_sample, uint16_t num_channels)
{
    // 先禁用通道
    i2s_channel_disable(tx_handle);

    i2s_data_bit_width_t bit_width;
    switch (bits_per_sample)
    {
    case 16:
        bit_width = I2S_DATA_BIT_WIDTH_16BIT;
        break;
    case 24:
        bit_width = I2S_DATA_BIT_WIDTH_24BIT;
        break;
    case 32:
        bit_width = I2S_DATA_BIT_WIDTH_32BIT;
        break;
    default:
        ESP_LOGW(TAG, "Unsupported bits %u, fallback to 16", bits_per_sample);
        bit_width = I2S_DATA_BIT_WIDTH_16BIT;
        break;
    }

    i2s_slot_mode_t slot_mode = (num_channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;

    std_cfg.slot_cfg.data_bit_width = bit_width;
    std_cfg.slot_cfg.ws_width = bit_width;
    std_cfg.slot_cfg.slot_mode = slot_mode;
    i2s_channel_reconfig_std_slot(tx_handle, &std_cfg.slot_cfg);

    std_cfg.clk_cfg.sample_rate_hz = sample_rate;
    i2s_channel_reconfig_std_clock(tx_handle, &std_cfg.clk_cfg);

    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));

    return ESP_OK;
}

static esp_err_t parse_wav_header(FILE *fp, wav_info_t *info)
{
    wav_header_t header;
    if (fread(&header, sizeof(wav_header_t), 1, fp) != 1)
    {
        ESP_LOGE(TAG, "Failed to read WAV header");
        return ESP_FAIL;
    }

    if (strncmp(header.riff, "RIFF", 4) != 0 || strncmp(header.wave, "WAVE", 4) != 0)
    {
        ESP_LOGE(TAG, "Not a valid WAV file");
        return ESP_FAIL;
    }

    if (header.audio_format != 1)
    {
        ESP_LOGE(TAG, "Only PCM format supported (format=%d)", header.audio_format);
        return ESP_FAIL;
    }

    info->sample_rate = header.sample_rate;
    info->bits_per_sample = header.bits_per_sample;
    info->num_channels = header.num_channels;

    // 计算 fmt chunk 后的偏移
    long offset = sizeof(wav_header_t);
    int extra_fmt_bytes = header.fmt_size - 16;
    if (extra_fmt_bytes > 0)
    {
        offset += extra_fmt_bytes;
    }

    // 搜索 "data" chunk
    fseek(fp, offset, SEEK_SET);

    char chunk_id[4];
    uint32_t chunk_size;

    while (1)
    {
        if (fread(chunk_id, 1, 4, fp) != 4)
        {
            ESP_LOGE(TAG, "Failed to read chunk ID");
            return ESP_FAIL;
        }
        if (fread(&chunk_size, 4, 1, fp) != 1)
        {
            ESP_LOGE(TAG, "Failed to read chunk size");
            return ESP_FAIL;
        }

        if (strncmp(chunk_id, "data", 4) == 0)
        {
            break;
        }

        // 跳过非 data chunk
        fseek(fp, chunk_size, SEEK_CUR);
    }

    info->data_size = chunk_size;
    info->data_offset = ftell(fp);

    ESP_LOGI(TAG, "WAV: sr=%lu, bits=%u, ch=%u, offset=%lu, size=%lu",
             info->sample_rate, info->bits_per_sample, info->num_channels,
             info->data_offset, info->data_size);
    return ESP_OK;
}