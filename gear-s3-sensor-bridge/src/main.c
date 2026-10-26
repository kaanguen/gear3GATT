#include "ble_server.h"
#include "protocol.h"
#include "sensors.h"
#include "ui.h"

#include <app.h>
#include <bluetooth.h>
#include <dlog.h>
#include <Ecore.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <sensor.h>
#include <privacy_privilege_manager.h>
#include <string.h>
#include <time.h>

#define LOG_TAG "GearS3SensorBridge"
#define DEFAULT_RATE_HZ 20
#define MAX_SAMPLE_AGE_MS 250

static Ecore_Timer *s_timer;
static bool s_measuring;
static bool s_foreground;
static bool s_permission_pending;
static uint16_t s_rate_hz = DEFAULT_RATE_HZ;
static uint16_t s_motion_sequence;
static uint16_t s_last_motion_sequence;
static uint16_t s_hr_sequence;
static uint32_t s_last_hr_sample_count;
static uint32_t s_next_send_ms;
static uint32_t s_next_bt_retry_ms;
static int s_ble_error;
static sensor_snapshot_t s_snapshot;

static uint32_t now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint32_t)((uint64_t)now.tv_sec * 1000u +
                      (uint64_t)now.tv_nsec / 1000000u);
}

static int16_t saturate_i16(double value)
{
    if (value > INT16_MAX)
        return INT16_MAX;
    if (value < INT16_MIN)
        return INT16_MIN;
    return (int16_t)lrint(value);
}

static void update_status(void)
{
    int transport_error = ble_server_last_error();
    if (transport_error != BLE_BRIDGE_OK)
        s_ble_error = transport_error;
    sb_status_t status = {
        .measuring = s_measuring,
        .connected = ble_server_is_connected(),
        .transmitting = s_measuring && ble_server_is_transmitting(),
        .accel_available = s_snapshot.accel_available,
        .gyro_available = s_snapshot.gyro_available,
        .hr_available = s_snapshot.hr_available,
        .error = (uint8_t)s_ble_error,
        .rate_hz = s_rate_hz,
        .motion_sequence = s_last_motion_sequence
    };
    uint8_t bytes[SB_STATUS_SIZE];
    sb_encode_status(&status, bytes);
    ble_server_notify_status(bytes, sizeof(bytes));

    const char *bluetooth_status =
        s_ble_error == BLE_BRIDGE_ERR_BLUETOOTH
            ? "Bluetooth ausgeschaltet"
            : (s_ble_error == BLE_BRIDGE_ERR_ADVERTISING
                ? "Advertising-Fehler"
                : (ble_server_is_advertising()
                    ? "Advertising aktiv"
                    : (ble_server_is_initialized()
                        ? "Bluetooth aktiv" : "BLE nicht bereit")));
    ui_set_status(bluetooth_status,
                  ble_server_is_connected() ? "verbunden" : "getrennt",
                  s_ble_error ? ble_server_error_string(s_ble_error) : "",
                  s_measuring,
                  s_measuring && ble_server_is_transmitting());
}

static int map_ble_start_error(int error)
{
    if (error == BT_ERROR_NONE)
        return BLE_BRIDGE_OK;
    if (error == BT_ERROR_NOT_ENABLED)
        return BLE_BRIDGE_ERR_BLUETOOTH;
    if (error == BT_ERROR_PERMISSION_DENIED)
        return BLE_BRIDGE_ERR_PERMISSION;
    if (error == BT_ERROR_NOT_SUPPORTED)
        return BLE_BRIDGE_ERR_UNSUPPORTED;
    return BLE_BRIDGE_ERR_ADVERTISING;
}

static void store_status(void)
{
    sb_status_t status = {
        .measuring = s_measuring,
        .connected = ble_server_is_connected(),
        .transmitting = s_measuring && ble_server_is_transmitting(),
        .accel_available = s_snapshot.accel_available,
        .gyro_available = s_snapshot.gyro_available,
        .hr_available = s_snapshot.hr_available,
        .error = (uint8_t)s_ble_error,
        .rate_hz = s_rate_hz,
        .motion_sequence = s_last_motion_sequence
    };
    uint8_t bytes[SB_STATUS_SIZE];
    sb_encode_status(&status, bytes);
    ble_server_store_status(bytes, sizeof(bytes));
}

static void on_sensor_error(const char *message, void *user_data)
{
    (void)user_data;
    dlog_print(DLOG_WARN, LOG_TAG, "%s", message ? message : "Sensor error");
    if (message && strstr(message, "Berechtigung"))
        s_ble_error = BLE_BRIDGE_ERR_PERMISSION;
    else
        s_ble_error = BLE_BRIDGE_ERR_SENSOR;
    update_status();
}

static bool start_measurement(void)
{
    if (s_ble_error == BLE_BRIDGE_ERR_SENSOR ||
        s_ble_error == BLE_BRIDGE_ERR_PERMISSION)
        s_ble_error = BLE_BRIDGE_OK;

    s_last_hr_sample_count = 0;
    if (sensors_start(on_sensor_error, NULL) != SENSOR_ERROR_NONE) {
        s_measuring = false;
        if (s_ble_error != BLE_BRIDGE_ERR_PERMISSION)
            s_ble_error = BLE_BRIDGE_ERR_SENSOR;
        return false;
    }
    s_measuring = true;
    sensors_get_snapshot(&s_snapshot);
    return true;
}

static void on_ble_command(uint8_t opcode, uint16_t rate_hz, void *user_data)
{
    (void)user_data;
    if (s_ble_error == BLE_BRIDGE_ERR_COMMAND ||
        s_ble_error == BLE_BRIDGE_ERR_NOTIFY ||
        s_ble_error == BLE_BRIDGE_ERR_BLUETOOTH ||
        s_ble_error == BLE_BRIDGE_ERR_ADVERTISING)
        s_ble_error = BLE_BRIDGE_OK;

    if (opcode == SB_COMMAND_START) {
        if (!s_measuring)
            start_measurement();
    } else if (opcode == SB_COMMAND_STOP) {
        s_measuring = false;
        sensors_stop();
        sensors_get_snapshot(&s_snapshot);
        ui_update_sensors(&s_snapshot);
    } else if (opcode == SB_COMMAND_SET_RATE && rate_hz >= 1 && rate_hz <= 20) {
        s_rate_hz = rate_hz;
    } else {
        s_ble_error = BLE_BRIDGE_ERR_COMMAND;
    }
    update_status();
}

static void on_ble_state(bool connected, int error, void *user_data)
{
    (void)connected;
    (void)user_data;
    if (error != BLE_BRIDGE_OK)
        s_ble_error = error;
    else if (s_ble_error == BLE_BRIDGE_ERR_BLUETOOTH ||
             s_ble_error == BLE_BRIDGE_ERR_ADVERTISING)
        s_ble_error = BLE_BRIDGE_OK;
    update_status();
}

static void permission_response(ppm_call_cause_e cause, ppm_request_result_e result,
                                const char *privilege, void *user_data)
{
    (void)privilege;
    (void)user_data;
    s_permission_pending = false;
    if (cause != PRIVACY_PRIVILEGE_MANAGER_CALL_CAUSE_ANSWER ||
        result != PRIVACY_PRIVILEGE_MANAGER_REQUEST_RESULT_ALLOW_FOREVER)
        s_ble_error = BLE_BRIDGE_ERR_PERMISSION;
    /* Start on the next explicit button press. A privacy dialog can pause
     * this UI app; never restart measurement from a late permission callback. */
    if (s_foreground)
        update_status();
}

static void toggle_measurement(void *user_data)
{
    (void)user_data;
    if (s_permission_pending)
        return;
    if (s_measuring) {
        s_measuring = false;
        sensors_stop();
    } else {
        ppm_check_result_e permission;
        int ret = ppm_check_permission("http://tizen.org/privilege/healthinfo", &permission);
        if (ret == 0 && permission == PRIVACY_PRIVILEGE_MANAGER_CHECK_RESULT_ASK) {
            s_permission_pending = true;
            ret = ppm_request_permission("http://tizen.org/privilege/healthinfo",
                                         permission_response, NULL);
            if (ret != 0) {
                s_permission_pending = false;
                s_ble_error = BLE_BRIDGE_ERR_PERMISSION;
            }
            return;
        }
        start_measurement();
    }
    sensors_get_snapshot(&s_snapshot);
    ui_update_sensors(&s_snapshot);
    update_status();
}

static uint32_t elapsed_u32(uint32_t now, uint32_t then)
{
    return now - then;
}

static Eina_Bool timer_tick(void *data)
{
    (void)data;
    sensors_get_snapshot(&s_snapshot);
    ui_update_sensors(&s_snapshot);

    const uint32_t now = now_ms();
    const bool advertising_needed = !ble_server_is_connected() &&
        (!ble_server_is_initialized() || !ble_server_is_advertising());
    if (advertising_needed &&
        elapsed_u32(now, s_next_bt_retry_ms) >= 2000) {
        s_next_bt_retry_ms = now;
        ble_server_callbacks_t callbacks = {
            .command = on_ble_command,
            .state_changed = on_ble_state,
            .user_data = NULL
        };
        int ret = ble_server_is_initialized()
            ? ble_server_retry_advertising() : ble_server_start(&callbacks);
        if (ret != BT_ERROR_NONE)
            s_ble_error = map_ble_start_error(ret);
        else if (s_ble_error == BLE_BRIDGE_ERR_BLUETOOTH ||
                 s_ble_error == BLE_BRIDGE_ERR_ADVERTISING)
            s_ble_error = BLE_BRIDGE_OK;
        update_status();
    }

    if (!s_measuring) {
        update_status();
        return ECORE_CALLBACK_RENEW;
    }

    const uint32_t period = 1000u / s_rate_hz;
    if (elapsed_u32(now, s_next_send_ms) < period)
        goto heart_rate;
    s_next_send_ms = now;

    sb_motion_t motion = {
        .accel_valid = s_snapshot.accel_valid &&
                       elapsed_u32(now, s_snapshot.accel_timestamp_ms) <= MAX_SAMPLE_AGE_MS,
        .gyro_valid = s_snapshot.gyro_valid &&
                      elapsed_u32(now, s_snapshot.gyro_timestamp_ms) <= MAX_SAMPLE_AGE_MS,
        .sequence = s_motion_sequence
    };
    if (motion.accel_valid && motion.gyro_valid) {
        motion.timestamp_ms =
            elapsed_u32(now, s_snapshot.accel_timestamp_ms) >=
            elapsed_u32(now, s_snapshot.gyro_timestamp_ms)
            ? s_snapshot.accel_timestamp_ms : s_snapshot.gyro_timestamp_ms;
    } else if (motion.accel_valid) {
        motion.timestamp_ms = s_snapshot.accel_timestamp_ms;
    } else if (motion.gyro_valid) {
        motion.timestamp_ms = s_snapshot.gyro_timestamp_ms;
    } else {
        motion.timestamp_ms = now;
    }

    for (int i = 0; i < 3; ++i) {
        double accel = (double)s_snapshot.accel_mps2[i] * 1000.0 / 9.80665;
        double gyro = (double)s_snapshot.gyro_rps[i] * 180000.0 / 3.14159265358979323846;
        /* A clipped value is not the measured value. Reject the whole
         * vector rather than label a saturated integer as valid. */
        if (!isfinite(accel) || accel < INT16_MIN || accel > INT16_MAX)
            motion.accel_valid = false;
        if (!isfinite(gyro) || gyro < INT16_MIN || gyro > INT16_MAX)
            motion.gyro_valid = false;
        motion.accel_mg[i] = isfinite(accel) ? saturate_i16(accel) : 0;
        motion.gyro_mdps[i] = isfinite(gyro) ? saturate_i16(gyro) : 0;
    }

    uint8_t frame[SB_MOTION_FRAME_SIZE];
    if (sb_encode_motion(&motion, frame)) {
        int ret = ble_server_notify_motion(frame, sizeof(frame));
        if (ret == BT_ERROR_NONE) {
            s_last_motion_sequence = s_motion_sequence;
            ++s_motion_sequence;
            if (s_ble_error == BLE_BRIDGE_ERR_NOTIFY)
                s_ble_error = BLE_BRIDGE_OK;
        } else if (ret != BT_ERROR_NOT_ENABLED && ret != BT_ERROR_NOW_IN_PROGRESS) {
            s_ble_error = BLE_BRIDGE_ERR_NOTIFY;
        }
    }

heart_rate:
    if (s_snapshot.hr_sample_count != s_last_hr_sample_count &&
        elapsed_u32(now, s_snapshot.hr_timestamp_ms) <= 2000) {
        uint8_t measurement[2];
        uint8_t metadata[SB_HR_METADATA_SIZE];
        bool standard_valid = s_snapshot.hr_valid &&
            sb_encode_hr_measurement(s_snapshot.bpm, measurement);
        if (sb_encode_hr_metadata(s_snapshot.hr_valid, s_hr_sequence, s_snapshot.hr_timestamp_ms,
                                  s_snapshot.bpm, metadata)) {
            bool metadata_notified = false;
            bool any_notified = false;
            int ret = ble_server_notify_heart_rate(
                standard_valid ? measurement : NULL,
                standard_valid ? sizeof(measurement) : 0,
                metadata, sizeof(metadata), &metadata_notified, &any_notified);
            if (ret != BT_ERROR_NONE && ret != BT_ERROR_NOT_ENABLED &&
                ret != BT_ERROR_NOW_IN_PROGRESS)
                s_ble_error = BLE_BRIDGE_ERR_NOTIFY;
            if (metadata_notified)
                ++s_hr_sequence;
            if (any_notified || ret == BT_ERROR_NOT_ENABLED)
                s_last_hr_sample_count = s_snapshot.hr_sample_count;
        }
    }
    update_status();
    store_status();
    return ECORE_CALLBACK_RENEW;
}

static void pause_app(void *data)
{
    (void)data;
    s_foreground = false;
    s_measuring = false;
    sensors_stop();
    sensors_get_snapshot(&s_snapshot);
    ui_update_sensors(&s_snapshot);
    if (s_timer) {
        ecore_timer_del(s_timer);
        s_timer = NULL;
    }
    ble_server_stop();
}

static void resume_app(void *data)
{
    (void)data;
    s_foreground = true;
    if (s_timer)
        return;
    ble_server_callbacks_t callbacks = {
        .command = on_ble_command,
        .state_changed = on_ble_state,
        .user_data = NULL
    };
    int ret = ble_server_is_initialized()
        ? BT_ERROR_NONE : ble_server_start(&callbacks);
    s_ble_error = map_ble_start_error(ret);
    s_timer = ecore_timer_add(0.05, timer_tick, NULL);
    if (!s_timer) {
        dlog_print(DLOG_ERROR, LOG_TAG, "Could not create sensor timer");
        s_measuring = false;
        sensors_stop();
        ble_server_stop();
        s_ble_error = BLE_BRIDGE_ERR_RUNTIME;
    }
    update_status();
}

static bool create_app(void *data)
{
    (void)data;
    ui_callbacks_t ui_callbacks = {
        .toggle_measurement = toggle_measurement,
        .user_data = NULL
    };
    if (ui_create(&ui_callbacks) != 0)
        return false;
    resume_app(NULL);
    return true;
}

static void terminate_app(void *data)
{
    (void)data;
    pause_app(NULL);
    ui_destroy();
}

static void app_control(app_control_h app_control, void *data)
{
    (void)app_control;
    (void)data;
}

int main(int argc, char *argv[])
{
    ui_app_lifecycle_callback_s callbacks = {
        .create = create_app,
        .terminate = terminate_app,
        .pause = pause_app,
        .resume = resume_app,
        .app_control = app_control
    };
    return ui_app_main(argc, argv, &callbacks, NULL);
}
