#!/bin/bash

# makes sure to run host setup
../common/setup-host.sh

mkdir rawdata results

# run benchmarks
#./impl/hermitux-uhyve-redis.sh
#./impl/rump-qemu-redis.sh
./impl/osv-qemu-redis.sh
#./impl/lupine-fc-redis.sh
./impl/lupine-qemu-redis.sh
./impl/linux-4-0-0-qemu-redis.sh
#./impl/microvm-fc-redis.sh
#./impl/microvm-qemu-redis.sh
#./impl/docker-redis.sh
./impl/native-redis.sh
./impl/unikraft-qemu-redis.sh
./impl/linux-5-14-qemu-redis.sh
./impl/ukl-base-qemu-redis.sh
./impl/ukl-bp-qemu-redis.sh
./impl/ukl-bp-sc-qemu-redis.sh

