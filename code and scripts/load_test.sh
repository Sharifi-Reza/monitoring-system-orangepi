#!/bin/bash
echo "Starting 50 concurrent curl loops for 30 seconds..."
for i in {1..50}; do
    # -k ignores the self-signed cert warning, -s silences output
    while true; do curl -k -s https://127.0.0.1:8443/api/v1/telemetry > /dev/null; done &
done

echo "Running... Open a second terminal and run 'htop' or 'top' to observe CPU/RAM."
sleep 30

echo "Stopping all curl requests..."
killall curl
echo "Test complete