#!/bin/sh
set -eu

chirky_user=retro
chirky_start=1
while [ "$#" -gt 0 ]; do
    case "$1" in
        --user) chirky_user=$2; shift 2 ;;
        --no-start) chirky_start=0; shift ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done
project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ "$(id -u)" -ne 0 ]; then
    if [ "$chirky_start" -eq 1 ]; then
        exec sudo sh "$0" --user "$chirky_user"
    fi
    exec sudo sh "$0" --user "$chirky_user" --no-start
fi

# Render the service for the actual checkout, including existing installations.
service_temp=$(mktemp)
trap 'rm -f "$service_temp"' EXIT HUP INT TERM
sed -e "s/^User=.*/User=$chirky_user/" -e "s/^Group=.*/Group=$chirky_user/" \
    -e "s|/home/retro/chirky|$project_root|g" \
    "$project_root/deploy/chirky.service" > "$service_temp"
install -d -o "$chirky_user" -g "$chirky_user" "$project_root/run"
install -m 0644 "$service_temp" /etc/systemd/system/chirky.service
# Migration only: never leave both DRM hosts enabled or running.
if systemctl cat two-forty.service >/dev/null 2>&1; then
    systemctl disable --now two-forty.service
fi
pkill -x two-forty-host 2>/dev/null || true
systemctl daemon-reload
systemctl enable chirky.service
if [ "$chirky_start" -eq 1 ]; then
    systemctl restart chirky.service
    systemctl --no-pager --full status chirky.service
fi
