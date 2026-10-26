#include "../src/protocol.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void test_motion_encoding_and_invalid_values(void)
{
    sb_motion_t motion = {
        .accel_valid = true,
        .gyro_valid = false,
        .sequence = 0xffff,
        .timestamp_ms = 0x12345678,
        .accel_mg = {-1000, 0, 1000},
        .gyro_mdps = {1, 2, 3}
    };
    uint8_t bytes[SB_MOTION_FRAME_SIZE];
    assert(sb_encode_motion(&motion, bytes));
    assert(bytes[0] == 1 && bytes[1] == 1);
    assert(bytes[2] == 0xff && bytes[3] == 0xff);
    assert(bytes[4] == 0x78 && bytes[7] == 0x12);
    assert(bytes[8] == 0x18 && bytes[9] == 0xfc);
    assert(bytes[14] == 0 && bytes[15] == 0);
}

static void test_commands(void)
{
    sb_command_t command;
    const uint8_t start[] = {1, SB_COMMAND_START, 0, 0};
    const uint8_t rate[] = {1, SB_COMMAND_SET_RATE, 20, 0};
    const uint8_t too_fast[] = {1, SB_COMMAND_SET_RATE, 21, 0};
    const uint8_t bad_rate[] = {1, SB_COMMAND_SET_RATE, 0, 0};
    const uint8_t bad_version[] = {2, SB_COMMAND_STOP, 0, 0};
    assert(sb_decode_command(start, sizeof(start), &command));
    assert(command.type == SB_COMMAND_START);
    assert(sb_decode_command(rate, sizeof(rate), &command));
    assert(command.rate_hz == 20);
    assert(!sb_decode_command(too_fast, sizeof(too_fast), &command));
    assert(!sb_decode_command(bad_rate, sizeof(bad_rate), &command));
    assert(!sb_decode_command(bad_version, sizeof(bad_version), &command));
    assert(!sb_decode_command(start, sizeof(start) - 1, &command));
}

static void test_heart_rate_and_sequence_rollover(void)
{
    uint8_t bytes[SB_HR_METADATA_SIZE];
    assert(sb_encode_hr_metadata(true, 0xffff, 0xffffffff, 72, bytes));
    assert(bytes[1] == 1 && bytes[2] == 0xff && bytes[3] == 0xff);
    assert(bytes[8] == 72 && bytes[9] == 0);
    assert(!sb_encode_hr_measurement(0, bytes));
    assert(!sb_encode_hr_measurement(256, bytes));
}

static void test_status_error_encoding(void)
{
    sb_status_t status = {
        .measuring = true,
        .connected = false,
        .transmitting = false,
        .accel_available = true,
        .gyro_available = false,
        .hr_available = true,
        .error = 3,
        .rate_hz = 20,
        .motion_sequence = 0xffff
    };
    uint8_t bytes[SB_STATUS_SIZE];
    sb_encode_status(&status, bytes);
    assert(bytes[0] == SB_PROTOCOL_VERSION);
    assert(bytes[1] == 0x29);
    assert(bytes[2] == 3);
    assert(bytes[4] == 20 && bytes[5] == 0);
    assert(bytes[6] == 0xff && bytes[7] == 0xff);
}

static void test_rejected_commands_do_not_change_output(void)
{
    const uint8_t invalid[][4] = {
        {1, SB_COMMAND_START, 1, 0},
        {1, SB_COMMAND_STOP, 0, 1},
        {1, 0xff, 0, 0},
        {1, SB_COMMAND_SET_RATE, 0xff, 0xff}
    };
    sb_command_t command = {SB_COMMAND_STOP, 17};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!sb_decode_command(invalid[i], sizeof(invalid[i]), &command));
        assert(command.type == SB_COMMAND_STOP && command.rate_hz == 17);
    }
    assert(!sb_decode_command(NULL, 4, &command));
    assert(!sb_decode_command(invalid[0], 4, NULL));
    const uint8_t too_long[] = {1, SB_COMMAND_START, 0, 0, 0};
    assert(!sb_decode_command(too_long, sizeof(too_long), &command));
}

static void test_full_motion_golden_vector_and_guards(void)
{
    sb_motion_t motion = {
        .accel_valid = true, .gyro_valid = true,
        .sequence = 0x1234, .timestamp_ms = 0x89abcdef,
        .accel_mg = {-32768, -1, 32767},
        .gyro_mdps = {0, 1, -2}
    };
    const uint8_t expected[] = {
        1, 3, 0x34, 0x12, 0xef, 0xcd, 0xab, 0x89,
        0, 0x80, 0xff, 0xff, 0xff, 0x7f, 0, 0, 1, 0, 0xfe, 0xff
    };
    uint8_t guarded[SB_MOTION_FRAME_SIZE + 2];
    memset(guarded, 0xa5, sizeof(guarded));
    assert(sb_encode_motion(&motion, guarded + 1));
    assert(memcmp(expected, guarded + 1, sizeof(expected)) == 0);
    assert(guarded[0] == 0xa5 && guarded[sizeof(guarded) - 1] == 0xa5);
    motion.accel_valid = motion.gyro_valid = false;
    assert(sb_encode_motion(&motion, guarded + 1));
    for (size_t i = 8; i < SB_MOTION_FRAME_SIZE; ++i)
        assert(guarded[i + 1] == 0);
    assert(!sb_encode_motion(NULL, guarded));
    assert(!sb_encode_motion(&motion, NULL));
}

static void test_invalid_hr_is_not_encoded_as_a_measurement(void)
{
    uint8_t bytes[SB_HR_METADATA_SIZE];
    assert(sb_encode_hr_metadata(false, 1, 42, 72, bytes));
    assert(bytes[1] == 0 && bytes[8] == 0 && bytes[9] == 0);
    assert(sb_encode_hr_measurement(72, bytes));
    assert(bytes[0] == 0 && bytes[1] == 72);
    assert(!sb_encode_hr_metadata(true, 0, 0, 72, NULL));
    assert(!sb_encode_hr_measurement(72, NULL));
}

int main(void)
{
    test_motion_encoding_and_invalid_values();
    test_commands();
    test_heart_rate_and_sequence_rollover();
    test_status_error_encoding();
    test_rejected_commands_do_not_change_output();
    test_full_motion_golden_vector_and_guards();
    test_invalid_hr_is_not_encoded_as_a_measurement();
    return 0;
}
