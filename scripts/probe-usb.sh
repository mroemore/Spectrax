#!/bin/sh
# probe-usb.sh — decide whether the device exposes USB networking.
#
# Resolves: transport.usb_gadget_host_ip, transport.usb_gadget_device_ip,
#           and whether 'usb-gadget' belongs in transport.order.
#
# Device side facts this probe checks:
#   * /sys/class/udc/ is populated
#   * the UDC's current function
#   * /sys/class/usb_role/*/role  (the Type-C role switch)
#   * a gadget NIC in /sys/class/net
#   * `usbgadget` with no arguments prints the current mode
#   * `usbgadget address` prints the link-local address
set -eu
. "$(dirname "$0")/lib/probe.sh"
probe_parse_args "$@"
probe_repo_root
# shellcheck disable=SC1091
SPECTRAX_DEVICE="$probe_device"
. "$PWD/scripts/lib/profile.sh"

[ "$probe_local" = 1 ] || { [ -n "$probe_ip" ] || probe_ip=$(scripts/discover-device.sh --device "$probe_device"); }

probe_heading "UDC"
probe_run "ls -1 /sys/class/udc/ 2>/dev/null || echo '(no /sys/class/udc)'"
probe_heading "UDC function"
probe_run "cat /sys/class/udc/*/function 2>/dev/null || true"
probe_heading "UDC state"
probe_run "cat /sys/class/udc/*/state 2>/dev/null || true"
probe_heading "USB role switch"
probe_run "for f in /sys/class/usb_role/*/role; do [ -e \"\$f\" ] && echo \"\$f = \$(cat \$f)\"; done"
probe_heading "gadget NICs"
probe_run "ls -1 /sys/class/net/ 2>/dev/null || true"
probe_heading "usbgadget mode"
probe_run "command -v usbgadget && usbgadget || echo '(no usbgadget helper)'"
probe_heading "usbgadget address"
probe_run "command -v usbgadget && usbgadget address || true"
probe_heading "usb_configfs functions"
probe_run "ls -1 /sys/kernel/config/usb_gadget/ 2>/dev/null || echo '(no configfs)'"

echo
echo "### host side"
if [ "$probe_local" = 1 ]; then
    probe_run "ip -br addr 2>/dev/null | grep -E '169\.254|usb0|gadget' || echo '(no gadget link on the host)'"
else
    echo "  (run this script on the host with the USB cable attached to check the link)"
fi

echo
echo "### suggested profile updates"
addr=$(probe_run "command -v usbgadget >/dev/null 2>&1 && usbgadget address" | head -1 | tr -d '\r')
if [ -n "$addr" ]; then
    host_ip=$(echo "$addr" | awk -F. '{print $1"."$2"."$3".1}')
    probe_apply transport usb_gadget_device_ip "$addr"
    probe_apply transport usb_gadget_host_ip "$host_ip"
    probe_resolve usb_gadget
else
    echo "  no gadget address: usb-gadget must be removed from transport.order"
fi
