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

uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

// mask of enabled ports
uint32_t enabled_port_mask = 0;

uint8_t nb_ports = 0;

int rx_queue_per_lcore = 1;

int traffic_status = 0;

// Master processing loop
int master_loop(void )
{
    int guiSocket, index, portid;
    int num_fds;
    uint64_t prev_tsc = 0, cur_tsc, diff_tsc;
    struct pollfd my_fds;
    char port[] = "ports";
    char port_name[10];
    int port_status = 1;

    int guiPort = 8454, statsPort = 7454;
    char statsServer[] = "192.168.44.2";

    if (database_connect(port)) {
        rte_exit(EXIT_FAILURE, "Cannot connect to Database\n");
    }

    //Update Database with Port Info
    for (portid = 0; portid < nb_ports; portid++) {
        sprintf(port_name,"Port %d",portid);
        insert_table(portid, port_name, port_status);
    }

    statsSocket = udpclient(statsServer, statsPort);
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
                //printf("Traffic Status %d\n",traffic_status);
                break;
            case CONFIG_TRAFFIC:
                configPackets(buffer);
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
    printf("\n======  ============  ============  ============  ============\n"
           " Port    rx_packets    tx_packets    tx_dropped    tx_thr_put \n"
           "------  ------------  ------------  ------------  ------------\n");
    for (i = 0; i < nb_ports; i++) {
        unsigned char buffer[512];
        int size =  0, j;
        uint64_t rx_packets = port_stats[i].rx_packets;
        uint64_t tx_packets = port_stats[i].tx_packets;
        uint64_t tx_dropped = port_stats[i].tx_dropped;
        port_stats[i].rx_packets = 0;
        port_stats[i].tx_packets = 0;
        port_stats[i].tx_dropped = 0;

        buffer[size++] = i;
        for(j = 0; j < 8; j++) buffer[size++] = tx_packets >> (8-1-j)*8;
        for(j = 0; j < 8; j++) buffer[size++] = tx_dropped >> (8-1-j)*8;
        for(j = 0; j < 8; j++) buffer[size++] = rx_packets >> (8-1-j)*8;
        send_msg ( statsSocket, buffer, size);
        printf("%3d %13"PRIu64" %13"PRIu64" %13"PRIu64" %15"PRIu64"\n", i,
                        rx_packets, tx_packets, tx_dropped, tx_packets*pblast.payloadSize*8);
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

            rx_lcore_id++;
            qconf = &lcore_queue_conf[rx_lcore_id];

            if (rx_lcore_id >= RTE_MAX_LCORE)
                rte_exit(EXIT_FAILURE, "Not enough cores\n");
        }
        qconf->rx_queue_list[qconf->n_rx_queue] = portid;
        qconf->n_rx_queue++;

        /* init port */
        printf("Initializing port %d on lcore %u... ", portid, rx_lcore_id);
        fflush(stdout);

        n_tx_queue = nb_lcores;
        if (n_tx_queue > MAX_TX_QUEUE_PER_PORT)
            n_tx_queue = MAX_TX_QUEUE_PER_PORT;
        ret = rte_eth_dev_configure(portid, 1, (uint16_t)n_tx_queue,
                        &port_conf);
        if (ret < 0)
            rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%d\n", ret, portid);

        rte_eth_macaddr_get(portid, &ports_eth_addr[portid]);
        print_ethaddr(" Address:", &ports_eth_addr[portid]);
        printf(", ");

        // init one RX queue
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

        // init one TX queue per couple (lcore,port)
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
