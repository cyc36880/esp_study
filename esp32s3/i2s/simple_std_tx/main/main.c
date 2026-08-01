




/*************************************************
 *  旧版 i2s 驱动，v4.x 版本，只作为记录，不建议使用
 *************************************************/

// #include "driver/gpio.h"
// #include "driver/i2s.h"
// #include "driver/i2s_std.h"
// #include "freertos/FreeRTOS.h"
// #include "math.h"
#define M_PI         3.1415926

// #define I2S_NUM      I2S_NUM_0
// #define I2S_BCLK_IO  GPIO_NUM_15
// #define I2S_WS_IO    GPIO_NUM_16
// #define I2S_DATA_OUT GPIO_NUM_7

// void i2s_init(void)
// {
//     i2s_config_t i2s_config = {
//         .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
//         .sample_rate = 44100,
//         .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
//         .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
//         .communication_format = I2S_COMM_FORMAT_STAND_I2S,
//         .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
//         .dma_buf_count = 8,
//         .dma_buf_len = 64,
//         .use_apll = false,
//         .tx_desc_auto_clear = true,
//         .fixed_mclk = 0};

//     i2s_pin_config_t pin_config = {
//         .bck_io_num = I2S_BCLK_IO,
//         .ws_io_num = I2S_WS_IO,
//         .data_out_num = I2S_DATA_OUT,
//         .data_in_num = I2S_PIN_NO_CHANGE};

//     i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
//     i2s_set_pin(I2S_NUM, &pin_config);
// }

// void app_main(void)
// {
//     i2s_init();

//     // 示例：发送 440Hz 正弦波数据
//     int16_t samples[64];
//     float phase = 0;

//     while (1)
//     {
//         for (int i = 0; i < 64; i++)
//         {
//             samples[i] = (int16_t)(3000 * sin(phase));
//             phase += 2 * M_PI * 440 / 44100;
//             if (phase > 2 * M_PI)
//                 phase -= 2 * M_PI;
//         }
//         size_t bytes_written;
//         i2s_write(I2S_NUM, samples, sizeof(samples), &bytes_written, portMAX_DELAY);
//     }
// }


/*************************************************
 *  新版 i2s 驱动，v5.x 版本
 *************************************************/

#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "math.h"

void app_main(void)
{
    i2s_chan_handle_t tx_handle;
    /* 通过辅助宏获取默认的通道配置
     * 这个辅助宏在 'i2s_common.h' 中定义，由所有 I2S 通信模式共享
     * 它可以帮助指定 I2S 角色和端口 ID */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    /* 分配新的 TX 通道并获取该通道的句柄 */
    i2s_new_channel(&chan_cfg, &tx_handle, NULL);

    /* 进行配置，可以通过宏生成声道配置和时钟配置
     * 这两个辅助宏在 'i2s_std.h' 中定义，只能用于 STD 模式
     * 它们可以帮助初始化或更新声道和时钟配置 */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),

        // 需要根据设备选择左右声道
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
    /* 初始化通道 */
    i2s_channel_init_std_mode(tx_handle, &std_cfg);

    /* 在写入数据之前，先启用 TX 通道 */
    i2s_channel_enable(tx_handle);

    /* 如果需要更新声道或时钟配置
     * 需要在更新前先禁用通道 */
    // i2s_channel_disable(tx_handle);
    // std_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO; // 默认为立体声
    // i2s_channel_reconfig_std_slot(tx_handle, &std_cfg.slot_cfg);
    // std_cfg.clk_cfg.sample_rate_hz = 96000;
    // i2s_channel_reconfig_std_clock(tx_handle, &std_cfg.clk_cfg);

    int32_t samples[64];
    float phase = 0;
    while (1)
    {
        for (int i = 0; i < 64; i++)
        {
            samples[i] = (int32_t)((3000 << 16) * sin(phase));
            phase += 2 * M_PI * 440 / 44100;
            if (phase > 2 * M_PI)
                phase -= 2 * M_PI;
        }
        size_t bytes_written;
        i2s_channel_write(tx_handle, samples, sizeof(samples), &bytes_written, portMAX_DELAY);
    }

    /* 删除通道之前必须先禁用通道 */
    i2s_channel_disable(tx_handle);
    /* 如果不再需要句柄，删除该句柄以释放通道资源 */
    i2s_del_channel(tx_handle);
}
