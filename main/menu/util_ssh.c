//
// Derived from badgeteam/terminal-emulator, libssh2 example code, nicolaielectronics/tanmatsu-launcher
//
#include <string.h>
#include <sys/_intsup.h>
#include <time.h>
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
#include "menu/message_dialog.h"
#include "pax_types.h"
#include "tanmatsu_coprocessor.h"
#include <libssh2.h>
#include "libssh2_setup.h"
#include "lwip/sockets.h"
#include "util_ssh.h"
#include "settings_ssh.h"

static char const TAG[] = "util_ssh";

#if defined(CONFIG_BSP_TARGET_TANMATSU) || defined(CONFIG_BSP_TARGET_KONSOOL) || \
    defined(CONFIG_BSP_TARGET_HACKERHOTEL_2026)
#define FOOTER_LEFT  ((gui_element_icontext_t[]){{get_icon(ICON_F5), "Settings"}, {get_icon(ICON_F6), "USB mode"}}), 2
#define FOOTER_RIGHT ((gui_element_icontext_t[]){{NULL, "↑ / ↓ / ← / → | ⏎ Select"}}), 1
#elif defined(CONFIG_BSP_TARGET_MCH2022)
#define FOOTER_LEFT  NULL, 0
#define FOOTER_RIGHT ((gui_element_icontext_t[]){{NULL, "🅰 Select"}}), 1
#else
#define FOOTER_LEFT  NULL, 0
#define FOOTER_RIGHT NULL, 0
#endif

#define TERMINAL_UART 0

#define BUFFER_SIZE 4096

//static uint8_t       read_buffer[BUFFER_SIZE] = {0};

struct cons_insts_s console_instance;

//// this function isn't used in terminal.c, so maybe not needed
//static void render(pax_buf_t* buffer, gui_theme_t* theme, bool partial, bool icons) {
//    if (!partial || icons) {
//        render_base_screen_statusbar(buffer, theme, !partial, !partial || icons, !partial,
//                                     ((gui_element_icontext_t[]){{get_icon(ICON_HOME), "Home"}}), 1, FOOTER_LEFT,
//                                     FOOTER_RIGHT);
//    }
//    display_blit_buffer(buffer);
//}

void ssh_console_write_cb(char* str, size_t len) {
    // the code in this function is commented out in terminal.c, so maybe not needed...
    //for (int pos = 0; pos < len; pos++) {
    //    console_put(&console_instance, str[pos]);
    //    putc(str[pos], stdout);
    //}
    //display_blit_buffer(buffer);
}

static void keyboard_backlight(void) {
    uint8_t brightness;
    bsp_input_get_backlight_brightness(&brightness);
    if (brightness != 100) {
        brightness = 100;
    } else {
        brightness = 0;
    }
    printf("Keyboard brightness: %u%%\r\n", brightness);
    bsp_input_set_backlight_brightness(brightness);
}

static void display_backlight(void) {
    uint8_t brightness;
    bsp_display_get_backlight_brightness(&brightness);
    brightness += 15;
    if (brightness > 100) {
        brightness = 10;
    }
    printf("Display brightness: %u%%\r\n", brightness);
    bsp_display_set_backlight_brightness(brightness);
}

void util_ssh(pax_buf_t* buffer, gui_theme_t* theme, ssh_settings_t* settings) {
    QueueHandle_t input_event_queue = NULL;
    ESP_ERROR_CHECK(bsp_input_get_queue(&input_event_queue));

    struct cons_config_s con_conf = {
        .font = pax_font_sky_mono, 
	.font_size_mult = 1.5, 
	.paxbuf = display_get_buffer(), 
	.output_cb = ssh_console_write_cb
    };

    ssize_t nbytes; // bytes read from ssh server
    int rc; // return code from libssh2 library calls
    struct sockaddr_in ssh_addr;
    char ssh_buffer[1024];
    LIBSSH2_SESSION *ssh_session;
    LIBSSH2_CHANNEL *ssh_channel;
    libssh2_socket_t ssh_sock;
    char ssh_out;
    //const char *fingerprint;
    //char *userauthlist;

    console_init(&console_instance, &con_conf);
    //console_set_colors(&console_instance, CONS_COL_VGA_GREEN, CONS_COL_VGA_BLACK);
    keyboard_backlight();

    ESP_LOGI(TAG, "initialising libssh2");
    rc = libssh2_init(0);
    if (rc) {
        ESP_LOGE(TAG, "libssh2 initialization failed (%d)", rc);
        return;
    }

    ESP_LOGI(TAG, "setting up destination host IP address and port");
    // TODO: IPv6 support
    // TODO: DNS lookup hostname
    inet_pton(AF_INET, settings->dest_host, &ssh_addr.sin_addr);
    // TODO: Allow user to choose custom port number
    ssh_addr.sin_port = htons(atoi(settings->dest_port));
    ssh_addr.sin_family = AF_INET;

    ESP_LOGI(TAG, "creating socket to use for ssh session");
    ssh_sock = socket(AF_INET, SOCK_STREAM, 0);
    /*if (ssh_sock == LIBSSH2_INVALID_SOCKET) {
        ESP_LOGE(TAG, "failed to create socket");
        return;
    }*/

    ESP_LOGI(TAG, "connecting...");
    if (connect(ssh_sock, (struct sockaddr*)&ssh_addr, sizeof(ssh_addr))) {
        ESP_LOGE(TAG, "failed to connect.");
        return;
    }

    ESP_LOGI(TAG, "initialising session");
    ssh_session = libssh2_session_init();
    if (!ssh_session) {
        ESP_LOGE(TAG, "could not initialize SSH session");
        return;
    }

    // XXX we can do verbose ssh debugging if needed... libssh2_trace(session, ~0);

    ESP_LOGI(TAG, "session handshake");
    rc = libssh2_session_handshake(ssh_session, ssh_sock);
    if(rc) {
        ESP_LOGE(TAG, "failure establishing SSH session: %d", rc);
        return;
    }

    //ESP_LOGI(TAG, "fingerprint check");
    //ssh_fingerprint = libssh2_hostkey_hash(ssh_session, LIBSSH2_HOSTKEY_HASH_SHA1);
    // TODO: Display server fingerprint on first connection
    // TODO: Cache server public key
    // TODO: Check cached public key and warn if there is a mismatch
    // TODO: UI for managing cached public keys

    //ESP_LOGI(TAG, "user auth methods check");
    //userauthlist = libssh2_userauth_list(ssh_session, ssh_username, (unsigned int)strlen(ssh_username));
    // TODO: Check list of supported auth methods
    // TODO: UI for user to pick their preferred auth method

    ESP_LOGI(TAG, "authenticating to %s:%d as user %s", settings->dest_host, settings->dest_port, settings->username);
    if (libssh2_userauth_password(ssh_session, settings->username, settings->password)) {
        ESP_LOGE(TAG, "authentication by password failed");
        return;
    }
    ESP_LOGE(TAG, "authentication by password succeeded");

    // TODO: Support keyboard_interactive auth
    // TODO: Support public key auth
    // TODO: Support agent auth
    //if (libssh2_userauth_keyboard_interactive(ssh_session, ssh_username, &kbd_callback) ) {
    //if (libssh2_userauth_publickey_fromfile(ssh_session, ssh_username, ssh_fn1, ssh_fn2, ssh_password)) {

    ESP_LOGI(TAG, "requesting session");
    ssh_channel = libssh2_channel_open_session(ssh_session);
    if (!ssh_channel) {
        ESP_LOGE(TAG, "unable to open a session");
        return;
    }

    ////ESP_LOGI(TAG, "sending env variables");
    // libssh2_channel_setenv(ssh_channel, "FOO", "BAR");

    ESP_LOGI(TAG, "requesting pty");
    // TODO: Let user set terminal type?
    // TODO: Test with TERM xterm-color etc
    if (libssh2_channel_request_pty(ssh_channel, "xterm-256color")) {
        ESP_LOGE(TAG, "failed requesting pty");
        return;
    }

    // TODO: check whether libssh2_channel_shell is required
    if (libssh2_channel_shell(ssh_channel)) {
        ESP_LOGE(TAG, "failed requesting shell");
        return;
    }

    ESP_LOGI(TAG, "making the channel non-blocking");
    libssh2_channel_set_blocking(ssh_channel, 0);

    ESP_LOGI(TAG, "ssh setup completed, entering main loop");
    // failed attempt to make cursor visible...
    //ssh_out = '\e';
    //libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
    //ssh_out = '[';
    //libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
    //ssh_out = '?';
    //libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
    //ssh_out = '2';
    //libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
    //ssh_out = '5';
    //libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
    //ssh_out = 'h';
    //libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));

    while (1) {
        bsp_input_event_t event;
        if (xQueueReceive(input_event_queue, &event, pdMS_TO_TICKS(10)) == pdTRUE) {
            //ESP_LOGI(TAG, "input received");
            switch (event.type) {
                case INPUT_EVENT_TYPE_KEYBOARD:
		    //ESP_LOGI(TAG, "normal keyboard event received");
		    ssh_out = event.args_keyboard.ascii; // XXX we should probably be using event.args_keyboard.utf8
                    libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
                    break;
		case INPUT_EVENT_TYPE_NONE:
		    ESP_LOGI(TAG, "input is a non-event");
		    break;
		case INPUT_EVENT_TYPE_ACTION:
		    ESP_LOGI(TAG, "input is an action event");
		    break;
		case INPUT_EVENT_TYPE_SCANCODE:
		    ESP_LOGI(TAG, "input is a scancode event");
		    break;
                case INPUT_EVENT_TYPE_NAVIGATION:
		    ESP_LOGI(TAG, "input is a navigation event");
                    if (event.args_navigation.state) {
		        //ESP_LOGI(TAG, "checking to see which navigation key/button has been pressed");
                        switch (event.args_navigation.key) {
                            case BSP_INPUT_NAVIGATION_KEY_ESC:
				ESP_LOGI(TAG, "esc key pressed");
				ssh_out = '\e';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
                                break;
                            case BSP_INPUT_NAVIGATION_KEY_F1:
				ESP_LOGI(TAG, "close key pressed - returning to app launcher");
                                return;
                            case BSP_INPUT_NAVIGATION_KEY_F2:
				ESP_LOGI(TAG, "keyboard backlight toggle");
				keyboard_backlight();
				break;
                            case BSP_INPUT_NAVIGATION_KEY_F3:
				ESP_LOGI(TAG, "display backlight toggle");
				display_backlight();
				break;
			    case BSP_INPUT_NAVIGATION_KEY_LEFT:
				ESP_LOGI(TAG, "left key pressed");
				ssh_out = '\e';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
				ssh_out = '[';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
				ssh_out = 'D';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
				break;
            		    case BSP_INPUT_NAVIGATION_KEY_RIGHT:
				ESP_LOGI(TAG, "right key pressed");
				ssh_out = '\e';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
				ssh_out = '[';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
				ssh_out = 'C';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
				break;
            		    case BSP_INPUT_NAVIGATION_KEY_UP:
				ESP_LOGI(TAG, "up key pressed");
				ssh_out = '\e';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
				ssh_out = '[';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
				ssh_out = 'A';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
				break;
            		    case BSP_INPUT_NAVIGATION_KEY_DOWN:
				ESP_LOGI(TAG, "down key pressed");
				ssh_out = '\e';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
				ssh_out = '[';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
				ssh_out = 'B';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
				break;
            		    case BSP_INPUT_NAVIGATION_KEY_TAB:
				ESP_LOGI(TAG, "tab key pressed");
				ssh_out = '\t';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
				break;
            		    case BSP_INPUT_NAVIGATION_KEY_BACKSPACE:
				ESP_LOGI(TAG, "backspace key pressed");
				ssh_out = '\b';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
                                break;
                            case BSP_INPUT_NAVIGATION_KEY_RETURN:
				ESP_LOGI(TAG, "return key pressed");
				ssh_out = '\n';
                                libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
                                break;
			    // TODO: figure out why shell prompt is shown twice
			    // TODO: make cursor visible
			    // TODO: improve escape character processing so we can use vi, emacs etc
			    // TODO: light/dark mode - maybe use a function key to toggle through several presets?
                            // TODO: font size +/-
                            // TODO: stretch goal: themes - fg/bg colours, fonts, text size
                            // TODO: connect/disconnect cleanly?
                            // TODO: change wifi network?
                            default:
				ESP_LOGI(TAG, "some other navigation key has been pressed");
                                break;
                        }
			default:
			    break;
		    }
		case INPUT_EVENT_TYPE_LAST:
		    break;
	    }
        }

	//ESP_LOGI(TAG, "check for server EOF");
        if (libssh2_channel_eof(ssh_channel)) {
            ESP_LOGI(TAG, "server sent EOF");
            break;
        }

        //ESP_LOGI(TAG, "read any data sent by server");
        bzero(ssh_buffer, sizeof(ssh_buffer));
        nbytes = libssh2_channel_read(ssh_channel, ssh_buffer, sizeof(ssh_buffer));
        //if (nbytes < 0) {
        //    ESP_LOGE(TAG, "unable to read response");
        //    break;
        //}

	//ESP_LOGI(TAG, "display data sent by server");
	if (nbytes > 0) {
            for (int pos = 0; pos < nbytes; pos++) {
                //putc(ssh_buffer[pos], stdout);
                console_put(&console_instance, ssh_buffer[pos]);
                // TODO: error check for console_put?
            }
            // TODO: error check for display_blit_buffer?
            display_blit_buffer(buffer);
	}
    }
}
