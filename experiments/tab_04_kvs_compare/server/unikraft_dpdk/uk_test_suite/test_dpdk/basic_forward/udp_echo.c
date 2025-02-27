#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <testsuite.h>
#include <uk/assert.h>
#include "measure.h"
#include "pktgen.h"
#include "dev.h"
#include <uk/netdev.h>
#define CONFIG_USE_DPDK_PMD
#define PRINT_ITERATION		10
#define RX_NUM_BUF 16384
#define TX_NUM_BUF 16384

static uint64_t	arg_stat_timer =  1000 * TIMER_MILLISECOND; /* default period is ~ 1s */
static uint64_t	arg_time_out = 100 * TIMER_MILLISECOND; /* default period is ~ 10 seconds */
/* 2 minutes Duration */
static uint64_t	arg_exp_end_time = 10 * 1000 * TIMER_MILLISECOND;
static uint32_t	arg_src_ipv4_addr = IPv4(172, 18, 0, 4);
static uint32_t	arg_dst_ipv4_addr = IPv4(172, 18, 0, 2);
static uint32_t arg_print_itr = 10;
static uint16_t	arg_src_port = 9000;
static uint16_t	arg_dst_port = 9001;
static int  burst_cnt = DEF_PKT_BURST;
static struct rte_ether_addr arg_dst_mac = {{0x68,0x05,0xCA,0x0C,0x47,0x51}};

void dump_stats(int iter)
{
	int i;
	/**
	 * Printing packets latency per second.
	 */
	printf("The latency(iteration):count of packet to received/transmitted per sec\n");
	for ( i = 0; i < iter; i++) {
		printf("%llu(%llu):%llu,%llu\n", pkt_stats.latency[i],
			pkt_stats.rxburst_itr[i], pkt_stats.total_rxpkts_pps[i], pkt_stats.rxpkt_dropped[i]);
	}

	/**
	 * printing total packets
	 */
	printf("The count of packet to received: %llu\n",
		pkt_stats.total_rxpkts);

#if 0
	printf("The count of packet to transmitted/NR of packet sent to virtio per sec/Total iteration\n");
	for ( i = 0; i < iter; i++) {
		printf("%llu/%llu/%llu/%llu/%llu/%llu\n",
		       pkt_stats.total_txpkts_pps[i],
		       pkt_stats.total_txpkts_tries_pps[i],
		       pkt_stats.total_txpkts_tries_ra_pps[i],
		       pkt_stats.total_txpkts_tries_dropped[i],
		       pkt_stats.total_txpkts_alloc_failed[i],
		       pkt_stats.txburst_itr[i]);
	}
#if 0
	pkt_stats.avg_txpkt_gen_cycles = 0;
	pkt_stats.avg_txpkt_buf_cycles = 0;
	pkt_stats.avg_txpkt_xmit_cycles = 0;
#endif

	printf("The latency of pkt alloc/pkt gen/ pkt send\n");
	for ( i = 0; i < iter; i++) {
		printf("%llu/%llu/%llu\n",
			pkt_stats.txpkt_buf_cycles[i],
			pkt_stats.txpkt_gen_cycles[i],
			pkt_stats.txpkt_xmit_cycles[i]);
	}
#endif

	printf("The latency of pkt recv/0 pkt recv/pkt process/ pkt free\n");
	for ( i = 0; i < iter; i++) {
		printf("%llu/%llu/%llu/%llu\n",
			pkt_stats.rxpkt_recv_cycles[i],
			pkt_stats.rxpkt_zrecv_cycles[i],
			pkt_stats.rxpkt_process_cycles[i],
			pkt_stats.rxpkt_buf_free[i]);
	}
}

static int64_t echo_packet(struct rte_mbuf *mbuf)
{
	uint16_t offset = 0;
	int rc = -1;
	struct rte_ether_hdr *eth_header = (struct ether_header *)
		rte_pktmbuf_mtod_offset(mbuf, char *, offset);

	struct rte_ether_addr eth_addr;
	struct rte_ipv4_hdr *ip_hdr;
	struct rte_udp_hdr *udp_hdr;
	struct request_header *rsq_hdr;
	int64_t oneway_latency;
	uint32_t ip_addr;
	uint16_t port_addr;

	if (eth_header->ether_type == 0x8) {
		offset += sizeof(struct rte_ether_hdr);
		ip_hdr = rte_pktmbuf_mtod_offset(mbuf, char *, offset);
		//rte_eth_macaddr_get(0, &addr);
		if (ip_hdr->next_proto_id == 0x11) {
			offset += sizeof(struct rte_ipv4_hdr);

			rte_ether_addr_copy(&eth_header->d_addr, &eth_addr);
			rte_ether_addr_copy(&eth_header->s_addr, &eth_header->d_addr);
			rte_ether_addr_copy(&eth_addr, &eth_header->s_addr);

			/* Switch IP addresses */
			ip_hdr->src_addr ^= ip_hdr->dst_addr;
			ip_hdr->dst_addr ^= ip_hdr->src_addr;
			ip_hdr->src_addr ^= ip_hdr->dst_addr;

			/* switch UDP PORTS */
			udp_hdr->src_port ^= udp_hdr->dst_port;
			udp_hdr->dst_port ^= udp_hdr->src_port;
			udp_hdr->src_port ^= udp_hdr->dst_port;

			rc = 0;
			/* No checksum requiere, they are 16 bits and
			 * switching them does not influence the checsum
			 * PS: I have also computed the cheksum and it's the same
			 * */
		}
	}

	return rc;
}

int pkt_burst_receive_cnt(int port_id, struct rte_mempool *mpool, int pkt_cnt,
			  int idx)
{
	int pkt = 0;
	uint64_t pkt_sent = 0;
	int i;

	uint64_t start_tsc, end_tsc;
	uint64_t curr_tsc;
#ifdef CONFIG_USE_DPDK_PMD
	struct rte_mbuf *bufs[DEF_PKT_BURST];
#else
	struct uk_netbuf *bufs[DEF_PKT_BURST];
#endif
	uint64_t latency = 0;
	int rc;


#ifndef CONFIG_USE_DPDK_PMD
	struct rte_eth_dev *vrtl_eth_dev;
	struct uk_ethdev_private *dev_private;
	vrtl_eth_dev = &rte_eth_devices[port_id];
	UK_ASSERT(vrtl_eth_dev);
	dev_private = vrtl_eth_dev->data->dev_private;
	UK_ASSERT(dev_private);
#endif
	while(pkt == 0)
		pkt = pkt_burst_receive(port_id, mpool, &bufs[0], DEF_PKT_BURST,
			 &latency, idx);

#ifdef CONFIG_USE_DPDK_PMD
	for (i = 0; i < pkt; i++) {
		echo_packet(bufs[i]);
	}
	pkt_sent = rte_eth_tx_burst(port_id, 0, &bufs[0], pkt)
#else
	latency = 0;
	for (i = 0; i < pkt; i++) {
		echo_packet(bufs[i]->priv);
	}
	pkt_sent = pkt;
        rc = uk_netdev_tx_burst(dev_private->netdev,
                                0, &bufs[0], &pkt_sent);
	if (unlikely(rc < 0 || pkt_sent < pkt)) {
		//pkt_stats.rxpkt_dropped[idx] += (pkt - pkt_sent);
		for (i = pkt_sent; i < pkt; i++)
			rte_pktmbuf_free(bufs[i]->priv);
	}
	//pkt_stats.latency[idx] += latency;
#endif
	//end_tsc = rte_rdtsc();
	//pkt_stats.rxpkt_buf_free[idx] += (end_tsc - start_tsc);
	//latency = 0;
	//pkt_stats.rxburst_itr[idx]++;

	return pkt;
}

int pkt_burst_transmit_cnt(int port_id, struct rte_mempool *mp, int cnt, int itr)
{
	int pkts = 0;
	int tx_cnt = 0;

	//while (pkts < cnt) {
		tx_cnt  = pkt_burst_transmit(port_id, mp, itr);
		pkts += tx_cnt;
	//}
	return pkts;
}


static int test_udpecho(int argc, char *argv[])
{
	struct rte_mempool *mbuf_pool;
	unsigned nb_ports;
	uint16_t portid, first_portid = 0xff;
	int i;

#if 0
	/* Application args process */
	int ret = test_app_args(argc, argv);
#endif

	/* Initialize the Environment Abstraction Layer (EAL). */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		UK_CRASH("Error with EAL initialization\n");

	argc -= ret;
	argv += ret;

	/* Check that there is an even number of ports to send/receive on. */
	nb_ports = rte_eth_dev_count_avail();
	printf("%d number of ports detected Pool Size: %d\n", nb_ports,
		RTE_MBUF_DEFAULT_BUF_SIZE);

	/* Creates a new mempool in memory to hold the mbufs. */
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
					    RTE_MEMPOOL_CACHE_MAX_SIZE, 0,
					    RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());


	/* Initialize all ports. */
	RTE_ETH_FOREACH_DEV(portid) {
		if (first_portid == 0xff)
			first_portid = portid;
		if (port_init(portid, mbuf_pool) != 0)
			UK_CRASH("Cannot init port %"PRIu16 "\n",
					portid);
	}

	tx_pkt_setup(arg_src_ipv4_addr, arg_dst_ipv4_addr,
		     arg_src_port, arg_dst_port);

	if (rte_lcore_count() > 1)
		printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");

	/* Call lcore_main on the master core only. */
	i = 0;
	int tx_count = 0, total_tx_count = 0;
	int rx_count = 0, total_rx_count = 0;
	uint64_t start_tsc = rte_rdtsc(); 
	uint64_t curr_tsc;
	uint64_t latency;
	uint64_t j = 0;
	int cnt = burst_cnt;
	uint64_t exp_start = start_tsc, exp_curr, dur = 0;
	do {
		while (dur < arg_exp_end_time) {
			//tx_count = pkt_burst_transmit_cnt(first_portid, mbuf_pool_tx, cnt, j);
			//pkt_stats.total_txpkts_pps[j] += tx_count;
			while (1)
				rx_count = pkt_burst_receive_cnt(first_portid, mbuf_pool, cnt, j);
			pkt_stats.total_rxpkts_pps[j] += rx_count;
			pkt_stats.txburst_itr[j]++;
			curr_tsc = rte_rdtsc();
			dur = curr_tsc - exp_start;

			if (curr_tsc - start_tsc > arg_stat_timer) {
				pkt_stats.total_rxpkts += pkt_stats.total_rxpkts_pps[j];
				pkt_stats.total_txpkts += pkt_stats.total_txpkts_pps[j];
				j++;
#if 0
				if (j == arg_print_itr) {
					/* Print stat Computation */
					dump_stats(j);
					j = 0;
				}
#endif
				/* Resetting the tx count */
				pkt_stats.total_txpkts_pps[j] = 0;
				pkt_stats.total_rxpkts_pps[j] = 0;
				pkt_stats.latency[j] = 0;
				pkt_stats.rxburst_itr[j] = 0;
				pkt_stats.txburst_itr[j] = 0;
				pkt_stats.total_txpkts_tries_pps[j] = 0;
				pkt_stats.total_txpkts_tries_dropped[j] = 0;
				pkt_stats.total_txpkts_tries_ra_pps[j] = 0;
				pkt_stats.total_txpkts_alloc_failed[j] = 0;
				pkt_stats.txpkt_xmit_cycles[j] = 0;
				pkt_stats.txpkt_gen_cycles[j] = 0;
				pkt_stats.txpkt_buf_cycles[j] = 0;
				pkt_stats.rxpkt_recv_cycles[j] = 0;
				pkt_stats.rxpkt_zrecv_cycles[j] = 0;
				pkt_stats.rxpkt_process_cycles[j] = 0;
				pkt_stats.rxpkt_buf_free[j] = 0;
				pkt_stats.rxpkt_dropped[j] = 0;

				/* Restart a new cycle */
				start_tsc = rte_rdtsc();
			}
		}
		exp_start = rte_rdtsc();
		dur = 0;
	} while (1);
	return 0;
}
TESTSUITE_REGISTER_ARGS(test_dpdk, test_udpecho, "start_rxonly","end_rxonly");
