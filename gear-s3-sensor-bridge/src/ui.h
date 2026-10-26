#ifndef SENSOR_BRIDGE_UI_H
#define SENSOR_BRIDGE_UI_H

#include "sensors.h"

#include <stdbool.h>

typedef struct {
    void (*toggle_measurement)(void *user_data);
    void *user_data;
} ui_callbacks_t;

int ui_create(const ui_callbacks_t *callbacks);
void ui_destroy(void);
void ui_set_status(const char *bluetooth, const char *connection,
                   const char *message, bool measuring, bool transmitting);
void ui_update_sensors(const sensor_snapshot_t *snapshot);

#endif
