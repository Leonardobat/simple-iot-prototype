/**
 * @file firebase_paths.h
 * @brief Realtime Database paths of this device, relative to CONFIG_FIREBASE_DB_URL.
 *
 * Paths have no leading '/' and no ".json" suffix (firebase_rtdb adds it). The data model is
 * described in docs/adr/0001-modelo-de-dados.md.
 */
#ifndef FIREBASE_PATHS_H
#define FIREBASE_PATHS_H

#include "sdkconfig.h"

/** Catalog and configuration of this device; streamed by device_listener. */
#define FIREBASE_DEVICE_PATH "devices/" CONFIG_DEVICE_ID

/** Sensors of this device: devices/<DEVICE_ID>/sensors/<sensorId>. */
#define FIREBASE_SENSORS_PATH FIREBASE_DEVICE_PATH "/sensors"

/** Actuators of this device: devices/<DEVICE_ID>/actuators/<actuatorId>. */
#define FIREBASE_ACTUATORS_PATH FIREBASE_DEVICE_PATH "/actuators"

/** Reading history of this device: readings/<DEVICE_ID>/<timestamp_ms>. */
#define FIREBASE_READINGS_PATH "readings/" CONFIG_DEVICE_ID

#endif /* FIREBASE_PATHS_H */
