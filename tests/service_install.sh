#!/bin/sh
# Exercise installation and legacy-service retirement without root or systemd.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT HUP INT TERM
mkdir -p "$fixture/bin"
export CHIRKY_TEST_LOG="$fixture/log" CHIRKY_TEST_SERVICE="$fixture/service"
cat > "$fixture/bin/id" <<'SH'
#!/bin/sh
echo 0
SH
cat > "$fixture/bin/install" <<'SH'
#!/bin/sh
printf 'install %s\n' "$*" >> "$CHIRKY_TEST_LOG"
if [ "$1" = '-m' ]; then cp "$3" "$CHIRKY_TEST_SERVICE"; fi
SH
cat > "$fixture/bin/systemctl" <<'SH'
#!/bin/sh
printf 'systemctl %s\n' "$*" >> "$CHIRKY_TEST_LOG"
SH
cat > "$fixture/bin/pkill" <<'SH'
#!/bin/sh
printf 'pkill %s\n' "$*" >> "$CHIRKY_TEST_LOG"
SH
chmod +x "$fixture/bin/"*
PATH="$fixture/bin:$PATH" sh "$root/deploy/install-service.sh" --user tester
grep -Fqx 'User=tester' "$CHIRKY_TEST_SERVICE"
grep -Fqx "WorkingDirectory=$root" "$CHIRKY_TEST_SERVICE"
grep -Fqx "ExecStart=$root/build/chirky-host" "$CHIRKY_TEST_SERVICE"
grep -Fqx 'systemctl disable --now two-forty.service' "$CHIRKY_TEST_LOG"
grep -Fqx 'systemctl restart chirky.service' "$CHIRKY_TEST_LOG"
: > "$CHIRKY_TEST_LOG"
PATH="$fixture/bin:$PATH" sh "$root/deploy/install-service.sh" --user tester --no-start
grep -Fqx 'systemctl enable chirky.service' "$CHIRKY_TEST_LOG"
if grep -q 'systemctl restart' "$CHIRKY_TEST_LOG"; then exit 1; fi
echo 'Service installer: actual checkout, user, legacy retirement and no-start passed.'
