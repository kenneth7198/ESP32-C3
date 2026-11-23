"""Upload a built firmware image without putting the OTA password in argv."""
import getpass
import importlib.util
import logging
import random
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python upload_ota.py DEVICE_IP")
    root = Path(__file__).resolve().parent
    image = root / ".pio" / "build" / "esp32-c3" / "firmware.bin"
    espota_path = (
        Path.home()
        / ".platformio"
        / "packages"
        / "framework-arduinoespressif32"
        / "tools"
        / "espota.py"
    )
    if not image.is_file() or not espota_path.is_file():
        raise SystemExit("Firmware or PlatformIO espota.py missing; build the project first")
    spec = importlib.util.spec_from_file_location("espota", espota_path)
    if spec is None or spec.loader is None:
        raise SystemExit("Could not load PlatformIO espota.py")
    espota = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(espota)
    espota.TIMEOUT = 10
    espota.PROGRESS = True
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    password = getpass.getpass("OTA password: ")
    if not password:
        raise SystemExit("OTA password is required")
    return espota.serve(sys.argv[1], "0.0.0.0", 3232, random.randint(10000, 60000), password, str(image))


if __name__ == "__main__":
    sys.exit(main())
