#!/bin/sh
set -eu
if [ "$(id -u)" -ne 0 ]; then echo 'Run as root.' >&2; exit 1; fi
if ! /usr/local/sbin/nearkey status >/dev/null; then
  echo 'NearKey service has no status; PAM was not changed.' >&2
  exit 1
fi
case "$(readlink -f /etc/systemd/system/display-manager.service)" in
  *gdm*service) service=gdm-password ;;
  *sddm*service) service=sddm ;;
  *lightdm*service) service=lightdm ;;
  *) echo 'Unsupported active display manager; PAM was not changed.' >&2; exit 1 ;;
esac
pam="/etc/pam.d/$service"
[ -f "$pam" ] || { echo "Missing PAM file: $pam" >&2; exit 1; }
module="/usr/lib/$(uname -m)-linux-gnu/security/pam_nearkey.so"
[ -f "$module" ] || { echo "Missing PAM module: $module" >&2; exit 1; }
if grep -Fq 'pam_nearkey.so' "$pam"; then
  echo "NearKey is already enabled in $pam"
  exit 0
fi
cp -p "$pam" "$pam.nearkey-backup"
temp=$(mktemp "/etc/pam.d/.nearkey.XXXXXX")
printf 'auth sufficient %s\n' "$module" > "$temp"
cat "$pam" >> "$temp"
chmod 0644 "$temp"
mv -f "$temp" "$pam"
echo "Enabled NearKey in $pam. Test both proximity and password login."
