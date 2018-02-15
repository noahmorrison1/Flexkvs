#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_launch.h>
#include <rte_memcpy.h>
#include <rte_lcore.h>

#include "iokvs.h"
#include "database.h"
#include "tester.h"
#include "global.h"

/* Use sockets */
#ifndef LINUX_SOCKETS
#define LINUX_SOCKETS 1
#endif

/* If linux, use recvmmsg instead of recvmsg (enables batching) */
#ifndef LINUX_RECVMMSG
#define LINUX_RECVMMSG 1
#define BATCH_MAX 32
#endif

/* Enable DPDK */
#ifndef LINUX_DPDK
#define LINUX_DPDK 0
#endif

/* Enable key-based steering if DPDK is enabled */
#ifndef DPDK_IPV6
#define DPDK_IPV6 0
#endif

/* DPDK batch size */
#ifndef BATCH_MAX
#define BATCH_MAX 32
#endif

#ifndef ENABLE_PREFETCHING
#define ENABLE_PREFETCHING 0
#endif

#define DPDK_V4_HWXSUM 1
#define MAX_MSGSIZE 2048



/******************************************************************************/
/* Linux Sockets */

#define __USE_GNU
#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "protocol_binary.h"



static int sock_fd = -1;

#define MAX_SENDBUF_SIZE (256 * 1024 * 1024)

static void maximize_sndbuf(const int sfd)
{
    socklen_t intsize = sizeof(int);
    int last_good = 0;
    int min, max, avg;
    int old_size;

    /* Start with the default size. */
    if (getsockopt(sfd, SOL_SOCKET, SO_SNDBUF, &old_size, &intsize) != 0) {
        perror("getsockopt SO_SNDBUF");
        abort();
    }

    /* Binary-search for the real maximum. */
    min = old_size;
    max = MAX_SENDBUF_SIZE;

    while (min <= max) {
        avg = ((unsigned int)(min + max)) / 2;
        if (setsockopt(sfd, SOL_SOCKET, SO_SNDBUF, (void *)&avg, intsize) == 0) {
            last_good = avg;
            min = avg + 1;
        } else {
            max = avg - 1;
        }
    }

    fprintf(stderr, "<%d send buffer was %d, now %d\n", sfd, old_size, last_good);
}

static void network_init(void)
{
    int flags;
    struct sockaddr_in sin;
    printf("Maybe...\n");
    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("creating socket failed");
        abort();
    }
	
    printf("Maybe...2 \n");
#if 0
    if ((flags = fcntl(sock_fd, F_GETFL, 0)) < 0 ||
            fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        perror("setting O_NONBLOCK");
        abort();
    }
#endif

    flags = 1;
    printf("HERE1 \n");
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags));
    maximize_sndbuf(sock_fd);
    printf("HERE2 \n");

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    printf("NET: %x, \n",AF_INET);
    sin.sin_port = htons(11211);
    printf("HERE3 \n");
    //sin.sin_addr.s_addr = inet_addr("192.168.1.103");
    sin.sin_addr.s_addr = inet_addr("10.0.0.6");

    if (bind(sock_fd, (struct sockaddr *) &sin, sizeof(sin)) == -1) {
        perror("bind failed");
        abort();
    }

    struct ifreq ifr;
    ifr.ifr_flags |= IFF_PROMISC;
    if( ioctl(sock_fd, SIOCSIFFLAGS, &ifr) != 0 )
    {
        // handle error here
        printf("COULDNT SET TO IFF_PROMISC \n");
        //exit(0);
    }

  printf("%d.%d.%d.%d\n",
  (int)(sin.sin_addr.s_addr&0xFF),
  (int)((sin.sin_addr.s_addr&0xFF00)>>8),
  (int)((sin.sin_addr.s_addr&0xFF0000)>>16),
  (int)((sin.sin_addr.s_addr&0xFF000000)>>24));
}

/******************************************************************************/


/******************************************************************************/
/* dpdk ipv4 */

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_arp.h>
#include <rte_ethdev.h>

#include "protocol_binary.h"


#define MY_IPADDR  0x0A000006 //cat1

struct iokvs_message_2 {
    struct ether_hdr ether;
    struct ipv4_hdr ipv4;
    struct udp_hdr udp;
    memcached_udp_header mcudp;
    protocol_binary_request_header mcr;
    uint8_t payload[];
} __attribute__ ((packed));


struct iokvs_message {
    memcached_udp_header mcudp;
    protocol_binary_request_header mcr;
    uint8_t payload[];
} __attribute__ ((packed));

static const struct rte_eth_conf eth_config = {
    .rxmode = {
        .mq_mode = ETH_MQ_RX_RSS,
        .hw_ip_checksum = 1,
        .hw_strip_crc = 1,
    },
    .txmode = {
        .mq_mode = ETH_MQ_TX_NONE,
    },
    .rx_adv_conf = {
        .rss_conf = {
        },
    },
    .fdir_conf = {
        .mode = RTE_FDIR_MODE_NONE,
    },
};

static const struct rte_eth_txconf eth_txconf = {
    .txq_flags = ETH_TXQ_FLAGS_NOOFFLOADS,
};

static struct rte_mempool *mempool;
static struct ether_addr mymac;
static unsigned num_ports;





bool test = false;
static size_t sent_count = 0;
static size_t mid_count = 0;
static size_t big_count = 0;
static size_t small_count = 0;
static size_t rec_count = 0;

static int send_counts[4];
static int rec_counts[4];

static size_t packet_loop(int fd, void **bufs, struct item_allocator *ia)
{
    static bool stop_sending = false;
    if(stop_sending) return 0;


    struct mmsghdr msgs[BATCH_MAX];
    struct sockaddr_in sin[BATCH_MAX];
    struct iovec iovs[BATCH_MAX];


    struct iokvs_message *hdrs[BATCH_MAX];
    int msglens[BATCH_MAX];
    bool drops[BATCH_MAX] = { false };
    uint32_t hashes[BATCH_MAX];
    void *keys[BATCH_MAX];
    size_t keylens[BATCH_MAX];
    protocol_binary_command cmds[BATCH_MAX];
    size_t totlens[BATCH_MAX];
    struct ssd_line *rdits[BATCH_MAX] = { NULL };
    struct item *newits[BATCH_MAX] = { NULL };
    uint64_t times[BATCH_MAX];
    uint16_t status[BATCH_MAX] = { PROTOCOL_BINARY_RESPONSE_SUCCESS };

    int i, j, n;
    uint32_t blen;

    /* Prepare to receive batch */
    for (i = 0; i < BATCH_MAX; i++) {
        msgs[i].msg_hdr.msg_name = sin + i;
        msgs[i].msg_hdr.msg_namelen = sizeof(sin[0]);
        msgs[i].msg_hdr.msg_iov = iovs + i;
        msgs[i].msg_hdr.msg_iovlen = 1;
        msgs[i].msg_hdr.msg_control = NULL;
        msgs[i].msg_hdr.msg_controllen = 0;
        msgs[i].msg_hdr.msg_flags = 0;

        iovs[i].iov_base = bufs[i];
        iovs[i].iov_len = MAX_MSGSIZE;
        drops[i] = false;
        status[i] = PROTOCOL_BINARY_RESPONSE_SUCCESS;
    }

static int recived = 0;

    // Receive batch 
    if ((n = recvmmsg(fd, msgs, BATCH_MAX, MSG_DONTWAIT, NULL)) == 0 ||
            (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)))
    {
        return 0;
    } else if (n < 0) {
        perror("recvmmsg failed");
        abort();
    }


    for (i = 0; i < n; i++) {
        msglens[i] = msgs[i].msg_len;

        hdrs[i] = bufs[i];
    



        /*if (hdrs[i]->ether.ether_type == htons(ETHER_TYPE_IPv4) &&
                hdrs[i]->ipv4.next_proto_id == 17 &&
                hdrs[i]->udp.dst_port == htons(11211) &&
                msglens[i] >= sizeof(hdrs[0][0]))
        {
            drops[i] = false;
        } else {
            printf("DROPPED PACKET %d \n",i);
            drops[i] = true;
        }*/

    }
     

    /* Parse requests */
    for (i = 0; i < n; i++) {
        if (drops[i])
        {

            continue;
        }

        /* Ensure request has full header */
        if (msglens[i] < sizeof(hdrs[0][0])) {
            if (settings.verbose) {
                fprintf(stderr, "Request malformed\n");
            }
            drops[i] = true;

            continue;
        }

        blen = ntohl(hdrs[i]->mcr.request.bodylen);
        keylens[i] = ntohs(hdrs[i]->mcr.request.keylen);

        /* Ensure request is complete */
        if (blen < keylens[i] + hdrs[i]->mcr.request.extlen ||
                msglens[i] < sizeof(hdrs[0][0]) + blen) {
            if (settings.verbose) {
                fprintf(stderr, "Request incomplete: %u %u %zu %zu\n", blen,
                    msglens[i], keylens[i] + hdrs[i]->mcr.request.extlen,
                    sizeof(hdrs[0][0]) + blen);
            }

            drops[i] = true;
            continue;
        }

        /* Check UDP header */
        if (hdrs[i]->mcudp.n_data != htons(1)) {
            if (settings.verbose) {
                fprintf(stderr, "UDP multi request\n");
            }


            drops[i] = true;
            continue;
        }

        /* Check request magic number */
        if (hdrs[i]->mcr.request.magic != PROTOCOL_BINARY_REQ) {
            if (settings.verbose) {
                fprintf(stderr, "Invalid magic: %x\n",
                        hdrs[i]->mcr.request.magic);
            }
            drops[i] = true;

            continue;
        }

        /* Unknown command */
        if (hdrs[i]->mcr.request.opcode != PROTOCOL_BINARY_CMD_GET &&
                hdrs[i]->mcr.request.opcode != PROTOCOL_BINARY_CMD_SET)
        {
            if (settings.verbose) {
                fprintf(stderr, "Unknown opcode: %x\n",
                        hdrs[i]->mcr.request.opcode);
            }
            drops[i] = true;


            continue;
        }

        cmds[i] = hdrs[i]->mcr.request.opcode;
        keys[i] = hdrs[i]->payload + hdrs[i]->mcr.request.extlen;
        totlens[i] = ntohl(hdrs[i]->mcr.request.bodylen) -
        hdrs[i]->mcr.request.extlen;     
        //times[i] = hdrs[i]->mcr.request.cas;

    }




    /* Allocate new items for set requests and initialize*/
    for (i = 0; i < n; i++) {
        if (drops[i]) continue;
        if (cmds[i] == PROTOCOL_BINARY_CMD_SET) {
            NVDIMM_write_entry(keys[i],keylens[i],(char*)keys[i] + keylens[i] , totlens[i] - keylens[i], hashes[i]);
        }
    }

    /* Hash table lookups */
    for (i = 0; i < n; i++) {
        if (drops[i]) continue;
        if (cmds[i] == PROTOCOL_BINARY_CMD_GET) {
            rdits[i] = database_get(keys[i], keylens[i], hashes[i]);
            if (rdits[i] == NULL) {
                status[i] = htons(PROTOCOL_BINARY_RESPONSE_KEY_ENOENT);
                continue;
            }
        }
    }




    /* Prepare Responses */
    for (i = 0; i < n; i++) {
        if (drops[i])
        {
            printf("DROPPED i: \n",i);
             continue;
        }
        hdrs[i]->mcr.request.magic = PROTOCOL_BINARY_RES;
        hdrs[i]->mcr.request.keylen = 0;
        hdrs[i]->mcr.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
        hdrs[i]->mcr.request.status = status[i];

        if (cmds[i] == PROTOCOL_BINARY_CMD_GET) {
            //if(blen2) printf("COMMAND GET RECIEVED \n");
            msglens[i] = sizeof(hdrs[i][0]) + 4;
            hdrs[i]->mcr.request.extlen = 4;
            hdrs[i]->mcr.request.bodylen = htonl(4);
            *((uint32_t *) hdrs[i]->payload) = 0;
            if (rdits[i] != NULL) {
                msglens[i] += rdits[i]->vallen;
                hdrs[i]->mcr.request.bodylen = htonl(4 + rdits[i]->vallen);
                rte_memcpy(hdrs[i]->payload + 4, rdits[i]->val,
                        rdits[i]->vallen);
            }
        } else {
            //if(blen2) printf("COMMAND OTHER RECIEVED: %d \n",cmds[i]);

            msglens[i] = sizeof(hdrs[i][0]);
            hdrs[i]->mcr.request.extlen = 0;
            hdrs[i]->mcr.request.bodylen = 0;
        }

    }


    /* Release items */
    for (i = 0; i < n; i++) {
        if (drops[i]) continue;
        if (cmds[i] == PROTOCOL_BINARY_CMD_SET && newits[i] != NULL) {
            //item_unref(newits[i]);
        } else if (cmds[i] == PROTOCOL_BINARY_CMD_GET && rdits[i] != NULL) {
            free(rdits[i]);
        }
    }


    /* Make out array contiguous */
    for (i = 0, j = 0; i < n; i++) {
        if (drops[i]) continue;
        if (i != j) {
            msgs[j] = msgs[i];
        }
        iovs[i].iov_len = msgs[i].msg_len = msglens[i];
        msgs[j].msg_hdr.msg_flags = 0;
        j++;
    }


    /* Send out responses */
    if ((n = sendmmsg(fd, msgs, j, 0)) >= 0 && n < j) {
        fprintf(stderr, "Incomplete send: %d instead of %d\n", n, j);
    } else if (n < 0) {
        perror("sendmmsg failed");
        abort();
    }

    return j;
}

static size_t clean_log(struct item_allocator *ia, bool idle)
{
    struct item *it, *nit;
    size_t n;

    if (!idle) {
        /* We're starting processing for a new request */
        ialloc_cleanup_nextrequest(ia);
    }

    n = 0;
    while ((it = ialloc_cleanup_item(ia, idle)) != NULL) {
        n++;
        if (it->refcount != 1) {
            if ((nit = ialloc_alloc(ia, sizeof(*nit) + it->keylen + it->vallen,
                    true)) == NULL)
            {
                fprintf(stderr, "Warning: ialloc_alloc failed during cleanup :-/\n");
                abort();
            }

            nit->hv = it->hv;
            nit->vallen = it->vallen;
            nit->keylen = it->keylen;
            rte_memcpy(item_key(nit), item_key(it), it->keylen + it->vallen);
            hasht_put(nit, it);
            item_unref(nit);
        }
        item_unref(it);
    }
    return n;
}

static struct item_allocator **iallocs;
static size_t n_ready = 0;

static int processing_thread(void *data)
{
#if LINUX_SOCKETS
    void *bufs[BATCH_MAX];
    int i, fd;
#elif LINUX_DPDK
    unsigned p;
#endif
    struct item_allocator ia;
    size_t had_pkts, total_reqs = 0, total_clean = 0;
    static uint16_t qcounter;
    uint16_t q;

    ialloc_init_allocator(&ia);
    q = __sync_fetch_and_add(&qcounter, 1);
#if LINUX_SOCKETS
    for (i = 0; i < BATCH_MAX; i++) {
        bufs[i] = malloc(MAX_MSGSIZE);
    }
    fd = dup(sock_fd);
#elif LINUX_DPDK
    p = 0;
#endif

    iallocs[q] = &ia;
    __sync_fetch_and_add(&n_ready, 1);

    printf("Worker starting %d  :: FD :: %d \n",rte_lcore_id(),fd);

    while (1) {
	//printf("Starting Worker Loop \n");    
#if LINUX_SOCKETS
        had_pkts = packet_loop(fd, bufs, &ia);
#else
        had_pkts = packet_loop(&ia, p, q);
        p = (p + 1) % num_ports;
#endif
	//printf("Worker Cleaning \n");
        NVDIMM_write_out_next();
#if 0
        if (total_reqs / 100000 != (total_reqs + had_pkts) / 100000) {
            printf("%u: total=%10zu  clean=%10zu\n", q, total_reqs, total_clean);
        }
#endif
        total_reqs += had_pkts;
	//printf("Ending Worker Loop \n");
    }

    return 0;
}

static void maintenance(void)
{
    size_t i, n;

    n = rte_lcore_count() - 1;
    while (1) {
        for (i = 0; i < n; i++) {
	    //printf("Starting Maintenance \n");
        NVDIMM_write_out_next();
	    //printf("Ending Maintenance \n");
        }
        usleep(1);
    }
}


int main(int argc, char *argv[])
{
    int n;
    if ((n = rte_eal_init(argc, argv)) < 0) {
        fprintf(stderr, "rte_eal_init failed: %s\n", rte_strerror(rte_errno));
        return -1;
    }

    argc -= n;
    argv += n;
    settings_init(argc - n, argv + n);

    global_init();
    database_init();
    printf("Initailizing networking\n");
    //rte_mempool_ops_init();
    network_init();
    printf("Networking initialized\n");
    iallocs = calloc(rte_lcore_count() - 1, sizeof(*iallocs));


    rte_eal_mp_remote_launch(processing_thread, NULL, SKIP_MASTER);

    while (n_ready < rte_lcore_count() - 1);
    printf("Starting maintenance\n");
    while (1) {
        maintenance();
    }
    return 0;
}
