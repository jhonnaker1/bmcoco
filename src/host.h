/*
 * host.h - services the Circle kernel provides to the emulator.
 *
 * The emulator (XRoar and the port in src/port/) is C and runs on core 1.
 * Everything it needs from the hardware goes through these functions, which
 * are implemented in src/kernel.cpp.
 *
 * This file is part of BMCoco, a bare metal Tandy Color Computer emulator for the
 * Raspberry Pi. See LICENSE for licensing details.
 */

#ifndef BMCOCO_HOST_H_
#define BMCOCO_HOST_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Options (cmdline.txt) ---------------------------------------------- */

const char *host_option(const char *name, const char *default_value);
int host_option_int(const char *name, int default_value);
double host_option_double(const char *name, double default_value);
/* Changes a setting, which then wins over cmdline.txt; host_options_save()
   writes the settings to bmcoco.cfg on the SD card. Both return 0 on
   failure. */
int host_option_set(const char *name, const char *value);
int host_options_save(void);

/* ---- Time ---------------------------------------------------------------- */

/* Seconds since boot. */
double host_time(void);
void host_sleep_us(unsigned us);
/* Lets Circle's tasks (VCHIQ, USB) run. Call regularly. */
void host_yield(void);

/* ---- Video --------------------------------------------------------------- */

/* Allocates the emulator's RGB565 frame buffer. PITCH is in bytes. */
int host_video_init(int width, int height, uint16_t **pixels, int *pitch);
/* Area of the frame buffer that is shown, and its display aspect ratio
   (width / height). */
void host_video_set_view(int x, int y, int w, int h, double aspect);
/* Shows the frame buffer. With WAIT_VSYNC, returns after the next vertical
   blank, which paces the emulation to the display. */
void host_video_present(int wait_vsync);

/* ---- Sound --------------------------------------------------------------- */

/* Opens audio output; returns the sample rate in use (0 on failure). */
unsigned host_sound_open(unsigned freq, int channels, unsigned latency_frames);
void host_sound_close(void);
/* Frames that can be written without exceeding the latency. */
unsigned host_sound_free_frames(void);
void host_sound_write(const int16_t *samples, unsigned frames);

/* ---- Input --------------------------------------------------------------- */

/* Pops the next keyboard event: USB HID usage code and pressed (1) or
   released (0). Returns 0 when there are no more events. */
int host_key_event(int *usage, int *pressed);
/* Is the key with this USB HID usage code down? */
int host_key_down(int usage);
/* USB HID modifier byte (bit 0 left ctrl ... bit 7 right GUI). */
unsigned host_key_modifiers(void);

#define HOST_JOY_UP     0x01
#define HOST_JOY_DOWN   0x02
#define HOST_JOY_LEFT   0x04
#define HOST_JOY_RIGHT  0x08
#define HOST_JOY_FIRE   0x10

#define HOST_PADS 4

/* Gamepad buttons, in a layout common to all pads. */
#define HOST_PAD_A       0x0001  /* bottom face button */
#define HOST_PAD_B       0x0002  /* right face button */
#define HOST_PAD_X       0x0004  /* left face button */
#define HOST_PAD_Y       0x0008  /* top face button */
#define HOST_PAD_L       0x0010
#define HOST_PAD_R       0x0020
#define HOST_PAD_SELECT  0x0040
#define HOST_PAD_START   0x0080
#define HOST_PAD_HOME    0x0100
#define HOST_PAD_UP      0x1000
#define HOST_PAD_DOWN    0x2000
#define HOST_PAD_LEFT    0x4000
#define HOST_PAD_RIGHT   0x8000

/* State of USB gamepad PAD (0..HOST_PADS-1) as HOST_PAD_* bits, or -1 if no
   pad is connected there. */
int host_pad_state(int pad);

/* Position of the left analog stick of USB gamepad PAD, AXIS 0 (x) or 1 (y),
   from 0 (left/up) to 65535 (right/down), or -1 if the pad has no analog
   stick (or none is connected there). */
int host_pad_axis(int pad, int axis);

/* USB mice: how far they moved since last asked, and the buttons held
   (bit 0 left, 1 right, 2 middle). Returns 0 if there is no mouse. */
int host_mouse_read(int *dx, int *dy, int *buttons);

/* State of the joystick wired to GPIO port PORT (0 or 1), as HOST_JOY_* bits.
   Always 0 when GPIO joysticks are disabled. */
int host_gpio_joystick(int port);

/* ---- System -------------------------------------------------------------- */

void host_reboot(void);
void host_power_off(void);
/* The Pi's IP address as text, when the network is up. Returns 0 if not. */
int host_network_address(char *address, unsigned size);
/* Network state for people: "Wi-Fi 192.168.1.50", "Off", "Ethernet:
   waiting for address"... */
void host_network_status(char *text, unsigned size);
/* A Wi-Fi network found by host_wifi_scan(). signal is in dBm. */
struct host_wifi_ap {
	char ssid[33];
	int signal;
	int secure;
};
/* Scans for Wi-Fi networks for a few seconds. Returns how many it found
   (up to MAX), or -1 if Wi-Fi isn't running (it starts only when it is the
   selected network). */
int host_wifi_scan(struct host_wifi_ap *aps, int max);
/* "Raspberry Pi 3 Model B+" */
const char *host_model_name(void);
/* The display's size in pixels. */
void host_display_size(int *width, int *height);
/* Asks for the display to refresh at FPS frames a second, where the host
   can set the video mode (the Pi 4 with video_driver=kms). Returns TRUE if
   the mode was changed; the refresh rate should then be measured again. */
int host_display_match_rate(double fps);
/* Writes a line of text to the log. */
void host_log(const char *message);

/* Where the log goes (enable_logging=off|uart|file). */
#define HOST_LOG_OFF  0
#define HOST_LOG_UART 1  /* serial port, GPIO14/15 at 115200 */
#define HOST_LOG_FILE 2  /* /bmcoco.log on the SD card */
int host_log_destination(void);
/* Changes it at once. Returns 0 if the log file cannot be opened. */
int host_set_log_destination(int destination);

#ifdef __cplusplus
}
#endif

#endif /* BMCOCO_HOST_H_ */
