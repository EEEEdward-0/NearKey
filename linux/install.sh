#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then echo 'Run as root.' >&2; exit 1; fi
base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case "$(uname -m)" in
  x86_64) arch=linux-x64; triplet=x86_64-linux-gnu ;;
  aarch64) arch=linux-arm64; triplet=aarch64-linux-gnu ;;
  *) echo 'Supported architectures: x86_64, aarch64.' >&2; exit 1 ;;
esac
if [ ! -f /etc/nearkey.conf ]; then
  install -m 0600 "$base/linux/nearkey.conf.example" /etc/nearkey.conf
  echo 'Edit /etc/nearkey.conf (user and stable BLE address), then rerun this installer.' >&2
  exit 1
fi
install -m 0755 "$base/dist/$arch/nearkey" /usr/local/sbin/nearkey
/usr/local/sbin/nearkey preview -65 >/dev/null
install -m 0644 "$base/linux/nearkey.service" /etc/systemd/system/nearkey.service
systemctl daemon-reload
systemctl enable nearkey.service
systemctl restart nearkey.service
sleep 6
if ! /usr/local/sbin/nearkey status >/dev/null; then
  echo 'NearKey service has not produced fresh status. PAM was not changed.' >&2
  exit 1
fi

module="/usr/lib/$triplet/security/pam_nearkey.so"
install -d -m 0755 "$(dirname "$module")"
install -m 0644 "$base/dist/$arch/pam_nearkey.so" "$module"
echo 'NearKey service and PAM module installed. Review status, then run sudo sh linux/enable-pam.sh to enable lock-screen authentication.'
