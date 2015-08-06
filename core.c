#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <string.h>
#include <sys/queue.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>
#include <netinet/ip.h> 
#include <unistd.h>

#include <rte_common.h>
#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_malloc.h>
#include <rte_fbk_hash.h>
#include <rte_ip.h>

#include <rte_tcp.h>
#include <rte_timer.h>

#include "misc.h"

extern int traffic_status;

// Send burst of packets on an output interface
void send_burst(struct lcore_queue_conf *qconf, uint8_t port)
{
    struct rte_mbuf **m_table;
    uint16_t n, queueid;
    int ret;

    queueid = qconf->tx_queue_id[port];
    m_table = (struct rte_mbuf **)qconf->tx_mbufs[port].m_table;
    n = qconf->tx_mbufs[port].len;

    ret = rte_eth_tx_burst(port, queueid, m_table, n);
	port_stats[port].tx_packets += ret;

    if (unlikely(ret < n))
        port_stats[port].tx_dropped += n - ret;

    while (unlikely (ret < n)) {
        rte_pktmbuf_free(m_table[ret]);
        ret++;
    }

    qconf->tx_mbufs[port].len = 0;
}

 // Write new Ethernet header to the outgoing packet,
 // and put it into the outgoing queue for the given port.
void mcast_send_pkt(struct rte_mbuf *pkt, struct lcore_queue_conf *qconf, uint8_t port)
{
    uint16_t len;

    // Put new packet into the output queue
    len = qconf->tx_mbufs[port].len;
    qconf->tx_mbufs[port].m_table[len] = pkt;
    qconf->tx_mbufs[port].len = ++len;

    // Transmit packets
    if (unlikely(MAX_PKT_BURST == len))
        send_burst(qconf, port);
}

// Main processing loop
int main_loop(__rte_unused void *dummy)
{
    unsigned lcore_id;
    struct rte_mbuf *m;
    struct ether_hdr *eth_hdr = NULL;
    struct ipv4_hdr *ipv4_hdr = NULL;
    struct tcp_hdr *tcp_hdr = NULL;
    struct udp_hdr *udp_hdr = NULL;
    size_t pkt_size;
    int port;
	uint32_t flows;
    struct lcore_queue_conf *qconf;

    lcore_id = rte_lcore_id();

    qconf = &lcore_queue_conf[lcore_id];

	while (1) {
		if (likely(traffic_status == 0))
		{
			sleep (1);
			continue;
		}
		for (flows=0;flows<pblast.flows;flows++)
		{
			pkt_size = 0;
			port = rte_lcore_to_socket_id(lcore_id);
			m = rte_pktmbuf_alloc(packet_pool);

			if (likely(pblast.l3protocol == TCP))
			{
				tcp_hdr = rte_pktmbuf_mtod(m, struct tcp_hdr *);
				tcp_hdr->src_port = htons(9000);
				tcp_hdr->dst_port = htons(80);
				tcp_hdr->sent_seq = 0;
				tcp_hdr->data_off = 0x50;
				tcp_hdr->rx_win = htons(65535);;
				tcp_hdr->tcp_flags = 0x02;
			}
			else if (likely(pblast.l3protocol == UDP))
			{
				udp_hdr = rte_pktmbuf_mtod(m, struct udp_hdr *);
			}

			if (likely(pblast.l2protocol == IPV4))
			{
				ipv4_hdr = (struct ipv4_hdr *) rte_pktmbuf_prepend(m, (uint16_t)sizeof(struct ipv4_hdr));
				memcpy(&ipv4_hdr->src_addr, &src_addr[flows], 4);
				memcpy(&ipv4_hdr->dst_addr, dst_addr, 4);
				ipv4_hdr->version_ihl = (IPv4_VERSION << 4) | (sizeof(struct ipv4_hdr) /4);
				ipv4_hdr->next_proto_id = 6;
				ipv4_hdr->time_to_live = 64;
				ipv4_hdr->type_of_service = 0;
				ipv4_hdr->packet_id = htonl (54321);
				ipv4_hdr->fragment_offset = 0;
				ipv4_hdr->total_length = htons(sizeof(struct ipv4_hdr) + sizeof(struct tcp_hdr));
				ipv4_hdr->hdr_checksum = 0;
				ipv4_hdr->hdr_checksum = rte_ipv4_phdr_cksum(ipv4_hdr, ipv4_hdr->total_length >> 1);
//				pkt_size += sizeof(struct ipv4_hdr);
			} else if (likely(pblast.l2protocol == IPV6))
			{
			}

			if (likely(pblast.l3protocol == TCP))
			{
				tcp_hdr->cksum = 0;
				tcp_hdr->cksum = rte_ipv4_udptcp_cksum(ipv4_hdr,tcp_hdr);
				tcp_hdr->cksum = rte_ipv4_phdr_cksum(ipv4_hdr,20);
//				pkt_size += sizeof(struct tcp_hdr);
			}

			if (likely(pblast.l1protocol == ETHERNET))
			{
				eth_hdr = (struct ether_hdr *) rte_pktmbuf_prepend(m, (uint16_t)sizeof(struct ether_hdr));
				memcpy(&eth_hdr->s_addr, &src_mac[flows], ETHER_ADDR_LEN);
				memcpy(&eth_hdr->d_addr, dst_mac, ETHER_ADDR_LEN);

				if (likely(pblast.l2protocol == IPV4))
					eth_hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);
				else if (likely(pblast.l2protocol == IPV6))
					eth_hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv6);
				else if (likely(pblast.l2protocol == ARP))
					eth_hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_ARP);
				else if (likely(pblast.l2protocol == RARP))
					eth_hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_RARP);
				else if (likely(pblast.l2protocol == VLAN))
					eth_hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_VLAN);

//				pkt_size += sizeof(struct ether_hdr) + pblast.payloadSize;
			}

			pkt_size = pblast.payloadSize;
			m->data_len = pkt_size;
			m->pkt_len = pkt_size;

			mcast_send_pkt(m, qconf, port);
		}
    }
}

int configPackets(unsigned char config[])
{
	//Skip first byte
	int index = 1, i;
	uint8_t srcMac[ETHER_ADDR_LEN];
	uint8_t srcAddr[4];

	pblast.flows = ((config[index] << 24) & 0xFF000000)
		         | ((config[index+1] << 16) & 0x00FF0000)
		         | ((config[index+2] <<  8) & 0x0000FF00)
		         | ( config[index+3]        & 0x000000FF);
	index += 4;
	pblast.streams = ((config[index] <<  8) & 0xFF00)
		           | ( config[index+1]        & 0x00FF);
	index += 2;
	pblast.l1protocol = config[index++];
	pblast.l2protocol = config[index++];
	pblast.l3protocol = config[index++];
	pblast.payloadSize = ((config[index] <<  8) & 0xFF00)
		               | ( config[index+1]        & 0x00FF) ;
	index += 2;

	for (i=0;i<6;i++)
		srcMac[i] = config[index++];

	for (i=0;i<6;i++)
		dst_mac[i] = config[index++];

	for (i=0;i<4;i++)
		srcAddr[i] = config[index++];

	for (i=0;i<4;i++)
		dst_addr[i] = config[index++];

	generateMacIPaddress(srcMac, srcAddr);
	return 0;
}

void generateMacIPaddress(uint8_t macaddr[], uint8_t ipaddr[])
{
	uint32_t i;
	unsigned int mac_incr_idx = ETHER_ADDR_LEN - 1, mac_parent_idx = ETHER_ADDR_LEN - 2 ;
	unsigned int ip_incr_idx = 3, ip_parent_idx = 2 ;
	unsigned char mac_addr[ETHER_ADDR_LEN], ip_addr[4] ;

	memcpy( &ip_addr[0], &ipaddr, 4) ;
	memcpy( &mac_addr[0], &macaddr, sizeof(unsigned char)*ETHER_ADDR_LEN) ;

	for(i=0; i<pblast.flows; i++)
	{
	    //IP Address
		if ( ip_addr[ip_incr_idx] == MAX_U8_VAL ) {
			if ( ip_addr[ip_parent_idx] == MAX_U8_VAL) {
				ip_addr[ip_parent_idx] = 0 ;
				ip_parent_idx--;
			}
			ip_addr[ip_parent_idx] +=1 ;
			ip_addr[ip_incr_idx] = 0 ;
		} else{
			ip_addr[ip_incr_idx]++ ;
		}
		memcpy( &src_addr[i][0], &ip_addr, 4) ;

		//MAC Address
		if ( mac_addr[mac_incr_idx] == MAX_U8_VAL ){
			if ( mac_addr[mac_parent_idx] == MAX_U8_VAL) {
				mac_addr[mac_parent_idx] = 0 ;
				mac_parent_idx--;
			}
			mac_addr[mac_parent_idx] +=1 ;
			mac_addr[mac_incr_idx] = 0 ;
		} else{
			mac_addr[mac_incr_idx]++ ;
		}

		memcpy( &src_mac[i][0], &mac_addr, sizeof(unsigned char)*ETHER_ADDR_LEN) ;
	}
}
