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

#define RTE_LOGTYPE_IPv4_MULTICAST RTE_LOGTYPE_USER1

#define TIMER_RESOLUTION_CYCLES 20000000ULL
static struct rte_timer timer;

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
static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

// ethernet addresses of ports
static struct ether_addr ports_eth_addr[MAX_PORTS];

// mask of enabled ports
static uint32_t enabled_port_mask = 0;

static uint8_t nb_ports = 0;

static int rx_queue_per_lcore = 1;

struct mbuf_table {
    uint16_t len;
    struct rte_mbuf *m_table[MAX_PKT_BURST];
};

int traffic_status = 1;

#define MAX_RX_QUEUE_PER_LCORE 16
#define MAX_TX_QUEUE_PER_PORT 16
struct lcore_queue_conf {
    uint64_t tx_tsc;
    uint16_t n_rx_queue;
    uint8_t rx_queue_list[MAX_RX_QUEUE_PER_LCORE];
    uint16_t tx_queue_id[MAX_PORTS];
    struct mbuf_table tx_mbufs[MAX_PORTS];
} __rte_cache_aligned;
static struct lcore_queue_conf lcore_queue_conf[RTE_MAX_LCORE];

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

static struct rte_mempool *packet_pool;

struct port_stats {
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t tx_dropped;
};
static struct port_stats port_stats[RTE_MAX_ETHPORTS];

struct rte_fbk_hash_table *mcast_hash = NULL;

struct mcast_group_params {
    uint32_t ip;
    uint16_t port_mask;
};


#define N_MCAST_GROUPS \
    (sizeof (mcast_group_table) / sizeof (mcast_group_table[0]))


// Send burst of packets on an output interface
static void send_burst(struct lcore_queue_conf *qconf, uint8_t port)
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
static inline void
mcast_send_pkt(struct rte_mbuf *pkt, struct lcore_queue_conf *qconf, uint8_t port)
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
static int main_loop(__rte_unused void *dummy)
{
    unsigned lcore_id;
    struct rte_mbuf *m;
    struct ether_hdr *eth_hdr;
    struct ipv4_hdr *ipv4_hdr;
    struct tcp_hdr *tcp_hdr;
    size_t pkt_size;
    int port;
    struct lcore_queue_conf *qconf;

    lcore_id = rte_lcore_id();

    qconf = &lcore_queue_conf[lcore_id];

    uint8_t dst_mac[ETHER_ADDR_LEN] = {0x90, 0xe2, 0xba, 0x1a, 0x2f, 0xf2};
    uint8_t src_mac[ETHER_ADDR_LEN] = {0x90, 0xe2, 0xba, 0x1a, 0x4a, 0xe4};
    uint8_t dst_addr[4] = {0xa, 0x00, 0x00, 0xbb};
    uint8_t src_addr[4] = {0xa, 0x00, 0x00, 0xba};

	while (1) {
		if (traffic_status == 0)
		{
			sleep (1);
			continue;
		}

		//for(count =0;count<256;count++) {
        port = rte_lcore_to_socket_id(lcore_id);
        m = rte_pktmbuf_alloc(packet_pool);
        pkt_size = sizeof(struct ether_hdr) + sizeof(struct ipv4_hdr) + sizeof(struct tcp_hdr);
        m->data_len = pkt_size;
        m->pkt_len = pkt_size;

        tcp_hdr = rte_pktmbuf_mtod(m, struct tcp_hdr *);
        tcp_hdr->src_port = htons(9000);
        tcp_hdr->dst_port = htons(80);
        tcp_hdr->sent_seq = 0;
        tcp_hdr->data_off = 0x50;
        tcp_hdr->rx_win = htons(65535);;
        tcp_hdr->tcp_flags = 0x02;

        //ipv4_hdr = rte_pktmbuf_mtod(m, struct ipv4_hdr *);
        ipv4_hdr = (struct ipv4_hdr *) rte_pktmbuf_prepend(m, (uint16_t)sizeof(struct ipv4_hdr));
        memcpy(&ipv4_hdr->src_addr,src_addr,4);
        memcpy(&ipv4_hdr->dst_addr,dst_addr,4);
        ipv4_hdr->version_ihl = (IPv4_VERSION << 4) | (sizeof(struct ipv4_hdr) /4);
        ipv4_hdr->next_proto_id = 6;
        ipv4_hdr->time_to_live = 64;
        ipv4_hdr->type_of_service = 0;
        ipv4_hdr->packet_id = htonl (54321);
        ipv4_hdr->fragment_offset = 0;
        ipv4_hdr->total_length = htons(sizeof(struct ipv4_hdr) + sizeof(struct tcp_hdr));
        ipv4_hdr->hdr_checksum = 0;
        ipv4_hdr->hdr_checksum = rte_ipv4_phdr_cksum(ipv4_hdr, ipv4_hdr->total_length >> 1);

        tcp_hdr->cksum = 0;
        tcp_hdr->cksum = rte_ipv4_udptcp_cksum(ipv4_hdr,tcp_hdr);
        tcp_hdr->cksum = rte_ipv4_phdr_cksum(ipv4_hdr,20);

        //eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);
        eth_hdr = (struct ether_hdr *) rte_pktmbuf_prepend(m, (uint16_t)sizeof(struct ether_hdr));
        memcpy(&eth_hdr->s_addr, src_mac, ETHER_ADDR_LEN);
        memcpy(&eth_hdr->d_addr, dst_mac, ETHER_ADDR_LEN);
        eth_hdr->ether_type = rte_cpu_to_be_16(ETHER_TYPE_IPv4);

        mcast_send_pkt(m, qconf, port);
    }
}

// Master processing loop
int master_loop(void )
{
    int guiSocket, index, portid, i;
    int guiPort = 8454, num_fds;
    uint64_t prev_tsc = 0, cur_tsc, diff_tsc;
	struct pollfd my_fds;
	char port[] = "ports";
	char port_name[10];
	int port_status = 1;

	if (database_connect(port)) {
		rte_exit(EXIT_FAILURE, "Cannot connect to Database\n");
	}

	//Update Database with Port Info
    for (portid = 0; portid < nb_ports; portid++) {
		sprintf(port_name,"Port %d",portid);
		insert_table(portid, port_name, port_status);
	}

	guiSocket = udpsocket(guiPort);

	my_fds.fd = guiSocket;
	my_fds.events = POLLIN;
	my_fds.revents = 0;
	num_fds = 1;

    while(1)
    {
		//Timer
        cur_tsc = rte_rdtsc();
        diff_tsc = cur_tsc - prev_tsc;
        if (diff_tsc > TIMER_RESOLUTION_CYCLES) {
            rte_timer_manage();
            prev_tsc = cur_tsc;
        }

		if (poll(&my_fds, num_fds, 100) == -1)
		{
    		perror("poll");
    		exit(0);
    	}
		if (my_fds.revents != POLLIN)
			continue;

        unsigned char buffer[512];
        int size = recv_msg(guiSocket,buffer);
        if (size <= 0)
            continue;

		index = 0;

//printf("Size %d\n",size);
//for (i=0;i<size ;i++ )
//    printf(" %d ",buffer[i]);
//printf("\nSize %d\n",size);
        switch (buffer[index++]) {
			case START_TRAFFIC:
				traffic_status = 1;
				printf("Traffic Status %d\n",traffic_status);
				break;
			case STOP_TRAFFIC:
				traffic_status = 0;
				printf("Traffic Status %d\n",traffic_status);
				break;
			case CONFIG_TRAFFIC:
				traffic_status = 0;
				printf("Traffic Status %d\n",traffic_status);
				break;
			case SEND_TX_PPS:
				index = 0;
				//buffer[index++] = 54;
				for(i = 0; i < 8; i++) buffer[index++] = port_stats[0].tx_packets >> (8-1-i)*8;
				send_msg(guiSocket,buffer,index);
				printf("Sent Tx packets to UI\n");
				break;
		}
	}
}

/* display usage */
static void
print_usage(const char *prgname)
{
    printf("%s [EAL options] -- -p PORTMASK [-q NQ]\n"
        "  -p PORTMASK: hexadecimal bitmask of ports to configure\n"
        "  -q NQ: number of queue (=ports) per lcore (default is 1)\n",
        prgname);
}

static uint32_t
parse_portmask(const char *portmask)
{
    char *end = NULL;
    unsigned long pm;

    /* parse hexadecimal string */
    pm = strtoul(portmask, &end, 16);
    if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0'))
        return 0;

    return ((uint32_t)pm);
}

static int
parse_nqueue(const char *q_arg)
{
    char *end = NULL;
    unsigned long n;

    /* parse numerical string */
    errno = 0;
    n = strtoul(q_arg, &end, 0);
    if (errno != 0 || end == NULL || *end != '\0' ||
            n == 0 || n >= MAX_RX_QUEUE_PER_LCORE)
        return (-1);

    return (n);
}

/* Parse the argument given in the command line of the application */
static int
parse_args(int argc, char **argv)
{
    int opt, ret;
    char **argvopt;
    int option_index;
    char *prgname = argv[0];
    static struct option lgopts[] = {
        {NULL, 0, 0, 0}
    };

    argvopt = argv;

    while ((opt = getopt_long(argc, argvopt, "p:q:",
                  lgopts, &option_index)) != EOF) {

        switch (opt) {
        /* portmask */
        case 'p':
            enabled_port_mask = parse_portmask(optarg);
            if (enabled_port_mask == 0) {
                printf("invalid portmask\n");
                print_usage(prgname);
                return -1;
            }
            break;

        /* nqueue */
        case 'q':
            rx_queue_per_lcore = parse_nqueue(optarg);
            if (rx_queue_per_lcore < 0) {
                printf("invalid queue number\n");
                print_usage(prgname);
                return -1;
            }
            break;

        default:
            print_usage(prgname);
            return -1;
        }
    }

    if (optind >= 0)
        argv[optind-1] = prgname;

    ret = optind-1;
    optind = 0; /* reset getopt lib */
    return ret;
}

static void
print_ethaddr(const char *name, struct ether_addr *eth_addr)
{
    char buf[ETHER_ADDR_FMT_SIZE];
    ether_format_addr(buf, ETHER_ADDR_FMT_SIZE, eth_addr);
    printf("%s%s", name, buf);
}

/* Check the link status of all ports in up to 9s, and print them finally */
static void
check_all_ports_link_status(uint8_t port_num, uint32_t port_mask)
{
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
    uint8_t portid, count, all_ports_up, print_flag = 0;
    struct rte_eth_link link;

    printf("\nChecking link status");
    fflush(stdout);
    for (count = 0; count <= MAX_CHECK_TIME; count++) {
        all_ports_up = 1;
        for (portid = 0; portid < port_num; portid++) {
            if ((port_mask & (1 << portid)) == 0)
                continue;
            memset(&link, 0, sizeof(link));
            rte_eth_link_get_nowait(portid, &link);
            /* print link status if flag set */
            if (print_flag == 1) {
                if (link.link_status)
                    printf("Port %d Link Up - speed %u "
                        "Mbps - %s\n", (uint8_t)portid,
                        (unsigned)link.link_speed,
                (link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
                    ("full-duplex") : ("half-duplex\n"));
                else
                    printf("Port %d Link Down\n",
                            (uint8_t)portid);
                continue;
            }
            /* clear all_ports_up flag if any link down */
            if (link.link_status == 0) {
                all_ports_up = 0;
                break;
            }
        }
        /* after finally printing all link status, get out */
        if (print_flag == 1)
            break;

        if (all_ports_up == 0) {
            printf(".");
            fflush(stdout);
            rte_delay_ms(CHECK_INTERVAL);
        }

        /* set the print_flag if all ports up or timeout */
        if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) {
            print_flag = 1;
            printf("done\n");
        }
    }
}

// timer callback
static void display_statistics(__attribute__((unused)) struct rte_timer *tim,
      __attribute__((unused)) void *arg)
{
    int i;
    printf("======  ============  ============  ============  ============\n"
           " Port    rx_packets    tx_packets    tx_dropped    tx_thr_put \n"
           "------  ------------  ------------  ------------  ------------\n");
    for (i = 0; i < nb_ports; i++) {
        printf("%3d %13"PRIu64" %13"PRIu64" %13"PRIu64" %15"PRIu64"\n", i,
                        port_stats[i].rx_packets,
                        port_stats[i].tx_packets,
                        port_stats[i].tx_dropped,
                        port_stats[i].tx_packets*54*8);
        port_stats[i].rx_packets = 0;
        port_stats[i].tx_packets = 0;
        port_stats[i].tx_dropped = 0;
    }
}


int main(int argc, char **argv)
{
    struct lcore_queue_conf *qconf;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf *txconf;
    int ret;
    uint16_t queueid;
    unsigned lcore_id = 0, rx_lcore_id = 0;
    uint32_t n_tx_queue, nb_lcores;
    uint8_t portid;
    uint64_t hz;

    // init EAL
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Invalid EAL parameters\n");
    argc -= ret;
    argv += ret;

    // parse application arguments (after the EAL ones)
    ret = parse_args(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "Invalid IPV4_MULTICAST parameters\n");

    // init RTE timer library
    rte_timer_subsystem_init();

    // init timer structures
    rte_timer_init(&timer);

    // load timer, every second, on master lcore, reloaded automatically
    hz = rte_get_timer_hz();
    lcore_id = rte_lcore_id();
    rte_timer_reset(&timer, hz, PERIODICAL, lcore_id, display_statistics, NULL);

    memset(&port_stats, 0, sizeof(port_stats));

    // create the mbuf pools
    packet_pool = rte_mempool_create("packet_pool", NB_PKT_MBUF,
        PKT_MBUF_SIZE, 32, sizeof(struct rte_pktmbuf_pool_private),
        rte_pktmbuf_pool_init, NULL, rte_pktmbuf_init, NULL,
        rte_socket_id(), 0);

    if (packet_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot init packet mbuf pool\n");

    nb_ports = rte_eth_dev_count();
    if (nb_ports == 0)
        rte_exit(EXIT_FAILURE, "No physical ports!\n");
    if (nb_ports > MAX_PORTS)
        nb_ports = MAX_PORTS;

    nb_lcores = rte_lcore_count();

    // initialize all ports
    for (portid = 0; portid < nb_ports; portid++) {
        // skip ports that are not enabled
        if ((enabled_port_mask & (1 << portid)) == 0) {
            printf("Skipping disabled port %d\n", portid);
            continue;
        }

        qconf = &lcore_queue_conf[rx_lcore_id];

        /* get the lcore_id for this port */
        while (rte_lcore_is_enabled(rx_lcore_id) == 0 ||
               qconf->n_rx_queue == (unsigned)rx_queue_per_lcore) {

            rx_lcore_id ++;
            qconf = &lcore_queue_conf[rx_lcore_id];

            if (rx_lcore_id >= RTE_MAX_LCORE)
                rte_exit(EXIT_FAILURE, "Not enough cores\n");
        }
        qconf->rx_queue_list[qconf->n_rx_queue] = portid;
        qconf->n_rx_queue++;

        /* init port */
        printf("Initializing port %d on lcore %u... ", portid,
               rx_lcore_id);
        fflush(stdout);

        n_tx_queue = nb_lcores;
        if (n_tx_queue > MAX_TX_QUEUE_PER_PORT)
            n_tx_queue = MAX_TX_QUEUE_PER_PORT;
        ret = rte_eth_dev_configure(portid, 1, (uint16_t)n_tx_queue,
                        &port_conf);
        if (ret < 0)
            rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%d\n",
                  ret, portid);

        rte_eth_macaddr_get(portid, &ports_eth_addr[portid]);
        print_ethaddr(" Address:", &ports_eth_addr[portid]);
        printf(", ");

        /* init one RX queue */
        queueid = 0;
        printf("rxq=%hu ", queueid);
        fflush(stdout);
        ret = rte_eth_rx_queue_setup(portid, queueid, nb_rxd,
                         rte_eth_dev_socket_id(portid),
                         NULL,
                         packet_pool);
        if (ret < 0)
            rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup: err=%d, port=%d\n",
                  ret, portid);

        /* init one TX queue per couple (lcore,port) */
        queueid = 0;

        RTE_LCORE_FOREACH(lcore_id) {
            if (rte_lcore_is_enabled(lcore_id) == 0)
                continue;
            printf("txq=%u,%hu ", lcore_id, queueid);
            fflush(stdout);

            rte_eth_dev_info_get(portid, &dev_info);
            txconf = &dev_info.default_txconf;
            txconf->txq_flags = 0;
            ret = rte_eth_tx_queue_setup(portid, queueid, nb_txd,
                             rte_lcore_to_socket_id(lcore_id), txconf);
            if (ret < 0)
                rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup: err=%d, "
                      "port=%d\n", ret, portid);

            qconf = &lcore_queue_conf[lcore_id];
            qconf->tx_queue_id[portid] = queueid;
            queueid++;
        }

        /* Start device */
        ret = rte_eth_dev_start(portid);
        if (ret < 0)
            rte_exit(EXIT_FAILURE, "rte_eth_dev_start: err=%d, port=%d\n",
                  ret, portid);

        printf("done:\n");
    }

    check_all_ports_link_status(nb_ports, enabled_port_mask);

    /* launch per-lcore init on every lcore */
    //rte_eal_mp_remote_launch(main_loop, NULL, CALL_MASTER);
    RTE_LCORE_FOREACH_SLAVE(lcore_id) {
        rte_eal_remote_launch(main_loop, NULL, lcore_id);
    }
    master_loop();
    RTE_LCORE_FOREACH_SLAVE(lcore_id) {
        if (rte_eal_wait_lcore(lcore_id) < 0)
            return -1;
    }

    return 0;
}
