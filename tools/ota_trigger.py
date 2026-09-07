#!/usr/bin/env python3
import argparse
import getpass
import hashlib
import os
import ssl
import sys
import urllib.request
from pathlib import Path

import paho.mqtt.client as mqtt


DEFAULT_FIRMWARE = Path(".pio/build/esp32dev/firmware.bin")
DEFAULT_URL = "https://www.warsztatweb.pl/esp32/dzialka/firmware.bin"
DEFAULT_TOPIC = "ems/sterownik-dzialka/ota/set"


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as firmware:
        for chunk in iter(lambda: firmware.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def remote_sha256(url: str) -> tuple[str, int]:
    digest = hashlib.sha256()
    total = 0
    with urllib.request.urlopen(url, timeout=30, context=ssl.create_default_context()) as response:
        if response.status != 200:
            raise RuntimeError(f"serwer zwrocil HTTP {response.status}")
        for chunk in iter(lambda: response.read(1024 * 1024), b""):
            digest.update(chunk)
            total += len(chunk)
    return digest.hexdigest(), total


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Sprawdz firmware na serwerze i wyzwol OTA przez MQTT.")
    parser.add_argument("--firmware", type=Path, default=DEFAULT_FIRMWARE)
    parser.add_argument("--host", default=os.getenv("EMS_MQTT_HOST", "mqtt.ele365.eu"))
    parser.add_argument("--port", type=int, default=int(os.getenv("EMS_MQTT_PORT", "8883")))
    parser.add_argument("--user", default=os.getenv("EMS_MQTT_USER", "esp32_dzialka"))
    parser.add_argument("--password", default=os.getenv("EMS_MQTT_PASSWORD"))
    parser.add_argument("--topic", default=DEFAULT_TOPIC)
    parser.add_argument("--skip-remote-check", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.firmware.is_file():
        print(f"Brak firmware: {args.firmware}", file=sys.stderr)
        return 2

    local_hash = file_sha256(args.firmware)
    local_size = args.firmware.stat().st_size
    print(f"Lokalny firmware: {local_size} B, SHA-256 {local_hash}")

    if not args.skip_remote_check:
        print(f"Sprawdzam {DEFAULT_URL}")
        try:
            server_hash, server_size = remote_sha256(DEFAULT_URL)
        except Exception as error:
            print(f"Nie mozna sprawdzic firmware na serwerze: {error}", file=sys.stderr)
            return 3
        if (server_hash, server_size) != (local_hash, local_size):
            print(
                f"Plik na serwerze jest inny: {server_size} B, SHA-256 {server_hash}",
                file=sys.stderr,
            )
            return 4

    password = args.password or getpass.getpass(f"Haslo MQTT dla {args.user}: ")
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.username_pw_set(args.user, password)
    client.tls_set(cert_reqs=ssl.CERT_REQUIRED)
    try:
        client.connect(args.host, args.port, keepalive=30)
        client.loop_start()
        result = client.publish(args.topic, local_hash, qos=1, retain=False)
        result.wait_for_publish(timeout=10)
        if result.rc != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError(mqtt.error_string(result.rc))
    except Exception as error:
        print(f"Nie udalo sie wyslac komendy MQTT: {error}", file=sys.stderr)
        return 5
    finally:
        client.disconnect()
        client.loop_stop()

    print(f"Wyslano trigger OTA na {args.topic}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
