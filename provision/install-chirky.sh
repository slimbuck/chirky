#!/bin/sh
set -eu

chirky_user=retro
chirky_offline=0
chirky_start=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --user) chirky_user=$2; shift 2 ;;
        --offline) chirky_offline=1; shift ;;
        --start) chirky_start=1; shift ;;
        -h|--help)
            echo "Usage: sudo sh provision/install-chirky.sh [--user NAME] [--offline] [--start]"
            exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

if [ "$(id -u)" -ne 0 ]; then
    echo "Run this script with sudo." >&2
    exit 1
fi
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if ! id "$chirky_user" >/dev/null 2>&1; then
    echo "User does not exist: $chirky_user" >&2
    exit 1
fi

packages="build-essential libdrm2 libgbm1 libegl1 libgles2 alsa-utils network-manager openssh-server"
if [ "$chirky_offline" -eq 0 ]; then
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $packages
else
    for command_name in cc make aplay nmcli ssh; do
        command -v "$command_name" >/dev/null 2>&1 || {
            echo "Offline install is missing required command: $command_name" >&2
            exit 1
        }
    done
fi

for group in audio video render input; do
    getent group "$group" >/dev/null 2>&1 && usermod -aG "$group" "$chirky_user"
done
chown -R "$chirky_user:$chirky_user" "$project_root"

runuser -u "$chirky_user" -- make -C "$project_root" clean all
install -d -o "$chirky_user" -g "$chirky_user" "$project_root/run" "$project_root/snapshots"

# Install one service and retire its predecessor, preserving the current root.
if [ "$chirky_start" -eq 1 ]; then
    sh "$project_root/deploy/install-service.sh" --user "$chirky_user"
else
    sh "$project_root/deploy/install-service.sh" --user "$chirky_user" --no-start
fi

echo "Chirky installed from $project_root."
echo "Service enabled: $(systemctl is-enabled chirky.service)"
if [ "$chirky_start" -eq 1 ]; then
    echo "Service state: $(systemctl is-active chirky.service)"
else
    echo "The service will start on the next boot."
fi
