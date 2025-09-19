//
// Derived from nicolaielectronics/wifi-manager
//
#pragma once
#include "esp_err.h"

#define SSH_SETTINGS_MAX 0xFF

typedef enum {
    SSH_AUTH_PASSWORD,
    SSH_AUTH_PUBKEY,
    SSH_AUTH_INTERACTIVE,
} ssh_auth_mode_t;

typedef struct {
    // Basic connection information
    char                      connection_name[128];
    char                      dest_host[128];
    char		      dest_port[6];
    char                      username[128];
    // Password, if not using key based authentication
    char                      password[64];
    ssh_auth_mode_t           auth_mode;
    // TODO: Store dest host fingerprints
    // TODO: Store dest host public keys
} ssh_settings_t;

esp_err_t ssh_settings_get(uint8_t index, ssh_settings_t* out_settings);
esp_err_t ssh_settings_set(uint8_t index, ssh_settings_t* settings);
esp_err_t ssh_settings_erase(uint8_t index);
int       ssh_settings_find_empty_slot(void);

