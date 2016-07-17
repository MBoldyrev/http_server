#!/bin/bash

while [ -z "$(pgrep final)" ]
do
echo -n > /dev/null
done

strace -p $(pgrep final | sort -n | head -n 1)
