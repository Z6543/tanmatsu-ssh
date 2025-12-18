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
//static char const CHR_BS[] = "\b";
static char const CHR_BS[] = "\x7f";
static char const CHR_NL[] = "\n";

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

#define BUFFER_SIZE 4096

//static uint8_t       read_buffer[BUFFER_SIZE] = {0};

struct cons_insts_s console_instance;

void ssh_console_write_cb(char* str, size_t len) {
    // NOOP
}

pax_buf_t ssh_bg_pax_buf = {0};

LIBSSH2_KNOWNHOSTS *nh;
static char const KNOWN_HOSTS_FILE[] = "/sd/ssh/known_hosts";


static bool load_ssh_bg(void) {
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
    return true;
}

static void keyboard_backlight(void) {
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

static void display_backlight(void) {
    uint8_t brightness;
    bsp_display_get_backlight_brightness(&brightness);
    brightness += 15;
    if (brightness > 100) {
        brightness = 10;
    }
    ESP_LOGI(TAG, "Display brightness: %u%%\r\n", brightness);
    bsp_display_set_backlight_brightness(brightness);
}

// XXX this is the main function but it's getting a bit unwieldy - consider breaking up into functional parts
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
    int i;
    int known_hosts = 0;
    struct sockaddr_in ssh_addr;
    char ssh_buffer[1024];
    char dialog_buffer[256];
    char ssh_comment[128];
    char ssh_password[128];
    LIBSSH2_SESSION *ssh_session;
    LIBSSH2_CHANNEL *ssh_channel;
    libssh2_socket_t ssh_sock;
    struct libssh2_knownhost *libssh2_knownhost;
    char ssh_out = '\0';
    const char *ssh_hostkey = '\0';
    const char *ssh_hostkey_fingerprint = '\0';
    char ssh_printable_fingerprint[128];
    size_t ssh_hostkey_len;
    int ssh_hostkey_type;
    char *ssh_userauthlist = '\0';
    int cx = 0; // old cursor x position
    int cy = 0; // old cursor y position
    int ocx = 0; // old cursor x position
    int ocy = 0; // old cursor y position
    int check = 0; // host key server check result

    console_init(&console_instance, &con_conf);
    //console_set_colors(&console_instance, CONS_COL_VGA_GREEN, CONS_COL_VGA_BLACK);
    console_instance.fg = 0xff00ff00;
    console_instance.bg = 0xff000000;
    keyboard_backlight();

    //busy_dialog(get_icon(ICON_REPOSITORY), "SSH", "Connecting to WiFi...");
    console_printf(&console_instance, "\nConnecting to WiFi...\n");
    display_blit_buffer(buffer);

    if (!wifi_stack_get_initialized()) {
        ESP_LOGE(TAG, "WiFi stack not initialized");
        message_dialog(get_icon(ICON_REPOSITORY), "SSH: fatal error", "WiFi stack not initialized", "Quit");
        return;
    }

    if (!wifi_connection_is_connected()) {
        if (wifi_connect_try_all() != ESP_OK) {
            ESP_LOGE(TAG, "Not connected to WiFi");
            message_dialog(get_icon(ICON_REPOSITORY), "SSH: fatal error", "Failed to connect to WiFi network", "Quit");
            return;
        }
    }

    //ESP_LOGI(TAG, "initialising libssh2");
    console_printf(&console_instance, "Initialising libssh2...\n");
    display_blit_buffer(buffer);
    rc = libssh2_init(0);
    if (rc) {
        ESP_LOGE(TAG, "libssh2 initialization failed (%d)", rc);
        return;
    }

    ESP_LOGI(TAG, "setting up destination host IP address and port");
    // TODO: check if any changes needed for IPv6 support
    // TODO: check if any changes needed for DNS lookup of hostnames
    inet_pton(AF_INET, settings->dest_host, &ssh_addr.sin_addr);
    ssh_addr.sin_port = htons(atoi(settings->dest_port));
    ssh_addr.sin_family = AF_INET;

    ESP_LOGI(TAG, "creating socket to use for ssh session");
    ssh_sock = socket(AF_INET, SOCK_STREAM, 0);
    /*if (ssh_sock == LIBSSH2_INVALID_SOCKET) {
        ESP_LOGE(TAG, "failed to create socket");
        return;
    }*/

    ESP_LOGI(TAG, "connecting...");
    console_printf(&console_instance, "Connecting...\n");
    display_blit_buffer(buffer);
    if (connect(ssh_sock, (struct sockaddr*)&ssh_addr, sizeof(ssh_addr))) {
        ESP_LOGE(TAG, "failed to connect.");
        return;
    }

    console_printf(&console_instance, "Starting SSH session...\n");
    display_blit_buffer(buffer);
    ESP_LOGI(TAG, "initialising session");
    ssh_session = libssh2_session_init();
    if (!ssh_session) {
        ESP_LOGE(TAG, "could not initialize SSH session");
        return;
    }

    // XXX we can do verbose ssh debugging if needed... 
    //libssh2_trace(ssh_session, ~0);
    // TODO: display server banner?
    // TODO: display the info we're currently logging about connection setup
    // TODO: condense multiple dialogs at connection time down to one

    ESP_LOGI(TAG, "session handshake");
    console_printf(&console_instance, "Session handshake...\n");
    rc = libssh2_session_handshake(ssh_session, ssh_sock);
    if (rc) {
        ESP_LOGE(TAG, "failure establishing SSH session: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "initialising known host database");
    console_printf(&console_instance, "Initialising known host database...\n");
    nh = libssh2_knownhost_init(ssh_session);
    if (!nh) {
        ESP_LOGE(TAG, "failure initialising known hosts database: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "checking to see if we have any saved known hosts");
    console_printf(&console_instance, "Looking for known hosts cache...\n");
    // TODO: check you can copy known_hosts over from another machine using badgelink?
    if (access(KNOWN_HOSTS_FILE, F_OK) == 0) {
	ESP_LOGI(TAG, "found %s file", KNOWN_HOSTS_FILE);
        known_hosts = libssh2_knownhost_readfile(nh, KNOWN_HOSTS_FILE, LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        if (known_hosts < 0) {
            ESP_LOGI(TAG, "error reading saved known host data");
	    // XXX should we quit here? corrupted known_hosts could be a Bad Thing?
	} else {
	    ESP_LOGI(TAG, "read %d known_host entries", known_hosts);
	}
    } else {
        ESP_LOGI(TAG, "couldn't access saved known hosts - but that's OK");
    }

    ESP_LOGI(TAG, "fetching destination host key");
    console_printf(&console_instance, "Fetching host key...\n");
    ssh_hostkey = libssh2_session_hostkey(ssh_session, &ssh_hostkey_len, &ssh_hostkey_type);
    ESP_LOGI(TAG, "remote host key len: %d", (int)ssh_hostkey_len);
    ESP_LOGI(TAG, "remote host key type: %d", ssh_hostkey_type);
    //fprintf(stderr, "ssh_hostkey: ");
    //for (i = 0; i < ssh_hostkey_len; i++) {
    //  fprintf(stderr, "%c", (unsigned char)ssh_hostkey[i]);
    //}
    //fprintf(stderr, "\n");
    //ESP_LOGI(TAG, "ssh_session->server_hostkey: %s", ssh_session->server_hostkey);
    
    switch (ssh_hostkey_type) {
	case LIBSSH2_HOSTKEY_TYPE_RSA:
	    ESP_LOGI(TAG, "  RSA key");
            break;
	case LIBSSH2_HOSTKEY_TYPE_DSS: // deprecated
	    ESP_LOGI(TAG, "  DSS key (deprecated)");
            break;
	case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: // deprecated
	    ESP_LOGI(TAG, "  ECDSA256 key");
            break;
	case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: // deprecated
	    ESP_LOGI(TAG, "  ECDSA384 key");
            break;
	case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: // deprecated
	    ESP_LOGI(TAG, "  ECDSA521 key");
            break;
	case LIBSSH2_HOSTKEY_TYPE_ED25519: // deprecated
	    ESP_LOGI(TAG, "  ED25519 key");
            break;
	case LIBSSH2_HOSTKEY_TYPE_UNKNOWN:
	default:
	    ESP_LOGI(TAG, "  unknown key type");
            break;
    }
  
    // TODO: Display server fingerprint on first connection
    ESP_LOGI(TAG, "calculating host key fingerprint");
    bzero(ssh_printable_fingerprint, sizeof(ssh_printable_fingerprint));
    ssh_hostkey_fingerprint = libssh2_hostkey_hash(ssh_session, LIBSSH2_HOSTKEY_HASH_SHA1);
    char* j = ssh_printable_fingerprint;
    for (i = 0; i < 20; i++) {
        //fprintf(stderr, "%02X ", (unsigned char)ssh_hostkey_fingerprint[i]);
	sprintf(j, "%02X ", (unsigned char)ssh_hostkey_fingerprint[i]);
	j += 3;
    }
    j = '\0';
    ESP_LOGI(TAG, "Host key fingerprint... %s\n", ssh_printable_fingerprint);
    console_printf(&console_instance, "Host key fingerprint... %s\n", ssh_printable_fingerprint);
  
    ESP_LOGI(TAG, "user auth methods check");
    ssh_userauthlist = libssh2_userauth_list(ssh_session, settings->username, (unsigned int)strlen(settings->username));
    ESP_LOGI(TAG, "user auth methods list: %s", ssh_userauthlist);
    console_printf(&console_instance, "Host supports auth methods... %s\n", ssh_userauthlist);
    // TODO: Check list of supported auth methods
    // TODO: UI for user to pick their preferred auth method

    ESP_LOGI(TAG, "checking host key against known hosts data");
    console_printf(&console_instance, "Checking to see if we have seen this host key before...\n");
    ESP_LOGI(TAG, "ssh_hostkey: %s", ssh_hostkey);
    if (ssh_hostkey) {
        //ESP_LOGI(TAG, "remote host key name: %s", ssh_session->hostkey->name);
        check = libssh2_knownhost_checkp(nh, settings->dest_host, atoi(settings->dest_port),
                                              ssh_hostkey, strlen(ssh_hostkey),
                                              LIBSSH2_KNOWNHOST_TYPE_PLAIN|LIBSSH2_KNOWNHOST_KEYENC_RAW|LIBSSH2_KNOWNHOST_KEY_ECDSA_256,
                                              &libssh2_knownhost);
	switch (check) {
            case LIBSSH2_KNOWNHOST_CHECK_MATCH: // hosts and keys match - yay!
	        ESP_LOGI(TAG, "host check successful");
		break;
            case LIBSSH2_KNOWNHOST_CHECK_FAILURE: // something prevented the check being made
	        ESP_LOGI(TAG, "host check failed - something prevented the check being made");
		sprintf(dialog_buffer, "Host check failed\n\nThe host key fingerprint is: %s\n\nWould you like to continue?", ssh_printable_fingerprint);
		break;
	    case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND: // no host match was found
	        ESP_LOGI(TAG, "host check failed - host key not found - but that's OK");
		sprintf(dialog_buffer, "Couldn't find saved host key, looks like this is a new connection.\n\nThe host key fingerprint is: %s\n\nWould you like to continue?", ssh_printable_fingerprint);
		break;
	    case LIBSSH2_KNOWNHOST_CHECK_MISMATCH: // host was found, but the keys did not match
	        ESP_LOGI(TAG, "host check failed - keys did not match");
		sprintf(dialog_buffer, "Host check failed - keys did not match.\n\nThe host key fingerprint is: %s\n\nWould you like to continue?", ssh_printable_fingerprint);
		break;
            default:
	        ESP_LOGI(TAG, "host check failed - unexpected return value %d", check);
		sprintf(dialog_buffer, "Host check failed - unexpected return value %d.\n\nWould you like to continue?", check);
		break;
	}

	if (check != LIBSSH2_KNOWNHOST_CHECK_MATCH) {
	    ESP_LOGI(TAG, "host key not found or has changed - prompting the user and showing them its fingerprint");
            int dialog_rc = adv_dialog_yes_no(get_icon(ICON_REPOSITORY), "SSH server key/fingerprint check", dialog_buffer);
            if (dialog_rc == MSG_DIALOG_RETURN_NO) {
                ESP_LOGI(TAG, "user decided not to carry on with connection after seeing ssh host key fingerprint");
                if (ssh_sock != LIBSSH2_INVALID_SOCKET) {
                     shutdown(ssh_sock, 2);
                     LIBSSH2_SOCKET_CLOSE(ssh_sock);
                 }
                     pax_draw_rect(buffer, 0xffefefef, 0, 0, 800, 480);
                 display_blit_buffer(buffer);
                 libssh2_exit();	
	         return;
	    }
        }

        pax_draw_rect(buffer, 0xff000000, 0, 0, 800, 480);
        display_blit_buffer(buffer);

	//ESP_LOGI(TAG, "trying to add host key to our known_host data");
	//if (atoi(settings->dest_port) == 22) {
	    sprintf(ssh_comment, "%s", settings->dest_host);
	//} else {
	//    sprintf(ssh_comment, "[%s]:%s", settings->dest_host, settings->dest_port);
	//}
	ESP_LOGI(TAG, "ssh_comment: %s", ssh_comment);
	rc = libssh2_knownhost_addc(nh,
                       settings->dest_host, NULL,
                       ssh_hostkey, strlen(ssh_hostkey),
                       ssh_comment, strlen(ssh_comment), // was settings->dest_host, strlen(settings->dest_host),
                       ssh_hostkey_type | LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_BASE64,
                       &libssh2_knownhost);

        //fprintf(stderr, "Host check: %d, key: %s\n", check,
        //        (check <= LIBSSH2_KNOWNHOST_CHECK_MISMATCH) ?
        //        ssh_knownhost->key : "<none>");
        //ESP_LOGI(TAG, "checking host key against known hosts data");
    }

    // TODO: Encrypt host key using random salt
    // TODO: UI for managing cached host keys

    // XXX we currently crash on libssh2_knownhost_writefile(), so commenting out...
    //ESP_LOGI(TAG, "we'll have a go at saving known_hosts since we either have a new host or a changed host key");
    //if (check == (LIBSSH2_KNOWNHOST_CHECK_NOTFOUND|LIBSSH2_KNOWNHOST_CHECK_MISMATCH)) {
    //    ESP_LOGI(TAG, "trying to save known hosts");
    //    //rc = libssh2_knownhost_writefile(nh, "/sd/ssh" "/" "known_hosts", LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    //    rc = libssh2_knownhost_writefile(nh, KNOWN_HOSTS_FILE, LIBSSH2_KNOWNHOST_FILE_OPENSSH);
    //    if (rc != 0) {
    //        ESP_LOGI(TAG, "error saving known hosts");
    //    }
    //}
    
    //ESP_LOGI(TAG, "trying to save known hosts in local storage");
    //if (known_hosts < 1) {
    //    ESP_LOGI(TAG, "there weren't any known hosts, checking whether /int/ssh exists in case we need to create it");
    //    struct stat statstr;
    // 	ESP_LOGI(TAG, "  stat()");
    //	XXX currently this next line crashes the app
    //    if (stat("/int/ssh", &statstr) != 0) {
    //        ESP_LOGI(TAG, "trying to mkdir(/int/ssh) since it doesn't exist");
    //        if (mkdir("/int/ssh", 0755) != 0) { // XXX mode flags on directory creation are probably a) wrong, b) irrelevant on ESP32?
    //	          ESP_LOGI(TAG, "couldn't make /int/ssh directory");
    //        }
    //    }
    //    free(&statstr);
    //} else {
    //    ESP_LOGI(TAG, "we already had a cache of known_hosts in /int/ssh/known_host, trying to rename it");
    //    rc = rename("/int/ssh/known_hosts", "/int/ssh/known_hosts.old");
    //    if (rc != 0) {
    //        ESP_LOGI(TAG, "renaming /int/ssh/known_hosts failed");
    //	  }
    //}

    ESP_LOGI(TAG, "checking to see if we have a saved password as part of this connection");
    if (strlen(settings->password) > 0) {
        ESP_LOGI(TAG, "using saved password");
	strncpy(ssh_password, settings->password, strlen(settings->password));
    } else {
        ESP_LOGI(TAG, "no saved password, so let's prompt the user for one");
        bool accepted  = false;
        memset(ssh_password, 0, sizeof(ssh_password)); // don't display the password
    
        menu_textedit(buffer, theme, "Password", ssh_password, sizeof(settings->password) + sizeof('\0'), true, &accepted);
        if (accepted) {
            ESP_LOGI(TAG, "updated password: <redacted>");
            //ESP_LOGI(TAG, "updated password: %s", ssh_password);
        } 
    }

    ESP_LOGI(TAG, "authenticating to %s:%s as user %s", settings->dest_host, settings->dest_port, settings->username);
    console_printf(&console_instance, "Authenticating to %s:%s as user %s", settings->dest_host, settings->dest_port, settings->username);
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
    console_printf(&console_instance, "Requesting ssh session...\n");
    ssh_channel = libssh2_channel_open_session(ssh_session);
    if (!ssh_channel) {
        ESP_LOGE(TAG, "unable to open a session");
        return;
    }

    //ESP_LOGI(TAG, "sending env variables");
    libssh2_channel_setenv(ssh_channel, "LANG", "en_US.UTF-8");

    ESP_LOGI(TAG, "requesting pty");
    console_printf(&console_instance, "Requesting pseudoterminal for interactive login session...\n");
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

    // TODO: make background image loading into a task, so it doesn't hold everything else up
    // TODO: see if we can find a way to stop background image from scrolling
    // TODO: function key switches between background images?
    ESP_LOGI(TAG, "trying to load background image");
    console_printf(&console_instance, "Looking for background images...\n");
    load_ssh_bg();
    console_clear(&console_instance);
    console_set_cursor(&console_instance, 0, 0);
    pax_draw_image(buffer, &ssh_bg_pax_buf, 0, 0);
    display_blit_buffer(buffer);

    ESP_LOGI(TAG, "ssh setup completed, entering main loop");

    while (1) {
        bsp_input_event_t event;
        if (xQueueReceive(input_event_queue, &event, pdMS_TO_TICKS(10)) == pdTRUE) {
            //ESP_LOGI(TAG, "input received");
            switch (event.type) {
                case INPUT_EVENT_TYPE_KEYBOARD:
		    //ESP_LOGI(TAG, "normal keyboard event received");
		    ssh_out = event.args_keyboard.ascii;
		    if (event.args_keyboard.modifiers & BSP_INPUT_MODIFIER_CTRL) {
			//ESP_LOGI(TAG, "applying CTRL modifier");
                        ssh_out &= 0x1f; // modify the keycode sent to make it a control character
		    }
		    // TODO: Add support for other modifiers where needed, e.g. ALT, FN
                    libssh2_channel_write(ssh_channel, &ssh_out, sizeof(ssh_out));
                    break;
		case INPUT_EVENT_TYPE_NONE:
		    ESP_LOGI(TAG, "input is a non-event");
		    break;
		case INPUT_EVENT_TYPE_ACTION:
		    ESP_LOGI(TAG, "input is an action event");
		    break;
		case INPUT_EVENT_TYPE_SCANCODE:
		    //ESP_LOGI(TAG, "input is a scancode event");
		    break;
                case INPUT_EVENT_TYPE_NAVIGATION:
		    //ESP_LOGI(TAG, "input is a navigation event");
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
				// TODO: ask if they really want to close the connection when they hit F1
				goto shutdown;
                                return;
                            case BSP_INPUT_NAVIGATION_KEY_F2:
				ESP_LOGI(TAG, "keyboard backlight toggle");
				keyboard_backlight();
				break;
                            case BSP_INPUT_NAVIGATION_KEY_F3:
				ESP_LOGI(TAG, "display backlight toggle");
				display_backlight();
				break;
                            case BSP_INPUT_NAVIGATION_KEY_F6:
				ESP_LOGI(TAG, "colour randomiser");
	        		//printf("clearing cursor visual at old cursor position... %d, %d\n", ocx, ocy);
  	        		pax_draw_line(buffer, 0xff000000, ocx, ocy, ocx, ocy + (console_instance.char_height - 1));
    				console_init(&console_instance, &con_conf);
    				console_clear(&console_instance);
                                int randfg = (rand() % 0xffffff) & 0xff000000;
                                int randbg = (rand() % 0xffffff) & 0xff000000;
	                        console_instance.fg = randfg;
	                        console_instance.bg = randbg;
				fprintf(stderr, "fg: %08x, bg: %08x\n", randfg, randbg);
				console_set_cursor(&console_instance, 0, 0);
				cx = cy = ocx = ocy = 0;
  	        		pax_draw_line(buffer, 0xff000000, ocx, ocy, ocx, ocy + (console_instance.char_height - 1));
                                libssh2_channel_write(ssh_channel, CHR_NL, sizeof(CHR_NL));
                                display_blit_buffer(buffer);
				break;
			    case BSP_INPUT_NAVIGATION_KEY_LEFT:
				ESP_LOGI(TAG, "left key pressed");
                                libssh2_channel_write(ssh_channel, CSI_LEFT, sizeof(CSI_LEFT));
				break;
            		    case BSP_INPUT_NAVIGATION_KEY_RIGHT:
				ESP_LOGI(TAG, "right key pressed");
                                libssh2_channel_write(ssh_channel, CSI_RIGHT, sizeof(CSI_RIGHT));
				break;
            		    case BSP_INPUT_NAVIGATION_KEY_UP:
				ESP_LOGI(TAG, "up key pressed");
                                libssh2_channel_write(ssh_channel, CSI_UP, sizeof(CSI_UP));
				break;
            		    case BSP_INPUT_NAVIGATION_KEY_DOWN:
				ESP_LOGI(TAG, "down key pressed");
                                libssh2_channel_write(ssh_channel, CSI_DOWN, sizeof(CSI_DOWN));
				break;
            		    case BSP_INPUT_NAVIGATION_KEY_TAB:
				ESP_LOGI(TAG, "tab key pressed");
                                libssh2_channel_write(ssh_channel, CHR_TAB, sizeof(CHR_TAB));
				break;
			    case BSP_INPUT_NAVIGATION_KEY_VOLUME_UP:
				ESP_LOGI(TAG, "volume up key pressed");
	        		printf("clearing cursor visual at old cursor position... %d, %d\n", ocx, ocy);
  	        		pax_draw_line(buffer, 0xff000000, ocx, ocy, ocx, ocy + (console_instance.char_height - 1));
                                con_conf.font_size_mult += 0.3;
    				console_init(&console_instance, &con_conf);
    				console_clear(&console_instance);
				console_set_cursor(&console_instance, 0, 0);
				cx = cy = ocx = ocy = 0;
  	        		pax_draw_line(buffer, 0xff000000, ocx, ocy, ocx, ocy + (console_instance.char_height - 1));
	                        console_instance.fg = 0xff00ff00;
	                        console_instance.bg = 0x00000000;
                                libssh2_channel_write(ssh_channel, CHR_NL, sizeof(CHR_NL));
                                display_blit_buffer(buffer);
				break;
			    case BSP_INPUT_NAVIGATION_KEY_VOLUME_DOWN:
				ESP_LOGI(TAG, "volume down key pressed");
	        		//printf("clearing cursor visual at old cursor position... %d, %d\n", ocx, ocy);
  	        		pax_draw_line(buffer, 0xff000000, ocx, ocy, ocx, ocy + (console_instance.char_height - 1));
                                con_conf.font_size_mult -= 0.3;
    				console_init(&console_instance, &con_conf);
    				console_clear(&console_instance);
				console_set_cursor(&console_instance, 0, 0);
				cx = cy = ocx = ocy = 0;
  	        		pax_draw_line(buffer, 0xff000000, ocx, ocy, ocx, ocy + (console_instance.char_height - 1));
	                        console_instance.fg = 0xff00ff00;
	                        console_instance.bg = 0x00000000;
                                libssh2_channel_write(ssh_channel, CHR_NL, sizeof(CHR_NL));
                                display_blit_buffer(buffer);
				break;
            		    case BSP_INPUT_NAVIGATION_KEY_BACKSPACE:
				ESP_LOGI(TAG, "backspace key pressed");
                                libssh2_channel_write(ssh_channel, CHR_BS, sizeof(CHR_BS));
                                break;
                            case BSP_INPUT_NAVIGATION_KEY_RETURN:
				ESP_LOGI(TAG, "return key pressed");
	        		//printf("clearing cursor visual at old cursor position... %d, %d\n", ocx, ocy);
  	        		pax_draw_line(buffer, 0xff000000, ocx, ocy, ocx, ocy + (console_instance.char_height - 1));
    				//ESP_LOGI(TAG, "redrawing background image");
				// XXX this is too slow to do every time return is pressed
				//pax_draw_image(buffer, &ssh_bg_pax_buf, 0, 0);
                                libssh2_channel_write(ssh_channel, CHR_NL, sizeof(CHR_NL));
                                break;
			    // TODO: handle control key combinations
			    // TODO: improve escape character processing so we can use vi, emacs etc
			    // TODO: light/dark mode - maybe use a function key to toggle through several presets?
                            // TODO: font size +/-
                            // TODO: stretch goal: themes - fg/bg colours, fonts, text size
                            // TODO: connect/disconnect cleanly - code can be cribbed from libssh2 ssh2_exec demo
                            // TODO: change wifi network? or maybe tie wifi network to ssh connection details
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
	    goto shutdown;
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
	    console_puts(&console_instance, ssh_buffer);
	    // Draw the cursor as a thin vertical line
	    // TODO: Other cursor styles e.g. block, blinking block, blinking line
	    // TODO: Abstract the logic for figuring out the x, y pixel coordinates of the cursor
	    cx = console_instance.char_width * console_instance.cursor_x;
	    cy = console_instance.char_height * console_instance.cursor_y;
	    printf("cursor position... %d, %d\n", console_instance.cursor_x, console_instance.cursor_y);
	    if (ocx != 0 && ocy != 0) {
		// clear old cursor by writing a new line in black
		// TODO: Change cursor clear vertical line draw to use background colour
	        printf("clearing cursor visual at old cursor position... %d, %d\n", ocx, ocy);
  	        pax_draw_line(buffer, 0xff000000, ocx, ocy, ocx, ocy + (console_instance.char_height - 1));
	    }
	    // save the current cursor position as ocx, ocy so we can refer to it later
	    ocx = cx;
	    ocy = cy;
	    // draw cursor as a light grey vertical line
	    // TODO: Change cursor vertical line draw to use foreground/text colour
	    printf("drawing cursor visual at cursor position %d, %d\n", cx, cy);
  	    pax_draw_line(buffer, 0xefefefef, cx, cy, cx, cy + (console_instance.char_height - 1));
	    console_instance.fg = 0xff00ff00;
	    console_instance.bg = 0x00000000;
	    // dump the contents of the ssh_buffer so we can see what escape codes etc are being sent
	    //for (int pos = 0; pos < nbytes; pos++) {
	    //    printf("%0x ", ssh_buffer[pos]);
            //}
	    display_blit_buffer(buffer);
	}
    }
 
    // closing the ssh connection and freeing resources
    // could be due to user action, or an error
 shutdown:
    ESP_LOGI(TAG, "in shutdown, clearing the screen...");
    pax_draw_rect(buffer, 0xffefefef, 0, 0, 800, 480);
    display_blit_buffer(buffer);
    ESP_LOGI(TAG, "freeing memory...");
    libssh2_channel_send_eof(ssh_channel);
    libssh2_channel_close(ssh_channel);
    libssh2_session_disconnect(ssh_session, "User closed session");
    libssh2_channel_free(ssh_channel);
    libssh2_knownhost_free(nh);
    if (ssh_sock != LIBSSH2_INVALID_SOCKET) {
        shutdown(ssh_sock, 2);
        LIBSSH2_SOCKET_CLOSE(ssh_sock);
    }
    libssh2_exit();	
}
