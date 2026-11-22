#!/bin/sh

echo "=========================================================================="
echo " TIME     | SERVICE           | FDs      | HEAP (VmData) | RES (VmRSS)"
echo "=========================================================================="

while true; do
    TIMESTAMP=$(date +%T)
    for name in manager_service encoder_service streaming_service camera_service; do
        pid=$(pidof $name)
        if [ -n "$pid" ]; then
            fds=$(ls -1 /proc/$pid/fd 2>/dev/null | wc -l)
            vmdata=$(grep VmData /proc/$pid/status 2>/dev/null | awk '{print $2 " " $3}')
            vmrss=$(grep VmRSS /proc/$pid/status 2>/dev/null | awk '{print $2 " " $3}')
            printf " %-8s | %-17s | %-8s | %-13s | %s\n" "$TIMESTAMP" "$name" "$fds" "$vmdata" "$vmrss"
        fi
    done
    echo "--------------------------------------------------------------------------"
    sleep 5
done
