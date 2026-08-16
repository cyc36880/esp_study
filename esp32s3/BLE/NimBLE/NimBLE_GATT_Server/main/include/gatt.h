#ifndef __GATT_H__
#define __GATT_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
/* NimBLE GATT APIs */
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"

/* NimBLE GAP APIs */
#include "host/ble_gap.h"

void gatt_svr_subscribe_cb(struct ble_gap_event *event);
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
void gatt_init(void);


#ifdef __cplusplus
}
#endif

#endif /* __GATT_H__ */



