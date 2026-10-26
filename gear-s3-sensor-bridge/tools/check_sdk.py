#!/usr/bin/env python3
"""Read-only SDK diagnosis. Never starts SDK programs, builds, or devices."""
import argparse
import json
import os
from pathlib import Path
import platform
import sys


def candidates(explicit=None):
    result = []
    if explicit:
        result.append(Path(explicit).expanduser())
    else:
        for name in ("SB_TIZEN_SDK", "TIZEN_SDK_INSTALLED_PATH"):
            if os.environ.get(name):
                result.append(Path(os.environ[name]).expanduser())
        result.append(Path.home() / "tizen-studio")
        config = Path.home() / ".tizen.path.config"
        if config.is_file():
            try:
                location = json.loads(config.read_text()).get("url")
                if location:
                    result.append(Path(location) / "server/sdktools/data")
            except (OSError, ValueError, TypeError):
                pass
    return list(dict.fromkeys(result))


def inspect_sdk(root):
    sysroot = root / "platforms/tizen-4.0/wearable/rootstraps/wearable-4.0-device.core"
    missing = []
    for relative in (
        "usr/include/network/bluetooth.h",
        "usr/include/network/bluetooth_type.h",
        "usr/include/sensor/sensor.h",
        "usr/include/privacy-privilege-manager/privacy_privilege_manager.h",
        "usr/lib/pkgconfig/capi-network-bluetooth.pc",
        "usr/lib/pkgconfig/capi-privacy-privilege-manager.pc",
        "usr/lib/libcapi-network-bluetooth.so",
    ):
        # A rootstrap library can be a link to a target-absolute path. Do not
        # follow that path into the host filesystem during diagnosis.
        path = sysroot / relative
        if not path.exists() and not path.is_symlink():
            missing.append(relative)
    compilers = sorted(root.glob("tools/*/bin/arm-linux-gnueabi-gcc"))
    if not compilers:
        missing.append("ARM compiler (tools/*/bin/arm-linux-gnueabi-gcc)")
    return sysroot, compilers, missing


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", help="SDK root to inspect; no download or installation")
    args = parser.parse_args()
    print("Target: Gear S3 SM-R760 / firmware 4.0.0.4 / Wearable API 4.0")
    print("Host: {} {}".format(platform.system(), platform.machine()))
    ready = False
    for root in candidates(args.sdk):
        print("\nSDK: {}".format(root))
        if not root.is_dir():
            print("  NOT FOUND")
            continue
        sysroot, compilers, missing = inspect_sdk(root)
        if missing:
            print("  INCOMPLETE for Gear S3; missing:")
            for item in missing:
                print("    - " + item)
        else:
            print("  Wearable 4.0 files and ARM compiler present (not executed)")
            print("  Sysroot: {}".format(sysroot))
            print("  Compiler: {}".format(compilers[0]))
            ready = True
    if platform.system() == "Darwin" and platform.machine() == "arm64":
        print("\nHistorical macOS SDK tools target x64. Their execution on this")
        print("Apple Silicon host is unverified; SDK files alone do not prove compatibility.")
    if not ready:
        print("\nWearable 4.0 SDK is missing/incomplete. See SETUP.md.")
        print("Installing another 8.0-10.0 Baseline SDK will not fix this target.")
    print("\nNo builds, downloads, installations, signing, or device access performed.")
    return 0 if ready else 2


if __name__ == "__main__":
    sys.exit(main())
