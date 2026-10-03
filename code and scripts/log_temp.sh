#!/bin/bash
echo "Time(s),Temperature(C)" > temp_log.csv
for i in {0..10}; do
    TEMP=$(cat /sys/class/thermal/thermal_zone0/temp)
    TEMP_C=$(echo "scale=1; $TEMP / 1000" | bc)
    echo "$((i * 30)),$TEMP_C" >> temp_log.csv
    echo "Sample $i: $TEMP_C °C"
    sleep 30
done
echo "Logging complete. Saved to temp_log.csv"log_mem.sh:#!/bin/bash
echo "Time(s),Memory(KB)" > mem_log.csv