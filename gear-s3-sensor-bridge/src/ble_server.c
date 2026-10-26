#include "ble_server.h"
#include "protocol.h"

#include <bluetooth.h>
#include <dlog.h>
#include <system_info.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>

#define UUID_BRIDGE_SERVICE "7b3a0001-6d2b-4a4e-9f12-8f4d2c7a1000"
#define UUID_MOTION "7b3a0002-6d2b-4a4e-9f12-8f4d2c7a1000"
#define UUID_HR_META "7b3a0003-6d2b-4a4e-9f12-8f4d2c7a1000"
#define UUID_COMMAND "7b3a0004-6d2b-4a4e-9f12-8f4d2c7a1000"
#define UUID_STATUS "7b3a0005-6d2b-4a4e-9f12-8f4d2c7a1000"
#define UUID_HR_SERVICE "0000180d-0000-1000-8000-00805f9b34fb"
#define UUID_HR_MEASUREMENT "00002a37-0000-1000-8000-00805f9b34fb"
#define LOG_TAG "GearS3SensorBridge"
#define SB_GATT_REQUEST_READ BT_GATT_REQUEST_TYPE_READ
#define SB_GATT_REQUEST_WRITE BT_GATT_REQUEST_TYPE_WRITE
#define SB_ATT_SUCCESS 0x00
#define SB_ATT_INVALID_OFFSET 0x07
#define SB_ATT_INVALID_ATTRIBUTE_LENGTH 0x0d

typedef struct {
    bt_gatt_server_h server;
    bt_gatt_h bridge_service;
    bt_gatt_h hr_service;
    bt_gatt_h motion;
    bt_gatt_h hr_meta;
    bt_gatt_h command;
    bt_gatt_h status;
    bt_gatt_h hr_measurement;
    bt_advertiser_h advertiser;
    ble_server_callbacks_t callbacks;
    uint8_t status_value[SB_STATUS_SIZE];
    bool connected;
    bool motion_notify;
    bool hr_notify;
    bool hr_meta_notify;
    bool status_notify;
    bool bluetooth_initialized;
    bool gatt_initialized;
    bool server_started;
    bool bridge_registered;
    bool hr_registered;
    bool advertising;
    bool advertising_pending;
    bool motion_pending;
    bool hr_pending;
    bool hr_meta_pending;
    bool status_pending;
    bool sample_sent;
    uint32_t last_sample_sent_ms;
    bool adapter_callback_registered;
    bool connection_callback_registered;
    int error;
} ble_state_t;

static ble_state_t s_ble;

static uint32_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint32_t)((uint64_t)now.tv_sec * 1000u + now.tv_nsec / 1000000u);
}
static void advertising_state_changed(int result,
                                      bt_advertiser_h advertiser,
                                      bt_adapter_le_advertising_state_e state,
                                      void *user_data);

static void log_cleanup_failure(const char *operation, int error)
{
    if (error != BT_ERROR_NONE)
        dlog_print(DLOG_WARN, LOG_TAG, "%s failed: %d", operation, error);
}

static int add_characteristic(bt_gatt_h service, const char *uuid,
                              int permissions, int properties,
                              bt_gatt_h *out)
{
    int ret = bt_gatt_characteristic_create(uuid, permissions, properties,
                                            NULL, 0, out);
    if (ret != BT_ERROR_NONE)
        return ret;
    ret = bt_gatt_service_add_characteristic(service, *out);
    if (ret != BT_ERROR_NONE) {
        bt_gatt_characteristic_destroy(*out);
        *out = NULL;
    }
    return ret;
}

static void notify_state(void)
{
    if (s_ble.callbacks.state_changed)
        s_ble.callbacks.state_changed(s_ble.connected, s_ble.error,
                                      s_ble.callbacks.user_data);
}

static int start_advertising(void)
{
    if (!s_ble.advertiser)
        return BT_ERROR_NOT_ENABLED;
    if (s_ble.advertising || s_ble.advertising_pending)
        return BT_ERROR_NONE;
    s_ble.advertising_pending = true;
    int ret = bt_adapter_le_start_advertising_new(
        s_ble.advertiser, advertising_state_changed, NULL);
    if (ret != BT_ERROR_NONE)
        s_ble.advertising_pending = false;
    return ret;
}

static void read_requested(const char *client_address, int request_id,
                           bt_gatt_server_h server,
                           bt_gatt_h characteristic,
                           int offset, void *user_data)
{
    (void)client_address;
    (void)server;
    (void)user_data;
    if (characteristic != s_ble.status || offset < 0 ||
        offset > SB_STATUS_SIZE) {
        bt_gatt_server_send_response(request_id, SB_GATT_REQUEST_READ,
                                     offset, SB_ATT_INVALID_OFFSET,
                                     NULL, 0);
        return;
    }
    bt_gatt_server_send_response(request_id, SB_GATT_REQUEST_READ,
                                 offset, SB_ATT_SUCCESS,
                                 (char *)s_ble.status_value,
                                 SB_STATUS_SIZE);
}

static void write_requested(const char *client_address, int request_id,
                            bt_gatt_server_h server,
                            bt_gatt_h characteristic,
                            bool response_needed, int offset,
                            const char *value, int value_length,
                            void *user_data)
{
    (void)client_address;
    (void)server;
    (void)user_data;
    int response_status = SB_ATT_SUCCESS;

    if (characteristic != s_ble.command || offset != 0 ||
        value_length < 0 || !value) {
        response_status = SB_ATT_INVALID_ATTRIBUTE_LENGTH;
    } else {
        sb_command_t command;
        if (!sb_decode_command((const uint8_t *)value, (size_t)value_length,
                               &command)) {
            response_status = SB_ATT_INVALID_ATTRIBUTE_LENGTH;
        } else if (s_ble.callbacks.command) {
            s_ble.error = BLE_BRIDGE_OK;
            s_ble.callbacks.command((uint8_t)command.type, command.rate_hz,
                                    s_ble.callbacks.user_data);
        }
    }

    if (response_needed)
        bt_gatt_server_send_response(request_id, SB_GATT_REQUEST_WRITE,
                                     offset, response_status, NULL, 0);
    if (response_status != SB_ATT_SUCCESS) {
        s_ble.error = BLE_BRIDGE_ERR_COMMAND;
        notify_state();
    }
}

static void notification_state_changed(bool enabled, bt_gatt_server_h server,
                                       bt_gatt_h characteristic,
                                       void *user_data)
{
    (void)server;
    (void)user_data;
    if (characteristic == s_ble.motion)
        s_ble.motion_notify = enabled;
    else if (characteristic == s_ble.hr_measurement)
        s_ble.hr_notify = enabled;
    else if (characteristic == s_ble.hr_meta)
        s_ble.hr_meta_notify = enabled;
    else if (characteristic == s_ble.status)
        s_ble.status_notify = enabled;
    notify_state();
}

static void connection_changed(int result, bool connected,
                               const char *remote_address, void *user_data)
{
    (void)remote_address;
    (void)user_data;
    if (result != BT_ERROR_NONE) {
        s_ble.error = BLE_BRIDGE_ERR_ADVERTISING;
        notify_state();
        return;
    }
    s_ble.connected = connected;
    s_ble.error = BLE_BRIDGE_OK;
    s_ble.sample_sent = false;
    if (connected) {
        if (s_ble.advertising || s_ble.advertising_pending)
            bt_adapter_le_stop_advertising(s_ble.advertiser);
        s_ble.advertising = false;
        s_ble.advertising_pending = false;
    }
    if (!connected) {
        s_ble.motion_pending = false;
        s_ble.hr_pending = false;
        s_ble.hr_meta_pending = false;
        s_ble.status_pending = false;
        s_ble.motion_notify = false;
        s_ble.hr_notify = false;
        s_ble.hr_meta_notify = false;
        s_ble.status_notify = false;
        if (s_ble.server_started) {
            int ret = start_advertising();
            if (ret != BT_ERROR_NONE)
                s_ble.error = ret == BT_ERROR_NOT_ENABLED
                    ? BLE_BRIDGE_ERR_BLUETOOTH : BLE_BRIDGE_ERR_ADVERTISING;
        }
    }
    notify_state();
}

static void advertising_state_changed(int result,
                                      bt_advertiser_h advertiser,
                                      bt_adapter_le_advertising_state_e state,
                                      void *user_data)
{
    (void)user_data;
    if (advertiser != s_ble.advertiser)
        return;
    s_ble.advertising_pending = false;
    if (result != BT_ERROR_NONE ||
        state == BT_ADAPTER_LE_ADVERTISING_STOPPED) {
        s_ble.advertising = false;
        if (result != BT_ERROR_NONE)
            s_ble.error = BLE_BRIDGE_ERR_ADVERTISING;
        notify_state();
    } else {
        s_ble.advertising = true;
        s_ble.error = BLE_BRIDGE_OK;
        notify_state();
    }
}

static void adapter_state_changed(int result, bt_adapter_state_e state,
                                  void *user_data)
{
    (void)user_data;
    if (result != BT_ERROR_NONE) {
        dlog_print(DLOG_WARN, LOG_TAG,
                   "Bluetooth adapter state callback failed: %d", result);
        return;
    }
    if (state == BT_ADAPTER_DISABLED) {
        /* The stack may discard local registrations when powered off.
         * Recreate the server on the next foreground retry. */
        ble_server_callbacks_t saved_callbacks = s_ble.callbacks;
        ble_server_stop();
        s_ble.callbacks = saved_callbacks;
        s_ble.error = BLE_BRIDGE_ERR_BLUETOOTH;
        s_ble.motion_notify = false;
        s_ble.hr_notify = false;
        s_ble.hr_meta_notify = false;
        s_ble.status_notify = false;
        notify_state();
    } else if (state == BT_ADAPTER_ENABLED && s_ble.server_started) {
        s_ble.error = start_advertising() == BT_ERROR_NONE
            ? BLE_BRIDGE_OK : BLE_BRIDGE_ERR_ADVERTISING;
        notify_state();
    }
}

static int register_characteristic_callbacks(bt_gatt_h characteristic,
                                             bool readable, bool writable,
                                             bool notifiable)
{
    int ret;
    if (readable) {
        ret = bt_gatt_server_set_read_value_requested_cb(
            characteristic, read_requested, NULL);
        if (ret != BT_ERROR_NONE)
            return ret;
    }
    if (writable) {
        ret = bt_gatt_server_set_write_value_requested_cb(
            characteristic, write_requested, NULL);
        if (ret != BT_ERROR_NONE)
            return ret;
    }
    if (notifiable) {
        ret = bt_gatt_server_set_characteristic_notification_state_change_cb(
            characteristic, notification_state_changed, NULL);
        if (ret != BT_ERROR_NONE)
            return ret;
    }
    return BT_ERROR_NONE;
}

int ble_server_start(const ble_server_callbacks_t *callbacks)
{
    int ret;
    if (s_ble.bluetooth_initialized)
        ble_server_stop();
    memset(&s_ble, 0, sizeof(s_ble));
    if (callbacks)
        s_ble.callbacks = *callbacks;

    bool supported = false;
    ret = system_info_get_platform_bool(
        "http://tizen.org/feature/network.bluetooth.le", &supported);
    if (ret != 0 || !supported)
        return BT_ERROR_NOT_SUPPORTED;
    ret = system_info_get_platform_bool(
        "http://tizen.org/feature/network.bluetooth.le.gatt.server", &supported);
    if (ret != 0 || !supported)
        return BT_ERROR_NOT_SUPPORTED;

    ret = bt_initialize();
    if (ret != BT_ERROR_NONE)
        return ret;
    s_ble.bluetooth_initialized = true;

    bt_adapter_state_e adapter_state;
    ret = bt_adapter_get_state(&adapter_state);
    if (ret != BT_ERROR_NONE)
        goto fail;
    if (adapter_state != BT_ADAPTER_ENABLED) {
        ret = BT_ERROR_NOT_ENABLED;
        goto fail;
    }

    ret = bt_gatt_server_initialize();
    if (ret != BT_ERROR_NONE)
        goto fail;
    s_ble.gatt_initialized = true;
    ret = bt_gatt_server_create(&s_ble.server);
    if (ret != BT_ERROR_NONE)
        goto fail;

    ret = bt_gatt_service_create(UUID_BRIDGE_SERVICE,
                                 BT_GATT_SERVICE_TYPE_PRIMARY,
                                 &s_ble.bridge_service);
    if (ret != BT_ERROR_NONE)
        goto fail;

    ret = add_characteristic(s_ble.bridge_service, UUID_MOTION, 0,
                             BT_GATT_PROPERTY_NOTIFY, &s_ble.motion);
    if (ret != BT_ERROR_NONE)
        goto fail;
    ret = add_characteristic(s_ble.bridge_service, UUID_HR_META, 0,
                             BT_GATT_PROPERTY_NOTIFY, &s_ble.hr_meta);
    if (ret != BT_ERROR_NONE)
        goto fail;
    ret = add_characteristic(s_ble.bridge_service, UUID_COMMAND,
                             BT_GATT_PERMISSION_WRITE,
                             BT_GATT_PROPERTY_WRITE, &s_ble.command);
    if (ret != BT_ERROR_NONE)
        goto fail;
    ret = add_characteristic(s_ble.bridge_service, UUID_STATUS,
                             BT_GATT_PERMISSION_READ,
                             BT_GATT_PROPERTY_READ | BT_GATT_PROPERTY_NOTIFY,
                             &s_ble.status);
    if (ret != BT_ERROR_NONE)
        goto fail;

    ret = bt_gatt_service_create(UUID_HR_SERVICE,
                                 BT_GATT_SERVICE_TYPE_PRIMARY,
                                 &s_ble.hr_service);
    if (ret != BT_ERROR_NONE)
        goto fail;
    ret = add_characteristic(s_ble.hr_service, UUID_HR_MEASUREMENT, 0,
                             BT_GATT_PROPERTY_NOTIFY,
                             &s_ble.hr_measurement);
    if (ret != BT_ERROR_NONE)
        goto fail;

    ret = register_characteristic_callbacks(s_ble.motion, false, false, true);
    if (ret == BT_ERROR_NONE)
        ret = register_characteristic_callbacks(s_ble.hr_meta, false, false, true);
    if (ret == BT_ERROR_NONE)
        ret = register_characteristic_callbacks(s_ble.hr_measurement,
                                                false, false, true);
    if (ret == BT_ERROR_NONE)
        ret = register_characteristic_callbacks(s_ble.status, true, false, true);
    if (ret == BT_ERROR_NONE)
        ret = register_characteristic_callbacks(s_ble.command, false, true, false);
    if (ret != BT_ERROR_NONE)
        goto fail;

    ret = bt_gatt_server_register_service(s_ble.server, s_ble.bridge_service);
    if (ret == BT_ERROR_NONE) {
        s_ble.bridge_registered = true;
        ret = bt_gatt_server_register_service(s_ble.server, s_ble.hr_service);
        if (ret == BT_ERROR_NONE)
            s_ble.hr_registered = true;
    }
    if (ret != BT_ERROR_NONE)
        goto fail;

    ret = bt_gatt_server_start();
    if (ret != BT_ERROR_NONE)
        goto fail;
    s_ble.server_started = true;

    ret = bt_adapter_le_create_advertiser(&s_ble.advertiser);
    if (ret != BT_ERROR_NONE)
        goto fail;
    ret = bt_adapter_le_set_advertising_connectable(
        s_ble.advertiser, true);
    if (ret == BT_ERROR_NONE)
        ret = bt_adapter_le_set_advertising_mode(
            s_ble.advertiser, BT_ADAPTER_LE_ADVERTISING_MODE_BALANCED);
    if (ret == BT_ERROR_NONE)
        ret = bt_adapter_le_add_advertising_service_uuid(
            s_ble.advertiser, BT_ADAPTER_LE_PACKET_ADVERTISING,
            UUID_BRIDGE_SERVICE);
    if (ret != BT_ERROR_NONE)
        goto fail;
    ret = bt_adapter_set_state_changed_cb(adapter_state_changed, NULL);
    if (ret != BT_ERROR_NONE)
        goto fail;
    s_ble.adapter_callback_registered = true;
    ret = bt_gatt_set_connection_state_changed_cb(connection_changed, NULL);
    if (ret != BT_ERROR_NONE)
        goto fail;
    s_ble.connection_callback_registered = true;
    ret = start_advertising();
    if (ret != BT_ERROR_NONE)
        goto fail;

    return BT_ERROR_NONE;

fail:
    ble_server_stop();
    return ret;
}

int ble_server_retry_advertising(void)
{
    if (!s_ble.server_started || !s_ble.advertiser)
        return BT_ERROR_NOT_ENABLED;

    bt_adapter_state_e state;
    int ret = bt_adapter_get_state(&state);
    if (ret != BT_ERROR_NONE)
        return ret;
    if (state != BT_ADAPTER_ENABLED)
        return BT_ERROR_NOT_ENABLED;
    return start_advertising();
}

void ble_server_stop(void)
{
    if (s_ble.connection_callback_registered)
        log_cleanup_failure("bt_gatt_unset_connection_state_changed_cb",
                            bt_gatt_unset_connection_state_changed_cb());
    if (s_ble.adapter_callback_registered)
        log_cleanup_failure("bt_adapter_unset_state_changed_cb",
                            bt_adapter_unset_state_changed_cb());
    if (s_ble.advertiser) {
        if (s_ble.advertising || s_ble.advertising_pending)
            log_cleanup_failure("bt_adapter_le_stop_advertising",
                                bt_adapter_le_stop_advertising(s_ble.advertiser));
        log_cleanup_failure("bt_adapter_le_destroy_advertiser",
                            bt_adapter_le_destroy_advertiser(s_ble.advertiser));
        s_ble.advertiser = NULL;
    }
    if (s_ble.bridge_service && !s_ble.bridge_registered)
        log_cleanup_failure("bt_gatt_service_destroy(bridge)",
                            bt_gatt_service_destroy(s_ble.bridge_service));
    if (s_ble.hr_service && !s_ble.hr_registered)
        log_cleanup_failure("bt_gatt_service_destroy(hr)",
                            bt_gatt_service_destroy(s_ble.hr_service));
    if (s_ble.server) {
        log_cleanup_failure("bt_gatt_server_destroy",
                            bt_gatt_server_destroy(s_ble.server));
    }
    s_ble.server = NULL;
    s_ble.bridge_service = NULL;
    s_ble.hr_service = NULL;
    if (s_ble.gatt_initialized) {
        log_cleanup_failure("bt_gatt_server_deinitialize",
                            bt_gatt_server_deinitialize());
        s_ble.gatt_initialized = false;
    }
    if (s_ble.bluetooth_initialized) {
        log_cleanup_failure("bt_deinitialize", bt_deinitialize());
        s_ble.bluetooth_initialized = false;
    }
    s_ble.connected = false;
    s_ble.server_started = false;
    s_ble.bridge_registered = false;
    s_ble.hr_registered = false;
    s_ble.motion = s_ble.hr_meta = s_ble.command = NULL;
    s_ble.status = s_ble.hr_measurement = NULL;
    s_ble.advertising_pending = false;
    s_ble.motion_pending = s_ble.hr_pending = false;
    s_ble.hr_meta_pending = s_ble.status_pending = false;
    s_ble.sample_sent = false;
    s_ble.advertising = false;
    s_ble.adapter_callback_registered = false;
    s_ble.connection_callback_registered = false;
    s_ble.error = BLE_BRIDGE_OK;
    s_ble.motion_notify = false;
    s_ble.hr_notify = false;
    s_ble.hr_meta_notify = false;
    s_ble.status_notify = false;
}

bool ble_server_is_connected(void)
{
    return s_ble.connected;
}

bool ble_server_is_advertising(void)
{
    return s_ble.advertising;
}

bool ble_server_is_initialized(void)
{
    return s_ble.server_started;
}

int ble_server_last_error(void)
{
    return s_ble.error;
}

bool ble_server_is_transmitting(void)
{
    return s_ble.connected && s_ble.sample_sent &&
        (s_ble.motion_notify || s_ble.hr_notify || s_ble.hr_meta_notify) &&
        (uint32_t)(monotonic_ms() - s_ble.last_sample_sent_ms) <= 2000u;
}

static bool *pending_for(bt_gatt_h characteristic)
{
    if (characteristic == s_ble.motion) return &s_ble.motion_pending;
    if (characteristic == s_ble.hr_measurement) return &s_ble.hr_pending;
    if (characteristic == s_ble.hr_meta) return &s_ble.hr_meta_pending;
    return &s_ble.status_pending;
}

static void notification_sent(int result, const char *address,
                              bt_gatt_server_h server, bt_gatt_h characteristic,
                              bool completed, void *user_data)
{
    (void)address;
    (void)user_data;
    if (server != s_ble.server || !s_ble.server_started)
        return;
    if (completed)
        *pending_for(characteristic) = false;
    if (result != BT_ERROR_NONE) {
        s_ble.error = BLE_BRIDGE_ERR_NOTIFY;
        s_ble.sample_sent = false;
    } else if (characteristic != s_ble.status) {
        s_ble.sample_sent = true;
        s_ble.last_sample_sent_ms = monotonic_ms();
        if (s_ble.error == BLE_BRIDGE_ERR_NOTIFY)
            s_ble.error = BLE_BRIDGE_OK;
    }
    /* main.c polls status; avoid recursive status notifications here. */
}

static int notify_value(bt_gatt_h characteristic, bool subscribed,
                        const uint8_t *value, int length)
{
    if (!s_ble.bluetooth_initialized || !s_ble.server_started)
        return BT_ERROR_NOT_ENABLED;
    if (!characteristic || !value || length <= 0 || length > 20)
        return BT_ERROR_INVALID_PARAMETER;
    if (!s_ble.connected || !subscribed)
        return BT_ERROR_NOT_ENABLED;
    bool *pending = pending_for(characteristic);
    if (*pending)
        return BT_ERROR_NOW_IN_PROGRESS;
    int ret = bt_gatt_set_value(characteristic, (const char *)value, length);
    if (ret != BT_ERROR_NONE)
        return ret;
    *pending = true;
    ret = bt_gatt_server_notify_characteristic_changed_value(
        characteristic, notification_sent, NULL, NULL);
    if (ret != BT_ERROR_NONE)
        *pending = false;
    return ret;
}

int ble_server_notify_motion(const uint8_t *value, int length)
{
    return notify_value(s_ble.motion, s_ble.motion_notify, value, length);
}

int ble_server_notify_heart_rate(const uint8_t *measurement,
                                 int measurement_length,
                                 const uint8_t *metadata,
                                 int metadata_length,
                                 bool *metadata_notified,
                                 bool *any_notified)
{
    if (metadata_notified)
        *metadata_notified = false;
    if (any_notified)
        *any_notified = false;

    int standard_ret = BT_ERROR_NOT_ENABLED;
    if (measurement && measurement_length > 0) {
        standard_ret = notify_value(s_ble.hr_measurement, s_ble.hr_notify,
                                    measurement, measurement_length);
    }
    int metadata_ret = notify_value(s_ble.hr_meta, s_ble.hr_meta_notify,
                                    metadata, metadata_length);
    if (metadata_notified)
        *metadata_notified = metadata_ret == BT_ERROR_NONE;
    if (any_notified)
        *any_notified = standard_ret == BT_ERROR_NONE ||
                        metadata_ret == BT_ERROR_NONE;

    if (standard_ret != BT_ERROR_NONE &&
        standard_ret != BT_ERROR_NOT_ENABLED)
        return standard_ret;
    if (metadata_ret != BT_ERROR_NONE &&
        metadata_ret != BT_ERROR_NOT_ENABLED)
        return metadata_ret;
    return standard_ret == BT_ERROR_NONE || metadata_ret == BT_ERROR_NONE
        ? BT_ERROR_NONE : BT_ERROR_NOT_ENABLED;
}

int ble_server_store_status(const uint8_t *value, int length)
{
    if (!value || length != SB_STATUS_SIZE)
        return BT_ERROR_INVALID_PARAMETER;
    memcpy(s_ble.status_value, value, SB_STATUS_SIZE);
    return BT_ERROR_NONE;
}

int ble_server_notify_status(const uint8_t *value, int length)
{
    int ret = ble_server_store_status(value, length);
    if (ret != BT_ERROR_NONE)
        return ret;
    return notify_value(s_ble.status, s_ble.status_notify, value, length);
}

const char *ble_server_error_string(int error)
{
    switch (error) {
    case BLE_BRIDGE_OK: return "Bereit";
    case BLE_BRIDGE_ERR_BLUETOOTH: return "Bluetooth ist ausgeschaltet";
    case BLE_BRIDGE_ERR_PERMISSION: return "Berechtigung verweigert";
    case BLE_BRIDGE_ERR_SENSOR: return "Sensor nicht verfuegbar";
    case BLE_BRIDGE_ERR_COMMAND: return "Ungueltiger Steuerbefehl";
    case BLE_BRIDGE_ERR_NOTIFY: return "BLE-Benachrichtigung fehlgeschlagen";
    case BLE_BRIDGE_ERR_ADVERTISING: return "BLE-Advertising fehlgeschlagen";
    case BLE_BRIDGE_ERR_RUNTIME: return "App-Ressource nicht verfuegbar";
    case BLE_BRIDGE_ERR_UNSUPPORTED: return "GATT-Server von Firmware nicht unterstuetzt";
    default: return "Bluetooth-/GATT-Fehler";
    }
}
