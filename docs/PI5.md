# Raspberry Pi 5, Pi 500 and Pi 500+

BMCoco runs on the Pi 5, Pi 500 and Pi 500+ as a 64-bit kernel,
`kernel_2712.img`. It shares the Pi 4's code (`src/av/`, `src/v3d/` and `src/kms/common/`, see
[PI4.md](PI4.md)) wherever the hardware is the same; the emulator, menus,
USB keyboards and gamepads, networking (Ethernet and Wi-Fi) and the web UI
work as on the other boards.

The Pi 5's firmware is in its EEPROM, so the card only adds the Pi 5's
device trees, `overlays/bcm2712d0.dtbo`, and the Wi-Fi firmware under the
name Circle looks for on the Pi 5
(`firmware/brcmfmac43455-sdio.raspberrypi,5-model-b.*`).

## Picture

BMCoco shows the picture through the Pi 5's HVS with its own display
driver (`src/kms/pi5/`, from BMA800XL 1.3.0), and runs the display at the
CoCo's frame rate, as
on the Pi 4 (see `video_driver` in [PI4.md](PI4.md)). The Pi 5's firmware
ignores `hdmi_mode` and picks the display's own preferred resolution, so the
*Resolution* setting (Video menu, or `resolution` in `cmdline.txt`) chooses
it instead: *Auto* (the firmware's), 1920x1080 or 1280x720. Sharp pixels
fill more of a 720p screen. `video_driver=firmware` uses the firmware's
framebuffer instead, and it is the fall back if the driver can't take over.
The CRT shader, and smooth scaling on the framebuffer, are drawn by the Pi 5's 3D GPU (V3D 7.1), with
the same options as on the Pi 4 (`crt_gpu`, `crt_gpu_test`); if the GPU
doesn't start, the ARM cores draw them.

The firmware's framebuffer on the Pi 5 is 32-bit (`framebuffer_depth` in
`config.txt`) and has a single page, so with `video_driver=firmware` fast
movement can tear.

## Sound

Sound goes out over HDMI, on the port the display is plugged into (HDMI0
if both are). The Pi 5 and Pi 500 have no headphone jack. The sound
runs about 24 ms later than on the Pi 4, which keeps it clean on the Pi 5.

## Shut Down

*Shut Down* (menu, web UI, a keyboard's power key, or a short press of the
Pi 5's power button or the Pi 500's power key) halts the Pi; the power
button or key starts it again.
