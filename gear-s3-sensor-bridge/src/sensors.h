#ifndef SENSOR_BRIDGE_SENSORS_H
#define SENSOR_BRIDGE_SENSORS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool accel_available;
    bool gyro_available;
    bool hr_available;
    bool accel_valid;
    bool gyro_valid;
    bool hr_valid;
    uint32_t accel_timestamp_ms;
    uint32_t gyro_timestamp_ms;
    uint32_t hr_timestamp_ms;
    uint32_t hr_sample_count;
    uint16_t bpm;
    float accel_mps2[3];
    float gyro_rps[3];
} sensor_snapshot_t;

typedef void (*sensor_status_cb)(const char *message, void *user_data);

int sensors_start(sensor_status_cb status_cb, void *user_data);
void sensors_stop(void);
void sensors_get_snapshot(sensor_snapshot_t *snapshot);

#endif
