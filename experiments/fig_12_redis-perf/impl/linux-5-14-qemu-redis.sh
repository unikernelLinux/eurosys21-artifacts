#!/bin/bash

# verbose output
set -x

NETIF=tux0
LOG=rawdata/linux-5-14-qemu-redis.txt
RESULTS=results/linux-5-14-qemu.csv
echo "operation	throughput" > $RESULTS
mkdir -p rawdata
touch $LOG

IMAGES=$(pwd)/images

source ../common/set-cpus.sh
source ../common/network.sh
source ../common/redis.sh

create_bridge $NETIF $BASEIP
killall -9 qemu-system-x86
pkill -9 qemu-system-x86

function cleanup {
	# kill all children (evil)
	delete_bridge $NETIF
	rm ${IMAGES}/redis.ext2.disposible
	killall -9 qemu-system-x86
	pkill -9 qemu-system-x86
	pkill -P $$
}

trap "cleanup" EXIT

for j in {1..5}
do
	cp ${IMAGES}/redis.ext2 ${IMAGES}/redis.ext2.disposible

	taskset -c ${CPU1} qemu-guest \
		-i ${IMAGES}/redis-initrd-normal.cpio.gz \
                -k ${IMAGES}/linux-5-14 \
                -a "console=ttyS0 net.ifnames=0 biosdevname=0 nowatchdog nosmap nosmep mds=off \
		ip=${BASEIP}.2:::255.255.255.0::eth0:none" \
                -m 1024 -p ${CPU2} \
                -b ${NETIF} -x
	# make sure that the server has properly started
	sleep 15

	# benchmark
	benchmark_redis_server ${BASEIP}.2 6379

	parse_redis_results $LOG $RESULTS

	# stop server
	killall -9 qemu-system-x86
	pkill -9 qemu-system-x86
	rm ${IMAGES}/redis.ext2.disposible
done
