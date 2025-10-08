/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "class_driver.h"

#define CLIENT_NUM_EVENT_MSG        5

typedef enum {
    ACTION_OPEN_DEV         = (1 << 0),
    ACTION_GET_DEV_INFO     = (1 << 1),
    ACTION_GET_CONFIG_DESC  = (1 << 2),
    ACTION_CLAIM_INTERFACE  = (1 << 3),
    ACTION_CLOSE_DEV        = (1 << 4),
} action_t;

#define DEV_MAX_COUNT           8 // Максимальна кількість пристроїв для обробки

typedef struct {
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    usb_device_handle_t dev_hdl;
    action_t actions;
    usb_transfer_t *midi_in_transfer;
} usb_device_t;

typedef struct {
    struct {
        union {
            struct {
                uint8_t unhandled_devices: 1;
                uint8_t shutdown: 1;
                uint8_t reserved6: 6;
            };
            uint8_t val;
        } flags;
        usb_device_t device[DEV_MAX_COUNT];
    } mux_protected;

    struct {
        usb_host_client_handle_t client_hdl;
        SemaphoreHandle_t mux_lock;
        QueueHandle_t midi_queue;
    } constant;
} class_driver_t;

static const char *TAG = "CLASS";
static class_driver_t *s_driver_obj;

static void midi_transfer_cb(usb_transfer_t *transfer)
{
    // A transfer has been completed. Handle the received data.
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        // Process MIDI data
        for (int i = 0; i < transfer->actual_num_bytes; i += 4) {
            // Check for Note On event (CIN 0x9) with velocity > 0
            if ((transfer->data_buffer[i] & 0x0F) == 0x09 && transfer->data_buffer[i + 2] > 0) {
                uint8_t note = transfer->data_buffer[i + 2];
                if (s_driver_obj && s_driver_obj->constant.midi_queue) {
                    // Send the note to the main application queue
                    xQueueSend(s_driver_obj->constant.midi_queue, &note, 0);
                }
            }
        }
        // Resubmit the transfer to continue listening for MIDI messages
        ESP_ERROR_CHECK(usb_host_transfer_submit(transfer));
    } else if (transfer->status != USB_TRANSFER_STATUS_NO_DEVICE && transfer->status != USB_TRANSFER_STATUS_CANCELED) {
        ESP_LOGW(TAG, "MIDI transfer failed status %d, resubmitting", transfer->status);
        ESP_ERROR_CHECK(usb_host_transfer_submit(transfer)); // Try to resubmit
    }
}

void class_driver_set_midi_queue(QueueHandle_t queue)
{
    if (s_driver_obj) {
        s_driver_obj->constant.midi_queue = queue;
    }
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    // This callback only handles device disconnection since polling is used for detection.
    class_driver_t *driver_obj = (class_driver_t *)arg;
    if (event_msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        ESP_LOGI(TAG, "MIDI device disconnected");
        xSemaphoreTake(driver_obj->constant.mux_lock, portMAX_DELAY);
        for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
            if (driver_obj->mux_protected.device[i].dev_hdl == event_msg->dev_gone.dev_hdl) {
                driver_obj->mux_protected.device[i].actions = ACTION_CLOSE_DEV; // Set flag to close device
                driver_obj->mux_protected.flags.unhandled_devices = 1;
                break;
            }
        }
        xSemaphoreGive(driver_obj->constant.mux_lock);
    }
}

static void action_open_dev(usb_device_t *device_obj)
{
    assert(device_obj->dev_addr != 0);
    ESP_LOGI(TAG, "Opening device at address %d", device_obj->dev_addr);
    ESP_ERROR_CHECK(usb_host_device_open(device_obj->client_hdl, device_obj->dev_addr, &device_obj->dev_hdl));
    device_obj->actions |= ACTION_GET_DEV_INFO; // Next action: get info
}

static void action_get_info(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting device information");
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(device_obj->dev_hdl, &dev_info));
    ESP_LOGI(TAG, "\t%s speed", (dev_info.speed == USB_SPEED_FULL) ? "Full" : "Low");
    ESP_LOGI(TAG, "\tbConfigurationValue %d", dev_info.bConfigurationValue);
    device_obj->actions |= ACTION_GET_CONFIG_DESC; // Next action: get config descriptor
}

static void action_get_config_desc(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting config descriptor");
    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(device_obj->dev_hdl, &config_desc));
    usb_print_config_descriptor(config_desc, NULL);
    device_obj->actions |= ACTION_CLAIM_INTERFACE; // Next action: claim interface
}

static void action_claim_interface(usb_device_t *device_obj)
{
    // This function claims the MIDI interface and starts listening for data.
    // It uses hardcoded values for the Yamaha keyboard based on the logs.
    // Interface Number: 3, Endpoint Address: 0x82, Packet Size: 64
    assert(device_obj->dev_hdl != NULL);
    
    int intf_num = 3;
    uint8_t ep_addr = 0x82;
    size_t ep_mps = 64;

    ESP_LOGI(TAG, "Claiming MIDI interface (num=%d, EP=0x%02X)", intf_num, ep_addr);
    esp_err_t err = usb_host_interface_claim(device_obj->client_hdl, device_obj->dev_hdl, intf_num, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to claim interface: 0x%x", err);
        return;
    }

    // Allocate and configure the USB transfer
    err = usb_host_transfer_alloc(ep_mps, 0, &device_obj->midi_in_transfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate transfer: 0x%x", err);
        usb_host_interface_release(device_obj->client_hdl, device_obj->dev_hdl, intf_num);
        return;
    }

    device_obj->midi_in_transfer->device_handle = device_obj->dev_hdl;
    device_obj->midi_in_transfer->bEndpointAddress = ep_addr;
    device_obj->midi_in_transfer->callback = midi_transfer_cb;
    device_obj->midi_in_transfer->context = device_obj;
    device_obj->midi_in_transfer->num_bytes = ep_mps;

    ESP_LOGI(TAG, "Submitting first MIDI IN transfer");
    err = usb_host_transfer_submit(device_obj->midi_in_transfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to submit transfer: 0x%x", err);
        usb_host_transfer_free(device_obj->midi_in_transfer);
        device_obj->midi_in_transfer = NULL;
        usb_host_interface_release(device_obj->client_hdl, device_obj->dev_hdl, intf_num);
    }
}

static void action_close_dev(usb_device_t *device_obj)
{
    ESP_LOGI(TAG, "Closing device addr %d", device_obj->dev_addr);
    if (device_obj->midi_in_transfer) {
        usb_host_transfer_free(device_obj->midi_in_transfer);
        device_obj->midi_in_transfer = NULL;
    }
    if (device_obj->dev_hdl) {
        ESP_ERROR_CHECK(usb_host_device_close(device_obj->client_hdl, device_obj->dev_hdl));
    }
    // Reset the device object for reuse
    device_obj->dev_hdl = NULL;
    device_obj->dev_addr = 0;
    device_obj->actions = 0;
}

// This function handles the state machine for a single device
static void class_driver_device_handle(usb_device_t *device_obj)
{
    // Loop until all actions for this device are handled
    while (device_obj->actions) {
        uint32_t action_to_take = device_obj->actions;
        device_obj->actions = 0; // Clear actions before handling

        if (action_to_take & ACTION_OPEN_DEV) action_open_dev(device_obj);
        if (action_to_take & ACTION_GET_DEV_INFO) action_get_info(device_obj);
        if (action_to_take & ACTION_GET_CONFIG_DESC) action_get_config_desc(device_obj);
        if (action_to_take & ACTION_CLAIM_INTERFACE) action_claim_interface(device_obj);
        if (action_to_take & ACTION_CLOSE_DEV) action_close_dev(device_obj);
    }
}

void class_driver_task(void *arg)
{
    static class_driver_t driver_obj = {0};
    usb_host_client_handle_t class_driver_client_hdl = NULL;

    ESP_LOGI(TAG, "Registering Client");

    SemaphoreHandle_t mux_lock = xSemaphoreCreateMutex();
    assert(mux_lock);

    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = CLIENT_NUM_EVENT_MSG,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = (void *) &driver_obj,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &class_driver_client_hdl));

    driver_obj.constant.mux_lock = mux_lock;
    driver_obj.constant.client_hdl = class_driver_client_hdl;
    for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
        driver_obj.mux_protected.device[i].client_hdl = class_driver_client_hdl;
    }
    s_driver_obj = &driver_obj;

    while (1) {
        // --- Polling for new devices (Workaround) ---
        uint8_t dev_addr_list[DEV_MAX_COUNT];
        int num_devs;
        ESP_ERROR_CHECK(usb_host_device_addr_list_fill(sizeof(dev_addr_list), dev_addr_list, &num_devs));

        xSemaphoreTake(driver_obj.constant.mux_lock, portMAX_DELAY);
        for (int i = 0; i < num_devs; i++) {
            uint8_t dev_addr = dev_addr_list[i];
            if (dev_addr == 0) continue;

            bool already_handled = false;
            for (int j = 0; j < DEV_MAX_COUNT; j++) {
                if (driver_obj.mux_protected.device[j].dev_addr == dev_addr) {
                    already_handled = true;
                    break;
                }
            }

            if (!already_handled) {
                ESP_LOGI(TAG, "Found new device with address %d", dev_addr);
                for (int j = 0; j < DEV_MAX_COUNT; j++) {
                    if (driver_obj.mux_protected.device[j].dev_addr == 0) { // Find an empty slot
                        driver_obj.mux_protected.device[j].dev_addr = dev_addr;
                        driver_obj.mux_protected.device[j].actions |= ACTION_OPEN_DEV;
                        driver_obj.mux_protected.flags.unhandled_devices = 1;
                        break;
                    }
                }
            }
        }
        xSemaphoreGive(driver_obj.constant.mux_lock);
        // --- End of Workaround ---

        // Handle any pending actions for devices
        if (driver_obj.mux_protected.flags.unhandled_devices) {
            xSemaphoreTake(driver_obj.constant.mux_lock, portMAX_DELAY);
            driver_obj.mux_protected.flags.unhandled_devices = 0; // Reset flag
            for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
                if (driver_obj.mux_protected.device[i].actions) {
                    class_driver_device_handle(&driver_obj.mux_protected.device[i]);
                }
            }
            xSemaphoreGive(driver_obj.constant.mux_lock);
        }
        
        // Handle library events (like disconnection)
        usb_host_client_handle_events(driver_obj.constant.client_hdl, pdMS_TO_TICKS(10));

        vTaskDelay(pdMS_TO_TICKS(100)); // Poll every 100ms
    }

    // Cleanup
    ESP_LOGI(TAG, "Deregistering Class Client");
    ESP_ERROR_CHECK(usb_host_client_deregister(class_driver_client_hdl));
    vSemaphoreDelete(mux_lock);
    vTaskDelete(NULL);
}
