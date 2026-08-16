#ifndef __GAP_H__
#define __GAP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"

/* Defines */
#define BLE_GAP_APPEARANCE_GENERIC_TAG 0x0200
#define BLE_GAP_URI_PREFIX_HTTPS 0x17
#define BLE_GAP_LE_ROLE_PERIPHERAL 0x00

void adv_init(void);
void gap_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __GAP_H__ */
