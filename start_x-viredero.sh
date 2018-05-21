#!/bin/sh

DIALOG_TITLE="x-viredero"
DIALOG_TEXT="Start x-viredero?"

log() {
    logger -t "x-viredero" "$1"
}

TARGET_UID="$(ls -1 /var/run/user | head -1)"
GLOBAL_CFG="/etc/viredero"
[ -f "$GLOBAL_CFG" ] && . $GLOBAL_CFG
[ "$DISABLED" = "1" ] && {
    log "x-viredero is disabled globally"
    exit 0
}
TARGET_UNAME="${TARGET_UNAME:-$(getent passwd $TARGET_UID | cut -d: -f1)}"
[ -z "$TARGET_UNAME" ] && {
    log "Could not determine user to run x-viredero for. Please define it in $GLOBAL_CFG"
    exit 1
}
TARGET_HOME="$(getent passwd $TARGET_UNAME | cut -d: -f6)"
LOCAL_CFG="$TARGET_HOME/.viredero"
[ -f "$LOCAL_CFG" ] && . $LOCAL_CFG

[ "$DISABLED" = "1" ] && {
    log "x-viredero is disabled for $TARGET_UNAME"
    exit 0
}

export DISPLAY="${DISPLAY:-:0}"
[ -z "$DONT_ASK" -o "$DONT_ASK" = "0" ] && {
    if [ -x /usr/bin/zenity ] ; then
        /usr/bin/zenity --question --title="$DIALOG_TITLE" --text="$DIALOG_TEXT"
    elif [ -x /usr/bin/kdialog ] ; then
        /usr/bin/kdialog --title "$DIALOG_TITLE" -yesno "$DIALOG_TEXT"
    elif [ -x /usr/bin/Xdialog ] ; then
        /usr/bin/Xdialog --title "$DIALOG_TITLE" --clear --yesno "$DIALOG_TEXT" 10 80
    else
        log "No suitable dialog program found. Will just proceed..."
    fi
    [ "$?" != "0" ] && {
        log "User doesn't want to start x-viredero :("
        exit 0
    }
    [ -z "$DONT_ASK" ] && echo "DONT_ASK=1" >>$LOCAL_CFG
}

[ -n "$PANNING_RESOLUTION" ] && {
    [ -n "$XRANDR_OUTPUT" ] && out="--output $XRANDR_OUTPUT"
    /usr/bin/xrandr --listactivemonitors | grep -G -m1 "^ 0: +$XRANDR_OUTPUT" \
        | sed "s/^ [^ ]\+ [^ ]\+ \([0-9]\+\)\/[0-9]\+x\([0-9]\+\).*$/\1x\2/" >$TARGET_HOME/.xvrd-resolution
    /usr/bin/xrandr $out --panning $PANNING_RESOLUTION \
        || log "Failed to set requested resolution $PANNING_RESOLUTION"
}

/bin/su $TARGET_UNAME -c "/usr/bin/x-viredero -u $1 -D $DISPLAY"

