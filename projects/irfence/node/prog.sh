#!/bin/bash

for dev in /dev/ff*
do
    make PROGRAMMING_PORT=$dev program &
done

wait
