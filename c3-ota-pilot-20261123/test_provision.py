import unittest
from unittest.mock import patch

import provision


class FakePort:
    def __init__(self, lines):
        self.lines = iter(lines)

    def readline(self):
        return next(self.lines)


class ProvisionTests(unittest.TestCase):
    def test_ota_password_length_boundaries(self):
        for length, expected in ((5, False), (6, True), (63, True), (64, False)):
            with self.subTest(length=length):
                self.assertEqual(provision.valid_ota_password("x" * length), expected)

    def test_wait_for_prompt_ignores_other_messages(self):
        port = FakePort([b"boot message\n", b"SSID?\r\n"])
        self.assertEqual(provision.wait_for(port, "SSID?"), "SSID?")

    def test_wait_for_prompt_reports_device_error(self):
        port = FakePort([b"CONFIG_ERROR: invalid SSID\n"])
        with self.assertRaisesRegex(RuntimeError, "invalid SSID"):
            provision.wait_for(port, "SSID?")

    @patch("provision.time.monotonic", side_effect=[0, 0, 2])
    def test_wait_for_prompt_times_out(self, _clock):
        with self.assertRaisesRegex(TimeoutError, "SSID"):
            provision.wait_for(FakePort([b""]), "SSID?", timeout=1)


if __name__ == "__main__":
    unittest.main()
