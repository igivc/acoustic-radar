#!/bin/bash

ffplay -f v4l2 -framerate 30 -video_size 1280x960 -an -noborder -alwaysontop -x 480 -y 360 -left 1400 -top 60 /dev/video3