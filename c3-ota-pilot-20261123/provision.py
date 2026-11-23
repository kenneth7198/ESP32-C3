"""Provision an attached C3 without putting passwords on a command line."""
import getpass
import sys
import time

import serial


def wait_for(port, expected, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if line.startswith("CONFIG_ERROR"):
            raise RuntimeError(line)
        if expected in line:
            return line
    raise TimeoutError(f"Device did not report {expected}")


def valid_ota_password(password):
    return 6 <= len(password.encode("utf-8")) <= 63


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python provision.py COM14")
    ssid = input("Wi-Fi SSID: ").strip()
    wifi_password = getpass.getpass("Wi-Fi password: ")
    ota_password = getpass.getpass("New OTA password (6-63 bytes): ")
    if ota_password != getpass.getpass("Confirm OTA password: "):
        raise SystemExit("OTA passwords do not match")
    values = (ssid, wifi_password, ota_password)
    if not 1 <= len(ssid.encode()) <= 32 or not 8 <= len(wifi_password.encode()) <= 63:
        raise SystemExit("Invalid Wi-Fi SSID or password length")
    if not valid_ota_password(ota_password) or any(
        "\r" in value or "\n" in value for value in values
    ):
        raise SystemExit("Invalid OTA password or newline in input")
    with serial.Serial(sys.argv[1], 115200, timeout=1) as port:
        time.sleep(2)
        port.reset_input_buffer()
        port.write(b"\x1bCONFIG\n")
        for value, prompt in zip(values, ("SSID?", "WIFI_PASSWORD?", "OTA_PASSWORD?")):
            wait_for(port, prompt)
            port.write(value.encode("utf-8") + b"\n")
        wait_for(port, "CONFIG_SAVED")
        print("Saved. The C3 will restart; check its IP using the router or serial monitor.")


if __name__ == "__main__":
    main()
