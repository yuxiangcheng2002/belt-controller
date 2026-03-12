#include "ble_midi.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_midi";

static const ble_uuid128_t s_midi_service_uuid =
    BLE_UUID128_INIT(0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
                     0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03);

static const ble_uuid128_t s_midi_chr_uuid =
    BLE_UUID128_INIT(0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1,
                     0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77);

static const char *s_device_name = "Belt MIDI Controller";
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_midi_val_handle;
static uint8_t s_own_addr_type;
static bool s_connected;
static bool s_notify_enabled;

static void start_advertising(void);

static int chr_midi_io(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return 0;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static int chr_dev_info(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid = ble_uuid_u16(ctxt->chr->uuid);

    if (uuid == 0x2A29) {
        const char *mfr = "Fiber Robotics";
        return os_mbuf_append(ctxt->om, mfr, strlen(mfr)) == 0
                   ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (uuid == 0x2A24) {
        const char *model = "Belt MIDI Controller";
        return os_mbuf_append(ctxt->om, model, strlen(model)) == 0
                   ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_midi_service_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]) {
                {
                    .uuid = &s_midi_chr_uuid.u,
                    .access_cb = chr_midi_io,
                    .val_handle = &s_midi_val_handle,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY |
                             BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                },
                {0},
            },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics =
            (struct ble_gatt_chr_def[]) {
                {
                    .uuid = BLE_UUID16_DECLARE(0x2A29),
                    .access_cb = chr_dev_info,
                    .flags = BLE_GATT_CHR_F_READ,
                },
                {
                    .uuid = BLE_UUID16_DECLARE(0x2A24),
                    .access_cb = chr_dev_info,
                    .flags = BLE_GATT_CHR_F_READ,
                },
                {0},
            },
    },
    {0},
};

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_connected = true;
            s_notify_enabled = false;
            ESP_LOGI(TAG, "Connected (handle=%d)", s_conn_handle);
        } else {
            ESP_LOGW(TAG, "Connect failed: %d", event->connect.status);
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected (reason=%d)", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_connected = false;
        s_notify_enabled = false;
        start_advertising();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_midi_val_handle) {
            s_notify_enabled = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "MIDI notify %s", s_notify_enabled ? "on" : "off");
        }
        break;

    default:
        break;
    }

    return 0;
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_hs_adv_fields scan_rsp = {0};
    int rc;

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t[]){s_midi_service_uuid};
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return;
    }

    scan_rsp.name = (const uint8_t *)s_device_name;
    scan_rsp.name_len = strlen(s_device_name);
    scan_rsp.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&scan_rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &params, gap_event_handler, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "Advertising as \"%s\"", s_device_name);
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

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

static esp_err_t ble_midi_send_message(const uint8_t *message, size_t message_len)
{
    uint8_t packet[8];
    uint16_t timestamp;
    struct os_mbuf *om;
    int rc;

    if (message == NULL || message_len == 0 || message_len > 3) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_connected || !s_notify_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    timestamp = (uint16_t)((esp_timer_get_time() / 1000ULL) & 0x1FFFU);
    packet[0] = (uint8_t)(0x80U | ((timestamp >> 7) & 0x3FU));
    packet[1] = (uint8_t)(0x80U | (timestamp & 0x7FU));
    memcpy(&packet[2], message, message_len);

    om = ble_hs_mbuf_from_flat(packet, message_len + 2);
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }

    rc = ble_gatts_notify_custom(s_conn_handle, s_midi_val_handle, om);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t ble_midi_init(const char *device_name)
{
    esp_err_t ret;
    int rc;

    if (device_name != NULL) {
        s_device_name = device_name;
    }

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init: %s", esp_err_to_name(ret));
        return ret;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(s_device_name);

    rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    nimble_port_freertos_init(nimble_host_task);
    return ESP_OK;
}

bool ble_midi_connected(void)
{
    return s_connected;
}

esp_err_t ble_midi_send_note(uint8_t channel, uint8_t note, uint8_t velocity, bool note_on)
{
    const uint8_t message[3] = {
        (uint8_t)((note_on ? 0x90U : 0x80U) | (channel & 0x0FU)),
        note,
        velocity,
    };

    return ble_midi_send_message(message, sizeof(message));
}

esp_err_t ble_midi_send_control_change(uint8_t channel, uint8_t controller, uint8_t value)
{
    const uint8_t message[3] = {
        (uint8_t)(0xB0U | (channel & 0x0FU)),
        controller,
        value,
    };

    return ble_midi_send_message(message, sizeof(message));
}
