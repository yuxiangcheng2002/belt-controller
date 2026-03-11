#include "ble_hid.h"

#include <string.h>
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

static const char *TAG = "ble_hid";

static const char *s_device_name = "Belt Controller";
static uint16_t    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool        s_connected;
static uint16_t    s_kb_report_handle;
static uint16_t    s_gp_report_handle;
static bool        s_kb_notify;
static bool        s_gp_notify;
static uint8_t     s_own_addr_type;

static void start_advertising(void);

/* ------------------------------------------------------------------ */
/*  Composite HID Report Descriptor                                    */
/*  Report ID 1 = Keyboard,  Report ID 2 = Gamepad                    */
/* ------------------------------------------------------------------ */

static const uint8_t hid_report_map[] = {
    /* ---- Report ID 1: Keyboard ---- */
    0x05, 0x01,                          /* Usage Page (Generic Desktop)   */
    0x09, 0x06,                          /* Usage (Keyboard)               */
    0xA1, 0x01,                          /* Collection (Application)       */
    0x85, 0x01,                          /*   Report ID (1)                */
    /* Modifier keys — 8 bits */
    0x05, 0x07,                          /*   Usage Page (Keyboard)        */
    0x19, 0xE0,                          /*   Usage Min (Left Ctrl)        */
    0x29, 0xE7,                          /*   Usage Max (Right GUI)        */
    0x15, 0x00,                          /*   Logical Min (0)              */
    0x25, 0x01,                          /*   Logical Max (1)              */
    0x75, 0x01,                          /*   Report Size  (1)             */
    0x95, 0x08,                          /*   Report Count (8)             */
    0x81, 0x02,                          /*   Input (Data, Var, Abs)       */
    /* Reserved byte */
    0x75, 0x08,                          /*   Report Size  (8)             */
    0x95, 0x01,                          /*   Report Count (1)             */
    0x81, 0x03,                          /*   Input (Const)                */
    /* Keycodes — 6 bytes */
    0x05, 0x07,                          /*   Usage Page (Keyboard)        */
    0x19, 0x00,                          /*   Usage Min (0)                */
    0x29, 0xFF,                          /*   Usage Max (255)              */
    0x15, 0x00,                          /*   Logical Min (0)              */
    0x26, 0xFF, 0x00,                    /*   Logical Max (255)            */
    0x75, 0x08,                          /*   Report Size  (8)             */
    0x95, 0x06,                          /*   Report Count (6)             */
    0x81, 0x00,                          /*   Input (Data, Array)          */
    0xC0,                                /* End Collection                 */

    /* ---- Report ID 2: Gamepad ---- */
    0x05, 0x01,                          /* Usage Page (Generic Desktop)   */
    0x09, 0x05,                          /* Usage (Gamepad)                */
    0xA1, 0x01,                          /* Collection (Application)       */
    0x85, 0x02,                          /*   Report ID (2)                */
    /* 2 buttons */
    0x05, 0x09,                          /*   Usage Page (Button)          */
    0x19, 0x01,                          /*   Usage Min (1)                */
    0x29, 0x02,                          /*   Usage Max (2)                */
    0x15, 0x00,                          /*   Logical Min (0)              */
    0x25, 0x01,                          /*   Logical Max (1)              */
    0x75, 0x01,                          /*   Report Size  (1)             */
    0x95, 0x02,                          /*   Report Count (2)             */
    0x81, 0x02,                          /*   Input (Data, Var, Abs)       */
    /* 6-bit padding */
    0x75, 0x01,                          /*   Report Size  (1)             */
    0x95, 0x06,                          /*   Report Count (6)             */
    0x81, 0x03,                          /*   Input (Const)                */
    /* X and Y axes — 16-bit, 0..4095 */
    0x05, 0x01,                          /*   Usage Page (Generic Desktop) */
    0x09, 0x30,                          /*   Usage (X)                    */
    0x09, 0x31,                          /*   Usage (Y)                    */
    0x15, 0x00,                          /*   Logical Min (0)              */
    0x27, 0xFF, 0x0F, 0x00, 0x00,        /*   Logical Max (4095)           */
    0x75, 0x10,                          /*   Report Size  (16)            */
    0x95, 0x02,                          /*   Report Count (2)             */
    0x81, 0x02,                          /*   Input (Data, Var, Abs)       */
    0xC0,                                /* End Collection                 */
};

static const uint8_t hid_information[] = { 0x11, 0x01, 0x00, 0x02 };
static uint8_t hid_protocol_mode = 0x01;

/* ------------------------------------------------------------------ */
/*  GATT characteristic callbacks                                      */
/* ------------------------------------------------------------------ */

static int chr_report_map(uint16_t ch, uint16_t ah,
                           struct ble_gatt_access_ctxt *c, void *a)
{
    return os_mbuf_append(c->om, hid_report_map, sizeof(hid_report_map)) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int chr_hid_info(uint16_t ch, uint16_t ah,
                         struct ble_gatt_access_ctxt *c, void *a)
{
    return os_mbuf_append(c->om, hid_information, sizeof(hid_information)) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int chr_control_point(uint16_t ch, uint16_t ah,
                              struct ble_gatt_access_ctxt *c, void *a)
{
    return 0;
}

static int chr_protocol_mode(uint16_t ch, uint16_t ah,
                              struct ble_gatt_access_ctxt *c, void *a)
{
    if (c->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(c->om, &hid_protocol_mode, 1) == 0
                   ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (c->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        ble_hs_mbuf_to_flat(c->om, &hid_protocol_mode, 1, NULL);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* Keyboard report — read returns empty 8-byte report */
static int chr_kb_report(uint16_t ch, uint16_t ah,
                          struct ble_gatt_access_ctxt *c, void *a)
{
    if (c->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        ble_hid_kb_report_t empty = {0};
        return os_mbuf_append(c->om, &empty, sizeof(empty)) == 0
                   ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* Gamepad report — read returns empty 5-byte report */
static int chr_gp_report(uint16_t ch, uint16_t ah,
                          struct ble_gatt_access_ctxt *c, void *a)
{
    if (c->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        ble_hid_gp_report_t empty = {0};
        return os_mbuf_append(c->om, &empty, sizeof(empty)) == 0
                   ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* Report Reference descriptors — Report ID + type (0x01 = Input) */
static int dsc_kb_report_ref(uint16_t ch, uint16_t ah,
                              struct ble_gatt_access_ctxt *c, void *a)
{
    static const uint8_t ref[] = { 0x01, 0x01 };  /* ID 1, Input */
    return os_mbuf_append(c->om, ref, sizeof(ref)) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int dsc_gp_report_ref(uint16_t ch, uint16_t ah,
                              struct ble_gatt_access_ctxt *c, void *a)
{
    static const uint8_t ref[] = { 0x02, 0x01 };  /* ID 2, Input */
    return os_mbuf_append(c->om, ref, sizeof(ref)) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int chr_battery(uint16_t ch, uint16_t ah,
                        struct ble_gatt_access_ctxt *c, void *a)
{
    uint8_t level = 100;
    return os_mbuf_append(c->om, &level, 1) == 0
               ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int chr_dev_info(uint16_t ch, uint16_t ah,
                         struct ble_gatt_access_ctxt *c, void *a)
{
    uint16_t uuid = ble_uuid_u16(c->chr->uuid);
    if (uuid == 0x2A29) {
        const char *mfr = "Fiber Robotics";
        return os_mbuf_append(c->om, mfr, strlen(mfr)) == 0
                   ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (uuid == 0x2A50) {
        static const uint8_t pnp[] = { 0x02, 0xE5, 0x02, 0x01, 0x00, 0x00, 0x01 };
        return os_mbuf_append(c->om, pnp, sizeof(pnp)) == 0
                   ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/* ------------------------------------------------------------------ */
/*  GATT service table                                                 */
/* ------------------------------------------------------------------ */

static const struct ble_gatt_svc_def gatt_svcs[] = {
    /* HID Service */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A4B), .access_cb = chr_report_map,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2A4A), .access_cb = chr_hid_info,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2A4C), .access_cb = chr_control_point,
              .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = BLE_UUID16_DECLARE(0x2A4E), .access_cb = chr_protocol_mode,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP },
            /* Keyboard Input Report (ID 1) */
            { .uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = chr_kb_report,
              .val_handle = &s_kb_report_handle,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY
                     | BLE_GATT_CHR_F_READ_ENC,
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .access_cb = dsc_kb_report_ref,
                    .att_flags = BLE_ATT_F_READ },
                  { 0 },
              }},
            /* Gamepad Input Report (ID 2) */
            { .uuid = BLE_UUID16_DECLARE(0x2A4D), .access_cb = chr_gp_report,
              .val_handle = &s_gp_report_handle,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY
                     | BLE_GATT_CHR_F_READ_ENC,
              .descriptors = (struct ble_gatt_dsc_def[]) {
                  { .uuid = BLE_UUID16_DECLARE(0x2908), .access_cb = dsc_gp_report_ref,
                    .att_flags = BLE_ATT_F_READ },
                  { 0 },
              }},
            { 0 },
        },
    },
    /* Device Information Service */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A29), .access_cb = chr_dev_info,
              .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2A50), .access_cb = chr_dev_info,
              .flags = BLE_GATT_CHR_F_READ },
            { 0 },
        },
    },
    /* Battery Service */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = chr_battery,
              .flags = BLE_GATT_CHR_F_READ },
            { 0 },
        },
    },
    { 0 },
};

/* ------------------------------------------------------------------ */
/*  GAP event handler                                                  */
/* ------------------------------------------------------------------ */

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_connected = true;
            s_kb_notify = false;
            s_gp_notify = false;
            ESP_LOGI(TAG, "Connected (handle=%d)", s_conn_handle);
            ble_gap_security_initiate(s_conn_handle);
        } else {
            ESP_LOGW(TAG, "Connect failed: %d", event->connect.status);
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected (reason=%d)", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_connected = false;
        s_kb_notify = false;
        s_gp_notify = false;
        start_advertising();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_kb_report_handle) {
            s_kb_notify = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "Keyboard notify %s", s_kb_notify ? "on" : "off");
        }
        if (event->subscribe.attr_handle == s_gp_report_handle) {
            s_gp_notify = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "Gamepad notify %s", s_gp_notify ? "on" : "off");
        }
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "Encryption changed, status=%d", event->enc_change.status);
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    default:
        break;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Advertising                                                        */
/* ------------------------------------------------------------------ */

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)s_device_name;
    fields.name_len = strlen(s_device_name);
    fields.name_is_complete = 1;
    fields.appearance = 0x03C0;  /* Generic HID (composite keyboard + gamepad) */
    fields.appearance_is_present = 1;
    fields.uuids16 = (ble_uuid16_t[]) { BLE_UUID16_INIT(0x1812) };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                      &params, gap_event_handler, NULL);
    ESP_LOGI(TAG, "Advertising as \"%s\"", s_device_name);
}

/* ------------------------------------------------------------------ */
/*  NimBLE host callbacks                                              */
/* ------------------------------------------------------------------ */

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "BLE host reset, reason=%d", reason);
}

static void nimble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

esp_err_t ble_hid_init(const char *device_name)
{
    if (device_name) {
        s_device_name = device_name;
    }

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init: %s", esp_err_to_name(ret));
        return ret;
    }

    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_hs_cfg.sm_io_cap         = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding        = 1;
    ble_hs_cfg.sm_mitm           = 0;
    ble_hs_cfg.sm_sc             = 1;
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(s_device_name);
    ble_svc_gap_device_appearance_set(0x03C0);

    int rc = ble_gatts_count_cfg(gatt_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(gatt_svcs);
    assert(rc == 0);

    ble_store_config_init();
    nimble_port_freertos_init(nimble_host_task);
    return ESP_OK;
}

bool ble_hid_connected(void)
{
    return s_connected;
}

esp_err_t ble_hid_send_keyboard(const ble_hid_kb_report_t *report)
{
    if (!s_connected || !s_kb_notify) {
        return ESP_ERR_INVALID_STATE;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(report, sizeof(*report));
    if (!om) {
        return ESP_ERR_NO_MEM;
    }
    int rc = ble_gatts_notify_custom(s_conn_handle, s_kb_report_handle, om);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t ble_hid_send_gamepad(const ble_hid_gp_report_t *report)
{
    if (!s_connected || !s_gp_notify) {
        return ESP_ERR_INVALID_STATE;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(report, sizeof(*report));
    if (!om) {
        return ESP_ERR_NO_MEM;
    }
    int rc = ble_gatts_notify_custom(s_conn_handle, s_gp_report_handle, om);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}
