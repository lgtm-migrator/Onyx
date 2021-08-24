#!/bin/sh
#
# Copyright (c) 2021 Pedro Falcato
# This file is part of Onyx, and is released under the terms of the MIT License
# check LICENSE at the root directory for more information
#

BIN_PATH=$1

if [ "$#" != "1" ]; then
    echo "strip-if-elf: Bad usage: Number of arguments was $#, should be 1"
    exit 1
fi

if [ "$STRIP" = "" ]; then
    STRIP="strip"
fi

if [ "$(file $BIN_PATH | grep "with debug_info")" != "" ]; then
    exec $STRIP $BIN_PATH
    #echo "Stripping $BIN_PATH"
fi
