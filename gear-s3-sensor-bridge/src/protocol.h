#ifndef SENSOR_BRIDGE_PROTOCOL_H
#define SENSOR_BRIDGE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SB_PROTOCOL_VERSION 1
#define SB_MOTION_FRAME_SIZE 20
#define SB_HR_METADATA_SIZE 10
#define SB_COMMAND_SIZE 4
#define SB_STATUS_SIZE 8

typedef enum {
    SB_COMMAND_START = 1,
    SB_COMMAND_STOP = 2,
    SB_COMMAND_SET_RATE = 3
} sb_command_type_t;

typedef struct {
    sb_command_type_t type;
    uint16_t rate_hz;
} sb_command_t;

typedef struct {
    bool accel_valid;
    bool gyro_valid;
    uint16_t sequence;
    uint32_t timestamp_ms;
    int16_t accel_mg[3];
    int16_t gyro_mdps[3];
} sb_motion_t;

typedef struct {
    bool measuring;
    bool connected;
    bool transmitting;
    bool accel_available;
    bool gyro_available;
    bool hr_available;
    uint8_t error;
    uint16_t rate_hz;
    uint16_t motion_sequence;
} sb_status_t;

bool sb_encode_motion(const sb_motion_t *motion, uint8_t out[SB_MOTION_FRAME_SIZE]);
bool sb_encode_hr_metadata(bool valid, uint16_t sequence, uint32_t timestamp_ms,
                           uint16_t bpm, uint8_t out[SB_HR_METADATA_SIZE]);
bool sb_encode_hr_measurement(uint16_t bpm, uint8_t out[2]);
bool sb_decode_command(const uint8_t *bytes, size_t length, sb_command_t *out);
void sb_encode_status(const sb_status_t *status, uint8_t out[SB_STATUS_SIZE]);

#endif
