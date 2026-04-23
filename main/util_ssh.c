//
// Derived from badgeteam/terminal-emulator, libssh2 example code, nicolaielectronics/tanmatsu-launcher
//
#include <string.h>
#include <sys/_intsup.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include "bsp/display.h"
#include "bsp/input.h"
#include "bsp/power.h"
#include "common/display.h"
#include "console.h"
#include "driver/uart.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "gui_element_footer.h"
#include "gui_style.h"
#include "icons.h"
#include "message_dialog.h"
#include "textedit.h"
#include "pax_types.h"
#include "pax_codecs.h"
#include "tanmatsu_coprocessor.h"
#include "wifi_connection.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include <libssh2.h>
#include "libssh2_setup.h"
#include "lwip/sockets.h"
#include "util_ssh.h"
#include "settings_ssh.h"

extern bool wifi_stack_get_initialized(void);

static char const TAG[] = "util_ssh";

// XXX probably not the right way to be doing this
static char const CSI_LEFT[] = "\e[D";
static char const CSI_RIGHT[] = "\e[C";
static char const CSI_UP[] = "\e[A";
static char const CSI_DOWN[] = "\e[B";
static char const CHR_TAB[] = "\t";
static char const CHR_BS[] = "\b";
static char const CHR_NL[] = "\n";
static char const CHR_ESC[] = "\e";

struct cons_insts_s console_instance;
LIBSSH2_CHANNEL *ssh_channel;
LIBSSH2_SESSION *ssh_session;
libssh2_socket_t ssh_sock = LIBSSH2_INVALID_SOCKET;
pax_buf_t ssh_bg_pax_buf = {0};


// cycle through keyboard brightness settings
static void util_ssh_keyboard_backlight(void) {
    uint8_t brightness;
    bsp_input_get_backlight_brightness(&brightness);
    if (brightness != 100) {
        brightness = 100;
    } else {
        brightness = 0;
    }
    ESP_LOGI(TAG, "Keyboard brightness: %u%%\r\n", brightness);
    bsp_input_set_backlight_brightness(brightness);
}


// cycle through screen backlight settings
static void util_ssh_display_backlight(void) {
    uint8_t brightness;
    bsp_display_get_backlight_brightness(&brightness);
    brightness += 15;
    if (brightness > 100) {
        brightness = 10;
    }
    ESP_LOGI(TAG, "Display brightness: %u%%\r\n", brightness);
    bsp_display_set_backlight_brightness(brightness);
}


// load a randomly chosen screen background if there are any present
static bool util_ssh_loadbg(pax_buf_t* buffer) {
    int backgrounds = 0;
    int randbgno = 0;
    DIR *d;
    struct dirent *dir;
    char bgfilename[PATH_MAX];
    
    //ESP_LOGI(TAG, "trying to opendir(/sd/bg)`");
    d = opendir("/sd/bg");
    if (!d) {
	ESP_LOGI(TAG, "no background images directory found");
	return false;
    }

    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type==DT_REG) {
            //ESP_LOGI(TAG, "found file %s\n", dir->d_name);
            backgrounds++;
        }
    }
    closedir(d);
    
    if (backgrounds == 0) {
	ESP_LOGI(TAG, "background images directory was empty - nothing loaded");
	return false;
    }

    //ESP_LOGI(TAG, "choosing a random background from %d", backgrounds + 1);
    randbgno = rand() % (backgrounds + 1);
    //ESP_LOGI(TAG, "picked number %d", randbgno);
    sprintf(bgfilename, "/sd/bg/%02d.png", randbgno);
    //ESP_LOGI(TAG, "which is filename %s", bgfilename);

    FILE* fd = fopen(bgfilename, "rb");
    if (fd == NULL) {
        ESP_LOGE(TAG, "Failed to open background image file");
        return false;
    }
    if (!pax_decode_png_fd(&ssh_bg_pax_buf, fd, PAX_BUF_32_8888ARGB, 0)) {  // CODEC_FLAG_EXISTING)) {
        ESP_LOGE(TAG, "Failed to decode png file");
        return false;
    }
    fclose(fd);

    console_clear(&console_instance);
    console_set_cursor(&console_instance, 0, 0);
    pax_draw_image(buffer, &ssh_bg_pax_buf, 0, 0); // this is a bit lame as it will disappear through scrolling or CLS
    display_blit_buffer(buffer);

    return true;
}


// choose a random foreground colour from the badge team terminal emulator pallette
//static void util_ssh_fgrand(struct cons_insts_s* console_instance, LIBSSH2_CHANNEL* ssh_channel) {
static void util_ssh_fgrand(struct cons_insts_s* console_instance) {
    int randfg = rand() % 10;
    int fgcol;

    switch (randfg) {
        case 0:
	    fgcol = CONS_COL_VGA_BLACK;
	    break;
	case 1:
	    fgcol = CONS_COL_VGA_RED;
	    break;
	case 2:
	    fgcol = CONS_COL_VGA_GREEN;
	    break;
	case 3:
	    fgcol = CONS_COL_VGA_YELLOW;
	    break;
	case 4:
	    fgcol = CONS_COL_VGA_BLUE;
	    break;
	case 5:
	    fgcol = CONS_COL_VGA_MAGENTA;
	    break;
	case 6:
	    fgcol = CONS_COL_VGA_CYAN;
	    break;
	case 7:
	    fgcol = CONS_COL_VGA_WHITE;
	    break;
	case 8:
	    fgcol = CONS_COL_VGA_GRAY;
	    break;
	default:
	    fgcol = CONS_COL_VGA_GREEN;
	    break;
    }
    ESP_LOGI(TAG, "changing fg colo(u)r to fg: %08x\n", fgcol);
    console_instance->fg = 0xff000000 | fgcol;
    console_clear(console_instance);
    console_set_cursor(console_instance, 0, 0);
    libssh2_channel_write(ssh_channel, CHR_NL, 1);
}

// choose a random background colour from the badge team terminal emulator pallette
//static void util_ssh_bgrand(struct cons_insts_s* console_instance, LIBSSH2_CHANNEL* ssh_channel) {
static void util_ssh_bgrand(struct cons_insts_s* console_instance) {
    int randbg = rand() % 8;
    int bgcol;

    switch(randbg) {
	case 0:
	    bgcol = CONS_COL_VGA_B_GREEN;
	    break;
	case 1:
	    bgcol = CONS_COL_VGA_B_YELLOW;
	    break;
	case 2:
	    bgcol = CONS_COL_VGA_B_BLUE;
	    break;
	case 3:
	    bgcol = CONS_COL_VGA_B_MAGNENTA;
	    break;
	case 4:
	    bgcol = CONS_COL_VGA_B_CYAN;
	    break;
	case 5:
	    bgcol = CONS_COL_VGA_B_WHITE;
	    break;
	case 6:
	    bgcol = CONS_COL_VGA_BLACK;
	    break;
	default:
	    bgcol = CONS_COL_VGA_BLACK;
	    break;
    }

    ESP_LOGI(TAG, "changing bg colo(u)r to: %08x\n", bgcol);
    console_instance->bg = 0xff000000 | bgcol;
    pax_background(console_instance->paxbuf, bgcol);
    console_clear(console_instance);
    console_set_cursor(console_instance, 0, 0);
    libssh2_channel_write(ssh_channel, CHR_NL, 1);
}


// set up our data structures, socket etc - then try to connect to the ssh server
//static bool util_ssh_init(struct cons_insts_s* console_instance, ssh_settings_t* settings, libssh2_socket_t ssh_sock, LIBSSH2_SESSION* ssh_session) {
static bool util_ssh_init(struct cons_insts_s* console_instance, ssh_settings_t* settings) {
    int rc;
    struct sockaddr_in ssh_addr;

    ESP_LOGI(TAG, "Initialising libssh2...");
    console_printf(console_instance, "Initialising libssh2... ");
    display_blit_buffer(console_instance->paxbuf);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    rc = libssh2_init(0);
    if (rc) {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_RED;
        console_printf(console_instance, "FAILED\n");
        display_blit_buffer(console_instance->paxbuf);
	vTaskDelay(pdMS_TO_TICKS(500));
        return false;
    } else {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_MAGENTA;
        console_printf(console_instance, "OK\n");
	console_instance->fg = 0xff000000 | CONS_COL_VGA_YELLOW;
        display_blit_buffer(console_instance->paxbuf);
	vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "Setting up destination host IP address and port...\n");
    console_printf(console_instance, "Setting up destination host IP address and port...\n");
    vTaskDelay(pdMS_TO_TICKS(50));
    inet_pton(AF_INET, settings->dest_host, &ssh_addr.sin_addr);
    ssh_addr.sin_port = htons(atoi(settings->dest_port));
    ssh_addr.sin_family = AF_INET;

    ESP_LOGI(TAG, "Creating socket to use for ssh session... ");
    console_printf(console_instance, "Creating socket to use for ssh session... ");
    vTaskDelay(pdMS_TO_TICKS(50));
    ssh_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ssh_sock == LIBSSH2_INVALID_SOCKET) {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_RED;
        console_printf(console_instance, "FAILED\n");
        display_blit_buffer(console_instance->paxbuf);
	vTaskDelay(pdMS_TO_TICKS(500));
        return false;
    } else {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_MAGENTA;
        console_printf(console_instance, "OK\n");
	console_instance->fg = 0xff000000 | CONS_COL_VGA_YELLOW;
        display_blit_buffer(console_instance->paxbuf);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "Connecting... ");
    console_printf(console_instance, "Connecting... ");
    display_blit_buffer(console_instance->paxbuf);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (connect(ssh_sock, (struct sockaddr*)&ssh_addr, sizeof(ssh_addr))) {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_RED;
        console_printf(console_instance, "FAILED\n");
        display_blit_buffer(console_instance->paxbuf);
	vTaskDelay(pdMS_TO_TICKS(500));
        return false;
    } else {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_MAGENTA;
        console_printf(console_instance, "OK\n");
	console_instance->fg = 0xff000000 | CONS_COL_VGA_YELLOW;
        display_blit_buffer(console_instance->paxbuf);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "Starting SSH session... ");
    console_printf(console_instance, "Starting SSH session... ");
    display_blit_buffer(console_instance->paxbuf);
    vTaskDelay(pdMS_TO_TICKS(50));
    ssh_session = libssh2_session_init();
    if (!ssh_session) {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_RED;
        console_printf(console_instance, "FAILED\n");
        display_blit_buffer(console_instance->paxbuf);
	vTaskDelay(pdMS_TO_TICKS(500));
        return false;
    } else {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_MAGENTA;
        console_printf(console_instance, "OK\n");
	console_instance->fg = 0xff000000 | CONS_COL_VGA_YELLOW;
        display_blit_buffer(console_instance->paxbuf);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "Session handshake... ");
    console_printf(console_instance, "Session handshake... ");
    vTaskDelay(pdMS_TO_TICKS(50));
    rc = libssh2_session_handshake(ssh_session, ssh_sock);
    if (rc) {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_RED;
        console_printf(console_instance, "FAILED\n");
        display_blit_buffer(console_instance->paxbuf);
	vTaskDelay(pdMS_TO_TICKS(500));
        return false;
    } else {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_MAGENTA;
        console_printf(console_instance, "OK\n");
	console_instance->fg = 0xff000000 | CONS_COL_VGA_YELLOW;
        display_blit_buffer(console_instance->paxbuf);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    return true;
}


// fetch the ssh server's key and check if not seen before
//static bool util_ssh_key_exchange(struct cons_insts_s* console_instance, LIBSSH2_SESSION* ssh_session, uint8_t connection_index) {
static bool util_ssh_key_exchange(struct cons_insts_s* console_instance, uint8_t connection_index) {
    const char* ssh_hostkey = '\0';
    char ssh_printable_fingerprint[128];
    size_t ssh_hostkey_len;
    int ssh_hostkey_type;
    const char* sha256_hash;
    char* j;
    int i;
    uint8_t saved_sha256[32] = {0};
    esp_err_t hk_res;
    int dialog_rc;
    char dialog_buffer[256] = {0};

    // Fetch and verify host key
    ESP_LOGI(TAG, "Fetching host key...");
    console_printf(console_instance, "Fetching host key... ");
    display_blit_buffer(console_instance->paxbuf);
    vTaskDelay(pdMS_TO_TICKS(50));
    ssh_hostkey = libssh2_session_hostkey(ssh_session, &ssh_hostkey_len, &ssh_hostkey_type);
    ESP_LOGI(TAG, "Fetched host key without crashing - yay!");
    if (!ssh_hostkey) {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_RED;
        console_printf(console_instance, "FAILED\n");
	vTaskDelay(pdMS_TO_TICKS(500));
        return false;
    } else {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_MAGENTA;
        console_printf(console_instance, "OK\n");
	console_instance->fg = 0xff000000 | CONS_COL_VGA_YELLOW;
    }

    ESP_LOGI(TAG, "remote host key len: %d, type: %d", (int)ssh_hostkey_len, ssh_hostkey_type);
    
    // Get SHA256 fingerprint for display and storage
    ESP_LOGI(TAG, "Computing host key SHA256 hash value... ");
    console_printf(console_instance, "Computing host key SHA256 hash value... ");
    display_blit_buffer(console_instance->paxbuf);
    vTaskDelay(pdMS_TO_TICKS(50));
    sha256_hash = libssh2_hostkey_hash(ssh_session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (!sha256_hash) {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_RED;
        console_printf(console_instance, "FAILED\n");
        display_blit_buffer(console_instance->paxbuf);
	vTaskDelay(pdMS_TO_TICKS(500));
        return false;
    } else {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_MAGENTA;
        console_printf(console_instance, "OK\n");
	console_instance->fg = 0xff000000 | CONS_COL_VGA_YELLOW;
        display_blit_buffer(console_instance->paxbuf);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Format printable SHA256 fingerprint
    bzero(ssh_printable_fingerprint, sizeof(ssh_printable_fingerprint));
    j = ssh_printable_fingerprint;
    for (i = 0; i < 32; i++) {
        sprintf(j, "%02X", (unsigned char)sha256_hash[i]);
        j += 2;
        if (i < 31) { *j++ = ':'; }
    }
    ESP_LOGI(TAG, "Host key fingerprint: %s\n", ssh_printable_fingerprint);
    console_printf(console_instance, "Host key fingerprint: %s\n", ssh_printable_fingerprint);
    display_blit_buffer(console_instance->paxbuf);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Check host key against saved fingerprint in NVS
    ESP_LOGI(TAG, "Checking for saved host key... \n");
    console_printf(console_instance, "Checking for saved host key... ");
    display_blit_buffer(console_instance->paxbuf);
    vTaskDelay(pdMS_TO_TICKS(50));

    hk_res = ssh_settings_get_host_key(connection_index, saved_sha256);

    if (hk_res == ESP_OK) {
        if (memcmp(saved_sha256, sha256_hash, 32) == 0) {
	    console_instance->fg = 0xff000000 | CONS_COL_VGA_MAGENTA;
            console_printf(console_instance, "OK\n");
	    console_instance->fg = 0xff000000 | CONS_COL_VGA_YELLOW;
            display_blit_buffer(console_instance->paxbuf);
        } else {
            ESP_LOGW(TAG, "HOST KEY MISMATCH - possible MITM attack");
            snprintf(dialog_buffer, sizeof(dialog_buffer),
                "WARNING: Host key has CHANGED!\n\n"
                "This could indicate a man-in-the-middle attack.\n\n"
                "SHA256: %.65s...\n\n"
                "Accept new key?", ssh_printable_fingerprint);
            dialog_rc = adv_dialog_yes_no(get_icon(ICON_ERROR), "Host key mismatch!", dialog_buffer);
            if (dialog_rc == MSG_DIALOG_RETURN_NO) {
                ESP_LOGI(TAG, "user rejected changed host key");
                return false;
            }
            // User accepted the new key - save it
            ESP_LOGI(TAG, "user accepted new host key, saving");
            ssh_settings_set_host_key(connection_index, (const uint8_t*)sha256_hash);
        }
    } else {
        // No saved fingerprint - first connection to this host
        ESP_LOGI(TAG, "no saved host key, first connection");
        snprintf(dialog_buffer, sizeof(dialog_buffer),
            "New host - no saved key.\n\n"
            "SHA256: %.65s...\n\n"
            "Trust this host?", ssh_printable_fingerprint);
        dialog_rc = adv_dialog_yes_no(get_icon(ICON_HELP), "New SSH host key", dialog_buffer);
        if (dialog_rc == MSG_DIALOG_RETURN_NO) {
            ESP_LOGI(TAG, "user rejected new host key");
            return false;
        }
        // Save the fingerprint for future connections
        ESP_LOGI(TAG, "user accepted host key, saving to NVS");
        ssh_settings_set_host_key(connection_index, (const uint8_t*)sha256_hash);
    }

    return true;
}


// authenticate against the ssh server and set up a session
//static bool util_ssh_auth_stage(struct cons_insts_s* console_instance, ssh_settings_t* settings, LIBSSH2_SESSION* ssh_session, LIBSSH2_CHANNEL* ssh_channel) {
static bool util_ssh_auth_stage(struct cons_insts_s* console_instance, ssh_settings_t* settings) {
    char* ssh_userauthlist = '\0';
    char ssh_password[128];
    bool accepted = false;

    ESP_LOGI(TAG, "user auth methods check");
    ssh_userauthlist = libssh2_userauth_list(ssh_session, settings->username, (unsigned int)strlen(settings->username));
    ESP_LOGI(TAG, "Host supports auth methods... %s\n", ssh_userauthlist);
    console_printf(console_instance, "Host supports auth methods... %s\n", ssh_userauthlist);
    display_blit_buffer(console_instance->paxbuf);
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "Checking for saved passwords... ");
    console_printf(console_instance, "Checking for saved passwords... ");
    display_blit_buffer(console_instance->paxbuf);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (strlen(settings->password) > 0) {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_MAGENTA;
        console_printf(console_instance, "using saved password");
	console_instance->fg = 0xff000000 | CONS_COL_VGA_YELLOW;
        display_blit_buffer(console_instance->paxbuf);
        vTaskDelay(pdMS_TO_TICKS(50));
	strncpy(ssh_password, settings->password, strlen(settings->password));
    } else {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_MAGENTA;
        console_printf(console_instance, "\nNo saved password, please enter one...\n");
	console_instance->fg = 0xff000000 | CONS_COL_VGA_YELLOW;
        display_blit_buffer(console_instance->paxbuf);
        vTaskDelay(pdMS_TO_TICKS(50));
        memset(ssh_password, 0, sizeof(ssh_password)); // don't display the password
    
        //menu_textedit(buffer, theme, "Password", ssh_password, sizeof(settings->password) + sizeof('\0'), true, &accepted);
        if (accepted) {
            ESP_LOGI(TAG, "updated password: <redacted>");
            //ESP_LOGI(TAG, "updated password: %s", ssh_password);
        } 
    }

    ESP_LOGI(TAG, "Authenticating to %s:%s as user %s\n", settings->dest_host, settings->dest_port, settings->username);
    console_printf(console_instance, "\nAuthenticating to %s:%s as user %s\n", settings->dest_host, settings->dest_port, settings->username);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (libssh2_userauth_password(ssh_session, settings->username, settings->password)) {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_RED;
        console_printf(console_instance, "\n!! Authentication by password failed !!\n");
        display_blit_buffer(console_instance->paxbuf);
	vTaskDelay(pdMS_TO_TICKS(500));
        return false;
    }

    ESP_LOGI(TAG, "Requesting ssh session... \n");
    console_printf(console_instance, "Requesting ssh session... ");
    display_blit_buffer(console_instance->paxbuf);
    vTaskDelay(pdMS_TO_TICKS(50));
    ssh_channel = libssh2_channel_open_session(ssh_session);
    if (!ssh_channel) {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_RED;
        console_printf(console_instance, "FAILED");
        display_blit_buffer(console_instance->paxbuf);
	vTaskDelay(pdMS_TO_TICKS(500));
        return false;
    } else {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_MAGENTA;
        console_printf(console_instance, "OK\n");
	console_instance->fg = 0xff000000 | CONS_COL_VGA_YELLOW;
        display_blit_buffer(console_instance->paxbuf);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    //ESP_LOGI(TAG, "sending env variables");
    libssh2_channel_setenv(ssh_channel, "LANG", "en_US.UTF-8");

    ESP_LOGI(TAG, "Requesting %dx%d pty... \n", console_instance->chars_x, console_instance->chars_y);
    console_printf(console_instance, "Requesting %dx%d pty... ", console_instance->chars_x, console_instance->chars_y);
    display_blit_buffer(console_instance->paxbuf);
    vTaskDelay(pdMS_TO_TICKS(50));
    if (libssh2_channel_request_pty(ssh_channel, "xterm-color")) {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_RED;
        console_printf(console_instance, "FAILED");
        display_blit_buffer(console_instance->paxbuf);
	vTaskDelay(pdMS_TO_TICKS(500));
        return false;
    } else {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_MAGENTA;
        console_printf(console_instance, "OK\n");
	console_instance->fg = 0xff000000 | CONS_COL_VGA_YELLOW;
        display_blit_buffer(console_instance->paxbuf);
	vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "Requesting shell... ");
    console_printf(console_instance, "Requesting shell... ");
    if (libssh2_channel_shell(ssh_channel)) {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_RED;
        console_printf(console_instance, "FAILED");
        display_blit_buffer(console_instance->paxbuf);
	vTaskDelay(pdMS_TO_TICKS(500));
        return false;
    } else {
	console_instance->fg = 0xff000000 | CONS_COL_VGA_MAGENTA;
        console_printf(console_instance, "OK\n");
	console_instance->fg = 0xff000000 | CONS_COL_VGA_YELLOW;
        display_blit_buffer(console_instance->paxbuf);
	vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "Making the channel non-blocking.\n");
    console_printf(console_instance, "Making the channel non-blocking.\n");
    display_blit_buffer(console_instance->paxbuf);
    vTaskDelay(pdMS_TO_TICKS(50));
    libssh2_channel_set_blocking(ssh_channel, 0);

    return true;
}


// task to display the data received from the ssh server
static void util_ssh_response_task(void* pvParameters) {
    char ssh_buffer[1024];
    ssize_t nbytes; // bytes read from ssh server
    int cx, cy;

    cx = console_instance.char_width * console_instance.cursor_x;
    cy = console_instance.char_height * console_instance.cursor_y;
    pax_draw_rect(console_instance.paxbuf, 0xff000000 | CONS_COL_VGA_WHITE, cx, cy, cx + 1, cy + (console_instance.char_height - 1));

    while (1) {
        //ESP_LOGI(TAG, "check for server EOF");
        if (libssh2_channel_eof(ssh_channel)) {
            ESP_LOGI(TAG, "server sent EOF");
	    vTaskDelete(NULL);
        }

        bzero(ssh_buffer, sizeof(ssh_buffer));
        nbytes = libssh2_channel_read(ssh_channel, ssh_buffer, sizeof(ssh_buffer));
        if (nbytes > 0) {
	    ESP_LOGI(TAG, "read %d bytes from ssh server", nbytes);
            //pax_draw_rect(console_instance.paxbuf, 0xff000000 | CONS_COL_VGA_BLACK, cx, cy, cx + 1, cy + (console_instance.char_height - 1));
            pax_draw_line(console_instance.paxbuf, console_instance.bg, cx, cy, cx, cy + (console_instance.char_height - 1));
            pax_draw_line(console_instance.paxbuf, console_instance.bg, cx + 1, cy, cx + 1, cy + (console_instance.char_height - 1));
	    console_puts(&console_instance, ssh_buffer);
	    //console_get_cursor(&console_instance, &cx, &cy);
            cx = console_instance.char_width * console_instance.cursor_x;
            cy = console_instance.char_height * console_instance.cursor_y;
	    ESP_LOGI(TAG, "got new cx: %d, cy: %d", cx, cy);
            //cx = console_instance.char_width * cx;
            //cy = console_instance.char_height * cy;
	    ESP_LOGI(TAG, "rectangle bounds x: %d, y: %d", cx + 1, cy + (console_instance.char_height -1));
            pax_draw_line(console_instance.paxbuf, 0xff000000 | CONS_COL_VGA_WHITE, cx, cy, cx, cy + (console_instance.char_height - 1));
            pax_draw_line(console_instance.paxbuf, 0xff000000 | CONS_COL_VGA_WHITE, cx + 1, cy, cx + 1, cy + (console_instance.char_height - 1));
            display_blit_buffer(console_instance.paxbuf);
        }

	vTaskDelay(pdMS_TO_TICKS(50));
    }
}


//static void util_ssh_shutdown(struct cons_insts_s* console_instance, libssh2_socket_t ssh_sock, LIBSSH2_SESSION* ssh_session, LIBSSH2_CHANNEL* ssh_channel) {
static void util_ssh_shutdown(struct cons_insts_s* console_instance) {
    // closing the ssh connection and freeing resources
    // could be due to user action, or an error
    console_clear(console_instance);
    console_set_cursor(console_instance, 0, 0);
    console_printf(console_instance, "Closing down session...\n");
    display_blit_buffer(console_instance->paxbuf);
    ESP_LOGI(TAG, "freeing memory... (free heap: %lu)", (unsigned long)esp_get_free_heap_size());
    if (ssh_channel) {
        libssh2_channel_send_eof(ssh_channel);
        libssh2_channel_close(ssh_channel);
        libssh2_channel_free(ssh_channel);
        ssh_channel = NULL;
    }
    if (ssh_session) {
        libssh2_session_disconnect(ssh_session, "User closed session");
        libssh2_session_free(ssh_session);
        ssh_session = NULL;
    }
    if (ssh_sock != LIBSSH2_INVALID_SOCKET) {
        shutdown(ssh_sock, 2);
        LIBSSH2_SOCKET_CLOSE(ssh_sock);
        ssh_sock = LIBSSH2_INVALID_SOCKET;
    }
    libssh2_exit();
    ESP_LOGI(TAG, "shutdown complete (free heap: %lu)", (unsigned long)esp_get_free_heap_size());
}


void util_ssh(pax_buf_t* buffer, gui_theme_t* theme, ssh_settings_t* settings, uint8_t connection_index) {
    ESP_LOGI(TAG, "Free heap at entry: %lu", (unsigned long)esp_get_free_heap_size());
    QueueHandle_t input_event_queue = NULL;
    ESP_ERROR_CHECK(bsp_input_get_queue(&input_event_queue));
    bsp_input_event_t event;
    char ssh_out;
    bool util_ssh_done = false;
    int ofg = 0;
    int obg = 0;

    struct cons_config_s con_conf = {
        .font = pax_font_sky_mono, 
	.font_size_mult = 1.5, 
	.paxbuf = display_get_buffer(), 
	.output_cb = NULL
    };

    console_init(&console_instance, &con_conf);
    //console_set_colors(&console_instance, CONS_COL_VGA_GREEN, CONS_COL_VGA_BLACK);
    console_instance.fg = 0xff000000 | CONS_COL_VGA_YELLOW;
    console_instance.bg = 0xff000000 | CONS_COL_VGA_BLACK;
    util_ssh_keyboard_backlight();

    console_printf(&console_instance, "\nConnecting to WiFi...\n");
    display_blit_buffer(buffer);

    if (!wifi_stack_get_initialized()) {
        ESP_LOGE(TAG, "WiFi stack not initialized");
        message_dialog(get_icon(ICON_TERMINAL), "SSH: fatal error", "WiFi stack not initialized", "Quit");
	vTaskDelay(pdMS_TO_TICKS(500));
        return;
    }

    if (!wifi_connection_is_connected()) {
        if (wifi_connect_try_all() != ESP_OK) {
            ESP_LOGE(TAG, "Not connected to WiFi");
            message_dialog(get_icon(ICON_TERMINAL), "SSH: fatal error", "Failed to connect to WiFi network", "Quit");
	    vTaskDelay(pdMS_TO_TICKS(500));
            return;
        }
    }

    //if (!util_ssh_init(&console_instance, settings, ssh_sock, ssh_session)) {
    //	util_ssh_shutdown(&console_instance, ssh_sock, ssh_session, ssh_channel);
    //}
    ESP_LOGI(TAG, "Free heap before init: %lu", (unsigned long)esp_get_free_heap_size());
    if (!util_ssh_init(&console_instance, settings)) {
      	util_ssh_shutdown(&console_instance);
	ESP_LOGI(TAG, "Free heap after failed init+shutdown: %lu", (unsigned long)esp_get_free_heap_size());
	return;
    }
    ESP_LOGI(TAG, "Free heap after init: %lu", (unsigned long)esp_get_free_heap_size());

    //if (!util_ssh_key_exchange(&console_instance, &ssh_session, connection_index)) {
    //    util_ssh_shutdown(&console_instance, ssh_sock, ssh_session, ssh_channel);
    //}
    if (!util_ssh_key_exchange(&console_instance, connection_index)) {
        util_ssh_shutdown(&console_instance);
	return;
    }

    //if (!util_ssh_auth_stage(&console_instance, settings, ssh_session, ssh_channel)) {
    //    util_ssh_shutdown(&console_instance, ssh_sock, ssh_session, ssh_channel);
    //}
    ESP_LOGI(TAG, "Free heap before auth: %lu", (unsigned long)esp_get_free_heap_size());
    if (!util_ssh_auth_stage(&console_instance, settings)) {
        util_ssh_shutdown(&console_instance);
	ESP_LOGI(TAG, "Free heap after failed auth+shutdown: %lu", (unsigned long)esp_get_free_heap_size());
	return;
    }
    ESP_LOGI(TAG, "Free heap after auth: %lu", (unsigned long)esp_get_free_heap_size());
    
    ESP_LOGI(TAG, "Looking for background images...\n");
    console_printf(&console_instance, "Looking for background images... ");
    if (!util_ssh_loadbg(buffer)) {
	console_instance.fg = 0xff000000 | CONS_COL_VGA_RED;
        console_printf(&console_instance, "NOT FOUND");
        display_blit_buffer(buffer);
	vTaskDelay(pdMS_TO_TICKS(50));
    } else {
	console_instance.fg = 0xff000000 | CONS_COL_VGA_MAGENTA;
        console_printf(&console_instance, "LOADED\n");
	console_instance.fg = 0xff000000 | CONS_COL_VGA_YELLOW;
        display_blit_buffer(buffer);
	vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "ssh setup completed, entering main loop");
    console_printf(&console_instance, "\n\n");
    console_instance.fg = 0xff000000 | CONS_COL_VGA_GREEN;

    xTaskCreate(util_ssh_response_task, TAG, 4096, NULL, 10, NULL);

    while (util_ssh_done == false) {
    	ssh_out = '\0';

        if (xQueueReceive(input_event_queue, &event, pdMS_TO_TICKS(1000)) != pdTRUE) {
	    continue;
	}

        //ESP_LOGI(TAG, "input received");
        if (event.type == INPUT_EVENT_TYPE_KEYBOARD) {
	    //ESP_LOGI(TAG, "normal keyboard event received");
	    ssh_out = event.args_keyboard.ascii;
	    if (event.args_keyboard.modifiers & BSP_INPUT_MODIFIER_CTRL) {
		ESP_LOGI(TAG, "applying CTRL modifier");
                ssh_out &= 0x1f; // modify the keycode sent to make it a control character
	    } else if (event.args_keyboard.modifiers & BSP_INPUT_MODIFIER_ALT) {
		ESP_LOGI(TAG, "applying ALT modifier - inserting an escape");
            	libssh2_channel_write(ssh_channel, "\e", 1);
	    }
	    if (ssh_out != '\b') {
                libssh2_channel_write(ssh_channel, &ssh_out, 1);
	    }
	    continue;
	}

        if (event.type == INPUT_EVENT_TYPE_NAVIGATION && event.args_navigation.state) {
            switch (event.args_navigation.key) {
                case BSP_INPUT_NAVIGATION_KEY_ESC:
		    ESP_LOGI(TAG, "esc key pressed - passing through");
                    libssh2_channel_write(ssh_channel, CHR_ESC, 1);
		    break;
                case BSP_INPUT_NAVIGATION_KEY_F1:
		    ESP_LOGI(TAG, "esc or close key pressed - returning to app launcher");
		    util_ssh_done = true;
	            util_ssh_shutdown(&console_instance);
		    break;
                case BSP_INPUT_NAVIGATION_KEY_F2:
		    ESP_LOGI(TAG, "keyboard backlight toggle");
		    util_ssh_keyboard_backlight();
		    break;
                case BSP_INPUT_NAVIGATION_KEY_F3:
		    ESP_LOGI(TAG, "display backlight toggle");
		    util_ssh_display_backlight();
		    break;
                case BSP_INPUT_NAVIGATION_KEY_F5:
		    ESP_LOGI(TAG, "fg colour randomiser");
		    //util_ssh_fgrand(&console_instance, ssh_channel);
		    util_ssh_fgrand(&console_instance);
		    break;
                case BSP_INPUT_NAVIGATION_KEY_F6:
		    ESP_LOGI(TAG, "bg colour randomiser");
		    //util_ssh_bgrand(&console_instance, ssh_channel);
		    util_ssh_bgrand(&console_instance);
		    break;
            	case BSP_INPUT_NAVIGATION_KEY_TAB:
		    ESP_LOGI(TAG, "tab key pressed");
                    libssh2_channel_write(ssh_channel, CHR_TAB, 1);
		    break;
            	case BSP_INPUT_NAVIGATION_KEY_BACKSPACE:
		    ESP_LOGI(TAG, "backspace key pressed");
                    libssh2_channel_write(ssh_channel, CHR_BS, 1);
                    break;
                case BSP_INPUT_NAVIGATION_KEY_RETURN:
		    ESP_LOGI(TAG, "return key pressed");
                    libssh2_channel_write(ssh_channel, CHR_NL, 1);
                    break;
		case BSP_INPUT_NAVIGATION_KEY_LEFT:
		    ESP_LOGI(TAG, "left key pressed");
                    libssh2_channel_write(ssh_channel, CSI_LEFT, strlen(CSI_LEFT));
		    break;
            	case BSP_INPUT_NAVIGATION_KEY_RIGHT:
		    ESP_LOGI(TAG, "right key pressed");
                    libssh2_channel_write(ssh_channel, CSI_RIGHT, strlen(CSI_RIGHT));
		    break;
            	case BSP_INPUT_NAVIGATION_KEY_UP:
		    ESP_LOGI(TAG, "up key pressed");
                    libssh2_channel_write(ssh_channel, CSI_UP, strlen(CSI_UP));
		    break;
            	case BSP_INPUT_NAVIGATION_KEY_DOWN:
		    ESP_LOGI(TAG, "down key pressed");
                    libssh2_channel_write(ssh_channel, CSI_DOWN, strlen(CSI_DOWN));
		    break;
		case BSP_INPUT_NAVIGATION_KEY_VOLUME_UP:
		    ESP_LOGI(TAG, "volume up key pressed");
                    con_conf.font_size_mult += 0.2;
		    ofg = console_instance.fg;
		    obg = console_instance.bg;
    		    console_init(&console_instance, &con_conf);
		    console_instance.fg = ofg;
		    console_instance.bg = obg;
                    pax_background(console_instance.paxbuf, obg);
    		    console_clear(&console_instance);
		    console_set_cursor(&console_instance, 0, 0);
                    libssh2_channel_write(ssh_channel, CHR_NL, 1);
                    display_blit_buffer(buffer);
		    break;
		case BSP_INPUT_NAVIGATION_KEY_VOLUME_DOWN:
		    ESP_LOGI(TAG, "volume down key pressed");
                    con_conf.font_size_mult -= 0.2;
		    ofg = console_instance.fg;
		    obg = console_instance.bg;
    		    console_init(&console_instance, &con_conf);
		    console_instance.fg = ofg;
		    console_instance.bg = obg;
                    pax_background(console_instance.paxbuf, obg);
    		    console_clear(&console_instance);
		    console_set_cursor(&console_instance, 0, 0);
                    libssh2_channel_write(ssh_channel, CHR_NL, 1);
                    display_blit_buffer(buffer);
		    break;
                default:
		    ESP_LOGI(TAG, "some other navigation key has been pressed");
                    break;
	    }
	}
    }
}

// TODO
// add support for other modifiers where needed, e.g. ALT, FN
// prompt user for password if not saved
// fix disconnection/exit bug which makes the launcher blue screen ~50% of the time on restarting
// fix whatever is preventing connections added entirely via badgelink from showing up
// tidy up spaghetti code and global variables
// check if any changes needed for IPv6 support
// check if any changes needed for DNS lookup of hostnames
// display server banner?
// encrypt saved credentials with a passphrase / prompt user to unlock
// UI for managing cached host keys
// support public key auth
// support agent auth
// let user set terminal type?
// test with TERM xterm-color etc
// convert background image loading into a task, so it doesn't hold everything else up
// see if we can find a way to stop background image from scrolling
// function key switches between background images?
// ask if they really want to close the connection when they hit F1
// improve escape character processing so we can use vi, emacs, fancy prompts etc
// ability to choose wifi network, or maybe tie wifi network to ssh connection details
// other cursor styles e.g. block, blinking block, blinking line?

// XXX holding pen for zh4ck's CSI processing improvements, pending incorporation into badgeteam terminal emulator

//if (*p == '\x1b' && p + 1 < ssh_buffer + nbytes && *(p+1) == '[') {
//    // CSI sequence: ESC[
//    p += 2;
//    char seq[32] = {0};
//    int i = 0;
//    // Parse parameters
//    while (p < ssh_buffer + nbytes && i < 31) {
//        if ((*p >= '0' && *p <= '9') || *p == ';' || *p == '?') {
//            seq[i++] = *p++;
//        } else {
//            break;
//        }
//    }
//    if (p < ssh_buffer + nbytes) {
//        char cmd = *p++;
//        if (cmd == 'J' && strcmp(seq, "2") == 0) {
//            // Clear screen
//            console_clear(&console_instance);
//            pax_draw_rect(buffer, 0xff000000, 0, 0, 800, 480);
//            if (ssh_bg_pax_buf.width > 0) {
//                pax_draw_image(buffer, &ssh_bg_pax_buf, 0, 0);
//            }
//            console_set_cursor(&console_instance, 0, 0);
//            cx = cy = ocx = ocy = 0;
//        } else if (cmd == 'H' || cmd == 'f') {
//            // Cursor position
//            int row = 1, col = 1;
//            if (seq[0]) sscanf(seq, "%d;%d", &row, &col);
//            console_set_cursor(&console_instance, col - 1, row - 1);
//        } else {
//            // Pass through other sequences
//            char esc_seq[64];
//            snprintf(esc_seq, sizeof(esc_seq), "\x1b[%s%c", seq, cmd);
//            console_puts(&console_instance, esc_seq);
//        }
//    }
//} else if (*p == 0x08) {
//    // Backspace: erase previous character and move cursor left
//    ESP_LOGI(TAG, "Detected BS (0x08), erasing character and moving cursor left from x=%d", console_instance.cursor_x);
//    if (console_instance.cursor_x > 0) {
//        console_instance.cursor_x--;
//        int erase_x = console_instance.char_width * console_instance.cursor_x;
//        int erase_y = console_instance.char_height * console_instance.cursor_y;
//        // Erase the character at the new cursor position
//        pax_draw_rect(buffer, console_instance.bg,
//                     erase_x, erase_y,
//                     console_instance.char_width, console_instance.char_height);
//    }
//    p++;
//} else {
//    console_put(&console_instance, *p++);
//}

