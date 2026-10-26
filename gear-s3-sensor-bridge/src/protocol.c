#include "protocol.h"

static void put_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xff);
    out[1] = (uint8_t)(value >> 8);
}

static void put_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xff);
    out[1] = (uint8_t)((value >> 8) & 0xff);
    out[2] = (uint8_t)((value >> 16) & 0xff);
    out[3] = (uint8_t)(value >> 24);
}

bool sb_encode_motion(const sb_motion_t *motion, uint8_t out[SB_MOTION_FRAME_SIZE])
{
    if (!motion || !out)
        return false;

    out[0] = SB_PROTOCOL_VERSION;
    out[1] = (uint8_t)((motion->accel_valid ? 0x01 : 0) |
                       (motion->gyro_valid ? 0x02 : 0));
    put_u16_le(out + 2, motion->sequence);
    put_u32_le(out + 4, motion->timestamp_ms);
    for (size_t i = 0; i < 3; ++i) {
        put_u16_le(out + 8 + i * 2, motion->accel_valid
                   ? (uint16_t)motion->accel_mg[i] : 0);
        put_u16_le(out + 14 + i * 2, motion->gyro_valid
                   ? (uint16_t)motion->gyro_mdps[i] : 0);
    }
    return true;
}

bool sb_encode_hr_metadata(bool valid, uint16_t sequence, uint32_t timestamp_ms,
                           uint16_t bpm, uint8_t out[SB_HR_METADATA_SIZE])
{
    if (!out)
        return false;

    out[0] = SB_PROTOCOL_VERSION;
    out[1] = valid ? 0x01 : 0x00;
    put_u16_le(out + 2, sequence);
    put_u32_le(out + 4, timestamp_ms);
    put_u16_le(out + 8, valid ? bpm : 0);
    return true;
}

bool sb_encode_hr_measurement(uint16_t bpm, uint8_t out[2])
{
    if (!out || bpm == 0 || bpm > UINT8_MAX)
        return false;
    out[0] = 0x00;
    out[1] = (uint8_t)bpm;
    return true;
}

bool sb_decode_command(const uint8_t *bytes, size_t length, sb_command_t *out)
{
    if (!bytes || !out || length != SB_COMMAND_SIZE ||
        bytes[0] != SB_PROTOCOL_VERSION)
        return false;

    const uint16_t value = (uint16_t)bytes[2] |
                           (uint16_t)((uint16_t)bytes[3] << 8);
    switch (bytes[1]) {
    case SB_COMMAND_START:
    case SB_COMMAND_STOP:
        if (value != 0)
            return false;
        out->type = (sb_command_type_t)bytes[1];
        out->rate_hz = 0;
        return true;
    case SB_COMMAND_SET_RATE:
        if (value < 1 || value > 20)
            return false;
        out->type = SB_COMMAND_SET_RATE;
        out->rate_hz = value;
        return true;
    default:
        return false;
    }
}

void sb_encode_status(const sb_status_t *status, uint8_t out[SB_STATUS_SIZE])
{
    if (!status || !out)
        return;

    out[0] = SB_PROTOCOL_VERSION;
    out[1] = (uint8_t)((status->measuring ? 0x01 : 0) |
                       (status->connected ? 0x02 : 0) |
                       (status->transmitting ? 0x04 : 0) |
                       (status->accel_available ? 0x08 : 0) |
                       (status->gyro_available ? 0x10 : 0) |
                       (status->hr_available ? 0x20 : 0));
    out[2] = status->error;
    out[3] = 0;
    put_u16_le(out + 4, status->rate_hz);
    put_u16_le(out + 6, status->motion_sequence);
}
