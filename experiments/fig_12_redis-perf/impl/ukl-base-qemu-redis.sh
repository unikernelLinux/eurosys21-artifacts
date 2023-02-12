#!/bin/bash

# verbose output
set -x

NETIF=tux0
LOG=rawdata/ukl-base-qemu-redis.txt
RESULTS=results/ukl-base-qemu.csv
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
		-i ${IMAGES}/redis-initrd.cpio.gz \
                -k ${IMAGES}//ukl-redis-base \
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
