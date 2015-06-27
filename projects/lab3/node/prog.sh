#!/bin/bash

for dev in /dev/ttyUSB*
do
    make PROGRAMMING_PORT=$dev program
done
