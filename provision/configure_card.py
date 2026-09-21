#!/usr/bin/env python3
"""Prepare a freshly flashed Raspberry Pi OS Trixie boot partition."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RGBERRY_BLOCK = (ROOT / "provision" / "rgbberry-config.txt").read_text(encoding="utf-8")


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Prepare a Chirky Trixie SD card.")
    parser.add_argument("--drive", "-d", default="E:", help="Mounted boot partition (default: E:)")
    parser.add_argument("--hostname", default="chirky")
    parser.add_argument("--user", default="retro")
    parser.add_argument("--address", default="192.168.137.2/24")
    parser.add_argument("--ssh-key", default=os.path.expanduser("~/.ssh/id_ed25519.pub"))
    parser.add_argument("--no-ssh-key", action="store_true")
    return parser.parse_args()


def configure_boot(boot: Path) -> None:
    path = boot / "config.txt"
    text = path.read_text(encoding="utf-8")
    for brand in ("CHIRKY", "TWO FORTY"):
        begin, end = f"# BEGIN {brand} RGBERRY", f"# END {brand} RGBERRY"
        if begin in text and end in text:
            before, remainder = text.split(begin, 1)
            _, after = remainder.split(end, 1)
            text = before.rstrip() + "\n" + after.lstrip("\r\n")
    owned = {
        "dtoverlay=audremap,pins_18_19",
        "dtoverlay=vc4-kms-dpi-generic,hactive=320,hfp=16",
        "dtparam=hsync=32,hbp=39",
        "dtparam=vactive=240,vfp=3,vsync=3,vbp=16",
        "dtparam=clock-frequency=6400000,rgb666-padhi",
        "dtparam=hsync-invert,vsync-invert",
    }
    owned_keys = (
        "dtparam=audio=", "camera_auto_detect=", "display_auto_detect=",
        "disable_fw_kms_setup=", "disable_overscan=",
    )
    text = "\n".join(
        line for line in text.splitlines()
        if line.strip() not in owned and not line.strip().startswith(owned_keys)
    )
    path.write_text(text.rstrip() + "\n\n[all]\n" + RGBERRY_BLOCK.rstrip() + "\n", encoding="utf-8")


def configure_network(boot: Path, address: str) -> None:
    (boot / "network-config").write_text(
        "network:\n"
        "  version: 2\n"
        "  renderer: NetworkManager\n"
        "  ethernets:\n"
        "    eth0:\n"
        "      dhcp4: false\n"
        "      dhcp6: false\n"
        f"      addresses: [{address}]\n"
        "      optional: true\n",
        encoding="utf-8",
    )


def configure_user(boot: Path, hostname: str, user_name: str, key: str | None) -> None:
    path = boot / "user-data"
    if key:
        quoted_key = json.dumps(key)
        path.write_text(
            "#cloud-config\n"
            f"hostname: {hostname}\n"
            "manage_etc_hosts: true\n"
            "ssh_pwauth: false\n"
            "users:\n"
            f"  - name: {user_name}\n"
            "    groups: [sudo, adm, audio, video, render, input]\n"
            "    lock_passwd: true\n"
            "    shell: /bin/bash\n"
            "    sudo: ALL=(ALL) NOPASSWD:ALL\n"
            "    ssh_authorized_keys:\n"
            f"      - {quoted_key}\n"
            "runcmd:\n"
            "  - systemctl enable ssh\n"
            "  - systemctl start ssh\n",
            encoding="utf-8",
        )
    else:
        print("No SSH key supplied: preserving Raspberry Pi Imager user-data")
    (boot / "ssh").touch()


def main() -> None:
    args = arguments()
    drive = args.drive.rstrip("\\/")
    if os.name == "nt" and len(drive) == 2 and drive[1] == ":":
        drive += "\\"
    boot = Path(drive)
    for required in ("config.txt", "user-data", "network-config"):
        if not (boot / required).exists():
            raise SystemExit(f"Not a compatible mounted boot partition: missing {boot / required}")
    key = None
    if not args.no_ssh_key:
        key_path = Path(args.ssh_key).expanduser()
        if not key_path.is_file():
            raise SystemExit(f"SSH public key not found: {key_path}")
        key = key_path.read_text(encoding="utf-8").strip()
    configure_boot(boot)
    configure_network(boot, args.address)
    configure_user(boot, args.hostname, args.user, key)
    print(f"Prepared {boot}: {args.hostname}, {args.user}, {args.address}, RGBerry 320x240p60")
    print("Eject the card, boot the Pi, then run provision/install.ps1 from the laptop.")


if __name__ == "__main__":
    main()
