//
// Derived from nicolaielectronics/wifi-manager
//
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_wifi_types_generic.h"
#include "gui_style.h"
#include "pax_types.h"
#include "settings_ssh.h"

bool menu_ssh_edit(pax_buf_t* buffer, gui_theme_t* theme, uint8_t index, bool new_entry);
