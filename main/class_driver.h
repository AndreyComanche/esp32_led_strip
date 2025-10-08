#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

void class_driver_task(void *arg);
void class_driver_client_deregister(void);
void class_driver_set_midi_queue(QueueHandle_t queue);
