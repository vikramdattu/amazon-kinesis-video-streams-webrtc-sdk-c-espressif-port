/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_netif.h"
#include "app_wifi_prov.h"

#if CONFIG_APP_NETWORK_PROV_BLE
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"
#endif

#if CONFIG_APP_NETWORK_USE_OPENETH
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_eth_netif_glue.h"
#endif

static const char *TAG = "app_wifi_prov";

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi_event_group;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
#if CONFIG_APP_NETWORK_USE_OPENETH
    else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Got ETH IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
#endif
}

#if CONFIG_APP_NETWORK_USE_OPENETH
/* Bring up the OpenCores Ethernet driver that qemu-xtensa exposes via
 * `-nic user,model=open_eth`. Same WIFI_CONNECTED_BIT signalling as
 * the WiFi path, so app_wifi_prov_wait_for_connection() works
 * unchanged. The IP comes from QEMU's slirp DHCP. */
static esp_err_t app_eth_init_openeth(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to init netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Failed to create event loop");

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);
    ESP_RETURN_ON_FALSE(eth_netif != NULL, ESP_FAIL, TAG, "esp_netif_new(eth) failed");

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_RETURN_ON_ERROR(esp_eth_driver_install(&eth_config, &eth_handle), TAG, "eth driver install");
    ESP_RETURN_ON_ERROR(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)),
                        TAG, "esp_netif_attach");

    /* Reuse the same wifi_event_handler — its IP_EVENT_ETH_GOT_IP arm
     * sets WIFI_CONNECTED_BIT identically to the WiFi path. */
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                        &wifi_event_handler, NULL, NULL), TAG, "ETH IP event handler");

    ESP_RETURN_ON_ERROR(esp_eth_start(eth_handle), TAG, "esp_eth_start");
    ESP_LOGI(TAG, "OpenCores Ethernet up; waiting for DHCP from slirp");
    return ESP_OK;
}
#endif

#if CONFIG_APP_NETWORK_PROV_BLE
static void prov_event_handler(void *user_data, network_prov_cb_event_t event, void *event_data)
{
    switch (event) {
        case NETWORK_PROV_START:
            ESP_LOGI(TAG, "Provisioning started");
            break;
        case NETWORK_PROV_WIFI_CRED_RECV:
            ESP_LOGI(TAG, "Received WiFi credentials");
            break;
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning successful");
            break;
        case NETWORK_PROV_WIFI_CRED_FAIL:
            ESP_LOGW(TAG, "Provisioning failed, please retry");
            network_prov_mgr_reset_wifi_provisioning();
            break;
        case NETWORK_PROV_END:
            ESP_LOGI(TAG, "Provisioning ended");
            /* Do NOT call network_prov_mgr_deinit() here — it destroys internal
             * state that network_prov_mgr_wait() needs to unblock.
             * Deinit is called after wait() returns in start_provisioning(). */
            break;
        default:
            break;
    }
}

static esp_err_t start_provisioning(const app_wifi_prov_config_t *cfg)
{
    network_prov_mgr_config_t config = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
        .app_event_handler = {
            .event_cb = prov_event_handler,
            .user_data = NULL,
        },
    };

    ESP_RETURN_ON_ERROR(network_prov_mgr_init(config), TAG, "Failed to init provisioning manager");

    bool provisioned = false;
    ESP_RETURN_ON_ERROR(network_prov_mgr_is_wifi_provisioned(&provisioned), TAG, "Failed to check provisioning status");

    if (provisioned) {
        ESP_LOGI(TAG, "Device already provisioned, skipping BLE provisioning");
        network_prov_mgr_deinit();
        return ESP_OK;
    }

    /* Call pre-provisioning callback (e.g. to init BT controller on coprocessor) */
    if (cfg->prov_start_cb) {
        ESP_RETURN_ON_ERROR(cfg->prov_start_cb(cfg->prov_cb_user_ctx),
                            TAG, "prov_start_cb failed");
    }

    ESP_LOGI(TAG, "Starting BLE provisioning...");

    /* Generate service name from MAC address */
    char service_name[16];
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(service_name, sizeof(service_name), "PROV_%02X%02X%02X", mac[3], mac[4], mac[5]);

    const char *pop = CONFIG_APP_NETWORK_PROV_POP;

    ESP_LOGI(TAG, "Use 'ESP BLE Provisioning' app to provision WiFi credentials");
    ESP_LOGI(TAG, "BLE device name: %s", service_name);
    ESP_LOGI(TAG, "Proof of Possession: %s", pop);

    ESP_RETURN_ON_ERROR(
        network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, (const void *) pop, service_name, NULL),
        TAG, "Failed to start provisioning");

    /* Block until provisioning completes, then release resources.
     * The prov_event_handler must NOT call deinit() — doing so destroys the
     * internal semaphore that wait() blocks on, causing a deadlock. */
    network_prov_mgr_wait();
    network_prov_mgr_deinit();
    ESP_LOGI(TAG, "BLE provisioning complete, resources released");

    /* Call post-provisioning callback (e.g. to deinit BT controller on coprocessor) */
    if (cfg->prov_end_cb) {
        cfg->prov_end_cb(cfg->prov_cb_user_ctx);
    }

    return ESP_OK;
}
#endif /* CONFIG_APP_NETWORK_PROV_BLE */

static bool wifi_is_provisioned(void)
{
    wifi_config_t wifi_cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK) {
        return false;
    }
    return strlen((const char *) wifi_cfg.sta.ssid) > 0;
}

esp_err_t app_wifi_prov_init(const app_wifi_prov_config_t *config)
{
    app_wifi_prov_config_t cfg;
    if (config) {
        cfg = *config;
    } else {
        cfg = (app_wifi_prov_config_t) APP_NETWORK_CONFIG_DEFAULT();
    }

    s_wifi_event_group = xEventGroupCreate();

#if CONFIG_APP_NETWORK_USE_OPENETH
    /* QEMU path: OpenCores Ethernet via slirp instead of WiFi. The
     * WIFI_CONNECTED_BIT semantics + wait helper work the same; from
     * the example's point of view nothing else changes. */
    ESP_RETURN_ON_ERROR(app_eth_init_openeth(), TAG, "OpenETH init failed");
    if (cfg.wifi_connect_timeout_ms > 0) {
        return app_wifi_prov_wait_for_connection(cfg.wifi_connect_timeout_ms);
    }
    return ESP_OK;
#endif

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Failed to init netif");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Failed to create event loop");

    /* Call pre-WiFi init callback (e.g. for coprocessor or custom netif setup) */
    if (cfg.pre_wifi_init_cb) {
        ESP_RETURN_ON_ERROR(cfg.pre_wifi_init_cb(cfg.pre_wifi_init_user_ctx),
                            TAG, "pre_wifi_init_cb failed");
    }

    if (!cfg.skip_default_sta_netif) {
        esp_netif_create_default_wifi_sta();
    }

    /* Register event handlers */
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                        &wifi_event_handler, NULL, NULL), TAG, "Failed to register WiFi event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                        &wifi_event_handler, NULL, NULL), TAG, "Failed to register IP event handler");

    /* Initialize WiFi */
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_cfg), TAG, "Failed to init WiFi");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Failed to set WiFi mode");

#if CONFIG_APP_NETWORK_PROV_BLE
    /* BLE provisioning: blocks until credentials are provided and WiFi connects,
     * or returns immediately if device is already provisioned. After this,
     * WiFi is already started and connected by the provisioning manager. */
    esp_err_t prov_ret = start_provisioning(&cfg);
    if (prov_ret == ESP_OK && wifi_is_provisioned()) {
        /* Provisioning manager already started WiFi and connected */
        if (cfg.wifi_connect_timeout_ms > 0) {
            return app_wifi_prov_wait_for_connection(cfg.wifi_connect_timeout_ms);
        }
        return ESP_OK;
    }
    if (prov_ret != ESP_OK) {
        ESP_LOGW(TAG, "BLE provisioning failed: %s, falling back to Kconfig credentials", esp_err_to_name(prov_ret));
    }
#endif

    /* No stored credentials — apply Kconfig defaults */
    if (!wifi_is_provisioned()) {
        wifi_config_t wifi_config = {
            .sta = {
                .ssid = CONFIG_ESP_WIFI_SSID,
                .password = CONFIG_ESP_WIFI_PASSWORD,
            },
        };
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config),
                            TAG, "Failed to set WiFi config");
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Failed to start WiFi");

    if (cfg.wifi_connect_timeout_ms > 0) {
        return app_wifi_prov_wait_for_connection(cfg.wifi_connect_timeout_ms);
    }

    return ESP_OK;
}

esp_err_t app_wifi_prov_wait_for_connection(uint32_t timeout_ms)
{
    ESP_LOGI(TAG, "Waiting for WiFi connection (use 'wifi-set <ssid> <pass>' to reconfigure)");

    TickType_t ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE, ticks);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "WiFi not connected within %" PRIu32 " ms, proceeding anyway", timeout_ms);
    return ESP_ERR_TIMEOUT;
}
