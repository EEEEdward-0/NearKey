#!/bin/sh
set -eu
if [ "$(id -u)" -ne 0 ]; then echo 'Run as root.' >&2; exit 1; fi
systemctl disable --now nearkey.service 2>/dev/null || true
for service in gdm-password sddm lightdm; do
  pam="/etc/pam.d/$service"
  [ -f "$pam" ] || continue
  temp=$(mktemp)
  sed '\|^auth sufficient /usr/lib/[^ ]*/security/pam_nearkey.so$|d' "$pam" > "$temp"
  install -m 0644 "$temp" "$pam"
  rm -f "$temp"
done
rm -f /etc/systemd/system/nearkey.service /usr/local/sbin/nearkey
rm -f /usr/lib/x86_64-linux-gnu/security/pam_nearkey.so /usr/lib/aarch64-linux-gnu/security/pam_nearkey.so
systemctl daemon-reload
echo 'NearKey service and PAM hook removed. /etc/nearkey.conf and PAM backups remain.'
