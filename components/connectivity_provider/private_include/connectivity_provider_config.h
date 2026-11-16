/**
 * @file connectivity_provider_config.h
 * @brief Private configuration of the connectivity_provider component.
 */
#ifndef CONNECTIVITY_PROVIDER_CONFIG_H
#define CONNECTIVITY_PROVIDER_CONFIG_H

#define CONNECTIVITY_NTP_SERVER "pool.ntp.org"

/** Reconnect backoff after a Wi-Fi drop: 1 s, 2 s, 4 s ... up to 30 s. */
#define CONNECTIVITY_RETRY_INITIAL_MS 1000U
#define CONNECTIVITY_RETRY_MAX_MS     30000U

/**
 * Weakest security accepted when connecting. Explicit, so the driver never falls back to an open
 * or WEP network that reuses CONFIG_WIFI_SSID. WPA2-PSK also accepts WPA2/WPA3 and WPA3 networks.
 */
#define CONNECTIVITY_MIN_AUTH_MODE WIFI_AUTH_WPA2_PSK

/** Networks listed by the start-up diagnostic scan. */
#define CONNECTIVITY_SCAN_MAX_APS 20U

#define CONNECTIVITY_US_PER_MS 1000ULL

#endif /* CONNECTIVITY_PROVIDER_CONFIG_H */
