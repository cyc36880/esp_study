#include "gatt.h"
#include "common.h"

static int gatt_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static uint16_t gatt_chr_val_handle = 0;

static const struct ble_gatt_svc_def gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1815),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A19),
                .access_cb = gatt_chr_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_WRITE,
                .val_handle = &gatt_chr_val_handle,
            },
            { 0 }, /* No more characteristics in this service. */
        },
    },
    { 0 }, /* No more services. */
};


static int gatt_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* Local variables */
    int rc = 0;
    
    static uint8_t val = 0;
    /* Verify connection handle */
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) 
    {
        ESP_LOGI(TAG,
                    "characteristic write by nimble stack; attr_handle=%d",
                    attr_handle);
    }

    if (attr_handle != gatt_chr_val_handle)
    {
        ESP_LOGE(
            TAG,
            "unexpected access operation to gatt characteristic, opcode: %d",
            ctxt->op);
        goto error;
    }


    switch (ctxt->op) 
    {
        // 写入
        case BLE_GATT_ACCESS_OP_WRITE_CHR:
            // if (ctxt->om->om_len == 1) 
            // {
            //     val = ctxt->om->om_data[0];
            //     ESP_LOGI(TAG,
            //                 "characteristic write by nimble stack; val=%d",
            //                 val);
            //     return rc;
            // }
            for (int i = 0; i < ctxt->om->om_len; i++)
            {
                val = ctxt->om->om_data[i];
                printf("%c", val);
            }
            printf("\n");
            return 0;
            break;

        // 读取
        case BLE_GATT_ACCESS_OP_READ_CHR:
            rc = os_mbuf_append(ctxt->om, &val,
                                sizeof(val));
            
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            break;

        default:
            goto error;
    }

error:
    ESP_LOGE(TAG,
             "unexpected access operation to gatt characteristic, opcode: %d",
             ctxt->op);

    return BLE_ATT_ERR_UNLIKELY;
}

void gatt_svr_subscribe_cb(struct ble_gap_event *event) 
{
    /* Check connection handle */
    if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "subscribe event; conn_handle=%d attr_handle=%d",
                 event->subscribe.conn_handle, event->subscribe.attr_handle);
    } else {
        ESP_LOGI(TAG, "subscribe by nimble stack; attr_handle=%d",
                 event->subscribe.attr_handle);
    }

    // /* Check attribute handle */
    // if (event->subscribe.attr_handle == heart_rate_chr_val_handle) {
    //     /* Update heart rate subscription status */
    //     heart_rate_chr_conn_handle = event->subscribe.conn_handle;
    //     heart_rate_chr_conn_handle_inited = true;
    //     heart_rate_ind_status = event->subscribe.cur_indicate;
    // }
}


void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
        /* Local variables */
    char buf[BLE_UUID_STR_LEN];

    /* Handle GATT attributes register events */
    switch (ctxt->op) 
    {
        /* Service register event */
        case BLE_GATT_REGISTER_OP_SVC:
            ESP_LOGD(TAG, "registered service %s with handle=%d",
                    ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                    ctxt->svc.handle);
            break;

        /* Characteristic register event */
        case BLE_GATT_REGISTER_OP_CHR:
            ESP_LOGD(TAG,
                    "registering characteristic %s with "
                    "def_handle=%d val_handle=%d",
                    ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                    ctxt->chr.def_handle, ctxt->chr.val_handle);
            break;

        /* Descriptor register event */
        case BLE_GATT_REGISTER_OP_DSC:
            ESP_LOGD(TAG, "registering descriptor %s with handle=%d",
                    ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                    ctxt->dsc.handle);
            break;

        /* Unknown event */
        default:
            ESP_LOGE(TAG, "unexpected gatt register op: %d", ctxt->op);
            break;
    }
}

void gatt_init(void)
{
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_services);
    ble_gatts_add_svcs(gatt_services);
}

