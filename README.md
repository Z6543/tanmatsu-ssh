# Tanmatsu ssh client

## But what does that mean?

This is an `ssh` client for the Tanmatsu / Konsool device, using the [ESP-IDF component wrapper for libssh2](https://components.espressif.com/components/skuodi/libssh2_esp/) and the [Badge.Team](https://badge.team) software stack. If you don't know what any of this means, don't worry, but this repo probably isn't for you. It was originally a hacked version of the Tanmatsu launcher, but is now a standalone app in its own right.

## It want it! How do I get it?

Right now you have to build it from source code - see below for further info. When it has evolved a bit more it will be submitted to the Tanmatsu app repository.

## How does it work?

~~When your Tanmatsu boots up you will see that there is a new home screen icon labelled SSH, and a new entry for SSH in the settings menu.~~

After installing the app, you should find that you have an extra entry in your Apps directory called `SSH`. When you launch it, you'll be prompted with a list of the `ssh` servers that the app knows about - initially this will be empty, but there is a GUI that should let you add server details.

When you have at least one server configured, you can press `ENTER` to start an `ssh` connection to the selected server. This spawns a full screen terminal emulation and manages the `ssh` session.

During the `ssh` session there are some useful things you can do with the Tanmatsu function keys:

- **X** - close the session, right now we do this without asking you if you're sure, but that will probably become a setting
- **Triangle** - adjust the keyboard backlight, keep pressing until you get the brightness level you want
- **Square** - adjust the screen backlight, keep pressing until you get the brightness level you want
- **Vol +/-** - make the terminal font larger (+) or smaller (-)

In the future there will probably be an option in the settings menu to set default font, font size, font colour, keyboard and screen backlight preferences, username etc. We don't support public key authentication yet, this will probably make an appearance too at some point.

We should cache `ssh` server host keys (in `/int/ssh/known_hosts`) and warn you when you are connecting to a new server or one whose host key has changed - the app should prompt you about this and show you the server's key fingerprint. However right now this isn't working - check back later! Also, we don't currently encrypt the known_hosts data, but that will likely change.

Just for fun, if you have some 800x480 PNG files on your SD card in `/sd/bg`, the app will load a randomly chosen background as the session starts up. The files should be numbered `00.png`, `01.png`, `02,png` and so on. This is quite resource intensive for the Tanmatsu, but the results look fantastic! In the future we'll probably move the background image rendering to a dedicated task so that it doesn't slow down session establishment. Also right now the background image will scroll off the screen when you get to the bottom, so it should probably be thought of as a "splash" image rather than a true background. That might change...

## Big Scary Warning

This software should be treated as a minimally functioning proof-of-concept. Right now it lacks some key features that you would want in order to be comfortable using it for real work or anything important etc - see TODO list below. Even once the app is feature complete, you should still think very carefully about which systems and accounts you use with it, just like any other software you might trust with sensitive data like passwords and encryption keys.

In particular, be aware that **your ssh connection settings are stored in the Tanmatsu configuration, including any passwords that you choose to save**. All the code you run on your Tanmatsu (_like apps written by other people_) will have access to this info. In the future we will probably encrypt these details and prompt you to unlock them as part of starting up the SSH app, but right now we don't.

OK, that's enough doom and gloom. On with the show...

## TODO

- [ ] Improve ANSI escape character handling so we can run things like vi, emacs, btop
- [ ] Add keyboard handling for other modifiers e.g. ALT, ALTGR, FN
- [ ] Fix it so the background image (if present) loads quicker, probably as a task
- [ ] Fix it so the background image (if present) doesn't scroll off the screen
- [ ] Consider separating the keyboard input processing and server output processing from the main loop into dedicated tasks
- [ ] Consider having two modes for the terminal emulator - full screen (as at present) and another which shows the status and navigation bars
- [ ] Figure out why a connection added via `badgelink` isn't visible in the connection list
- [ ] More checks and balances - right now it's still a bit too easy to crash, check for any obvious stuff the compiler didn't catch like use-after-free, use of uninitialised variables etc
- [ ] Display meaningful error messages to the user when something goes wrong - don't just log it
- [ ] Support keyboard_interactive auth - or at least prompt the user if they don't have a saved password?
- [ ] Support public key auth - `libssh2` has some useful convenience functions, will need to prompt the user for their passphrase or use the stored "password" for the connection
- [ ] Settings UI for managing cached public keys
- [ ] Settings UI for managing cached host keys
- [ ] Settings UI for preferences (colours, fonts etc) - right now we only have connection settings
- [ ] Maybe let user set terminal type (xterm-color etc) as part of settings UI
- [ ] Light/Dark mode - setting option, and/or function key to toggle between light/dark or through several presets?
- [ ] Tidy up unused code carried over from Tanmatsu launcher, Nicolai wifi manager, libssh2 examples
- [ ] Modularise the sprawling main function, minimise global variables etc
- [ ] Fix duplicate symbols flagged at build time e.g. "ESP_WIFI_TX_BUFFER defined in multiple locations"
- [ ] Check that we haven't reinvented the wheel and created a new function which duplicates something from our dependencies
- [ ] Check we are using up to date versions of dependencies and bump managed component versions / merge in changes as needed
- [ ] Replace "magic numbers" with sensible values where possible e.g. `PATH_MAX` for variables holding file paths
- [ ] Write PRs for any changes to dependencies, e.g. escape code handling in the Badge Team terminal emulator

### Stretch Goals

- [ ] Encryption at rest for secrets saved in Tanmatsu system config - prompt the user for a passphrase to unlock
- [ ] Settings UI for user to pick their preferred / permitted encryption algorithms
- [ ] Support for `ssh-agent` - but may not be particularly meaningful as a concept until/unless Tanmatsu is multi-tasking
- [x] Decouple from the launcher and make into an app in its own right
- [ ] Support for captive portals - coffee shop wifi, trains, hotels etc
- [ ] Support for USB networking - e.g. tether to laptop and use its Internet connection
- [ ] Support for system level themes - fg/bg colours, fonts, cursor types, text size etc. Tanmatsu codebase has hooks for this, but right now there is only the default theme and no settings UI
- [ ] Port to other Badge Team devices like the MCH badge, or at least make it a bit more inherently portable
- [ ] Internationalisation - add message catalogue so dialogs etc can ported to other languages
- [ ] Accessibility - add support for basic audio feedback (beeps/WAVs) for system events and some keyclicks via the Tanmatsu speaker
- [ ] Accessibility - add spoken version of prompts, dialogs etc using the Tanmatsu speaker (could be WAV files?)
- [ ] Accessibility - add support for screen reader type accessibility, cf. Speakup, ESpeakup, Speechdup, Fenrir etc

### Testing

- [x] Test with IPv4 hosts by IP address
- [ ] Test with IPv6 hosts by IP address
- [ ] Test with hosts by DNS entry
- [ ] Test with a variety of `ssh` implementations and versions - different encryption algorithms, key types etc
- [ ] Test TUI apps with TERM xterm-color, xterm-256color etc, once fancy login session working
- [ ] Check we are freeing memory etc on the Tanmatsu when no longer required
- [ ] Check that we haven't inadvertently committed Licensing Crimes by mixing and matching code from different sources

### Not Doing This

This app is all about interactive remote login, but you can do other stuff with `ssh` like `scp` and `sftp` file transfer and batch mode remote command execution. This app isn't doing that stuff, at least for now. It would probably be better to have dedicated apps for these other functions. And some sort of shell / REPL for that matter...

## Building

You should be able to build it in the same way as the regular launcher, and it should pull in the `skuodi/libssh2_esp` component at build time. I've had good results using ESP-IDF 5.5 and Python 3.12.7 in case this is relevant.

## Code contributions

If you make any fixes or improvements, please do raise a PR. Let's make this a really great app together!

Also the vibe is very much "good vibes" more than vibe coding, so don't be too despondent if your epic 10,000 line LLM generated patch gets rejected ;-)

## Terminal icon

You'll need to copy the new ICON_TERMINAL image to your FAT filesystem, e.g. using `badgelink`:

```
python3 ./badgelink.py fs upload /int/icons/menu/terminal.png ~/src/tanmatsu-ssh/fat/tanmatsu/icons/menu/terminal.png
```

The terminal icon is from the MIT Licensed [Tabler Icons](https://github.com/tabler/tabler-icons) collection. This is an SVG which has been saved as a 32x32 pixel PNG. If you want to use your own choice of icon you might need to do some massaging, e.g.

```
convert ~/Downloads/terminal.png -resize 32x32\! -depth 8 -type TrueColor ~/src/tanmatsu-ssh/fat/tanmatsu/icons/menu/terminal.png
```

## SSH session configuration and testing

It's a bit of a pain typing in system connection details using the Tanmatsu keyboard, but you can always use `badgelink` and save yourself some trouble:

```
python3 ./badgelink.py nvs write ssh s00.conn_name str test-server
python3 ./badgelink.py nvs write ssh s00.dest_host str 10.0.0.1
python3 ./badgelink.py nvs write ssh s00.dest_port str 2025
python3 ./badgelink.py nvs write ssh s00.username str tanmatsu-test
python3 ./badgelink.py nvs write ssh s00.password str lol-jk-etc
```

(however new entries that are added entirely via `badgelink` don't seem to show up - so you might find you need to add a new connection using the default values and then change the details via `badgelink`)

You don't have to use your real credentials, ssh server etc. Maybe consider running an `sshd` on a non-standard port, on a sacrificial VM, creating a test user rather than using your real credentials. Something like this can be handy:

```
sshd -f sshd_config -p 2025
```

One thing that might be useful if you are out and about - you can test on Android by installing `openssh` under Termux, setting a password for the Termux user, creating a wifi hotspot for your Tanmatsu to connect to, and running `sshd` as per above.

# Original Tanmatsu README

[![Build](https://github.com/Nicolai-Electronics/tanmatsu-launcher/actions/workflows/build.yml/badge.svg)](https://github.com/Nicolai-Electronics/tanmatsu-launcher/actions/workflows/build.yml)

A launcher firmware for ESP32 based devices which allows users to configure WiFi, browse apps from an online repository and download and run apps on their devices.

This application supports the following boards:

 - Tanmatsu (including Konsool and Hackerhotel 2026 variants)
 - MCH2022 badge
 - Hackerhotel 2024 badge
 - ESP32-P4 function EV board


## License

This project is made available under the terms of the [MIT license](LICENSE).
