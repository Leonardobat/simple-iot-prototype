#include "connectivity_provider.h"

#include <assert.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include "backoff.h"
#include "connectivity_provider_config.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "wifi_diagnostics.h"

#define CONNECTED_BIT  BIT0 /* Wi-Fi associated and an IP address held. */
#define TIME_VALID_BIT BIT1 /* SNTP has set the clock since this init. */

static const char *TAG = "connectivity";

/* Acquired in init order; each is released by deinit only if it was acquired. */
static EventGroupHandle_t s_wifi_events;
static bool s_nvs_ready;
static bool s_event_loop_ready;
static esp_netif_t *s_netif;
static bool s_wifi_ready;
static esp_event_handler_instance_t s_wifi_handler;
static esp_event_handler_instance_t s_ip_handler;
static esp_timer_handle_t s_retry_timer;
static atomic_bool s_sntp_ready; /* Read by the event loop task, written by init/deinit. */

static atomic_bool s_stopping;
static uint32_t s_retry_delay_ms; /* Used only by the default event loop task. */

static void warn_on_error(esp_err_t err, const char *step)
{
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s failed: %s", step, esp_err_to_name(err));
    }
}

/* ---- Reconnection ---- */

static void on_retry_timer(void *arg)
{
    (void)arg; /* No argument: the state is static. */
    if (!atomic_load(&s_stopping)) {
        warn_on_error(esp_wifi_connect(), "Reconnect");
    }
}

static void schedule_reconnect(void)
{
    s_retry_delay_ms = backoff_next_delay_ms(s_retry_delay_ms, CONNECTIVITY_RETRY_INITIAL_MS,
                                             CONNECTIVITY_RETRY_MAX_MS);
    const esp_err_t err =
        esp_timer_start_once(s_retry_timer, (uint64_t)s_retry_delay_ms * CONNECTIVITY_US_PER_MS);
    /* ESP_ERR_INVALID_STATE: a reconnect is already scheduled. */
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        ESP_LOGE(TAG, "Reconnect timer: %s", esp_err_to_name(err));
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;  /* Registered without argument. */
    (void)base; /* Registered for WIFI_EVENT only. */
    if ((id != WIFI_EVENT_STA_DISCONNECTED) || atomic_load(&s_stopping)) {
        return;
    }
    const wifi_event_sta_disconnected_t *event = data;
    (void)xEventGroupClearBits(s_wifi_events, CONNECTED_BIT);
    schedule_reconnect();
    ESP_LOGW(TAG, "Disconnected (reason %u: %s, RSSI %d), retrying in %" PRIu32 " ms",
             (unsigned)event->reason, wifi_diagnostics_disconnect_hint(event->reason),
             (int)event->rssi, s_retry_delay_ms);
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;  /* Registered without argument. */
    (void)base; /* Registered for IP_EVENT only. */
    if (id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGW(TAG, "Lost IP");
        (void)xEventGroupClearBits(s_wifi_events, CONNECTED_BIT);
        return;
    }
    if (id != IP_EVENT_STA_GOT_IP) {
        return; /* Other IP events (IPv6 link-local...) carry other data and do not mean online. */
    }
    const ip_event_got_ip_t *event = data;
    s_retry_delay_ms = 0U;
    ESP_LOGI(TAG, "Got IP " IPSTR, IP2STR(&event->ip_info.ip));
    (void)xEventGroupSetBits(s_wifi_events, CONNECTED_BIT);
    /* A request sent before the network was up failed, and lwIP waits 15 s or more before the next
     * one: ask again now. Also refreshes the clock after a reconnection. */
    if (atomic_load(&s_sntp_ready)) {
        warn_on_error(esp_netif_sntp_start(), "SNTP restart");
    }
}

/* ---- Initialization steps ---- */

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        err = nvs_flash_init();
    }
    s_nvs_ready = (err == ESP_OK);
    return err;
}

static esp_err_t init_event_infrastructure(void)
{
    ESP_RETURN_ON_ERROR(init_nvs(), TAG, "NVS init failed");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Event loop init failed");
    s_event_loop_ready = true;

    const esp_timer_create_args_t timer_args = {.callback = on_retry_timer, .name = "wifi_retry"};
    return esp_timer_create(&timer_args, &s_retry_timer);
}

static esp_err_t register_handlers(void)
{
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                            on_wifi_event, NULL, &s_wifi_handler),
                        TAG, "Wi-Fi handler");
    /* GOT_IP and LOST_IP: the handler tells them apart. */
    return esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ip_event, NULL,
                                               &s_ip_handler);
}

/* Kconfig does not limit string lengths: reject values strlcpy() would truncate at build time. */
_Static_assert(sizeof(CONFIG_WIFI_SSID) <= sizeof(((wifi_config_t *)NULL)->sta.ssid),
               "CONFIG_WIFI_SSID is longer than 31 characters");
_Static_assert(sizeof(CONFIG_WIFI_PASSWORD) <= sizeof(((wifi_config_t *)NULL)->sta.password),
               "CONFIG_WIFI_PASSWORD is longer than 63 characters");

static esp_err_t configure_station(void)
{
    wifi_config_t wifi_config = {0};
    /* Cannot truncate: checked by the static asserts above. */
    (void)strlcpy((char *)wifi_config.sta.ssid, CONFIG_WIFI_SSID, sizeof(wifi_config.sta.ssid));
    (void)strlcpy((char *)wifi_config.sta.password, CONFIG_WIFI_PASSWORD,
                  sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = CONNECTIVITY_MIN_AUTH_MODE;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Station mode");
    return esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}

static esp_err_t start_wifi_station(void)
{
    s_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(s_netif != NULL, ESP_ERR_NO_MEM, TAG, "Station netif");

    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "Wi-Fi init failed");
    s_wifi_ready = true;
    ESP_RETURN_ON_ERROR(esp_wifi_set_country_code(CONFIG_WIFI_COUNTRY_CODE, true), TAG,
                        "Invalid country code '%s'", CONFIG_WIFI_COUNTRY_CODE);
    ESP_RETURN_ON_ERROR(register_handlers(), TAG, "Event handlers");
    ESP_RETURN_ON_ERROR(configure_station(), TAG, "Station config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start failed");

    wifi_diagnostics_log_nearby_networks();
    return esp_wifi_connect();
}

/* Runs in the SNTP (tcpip) task when the clock was set. */
static void on_time_synced(struct timeval *tv)
{
    (void)tv; /* The clock is already set; only the fact matters. */
    ESP_LOGI(TAG, "Time synchronized");
    (void)xEventGroupSetBits(s_wifi_events, TIME_VALID_BIT);
}

/* SNTP keeps retrying by itself, and on_ip_event() restarts it whenever the station gets an IP. */
static esp_err_t start_sntp(void)
{
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONNECTIVITY_NTP_SERVER);
    config.sync_cb = on_time_synced;
    ESP_RETURN_ON_ERROR(esp_netif_sntp_init(&config), TAG, "SNTP init failed");
    atomic_store(&s_sntp_ready, true);
    return ESP_OK;
}

static esp_err_t bring_up(void)
{
    ESP_RETURN_ON_ERROR(init_event_infrastructure(), TAG, "Event infrastructure");
    ESP_RETURN_ON_ERROR(start_wifi_station(), TAG, "Wi-Fi station");
    return start_sntp();
}

esp_err_t connectivity_provider_init(void)
{
    assert(s_wifi_events == NULL);

    atomic_store(&s_stopping, false);
    s_retry_delay_ms = 0U;
    s_wifi_events = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_wifi_events != NULL, ESP_ERR_NO_MEM, TAG, "Event group alloc failed");

    const esp_err_t err = bring_up();
    if (err != ESP_OK) {
        connectivity_provider_deinit();
        return err;
    }
    ESP_LOGI(TAG, "Connecting to '%s' in the background", CONFIG_WIFI_SSID);
    return ESP_OK;
}

bool connectivity_provider_is_connected(void)
{
    assert(s_wifi_events != NULL);
    return (xEventGroupGetBits(s_wifi_events) & CONNECTED_BIT) != 0U;
}

bool connectivity_provider_is_time_valid(void)
{
    assert(s_wifi_events != NULL);
    return (xEventGroupGetBits(s_wifi_events) & TIME_VALID_BIT) != 0U;
}

/* ---- Deinitialization (reverse order) ---- */

static void release_wifi(void)
{
    if (s_retry_timer != NULL) {
        (void)esp_timer_stop(s_retry_timer); /* ESP_ERR_INVALID_STATE when not running: fine. */
        warn_on_error(esp_timer_delete(s_retry_timer), "Timer delete");
        s_retry_timer = NULL;
    }
    if (s_wifi_ready) {
        (void)esp_wifi_stop(); /* ESP_ERR_WIFI_NOT_STARTED when start failed: fine. */
        warn_on_error(esp_wifi_deinit(), "Wi-Fi deinit");
        s_wifi_ready = false;
    }
    if (s_wifi_handler != NULL) {
        warn_on_error(
            esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler),
            "Wi-Fi handler");
        s_wifi_handler = NULL;
    }
    if (s_ip_handler != NULL) {
        warn_on_error(
            esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, s_ip_handler),
            "IP handler");
        s_ip_handler = NULL;
    }
}

static void release_infrastructure(void)
{
    if (s_netif != NULL) {
        esp_netif_destroy_default_wifi(s_netif);
        s_netif = NULL;
    }
    if (s_event_loop_ready) {
        warn_on_error(esp_event_loop_delete_default(), "Event loop delete");
        s_event_loop_ready = false;
    }
    if (s_nvs_ready) {
        warn_on_error(nvs_flash_deinit(), "NVS deinit");
        s_nvs_ready = false;
    }
}

void connectivity_provider_deinit(void)
{
    atomic_store(&s_stopping, true);
    if (atomic_load(&s_sntp_ready)) {
        atomic_store(&s_sntp_ready, false);
        esp_netif_sntp_deinit();
    }
    release_wifi();
    release_infrastructure();
    if (s_wifi_events != NULL) {
        vEventGroupDelete(s_wifi_events);
        s_wifi_events = NULL;
    }
}
