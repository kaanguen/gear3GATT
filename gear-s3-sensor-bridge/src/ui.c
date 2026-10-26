#include "ui.h"

#include <Elementary.h>
#include <stdio.h>
#include <string.h>

static Evas_Object *s_window;
static Evas_Object *s_status;
static Evas_Object *s_connection;
static Evas_Object *s_measurement;
static Evas_Object *s_values;
static Evas_Object *s_button;
static ui_callbacks_t s_callbacks;
static bool s_measuring;

static void button_clicked(void *data, Evas_Object *object, void *event_info)
{
    (void)data;
    (void)object;
    (void)event_info;
    if (s_callbacks.toggle_measurement)
        s_callbacks.toggle_measurement(s_callbacks.user_data);
}

static void window_close(void *data, Evas_Object *object, void *event_info)
{
    (void)data;
    (void)object;
    (void)event_info;
    elm_exit();
}

int ui_create(const ui_callbacks_t *callbacks)
{
    if (callbacks)
        s_callbacks = *callbacks;

    s_window = elm_win_util_standard_add("sensor_bridge", "Sensor Bridge");
    if (!s_window)
        return -1;
    elm_win_autodel_set(s_window, EINA_FALSE);
    evas_object_smart_callback_add(s_window, "delete,request",
                                   window_close, NULL);

    Evas_Object *box = elm_box_add(s_window);
    if (!box)
        return -1;
    elm_box_padding_set(box, 0, 8);
    elm_win_resize_object_add(s_window, box);
    evas_object_show(box);

    s_status = elm_label_add(box);
    s_connection = elm_label_add(box);
    s_measurement = elm_label_add(box);
    s_values = elm_label_add(box);
    if (!s_status || !s_connection || !s_measurement || !s_values)
        return -1;

    elm_object_text_set(s_status, "Bluetooth: wird geprueft");
    elm_object_text_set(s_connection, "Verbindung: getrennt");
    elm_object_text_set(s_measurement, "Messung: gestoppt");
    elm_object_text_set(s_values, "Beschleunigung: nicht verfuegbar<br>Gyro: nicht verfuegbar<br>Herzfrequenz: nicht verfuegbar");

    elm_box_pack_end(box, s_status);
    elm_box_pack_end(box, s_connection);
    elm_box_pack_end(box, s_measurement);
    elm_box_pack_end(box, s_values);

    s_button = elm_button_add(box);
    if (!s_button)
        return -1;
    elm_object_text_set(s_button, "Starten");
    evas_object_smart_callback_add(s_button, "clicked", button_clicked, NULL);
    elm_box_pack_end(box, s_button);
    evas_object_show(s_status);
    evas_object_show(s_connection);
    evas_object_show(s_measurement);
    evas_object_show(s_values);
    evas_object_show(s_button);

    evas_object_resize(s_window, 360, 360);
    evas_object_show(s_window);
    return 0;
}

void ui_destroy(void)
{
    if (s_window)
        evas_object_del(s_window);
    s_window = NULL;
    s_status = NULL;
    s_connection = NULL;
    s_measurement = NULL;
    s_values = NULL;
    s_button = NULL;
}

void ui_set_status(const char *bluetooth, const char *connection,
                   const char *message, bool measuring, bool transmitting)
{
    char line[256];
    if (s_status) {
        snprintf(line, sizeof(line), "Bluetooth: %s", bluetooth ? bluetooth : "unbekannt");
        elm_object_text_set(s_status, line);
    }
    if (s_connection) {
        snprintf(line, sizeof(line), "Verbindung: %s", connection ? connection : "unbekannt");
        elm_object_text_set(s_connection, line);
    }

    s_measuring = measuring;
    if (s_measurement) {
        const char *state = measuring
            ? (transmitting ? "misst / uebertraegt" : "misst / wartet auf Client")
            : "gestoppt";
        snprintf(line, sizeof(line), "Messung: %s%s%s",
                 state, message && *message ? " - " : "",
                 message && *message ? message : "");
        elm_object_text_set(s_measurement, line);
    }
    if (s_button)
        elm_object_text_set(s_button, s_measuring ? "Stoppen" : "Starten");
}

void ui_update_sensors(const sensor_snapshot_t *snapshot)
{
    if (!snapshot || !s_values)
        return;
    char line[512];
    char accel[96];
    char gyro[96];
    char hr[64];

    if (snapshot->accel_valid)
        snprintf(accel, sizeof(accel), "Beschl.: %.2f, %.2f, %.2f m/s2",
                 snapshot->accel_mps2[0], snapshot->accel_mps2[1],
                 snapshot->accel_mps2[2]);
    else
        snprintf(accel, sizeof(accel), "Beschl.: %s",
                 snapshot->accel_available ? "warte auf Messwert" : "nicht verfuegbar");

    if (snapshot->gyro_valid)
        snprintf(gyro, sizeof(gyro), "Gyro: %.3f, %.3f, %.3f rad/s",
                 snapshot->gyro_rps[0], snapshot->gyro_rps[1],
                 snapshot->gyro_rps[2]);
    else
        snprintf(gyro, sizeof(gyro), "Gyro: %s",
                 snapshot->gyro_available ? "warte auf Messwert" : "nicht verfuegbar");

    if (snapshot->hr_valid)
        snprintf(hr, sizeof(hr), "Puls: %u bpm", snapshot->bpm);
    else
        snprintf(hr, sizeof(hr), "Puls: %s",
                 snapshot->hr_available ? "warte auf Messwert" : "nicht verfuegbar");

    snprintf(line, sizeof(line), "%s<br>%s<br>%s", accel, gyro, hr);
    elm_object_text_set(s_values, line);
}
