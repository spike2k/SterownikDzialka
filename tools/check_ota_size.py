Import("env")

import os

LEGACY_OTA_LIMIT = 0x140000


def check_legacy_ota_size(target, source, env):
    firmware_path = str(target[0])
    firmware_size = os.path.getsize(firmware_path)
    margin = LEGACY_OTA_LIMIT - firmware_size
    print(
        "Legacy OTA partition: {} / {} bytes (margin: {} bytes)".format(
            firmware_size, LEGACY_OTA_LIMIT, margin
        )
    )
    if margin < 0:
        raise RuntimeError(
            "Firmware is {} bytes too large for the installed 0x140000 OTA partition".format(
                -margin
            )
        )


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_legacy_ota_size)
