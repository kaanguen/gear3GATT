#include "sensors.h"

#include <sensor.h>
#include <dlog.h>
#include <privacy_privilege_manager.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <string.h>

typedef struct {
    sensor_h sensor;
    sensor_listener_h listener;
    bool available;
} sensor_slot_t;

static sensor_slot_t s_accel;
static sensor_slot_t s_gyro;
static sensor_slot_t s_hr;
static sensor_snapshot_t s_latest;
static sensor_status_cb s_status_cb;
static void *s_status_data;
static pthread_mutex_t s_snapshot_lock = PTHREAD_MUTEX_INITIALIZER;

#define SENSOR_LOG_TAG "GearS3SensorBridge"

static uint32_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint32_t)((uint64_t)now.tv_sec * 1000u +
                      (uint64_t)now.tv_nsec / 1000000u);
}

static void report_sensor_error(int error)
{
    if (error == SENSOR_ERROR_PERMISSION_DENIED && s_status_cb)
        s_status_cb("Sensorberechtigung verweigert", s_status_data);
    else if (s_status_cb)
        s_status_cb("Sensor nicht verfuegbar", s_status_data);
}

static void accel_updated(sensor_h sensor, sensor_event_s *event, void *user_data)
{
    (void)sensor;
    (void)user_data;
    if (!event || event->value_count < 3 ||
        event->accuracy == SENSOR_DATA_ACCURACY_BAD ||
        !isfinite(event->values[0]) || !isfinite(event->values[1]) ||
        !isfinite(event->values[2])) {
        pthread_mutex_lock(&s_snapshot_lock);
        s_latest.accel_valid = false;
        pthread_mutex_unlock(&s_snapshot_lock);
        return;
    }
    pthread_mutex_lock(&s_snapshot_lock);
    memcpy(s_latest.accel_mps2, event->values, sizeof(s_latest.accel_mps2));
    s_latest.accel_valid = true;
    s_latest.accel_timestamp_ms = monotonic_ms();
    pthread_mutex_unlock(&s_snapshot_lock);
}

static void gyro_updated(sensor_h sensor, sensor_event_s *event, void *user_data)
{
    (void)sensor;
    (void)user_data;
    if (!event || event->value_count < 3 ||
        event->accuracy == SENSOR_DATA_ACCURACY_BAD ||
        !isfinite(event->values[0]) || !isfinite(event->values[1]) ||
        !isfinite(event->values[2])) {
        pthread_mutex_lock(&s_snapshot_lock);
        s_latest.gyro_valid = false;
        pthread_mutex_unlock(&s_snapshot_lock);
        return;
    }
    pthread_mutex_lock(&s_snapshot_lock);
    memcpy(s_latest.gyro_rps, event->values, sizeof(s_latest.gyro_rps));
    s_latest.gyro_valid = true;
    s_latest.gyro_timestamp_ms = monotonic_ms();
    pthread_mutex_unlock(&s_snapshot_lock);
}

static void hr_updated(sensor_h sensor, sensor_event_s *event, void *user_data)
{
    (void)sensor;
    (void)user_data;
    const bool valid = event && event->value_count >= 1 &&
        event->accuracy != SENSOR_DATA_ACCURACY_BAD &&
        isfinite(event->values[0]) && event->values[0] >= 1 &&
        event->values[0] <= UINT8_MAX;
    pthread_mutex_lock(&s_snapshot_lock);
    s_latest.bpm = valid ? (uint16_t)lrintf(event->values[0]) : 0;
    s_latest.hr_valid = valid;
    s_latest.hr_timestamp_ms = monotonic_ms();
    ++s_latest.hr_sample_count;
    pthread_mutex_unlock(&s_snapshot_lock);
}

static int start_slot(sensor_slot_t *slot, sensor_type_e type,
                      sensor_event_cb callback, unsigned int interval_ms)
{
    bool supported = false;
    int ret = sensor_is_supported(type, &supported);
    if (ret != SENSOR_ERROR_NONE || !supported) {
        report_sensor_error(ret != SENSOR_ERROR_NONE ? ret : SENSOR_ERROR_NOT_SUPPORTED);
        return ret != SENSOR_ERROR_NONE ? ret : SENSOR_ERROR_NOT_SUPPORTED;
    }
    ret = sensor_get_default_sensor(type, &slot->sensor);
    if (ret != SENSOR_ERROR_NONE) {
        report_sensor_error(ret);
        return ret;
    }

    int minimum_interval_ms = 0;
    ret = sensor_get_min_interval(slot->sensor, &minimum_interval_ms);
    if (ret == SENSOR_ERROR_NONE && interval_ms != 0 && minimum_interval_ms > (int)interval_ms)
        interval_ms = (unsigned int)minimum_interval_ms;
    else if (ret != SENSOR_ERROR_NONE) {
        dlog_print(DLOG_WARN, SENSOR_LOG_TAG,
                   "sensor_get_min_interval failed for type %d: %d; "
                   "using requested interval %u ms",
                   type, ret, interval_ms);
    }

    ret = sensor_create_listener(slot->sensor, &slot->listener);
    if (ret != SENSOR_ERROR_NONE) {
        report_sensor_error(ret);
        return ret;
    }
    if (type != SENSOR_HRM) {
        ret = sensor_listener_set_attribute_int(slot->listener,
            SENSOR_ATTRIBUTE_AXIS_ORIENTATION, SENSOR_AXIS_DEVICE_ORIENTED);
        if (ret != SENSOR_ERROR_NONE) {
            report_sensor_error(ret);
            sensor_destroy_listener(slot->listener);
            slot->listener = NULL;
            return ret;
        }
    }
    ret = sensor_listener_set_event_cb(slot->listener, interval_ms,
                                       callback, NULL);
    if (ret != SENSOR_ERROR_NONE) {
        report_sensor_error(ret);
        sensor_destroy_listener(slot->listener);
        slot->listener = NULL;
        return ret;
    }
    ret = sensor_listener_start(slot->listener);
    if (ret != SENSOR_ERROR_NONE) {
        report_sensor_error(ret);
        sensor_listener_unset_event_cb(slot->listener);
        sensor_destroy_listener(slot->listener);
        slot->listener = NULL;
        return ret;
    }
    slot->available = true;
    return SENSOR_ERROR_NONE;
}

int sensors_start(sensor_status_cb status_cb, void *user_data)
{
    sensors_stop();
    pthread_mutex_lock(&s_snapshot_lock);
    memset(&s_accel, 0, sizeof(s_accel));
    memset(&s_gyro, 0, sizeof(s_gyro));
    memset(&s_hr, 0, sizeof(s_hr));
    memset(&s_latest, 0, sizeof(s_latest));
    pthread_mutex_unlock(&s_snapshot_lock);
    s_status_cb = status_cb;
    s_status_data = user_data;

    start_slot(&s_accel, SENSOR_ACCELEROMETER, accel_updated, 50);
    start_slot(&s_gyro, SENSOR_GYROSCOPE, gyro_updated, 50);
    ppm_check_result_e permission;
    if (ppm_check_permission("http://tizen.org/privilege/healthinfo", &permission) == 0 &&
        permission == PRIVACY_PRIVILEGE_MANAGER_CHECK_RESULT_ALLOW)
        start_slot(&s_hr, SENSOR_HRM, hr_updated, 0); /* Sensor default cadence. */
    else
        report_sensor_error(SENSOR_ERROR_PERMISSION_DENIED);

    pthread_mutex_lock(&s_snapshot_lock);
    s_latest.accel_available = s_accel.available;
    s_latest.gyro_available = s_gyro.available;
    s_latest.hr_available = s_hr.available;
    pthread_mutex_unlock(&s_snapshot_lock);
    return (s_accel.available || s_gyro.available || s_hr.available)
        ? SENSOR_ERROR_NONE : SENSOR_ERROR_NOT_SUPPORTED;
}

static void stop_slot(sensor_slot_t *slot)
{
    if (slot->listener) {
        int ret = sensor_listener_unset_event_cb(slot->listener);
        if (ret != SENSOR_ERROR_NONE)
            dlog_print(DLOG_WARN, SENSOR_LOG_TAG,
                       "sensor_listener_unset_event_cb failed: %d", ret);
        ret = sensor_listener_stop(slot->listener);
        if (ret != SENSOR_ERROR_NONE)
            dlog_print(DLOG_WARN, SENSOR_LOG_TAG,
                       "sensor_listener_stop failed: %d", ret);
        ret = sensor_destroy_listener(slot->listener);
        if (ret != SENSOR_ERROR_NONE)
            dlog_print(DLOG_WARN, SENSOR_LOG_TAG,
                       "sensor_destroy_listener failed: %d", ret);
        slot->listener = NULL;
    }
    slot->available = false;
}

void sensors_stop(void)
{
    stop_slot(&s_accel);
    stop_slot(&s_gyro);
    stop_slot(&s_hr);
    pthread_mutex_lock(&s_snapshot_lock);
    s_latest.accel_valid = false;
    s_latest.gyro_valid = false;
    s_latest.hr_valid = false;
    pthread_mutex_unlock(&s_snapshot_lock);
}

void sensors_get_snapshot(sensor_snapshot_t *snapshot)
{
    if (!snapshot)
        return;
    pthread_mutex_lock(&s_snapshot_lock);
    *snapshot = s_latest;
    pthread_mutex_unlock(&s_snapshot_lock);
    const uint32_t now = monotonic_ms();
    snapshot->accel_valid = snapshot->accel_valid &&
        (uint32_t)(now - snapshot->accel_timestamp_ms) <= 250u;
    snapshot->gyro_valid = snapshot->gyro_valid &&
        (uint32_t)(now - snapshot->gyro_timestamp_ms) <= 250u;
    snapshot->hr_valid = snapshot->hr_valid &&
        (uint32_t)(now - snapshot->hr_timestamp_ms) <= 2000u;
}
