#!/bin/bash
echo "Time(s),Memory(KB)" > mem_log.csv
# Get the Process ID (PID) of your C webserver
PID=$(pgrep -x webserver)

if [ -z "$PID" ]; then
    echo "Webserver is not running!"
    exit 1
fi

for i in {0..60}; do
    # Get Resident Set Size (RSS) memory of the process
    MEM=$(ps -o rss= -p $PID)
    echo "$((i * 5)),$MEM" >> mem_log.csv
    echo "Sample $i: $MEM KB"
    sleep 5
done
echo "Logging complete. Saved to mem_log.csv"