#ifndef SENSOR_BRIDGE_BLE_SERVER_H
#define SENSOR_BRIDGE_BLE_SERVER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BLE_BRIDGE_OK = 0,
    BLE_BRIDGE_ERR_BLUETOOTH = 1,
    BLE_BRIDGE_ERR_PERMISSION = 2,
    BLE_BRIDGE_ERR_SENSOR = 3,
    BLE_BRIDGE_ERR_COMMAND = 4,
    BLE_BRIDGE_ERR_NOTIFY = 5,
    BLE_BRIDGE_ERR_ADVERTISING = 6,
    BLE_BRIDGE_ERR_RUNTIME = 7,
    BLE_BRIDGE_ERR_UNSUPPORTED = 8
} ble_bridge_error_t;

typedef struct {
    void (*command)(uint8_t opcode, uint16_t rate_hz, void *user_data);
    void (*state_changed)(bool connected, int error, void *user_data);
    void *user_data;
} ble_server_callbacks_t;

int ble_server_start(const ble_server_callbacks_t *callbacks);
int ble_server_retry_advertising(void);
void ble_server_stop(void);
bool ble_server_is_connected(void);
bool ble_server_is_advertising(void);
bool ble_server_is_transmitting(void);
bool ble_server_is_initialized(void);
int ble_server_last_error(void);
int ble_server_notify_motion(const uint8_t *value, int length);
int ble_server_notify_heart_rate(const uint8_t *measurement, int measurement_length,
                                 const uint8_t *metadata, int metadata_length,
                                 bool *metadata_notified, bool *any_notified);
int ble_server_store_status(const uint8_t *value, int length);
int ble_server_notify_status(const uint8_t *value, int length);
const char *ble_server_error_string(int error);

#endif
