/**
 * @file connectivity_provider.h
 * @brief Brings the device online in the background: NVS, Wi-Fi station, automatic reconnection and
 *        SNTP time.
 */
#ifndef CONNECTIVITY_PROVIDER_H
#define CONNECTIVITY_PROVIDER_H

#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Starts connecting to CONFIG_WIFI_SSID and synchronizing the clock, in the background.
 *
 * Initializes NVS, the TCP/IP stack, the default event loop, Wi-Fi in station mode and SNTP, logs a
 * diagnostic scan, then returns without waiting: a missing network is not an error here. The
 * connection is retried with exponential backoff (1 s up to 30 s) until it succeeds and after any
 * drop; SNTP asks again whenever the station gets an IP. See connectivity_provider_is_connected()
 * and connectivity_provider_is_time_valid().
 *
 * @return
 *  - ESP_OK once the background work started
 *  - ESP_ERR_NO_MEM, ESP_ERR_WIFI_*, ESP_ERR_NVS_* on initialization failures
 *  On failure everything is released (see connectivity_provider_deinit()).
 * @pre Not initialized (asserted).
 * @note Not thread-safe: call once at start-up. Non-blocking apart from a short diagnostic scan.
 */
esp_err_t connectivity_provider_init(void);

/**
 * @brief Tells whether the station is associated and holds an IP address (from the Wi-Fi and IP
 *        events). It does not prove that the Internet is reachable.
 *
 * @return true while connected.
 * @pre Initialized (asserted).
 * @note Thread-safe, non-blocking.
 */
bool connectivity_provider_is_connected(void);

/**
 * @brief Tells whether SNTP has set the clock since connectivity_provider_init(). It stays true
 *        when the Wi-Fi drops: the clock keeps counting.
 *
 * @return true once the clock is valid.
 * @pre Initialized (asserted).
 * @note Thread-safe, non-blocking.
 */
bool connectivity_provider_is_time_valid(void);

/**
 * @brief Disconnects and releases Wi-Fi, SNTP, the default event loop and NVS.
 *
 * The TCP/IP stack stays initialized (ESP-IDF does not support esp_netif_deinit()); a later
 * connectivity_provider_init() reuses it.
 *
 * @pre No other component uses the network or the default event loop.
 * @post The component can be initialized again.
 * @note Not thread-safe. Safe to call on a partially initialized component.
 */
void connectivity_provider_deinit(void);

#endif /* CONNECTIVITY_PROVIDER_H */
