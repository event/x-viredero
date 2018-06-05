#!/bin/sh

pid="$(pgrep -f "/usr/bin/x-viredero -u $1")"
logger -t "xvrd" "Found x-viredero w/ pid $pid"
[ -z "$pid" ] && exit 0

uid="$(stat -c %u /proc/$pid)"
kill $pid || exit 0

home="$(getent passwd $uid | cut -d: -f6)"
[ -f "$home/.xvrd-resolution" ] || exit 0

for v in OUTPUT RESOLUTION DISPLAY; do
    read $v
done < $home/.xvrd-resolution
logger -t "xvrd" "Set resolution back to $RESOLUTION on output $OUTPUT for display $DISPLAY"

username="$(getent passwd $uid | cut -d: -f1)"
export DISPLAY
/bin/su $username -c "/usr/bin/xrandr --output $OUTPUT --panning $RESOLUTION"
logger -t "xvrd" "Set resolution back: $?"
rm -f $home/.xvrd-resolution
