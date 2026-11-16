#include "wifi_diagnostics.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "connectivity_provider_config.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "connectivity";

const char *wifi_diagnostics_disconnect_hint(uint16_t reason)
{
    switch (reason) {
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "network not found: check CONFIG_WIFI_SSID and that it is 2.4 GHz";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "authentication failed: check CONFIG_WIFI_PASSWORD";
    default:
        return "see wifi_err_reason_t in esp_wifi_types_generic.h";
    }
}

static const char *auth_mode_name(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN:
        return "open";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2-PSK";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3-PSK";
    case WIFI_AUTH_OWE:
        return "OWE";
    case WIFI_AUTH_ENTERPRISE:
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA_ENTERPRISE:
        return "Enterprise (802.1X, not supported)";
    default:
        return "other";
    }
}

static void log_scan_records(const wifi_ap_record_t *records, uint16_t count, uint16_t total)
{
    bool target_found = false;
    ESP_LOGI(TAG, "Scan: %u networks (showing %u)", (unsigned)total, (unsigned)count);
    for (uint16_t i = 0U; i < count; i++) {
        const char *ssid = (const char *)records[i].ssid;
        const bool is_target = strcmp(ssid, CONFIG_WIFI_SSID) == 0;
        target_found = target_found || is_target;
        ESP_LOGI(TAG, "%s '%s' ch %u RSSI %d %s", is_target ? "->" : "  ", ssid,
                 (unsigned)records[i].primary, (int)records[i].rssi,
                 auth_mode_name(records[i].authmode));
    }
    if (!target_found) {
        ESP_LOGW(TAG,
                 "'%s' not found: check the SSID (case, spaces), that it is 2.4 GHz and in range",
                 CONFIG_WIFI_SSID);
    }
}

static void log_config(void)
{
    ESP_LOGI(TAG, "Wi-Fi config: SSID '%s' (%u chars), password %u chars, country %s",
             CONFIG_WIFI_SSID, (unsigned)strlen(CONFIG_WIFI_SSID),
             (unsigned)strlen(CONFIG_WIFI_PASSWORD), CONFIG_WIFI_COUNTRY_CODE);
}

void wifi_diagnostics_log_nearby_networks(void)
{
    log_config();
    const esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan failed: %s", esp_err_to_name(err));
        return;
    }

    uint16_t total = 0U;
    if (esp_wifi_scan_get_ap_num(&total) != ESP_OK) {
        total = 0U;
    }
    const uint16_t capacity =
        (total < CONNECTIVITY_SCAN_MAX_APS) ? total : (uint16_t)CONNECTIVITY_SCAN_MAX_APS;
    wifi_ap_record_t *records = calloc((capacity > 0U) ? capacity : 1U, sizeof(*records));
    if (records == NULL) {
        ESP_LOGW(TAG, "No memory for the scan list");
        (void)esp_wifi_clear_ap_list(); /* Best effort: only frees the driver's list. */
        return;
    }
    uint16_t count = capacity;
    if (esp_wifi_scan_get_ap_records(&count, records) == ESP_OK) {
        /* Never trust the reported count beyond the allocated capacity. */
        log_scan_records(records, (count < capacity) ? count : capacity, total);
    }
    free(records);
}
