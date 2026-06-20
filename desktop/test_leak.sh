#!/bin/bash
valgrind --leak-check=full --show-leak-kinds=definite,indirect --log-file=valgrind_video.log ./desktop &
PID=$!
echo "Waiting for desktop to initialize..."
sleep 3
echo "Loading video 1..."
echo "$HOME/Pictures/vaxp-will/hunt-showdown-dark-ritual-skull-wallpaperwaves-com.mp4" > ~/.config/vaxp/wallpaper
sleep 4
echo "Switching to video 2..."
echo "$HOME/Videos/Recording_2026-05-20_14-37-10.mp4" > ~/.config/vaxp/wallpaper
sleep 4
echo "Switching to static image..."
echo "" > ~/.config/vaxp/wallpaper
sleep 2
echo "Terminating desktop..."
kill -TERM $PID
wait $PID
echo "=== Valgrind Summary ==="
grep -E "definitely lost:|indirectly lost:|possibly lost:|still reachable:" valgrind_video.log
echo "=== Definite Leaks ==="
grep -A 30 "definitely lost in" valgrind_video.log
