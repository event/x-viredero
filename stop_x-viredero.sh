#!/bin/sh

pid="$(pgrep -f "/usr/bin/x-viredero -u $1")"
logger -t "xvrd" "Found x-viredero w/ pid $pid"
[ -z "$pid" ] && exit 0

uid="$(cat /proc/$pid/loginuid)"
kill $pid || exit 0

home="$(getent passwd $uid | cut -d: -f6)"
restore_resolution="$(cat $home/.xvrd-resolution)"
[ -z "$restore_resolution" ] && exit 0

GLOBAL_CFG="/etc/viredero"
[ -f "$GLOBAL_CFG" ] && . $GLOBAL_CFG
LOCAL_CFG="$home/.viredero"
[ -f "$LOCAL_CFG" ] && . $LOCAL_CFG

[ -n "$XRANDR_OUTPUT" ] && out="--output $XRANDR_OUTPUT"
/usr/bin/xrandr $out --panning $restore_resolution
rm -f $home/.xvrd-resolution
