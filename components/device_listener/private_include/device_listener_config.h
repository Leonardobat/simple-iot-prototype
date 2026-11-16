/**
 * @file device_listener_config.h
 * @brief Private configuration of the device_listener component.
 */
#ifndef DEVICE_LISTENER_CONFIG_H
#define DEVICE_LISTENER_CONFIG_H

/** Stream task stack: TLS reads, JSON parsing and Log V2. Check the logged high water mark. */
#define DEVICE_LISTENER_TASK_STACK_SIZE 8192U
#define DEVICE_LISTENER_TASK_PRIORITY   5U

/** Reconnect backoff after the stream closes: 1 s, 2 s, 4 s ... up to 60 s. */
#define DEVICE_LISTENER_RETRY_INITIAL_MS 1000U
#define DEVICE_LISTENER_RETRY_MAX_MS     60000U

/** Longest wait in device_listener_deinit(): the stream notices a cancel within ~1 s. */
#define DEVICE_LISTENER_STOP_TIMEOUT_MS 5000

/** Event paths and field paths, e.g. "/actuators/light_1/command". Longer event paths are skipped.
 */
#define DEVICE_LISTENER_PATH_MAX_LEN 64U

/** JSON tokens of one event: the snapshot of the whole device node is the largest. */
#define DEVICE_LISTENER_JSON_MAX_TOKENS 192

/** Children of devices/<DEVICE_ID> and the fields the device reads from them. */
#define DEVICE_LISTENER_SENSORS_KEY   "sensors"
#define DEVICE_LISTENER_ACTUATORS_KEY "actuators"
#define DEVICE_LISTENER_ENABLED_KEY   "enabled"
#define DEVICE_LISTENER_COMMAND_KEY   "command"
#define DEVICE_LISTENER_COLOR_KEY     "color"

/** Longest "color" accepted: "#rrggbb" fits with room to spare, so slightly longer (invalid) values
 * are still read and rejected by actuator_controller rather than truncated. */
#define DEVICE_LISTENER_TEXT_MAX_LEN 16U

#endif /* DEVICE_LISTENER_CONFIG_H */
