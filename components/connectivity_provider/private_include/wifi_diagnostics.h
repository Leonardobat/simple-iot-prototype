/**
 * @file wifi_diagnostics.h
 * @brief Logs that help to diagnose Wi-Fi configuration mistakes (private to
 * connectivity_provider).
 */
#ifndef WIFI_DIAGNOSTICS_H
#define WIFI_DIAGNOSTICS_H

#include <stdint.h>

/**
 * @brief Explains a disconnect reason that usually means a configuration mistake.
 *
 * @param[in] reason wifi_err_reason_t value from WIFI_EVENT_STA_DISCONNECTED.
 * @return Static, human-readable hint (never NULL).
 * @note Thread-safe, non-blocking.
 */
const char *wifi_diagnostics_disconnect_hint(uint16_t reason);

/**
 * @brief Logs the Wi-Fi configuration (password length only) and the networks in range.
 *
 * Marks the configured SSID and warns when it is not visible (wrong name, 5 GHz, out of range).
 *
 * @pre Wi-Fi is started in station mode and not connecting.
 * @note Blocking: the scan takes ~2.5 s. Allocates the scan list temporarily (start-up only).
 */
void wifi_diagnostics_log_nearby_networks(void);

#endif /* WIFI_DIAGNOSTICS_H */
