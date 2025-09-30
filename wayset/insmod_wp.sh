#!/bin/bash

LSMOD_WAYPART=`lsmod | grep way_part_control`

# ドライバがインストールされていなければインストール
PATH_TO_KO="$HOME/Pi5-CacheWayPartition/way-part-control.ko"
if [ -z "$LSMOD_WAYPART" ]; then
    sudo insmod ${PATH_TO_KO}
    echo "insmod way-part-control.ko"
fi