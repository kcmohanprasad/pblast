#ifndef MISC_H
#define MISC_H 1

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

#define MAX_LEN                    1500

#define START_TRAFFIC              1
#define STOP_TRAFFIC               2
#define CONFIG_TRAFFIC             3
#define SEND_TX_PPS                11

#define ARPHRD_ETHER               1
#define ARP_OP_REQUEST             1
#define ARP_OP_REPLY               2
#define RARP_OP_REQUEST            3
#define RARP_OP_REPLY              4

// L1 Protocols
#define ETHERNET                   1
#define RAW                        2

// L2 Protocols
#define IPV4                       1
#define IPV6                       2
#define ARP                        3
#define RARP                       4
#define VLAN                       5

// L3 Protocols
#define ICMP                       1
#define IGMP                       2
#define TCP                        6
#define EGP                        8
#define IGP                        9
#define UDP                        17
#define RDP                        27
#define IPv6_ICMP                  58
#define EIGRP                      88
#define OSPF                       89
#define MTP                        92
#define IPIP                       94
#define SCTP                       132

#define MAX_HOSTS                  5000

#define RTE_LOGTYPE_IPv4_MULTICAST RTE_LOGTYPE_USER1

#define TIMER_RESOLUTION_CYCLES 20000000ULL
struct rte_timer timer;

#define MAX_PORTS 16

#define IPv4_VERSION    4
#define IPv6_VERSION    6

#define    MCAST_CLONE_PORTS    2
#define    MCAST_CLONE_SEGS    2

#define    PKT_MBUF_SIZE    (2048 + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define    NB_PKT_MBUF    8192

#define    HDR_MBUF_SIZE    (sizeof(struct rte_mbuf) + 2 * RTE_PKTMBUF_HEADROOM)
#define    NB_HDR_MBUF    (NB_PKT_MBUF * MAX_PORTS)

#define    CLONE_MBUF_SIZE    (sizeof(struct rte_mbuf))
#define    NB_CLONE_MBUF    (NB_PKT_MBUF * MCAST_CLONE_PORTS * MCAST_CLONE_SEGS * 2)

// allow max jumbo frame 9.5 KB
#define    JUMBO_FRAME_MAX_SIZE    0x2600

#define MAX_PKT_BURST 32
//#define BURST_TX_DRAIN_US 100 // TX drain every ~100us

// Configure how many packets ahead to prefetch, when reading packets
#define PREFETCH_OFFSET    3

// Configurable number of RX/TX ring descriptors

#define RTE_TEST_RX_DESC_DEFAULT 128
#define RTE_TEST_TX_DESC_DEFAULT 512

// ethernet addresses of ports
struct ether_addr ports_eth_addr[MAX_PORTS];

struct mbuf_table {
    uint16_t len;
    struct rte_mbuf *m_table[MAX_PKT_BURST];
};

#define MAX_RX_QUEUE_PER_LCORE 16
#define MAX_TX_QUEUE_PER_PORT 16
struct lcore_queue_conf {
    uint64_t tx_tsc;
    uint16_t n_rx_queue;
    uint8_t rx_queue_list[MAX_RX_QUEUE_PER_LCORE];
    uint16_t tx_queue_id[MAX_PORTS];
    struct mbuf_table tx_mbufs[MAX_PORTS];
} __rte_cache_aligned;
struct lcore_queue_conf lcore_queue_conf[RTE_MAX_LCORE];

static const struct rte_eth_conf port_conf = {
    .rxmode = {
        .max_rx_pkt_len = JUMBO_FRAME_MAX_SIZE,
        .split_hdr_size = 0,
        .header_split   = 0, /**< Header Split disabled */
        .hw_ip_checksum = 0, /**< IP checksum offload disabled */
        .hw_vlan_filter = 0, /**< VLAN filtering disabled */
        .jumbo_frame    = 1, /**< Jumbo Frame Support enabled */
        .hw_strip_crc   = 0, /**< CRC stripped by hardware */
    },
    .txmode = {
        .mq_mode = ETH_MQ_TX_NONE,
    },
};

struct rte_mempool *packet_pool;

struct port_stats {
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t tx_dropped;
};
struct port_stats port_stats[RTE_MAX_ETHPORTS];

//struct rte_fbk_hash_table *mcast_hash = NULL;

struct mcast_group_params {
    uint32_t ip;
    uint16_t port_mask;
};

int statsSocket;

struct pblast_protocol
{
	uint8_t l1protocol;
	uint8_t l2protocol;
	uint8_t l3protocol;
	uint8_t l4protocol;
	uint32_t flows;
	uint16_t streams;
	uint32_t payloadSize;
}pblast;

#define MAX_U8_VAL                      0xff
//Default MAC & IP Address
//uint8_t def_dst_mac[ETHER_ADDR_LEN] = {0x90, 0xe2, 0xba, 0x1a, 0x2f, 0xf2};
//uint8_t def_src_mac[ETHER_ADDR_LEN] = {0x00, 0x01, 0x02, 0x03, 0x00, 0x01};
//uint8_t def_dst_addr[4] = {0xa, 0x00, 0x00, 0xfe};
//uint8_t def_src_addr[4] = {0xa, 0x00, 0x00, 0x01};

uint8_t src_mac[MAX_HOSTS][ETHER_ADDR_LEN];
uint8_t dst_mac[ETHER_ADDR_LEN];
uint8_t src_addr[MAX_HOSTS][4];
uint8_t dst_addr[4];

#define N_MCAST_GROUPS \
    (sizeof (mcast_group_table) / sizeof (mcast_group_table[0]))

void send_burst(struct lcore_queue_conf *qconf, uint8_t port);
void mcast_send_pkt(struct rte_mbuf *pkt, struct lcore_queue_conf *qconf, uint8_t port);
int main_loop(__rte_unused void *dummy);


int master_loop(void );

//Socket Methods
int udpsocket( int myport );
int udpclient( char* host, int theirport );
void send_msg ( int sockfd, unsigned char * outMsgP, int len);
int recv_msg(int sockfd, unsigned char * inBufP );

//Database Methods
int database_connect( char *database);

int insert_table(int port, char *port_name, int status);

int update_table(int port, int status);

int configPackets(unsigned char config[]);
void generateMacIPaddress(uint8_t macaddr[], uint8_t ipaddr[]);

#endif
