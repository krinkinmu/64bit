#!/bin/bash

DIR=initramfs
INITRD=$(pwd)/initrd.img

cd $DIR
find . -print0 | cpio --null -ov --format=newc > $INITRD
cd -
