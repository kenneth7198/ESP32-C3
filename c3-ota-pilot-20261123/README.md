# ESP32-C3 LED OTA pilot

This isolated PlatformIO project targets an ESP32-C3 with 30 WS2812B LEDs on
GPIO2. The LEDs show dim red while disconnected, a low-brightness moving
rainbow when connected, and amber during an OTA transfer. A web page hosted
on the device controls the strip's mode, color, and brightness.

## Hardware

Use a suitable regulated 5 V supply for the strip, connect its ground to the
C3 ground, and connect GPIO2 to DIN (ideally through a 330–470 ohm resistor
and a suitable 3.3 V-to-5 V level shifter). Do not power the strip from a
microcontroller GPIO or 3.3 V pin. A 30-LED strip can draw roughly 1.8 A at
full white. GPIO2 is a boot-strapping pin: external circuits must not pull it
low during reset. Do not hot-plug the strip while powered.

## Initial USB setup on proart

1. Verify COM14 is the intended device and check the wiring above.
2. From this directory run `python -m platformio run -e esp32-c3 -t upload --upload-port COM14`.
   This replaces existing firmware and partition layout.
3. Run `python provision.py COM14`. Enter the 2.4 GHz Wi-Fi SSID and password,
   and choose a separate OTA password of 6–63 bytes (six digits are allowed).
   A six-digit password is weak; use a trusted isolated network. Passwords are not
   echoed or passed as command-line arguments. Run `python status.py COM14`
   to check the connection and C3 IP. Credentials are stored in the device's NVS; physical
   access can expose them unless flash encryption is enabled.
4. Record the IP or reserve it in DHCP. Verify proart can reach that IP over
   the network before removing USB. The C3 and proart need routable network
   access in both directions; guest/client isolation can prevent OTA.

## Web control

After provisioning, connect a phone or computer to the same trusted network
and open `http://DEVICE_IP/`, replacing `DEVICE_IP` with the IP reported by
`python status.py COM14` or the router. Use the IP address rather than a
hostname. Choose rainbow, solid color, or off, then press Apply. Brightness
is limited to 20 out of 255. Changes are kept in memory: a restart restores
the default rainbow and brightness. Wi-Fi disconnection and OTA use their
status colors; the chosen setting resumes after reconnection or an OTA error.

The page and HTTP API have **no login or encryption**: anyone who can reach
the device can control the LEDs. Use only a trusted isolated network; do not
expose the web server or OTA to the Internet. The browser interface requires
JavaScript and a current browser.

## OTA

Build a new version with `python -m platformio run -e esp32-c3`. Run
`python upload_ota.py DEVICE_IP` on proart and enter the OTA password at the
prompt. Keep the device powered throughout the transfer and verify the new
version after it restarts. Firmware
updates must retain OTA, Wi-Fi credentials handling, and compatible
partitions, or the next update may require USB.

ArduinoOTA requires inbound access to the device and a return connection to
the uploader; Tailscale access to proart alone does not ensure this path.
OTA password authentication does not encrypt traffic or authenticate signed
firmware. Use a trusted isolated network; this pilot does not provide signed
firmware, secure boot, or automatic rollback. Never expose OTA to the Internet.
Changing network, powering off during an update, or installing broken firmware
can require physical USB recovery.
