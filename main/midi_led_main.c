#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "led_strip_encoder.h"
#include "usb/usb_host.h"
#include "driver/gpio.h"
#include "class_driver.h"

#define RMT_LED_STRIP_RESOLUTION_HZ 10000000 // 10MHz
#define RMT_LED_STRIP_GPIO_NUM      16
#define EXAMPLE_LED_NUMBERS         72
#define APP_QUIT_PIN                CONFIG_APP_QUIT_PIN

static const char *TAG = "midi_game";

static uint8_t led_strip_pixels[EXAMPLE_LED_NUMBERS * 3];
static rmt_channel_handle_t led_chan = NULL;
static rmt_encoder_handle_t led_encoder = NULL;
static QueueHandle_t midi_event_queue = NULL;

// Melody and notes, mapped to a 61-key keyboard starting at C2 (MIDI 36)
#define NOTE_C4 24 // MIDI 60
#define NOTE_D4 26 // MIDI 62
#define NOTE_E4 28 // MIDI 64
#define NOTE_G4 31 // MIDI 67

typedef struct {
    int note_led;
    int duration_ms;
} musical_note_t;

musical_note_t melody[] = {
    {NOTE_E4, 300}, {NOTE_D4, 300}, {NOTE_C4, 300}, {NOTE_D4, 300},
    {NOTE_E4, 300}, {NOTE_E4, 300}, {NOTE_E4, 600},
    {NOTE_D4, 300}, {NOTE_D4, 300}, {NOTE_D4, 600},
    {NOTE_E4, 300}, {NOTE_G4, 300}, {NOTE_G4, 600},
};

static void set_pixel_color(int index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= 0 && index < EXAMPLE_LED_NUMBERS) {
        led_strip_pixels[index * 3 + 0] = g;
        led_strip_pixels[index * 3 + 1] = r;
        led_strip_pixels[index * 3 + 2] = b;
    }
}

static void flush_leds()
{
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };
    ESP_ERROR_CHECK(rmt_transmit(led_chan, led_encoder, led_strip_pixels, sizeof(led_strip_pixels), &tx_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));
}

static void show_feedback(int led_index, bool correct)
{
    if (correct) {
        set_pixel_color(led_index, 0, 255, 0); // Green
    } else {
        set_pixel_color(led_index, 255, 0, 0); // Red
    }
    flush_leds();
    vTaskDelay(pdMS_TO_TICKS(500));
}

void melody_game_task(void *arg)
{
    int melody_len = sizeof(melody) / sizeof(melody[0]);
    int current_note_index = 0;

    while(1) {
        musical_note_t current_note = melody[current_note_index];
        int led_index = current_note.note_led;

        // 1. Show the note to be played
        memset(led_strip_pixels, 0, sizeof(led_strip_pixels));
        set_pixel_color(led_index, 0, 0, 255); // Blue
        flush_leds();
        ESP_LOGI(TAG, "Next note to play: LED %d (MIDI %d)", led_index, led_index + 36);

        // 2. Wait for user input
        uint8_t received_note;
        if (xQueueReceive(midi_event_queue, &received_note, portMAX_DELAY)) {
            int received_led_index = received_note - 36; // Lowest note on 61-key keyboard is C2 (MIDI 36)
            ESP_LOGI(TAG, "Received MIDI note: %d, Mapped to LED: %d", received_note, received_led_index);

            if (received_led_index == led_index) {
                // Correct note
                show_feedback(led_index, true);
                current_note_index = (current_note_index + 1) % melody_len;
            } else {
                // Incorrect note
                show_feedback(received_led_index, false);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // Small delay
    }
}

static void usb_host_lib_task(void *arg)
{
    ESP_LOGI(TAG, "Installing USB Host Library");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    xTaskNotifyGive(arg);

    bool has_clients = true;
    bool has_devices = false;
    while (has_clients) {
        uint32_t event_flags;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            if (ESP_OK == usb_host_device_free_all()) {
                has_clients = false;
            } else {
                has_devices = true;
            }
        }
        if (has_devices && event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            has_clients = false;
        }
    }
    ESP_LOGI(TAG, "USB Host Library uninstalled");
    ESP_ERROR_CHECK(usb_host_uninstall());
    vTaskSuspend(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Create RMT TX channel");
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = RMT_LED_STRIP_GPIO_NUM,
        .mem_block_symbols = 64,
        .resolution_hz = RMT_LED_STRIP_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &led_chan));

    ESP_LOGI(TAG, "Install led strip encoder");
    led_strip_encoder_config_t encoder_config = {
        .resolution = RMT_LED_STRIP_RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(rmt_new_led_strip_encoder(&encoder_config, &led_encoder));

    ESP_LOGI(TAG, "Enable RMT TX channel");
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    midi_event_queue = xQueueCreate(10, sizeof(uint8_t));

    TaskHandle_t host_lib_task_hdl, class_driver_task_hdl, game_task_hdl;

    BaseType_t task_created = xTaskCreatePinnedToCore(usb_host_lib_task, "usb_host", 4096, xTaskGetCurrentTaskHandle(), 2, &host_lib_task_hdl, 0);
    assert(task_created == pdTRUE);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    task_created = xTaskCreatePinnedToCore(class_driver_task, "class", 5 * 1024, NULL, 3, &class_driver_task_hdl, 0);
    assert(task_created == pdTRUE);
    vTaskDelay(pdMS_TO_TICKS(100)); // Allow class driver to initialize
    class_driver_set_midi_queue(midi_event_queue);

    task_created = xTaskCreatePinnedToCore(melody_game_task, "melody_game", 4096, NULL, 4, &game_task_hdl, 0);
    assert(task_created == pdTRUE);
}
