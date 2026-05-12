#!/bin/bash

echo "[AUDIO] Ensuring ALSA loopback module is loaded..."
sudo modprobe snd-aloop

# Give PulseAudio/PipeWire a second to discover the hardware
sleep 2

# Find the internal names of the loopback sink (speaker) and source (mic)
SINK_NAME=$(pactl list short sinks | grep "snd_aloop" | awk '{print $2}' | head -n 1)
SOURCE_NAME=$(pactl list short sources | grep "snd_aloop" | grep -v "monitor" | awk '{print $2}' | head -n 1)

if [ -n "$SINK_NAME" ]; then
    echo "[AUDIO] Renaming Loopback Sink to 'BerryAuto Speaker'..."
    pactl update-sink-proplist "$SINK_NAME" device.description="BerryAuto Speaker"
    
    echo "[AUDIO] Setting 'BerryAuto Speaker' as the default system output..."
    pactl set-default-sink "$SINK_NAME"
    
    echo "[AUDIO] Moving all actively playing audio to 'BerryAuto Speaker'..."
    for i in $(pactl list short sink-inputs | awk '{print $1}'); do
        pactl move-sink-input "$i" "$SINK_NAME" 2>/dev/null
    done
else
    echo "[ERROR] Could not find the snd-aloop sink. Is PulseAudio/PipeWire running?"
fi

if [ -n "$SOURCE_NAME" ]; then
    echo "[AUDIO] Renaming Loopback Source to 'BerryAuto Mic'..."
    pactl update-source-proplist "$SOURCE_NAME" device.description="BerryAuto Mic"
    pactl set-default-source "$SOURCE_NAME"
fi

echo "[AUDIO] Routing complete! Check pavucontrol to verify."
