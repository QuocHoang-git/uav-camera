#!/bin/sh

killall -9 camera_service encoder_service manager_service streaming_service 2>/dev/null
sleep 1
/tmp/bin/manager_service > /tmp/mgr.log 2>&1 </dev/null &
sleep 1
/tmp/bin/encoder_service > /tmp/enc.log 2>&1 </dev/null &
sleep 1
/tmp/bin/streaming_service -p 554 > /tmp/stream.log 2>&1 </dev/null &
sleep 1
/tmp/bin/camera_service --auto-start > /tmp/cam.log 2>&1 </dev/null &