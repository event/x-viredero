#!/bin/sh
TARGET_UID="$(ls -1 /var/run | head -1)"
GLOBAL_CFG="/etc/viredero"
TARGET_UNAME="${TARGET_UNAME:-$(getent passwd $TARGET_UID | cut -d: -f1)}"
[ -z "$TARGET_UNAME" ] && {
    echo "Could not determine user to run x-viredero for. Please define it in $GLOBAL_CFG" >&2
    exit 1
}
LOCAL_CFG="/home/$TARGET_UNAME/.viredero"

export DISPLAY="${DISPLAY:-:0}"
/usr/bin/zenity --question --text="Start X-Viredero?" || {
    echo "User doesn't want to start x-viredero :(" >&2
    exit 0
}

[ -n "$PANNING_RESOLUTION" ] && {
    /usr/bin/xrandr --panning $RESOLUTION
}

/bin/su event -c "/usr/bin/x-viredero -u $1 -D $DISPLAY"
