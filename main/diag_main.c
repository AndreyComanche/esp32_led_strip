#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"

#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define RMT_LED_STRIP_GPIO_NUM      16

// IMPORTANT: Set this to the number of LEDs you have currently soldered (e.g., 12, 24, 36...)
#define EXAMPLE_LED_NUMBERS         72

static const char *TAG = "diag_tool";

static uint8_t led_strip_pixels[EXAMPLE_LED_NUMBERS * 3];

// Sets the color of a single pixel
void set_pixel_color(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index < EXAMPLE_LED_NUMBERS) {
        // The color order is GRB for WS2812 strips
        led_strip_pixels[index * 3 + 0] = g;
        led_strip_pixels[index * 3 + 1] = r;
        led_strip_pixels[index * 3 + 2] = b;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_channel_handle_t led_chan = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    ESP_LOGI(TAG, "Install led strip encoder");
    rmt_encoder_handle_t led_encoder = NULL;
    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));

    ESP_LOGI(TAG, "Enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    ESP_LOGI(TAG, "Start Diagnostics Tool");
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    while (1) {
        ESP_LOGI(TAG, "Testing individual LEDs...");
        // --- Test 1: Cycle through each LED individually (R, G, B) ---
        for (int i = 0; i < EXAMPLE_LED_NUMBERS; i++) {
            // Red
            memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
            set_pixel_color(i, 255, 0, 0);
            ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
            ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
            vTaskDelay(pdMS_TO_TICKS(250));

            // Green
            memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
            set_pixel_color(i, 0, 255, 0);
            ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
            ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
            vTaskDelay(pdMS_TO_TICKS(250));

            // Blue
            memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
            set_pixel_color(i, 0, 0, 255);
            ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
            ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
            vTaskDelay(pdMS_TO_TICKS(250));
        }

        // Turn all off before next test
        memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
        ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
        vTaskDelay(pdMS_TO_TICKS(1000));

        // --- Test 2: Light up the last soldered octave ---
        int last_octave_start_index = (EXAMPLE_LED_NUMBERS / 12 - 1) * 12;
        if (last_octave_start_index < 0) {
            last_octave_start_index = 0;
        }
        int end_index = last_octave_start_index + 12;
        if (end_index > EXAMPLE_LED_NUMBERS) {
            end_index = EXAMPLE_LED_NUMBERS;
        }

        ESP_LOGI(TAG, "Testing octave from LED %d to %d", last_octave_start_index, end_index - 1);
        memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
        for (int i = last_octave_start_index; i < end_index; i++) {
            set_pixel_color(i, 128, 128, 128); // White
        }
        ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
        vTaskDelay(pdMS_TO_TICKS(3000));

        // Turn all off before restarting loop
        memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
        ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
        ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
