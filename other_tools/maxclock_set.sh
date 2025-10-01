#!/bin/bash

OPTION=$1
CPU_NUM=$(lscpu | grep "^CPU(s):" | awk '{print $2}')

CPU_NUM=$((CPU_NUM - 1))
if [ "$OPTION" = "-r" ]; then
    for i in $(seq 0 $CPU_NUM); do
        echo ondemand | sudo tee /sys/devices/system/cpu/cpu${i}/cpufreq/scaling_governor
    done

    exit 0
fi

for i in $(seq 0 $CPU_NUM); do
    MAX_FREQ=$(cat "/sys/devices/system/cpu/cpu${i}/cpufreq/cpuinfo_max_freq")
    echo userspace | sudo tee /sys/devices/system/cpu/cpu${i}/cpufreq/scaling_governor
    echo $MAX_FREQ | sudo tee /sys/devices/system/cpu/cpu${i}/cpufreq/scaling_setspeed
done

echo
echo =====clock=====
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq
echo ===============
