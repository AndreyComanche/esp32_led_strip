#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"

#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz resolution, 1 tick = 0.1us (led strip needs a high resolution)
#define RMT_LED_STRIP_GPIO_NUM      16

#define EXAMPLE_LED_NUMBERS         72

static const char *TAG = "melody_example";

static uint8_t led_strip_pixels[EXAMPLE_LED_NUMBERS * 3];

// Define notes and melody
#define NOTE_C4 30
#define NOTE_D4 32
#define NOTE_E4 34
#define NOTE_G4 37

// Simple melody structure
typedef struct {
    int note_led;
    int duration_ms;
} musical_note_t;

// A simple melody (e.g., part of "Mary Had a Little Lamb")
musical_note_t melody[] = {
    {NOTE_E4, 300}, {NOTE_D4, 300}, {NOTE_C4, 300}, {NOTE_D4, 300},
    {NOTE_E4, 300}, {NOTE_E4, 300}, {NOTE_E4, 600},
    {NOTE_D4, 300}, {NOTE_D4, 300}, {NOTE_D4, 600},
    {NOTE_E4, 300}, {NOTE_G4, 300}, {NOTE_G4, 600},
};

void app_main(void)
{
    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_channel_handle_t led_chan = NULL;
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT, // select source clock
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 64, // increase the block size can make the LED less flickering
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4, // set the number of transactions that can be pending in the background
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

    ESP_LOGI(TAG, "Start playing melody");
    rmt_transmit_config_t tx_config = {
        .loop_count = 0, // no transfer loop
    };

    int melody_len = sizeof(melody) / sizeof(melody[0]);

    while (1) {
        for (int i = 0; i < melody_len; i++) {
            // 1. Clear all LEDs to turn them off
            memset(led_strip_pixels, 0, sizeof(led_strip_pixels));

            // 2. Light up the LED for the current note (in blue)
            int led_index = melody[i].note_led;
            if (led_index >= 0 && led_index < EXAMPLE_LED_NUMBERS) {
                led_strip_pixels[led_index * 3 + 0] = 0;   // Green
                led_strip_pixels[led_index * 3 + 1] = 255; // Blue
                led_strip_pixels[led_index * 3 + 2] = 0;   // Red
            }

            // 3. Send the updated pixel data to the LED strip
            ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
            ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));

            // 4. Wait for the note duration
            vTaskDelay(pdMS_TO_TICKS(melody[i].duration_ms));
        }
        // Wait a bit before repeating the melody
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
