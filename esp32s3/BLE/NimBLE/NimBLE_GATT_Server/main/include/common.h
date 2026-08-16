#ifndef __COMMON_H__
#define __COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif

/* std api */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* esp apis */
#include "esp_log.h"
#include "nvs_flash.h"


/* FreeRTOS APIs */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* NimBLE stack APIs */
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

/* Defines */
#define TAG "NimBLE_GATT_Server"
#define DEVICE_NAME "NimBLE_GATT"

#ifdef __cplusplus
}
#endif

#endif /* __COMMON_H__ */
