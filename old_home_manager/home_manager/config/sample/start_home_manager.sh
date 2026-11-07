#!/bin/bash
ps cax | grep home_manager > /dev/null
if [ $? -eq 0 ]; then
  echo "Process is running."
else
  echo "Process is not running. Starting up"
  sudo modprobe w1-gpio
  sudo modprobe w1-therm
  at now -f "/home/pi/github/home_manager/config/sample/startup_sample"
fi

