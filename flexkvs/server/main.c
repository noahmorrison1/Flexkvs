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
/* Use sockets */
#ifndef LINUX_SOCKETS
#define LINUX_SOCKETS 0
#endif

/* If linux, use recvmmsg instead of recvmsg (enables batching) */
#ifndef LINUX_RECVMMSG
#define LINUX_RECVMMSG 0
#define BATCH_MAX 1
#endif

/* Enable DPDK */
#ifndef LINUX_DPDK
#define LINUX_DPDK 1
#endif

/* Enable key-based steering if DPDK is enabled */
#ifndef DPDK_IPV6
#define DPDK_IPV6 1
#endif

/* DPDK batch size */
#ifndef BATCH_MAX
#define BATCH_MAX 1
#endif

#ifndef ENABLE_PREFETCHING
#define ENABLE_PREFETCHING 0
#endif

#define DPDK_V4_HWXSUM 1
#define MAX_MSGSIZE 2048

#if LINUX_SOCKETS

/******************************************************************************/
/* Linux Sockets */

#define __USE_GNU
#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>


#include "protocol_binary.h"

struct iokvs_message {
    memcached_udp_header mcudp;
    protocol_binary_request_header mcr;
    uint8_t payload[];
} __attribute__ ((packed));

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

    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("creating socket failed");
        abort();
    }

#if 0
    if ((flags = fcntl(sock_fd, F_GETFL, 0)) < 0 ||
            fcntl(sock_fd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        perror("setting O_NONBLOCK");
        abort();
    }
#endif

    flags = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &flags, sizeof(flags));
    maximize_sndbuf(sock_fd);

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(11211);

    if (bind(sock_fd, (struct sockaddr *) &sin, sizeof(sin)) == -1) {
        perror("bind failed");
        abort();
    }
}

/******************************************************************************/

#elif LINUX_DPDK

/******************************************************************************/
/* dpdk ipv4 */

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_arp.h>
#include <rte_ethdev.h>

#include "protocol_binary.h"

#define MY_IPADDR 0x80d006ec
//#define MY_IPADDR 0x0a010102

struct iokvs_message {
    struct ether_hdr ether;
#if DPDK_IPV6
    struct ipv6_hdr ipv6;
#else
    struct ipv4_hdr ipv4;
#endif
    struct udp_hdr udp;
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
#if DPDK_IPV6
            .rss_hf = ETH_RSS_IPV6,
#else
            .rss_hf = ETH_RSS_NONFRAG_IPV4_UDP,
#endif
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


static void network_init(void)
{
    int res, i, q, n = rte_lcore_count();
    unsigned j;

    mempool = rte_mempool_create("bufpool", 16 * 1024,
             MAX_MSGSIZE + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM, 32,
             sizeof(struct rte_pktmbuf_pool_private), rte_pktmbuf_pool_init,
             NULL, rte_pktmbuf_init, NULL, rte_socket_id(), 0);

    num_ports = rte_eth_dev_count();
    for (j = 0; j < num_ports; j++) {
        if (rte_eth_dev_configure(j, n - 1 , n - 1, &eth_config) != 0) {
            fprintf(stderr, "rte_eth_dev_configure failed\n");
            abort();
        }

        if (j == 0) {
            rte_eth_macaddr_get(0, &mymac);
        }
        rte_eth_promiscuous_enable(j);

        printf("Preparing queues... %u", j);
        q = 0;
        RTE_LCORE_FOREACH_SLAVE(i) {
            if ((res = rte_eth_rx_queue_setup(j, q, 512, rte_lcore_to_socket_id(i),
                            NULL, mempool)) < 0)
            {
                fprintf(stderr, "rte_eth_rx_queue_setup failed: %d %s\n", res,
                        strerror(-res));
                abort();
            }

            if ((res = rte_eth_tx_queue_setup(j, q, 512, rte_lcore_to_socket_id(i),
                            &eth_txconf)) < 0)
            {
                fprintf(stderr, "rte_eth_tx_queue_setup failed: %d %s\n", res,
                        strerror(-res));
                abort();
            }
            printf("Done with queue %u %d\n", j, q);
            q++;
        }

        printf("Starting device %u\n", j);
        rte_eth_dev_start(j);
        printf("Device started %u\n", j);
    }
}

static void packet_slow_path(struct rte_mbuf *mbuf, struct iokvs_message *msg,
        unsigned port, uint16_t q)
{
    struct arp_hdr *arp = (struct arp_hdr *) (&msg->ether + 1);

    /* Currently we're only handling ARP here */
    if (msg->ether.ether_type == htons(ETHER_TYPE_ARP) &&
            arp->arp_hrd == htons(ARP_HRD_ETHER) && arp->arp_pln == 4 &&
            arp->arp_op == htons(ARP_OP_REQUEST) && arp->arp_hln == 6 &&
            arp->arp_data.arp_tip == htonl(MY_IPADDR))
    {
        printf("Responding to ARP\n");
        msg->ether.d_addr = msg->ether.s_addr;
        msg->ether.s_addr = mymac;
        arp->arp_op = htons(ARP_OP_REPLY);
        arp->arp_data.arp_tha = arp->arp_data.arp_sha;
        arp->arp_data.arp_sha = mymac;
        arp->arp_data.arp_tip = arp->arp_data.arp_sip;
        arp->arp_data.arp_sip = htonl(MY_IPADDR);

        rte_mbuf_refcnt_update(mbuf, 1);

        mbuf->ol_flags = PKT_TX_L4_NO_CKSUM;
        mbuf->tx_offload = 0;

        while (rte_eth_tx_burst(port, q, &mbuf, 1) == 0);
        printf("Responded to ARP\n");
    }
}

static void mbuf_adjust(struct rte_mbuf *mbuf, int len)
{
    int mblen = rte_pktmbuf_pkt_len(mbuf);
    if (len > mblen) {
        rte_pktmbuf_append(mbuf, len - mblen);
    } else if (mblen > len) {
        rte_pktmbuf_trim(mbuf, mblen - len);
    }
}

/******************************************************************************/
#endif




#if LINUX_SOCKETS
static size_t packet_loop(int fd, void **bufs, struct item_allocator *ia)
#else
static size_t packet_loop(struct item_allocator *ia, unsigned p, uint16_t q)
#endif
{
#if LINUX_SOCKETS
    struct mmsghdr msgs[BATCH_MAX];
    struct sockaddr_in sin[BATCH_MAX];
    struct iovec iovs[BATCH_MAX];
  #if !LINUX_RECVMMSG
    ssize_t bytes;
  #endif
#elif LINUX_DPDK
    struct rte_mbuf *mbufs[BATCH_MAX];
#endif

    struct iokvs_message *hdrs[BATCH_MAX];
    int msglens[BATCH_MAX];
    bool drops[BATCH_MAX] = { false };
    uint32_t hashes[BATCH_MAX];
    void *keys[BATCH_MAX];
    size_t keylens[BATCH_MAX];
    protocol_binary_command cmds[BATCH_MAX];
    size_t totlens[BATCH_MAX];
    struct item *rdits[BATCH_MAX] = { NULL };
    struct item *newits[BATCH_MAX] = { NULL };
    uint16_t status[BATCH_MAX] = { PROTOCOL_BINARY_RESPONSE_SUCCESS };

    int i, j, n;
    uint32_t blen;

#if LINUX_SOCKETS
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

#if LINUX_RECVMMSG
    /* Receive batch */
    if ((n = recvmmsg(fd, msgs, BATCH_MAX, MSG_DONTWAIT, NULL)) == 0 ||
            (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)))
    {
        return 0;
    } else if (n < 0) {
        perror("recvmmsg failed");
        abort();
    }
#else
    if ((bytes = recvmsg(fd, &msgs[0].msg_hdr, MSG_DONTWAIT)) == 0 ||
            (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)))
    {
        return 0;
    } else if (bytes < 0) {
        perror("recvmsg failed");
        abort();
    }
    n = 1;
    msgs[0].msg_len = bytes;
#endif

    for (i = 0; i < n; i++) {
        msglens[i] = msgs[i].msg_len;
        hdrs[i] = bufs[i];
    }
#elif LINUX_DPDK
    if ((n = rte_eth_rx_burst(p, q, mbufs, BATCH_MAX)) <= 0) {
        return 0;
    }
#if 0
    printf("Got packets n=%d q=%d lc=%d!\n", n, q, rte_lcore_id());
#endif

    for (i = 0; i < n; i++) {
        hdrs[i] = rte_pktmbuf_mtod(mbufs[i], struct iokvs_message *);
        msglens[i] = rte_pktmbuf_pkt_len(mbufs[i]);

#if 0
            printf("Received packet:\n");
            rte_pktmbuf_dump(stdout, mbufs[i], msglens[i]);
#endif

#if DPDK_IPV6
        if (hdrs[i]->ether.ether_type == htons(ETHER_TYPE_IPv6) &&
                hdrs[i]->ipv6.proto == 17 &&
#else
        if (hdrs[i]->ether.ether_type == htons(ETHER_TYPE_IPv4) &&
                hdrs[i]->ipv4.next_proto_id == 17 &&
#endif
                hdrs[i]->udp.dst_port == htons(11211) &&
                msglens[i] >= sizeof(hdrs[0][0]))
        {
            drops[i] = false;
        } else {
            drops[i] = true;
            packet_slow_path(mbufs[i], hdrs[i], p, q);
        }
     }
#endif

    /* Parse requests */
    for (i = 0; i < n; i++) {
        if (drops[i]) continue;

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
#if LINUX_DPDK && DPDK_IPV6
        hashes[i] = mbufs[i]->hash.rss;
#else
        hashes[i] = jenkins_hash(keys[i], keylens[i]);
#endif
#if ENABLE_PREFETCHING
        hasht_prefetch1(hashes[i]);
#endif
        totlens[i] = ntohl(hdrs[i]->mcr.request.bodylen) -
            hdrs[i]->mcr.request.extlen;
    }

#if ENABLE_PREFETCHING
    /* Prefetch items */
    for (i = 0; i < n; i++) {
        if (drops[i]) continue;
        hasht_prefetch2(hashes[i]);
    }
#endif

    /* Allocate new items for set requests and initialize*/
    for (i = 0; i < n; i++) {
        if (drops[i]) continue;
        if (cmds[i] == PROTOCOL_BINARY_CMD_SET) {
            newits[i] = ialloc_alloc(ia, sizeof(*newits[i]) + totlens[i], false);
            if (newits[i] == NULL) {
                //printf("Out of memory: %zu\n", totlens[i]);
                status[i] = htons(PROTOCOL_BINARY_RESPONSE_ENOMEM);
                continue;
            }

            newits[i]->hv = hashes[i];
            newits[i]->vallen = totlens[i] - keylens[i];
            newits[i]->keylen = keylens[i];
            rte_memcpy(item_key(newits[i]), keys[i], totlens[i]);

            hasht_put(newits[i], NULL);
        }
    }

    /* Hash table lookups */
    for (i = 0; i < n; i++) {
        if (drops[i]) continue;
        if (cmds[i] == PROTOCOL_BINARY_CMD_GET) {
            rdits[i] = hasht_get(keys[i], keylens[i], hashes[i]);
            if (rdits[i] == NULL) {
                status[i] = htons(PROTOCOL_BINARY_RESPONSE_KEY_ENOENT);
                continue;
            }
        }
    }

    /* Prepare Responses */
    for (i = 0; i < n; i++) {
        if (drops[i]) continue;
        hdrs[i]->mcr.request.magic = PROTOCOL_BINARY_RES;
        hdrs[i]->mcr.request.keylen = 0;
        hdrs[i]->mcr.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
        hdrs[i]->mcr.request.status = status[i];

        if (cmds[i] == PROTOCOL_BINARY_CMD_GET) {
            msglens[i] = sizeof(hdrs[i][0]) + 4;
            hdrs[i]->mcr.request.extlen = 4;
            hdrs[i]->mcr.request.bodylen = htonl(4);
            *((uint32_t *) hdrs[i]->payload) = 0;
            if (rdits[i] != NULL) {
                msglens[i] += rdits[i]->vallen;
                hdrs[i]->mcr.request.bodylen = htonl(4 + rdits[i]->vallen);
                rte_memcpy(hdrs[i]->payload + 4, item_value(rdits[i]),
                        rdits[i]->vallen);
            }
        } else {
            msglens[i] = sizeof(hdrs[i][0]);
            hdrs[i]->mcr.request.extlen = 0;
            hdrs[i]->mcr.request.bodylen = 0;
        }
    }

    /* Release items */
    for (i = 0; i < n; i++) {
        if (drops[i]) continue;
        if (cmds[i] == PROTOCOL_BINARY_CMD_SET && newits[i] != NULL) {
            item_unref(newits[i]);
        } else if (cmds[i] == PROTOCOL_BINARY_CMD_GET && rdits[i] != NULL) {
            item_unref(rdits[i]);
        }
    }

#if LINUX_SOCKETS
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

#if LINUX_RECVMMSG
    /* Send out responses */
    if ((n = sendmmsg(fd, msgs, j, 0)) >= 0 && n < j) {
        fprintf(stderr, "Incomplete send: %d instead of %d\n", n, j);
    } else if (n < 0) {
        perror("sendmmsg failed");
        abort();
    }
#else
    if (!drops[0]) {
        if ((bytes = sendmsg(fd, &msgs[0].msg_hdr, 0)) > 0 &&
                bytes < msgs[0].msg_len)
        {
            fprintf(stderr, "Incomplete send: %zu instead of %d\n", bytes,
                msgs[0].msg_len);
        } else if (bytes < 0) {
            perror("sendmsg failed");
            abort();
        }
    }	
#endif
#elif LINUX_DPDK
    /* Free dropped mbufs */
    for (i = 0, j = 0; i < n; i++) {
        if (drops[i]) {
            rte_pktmbuf_free(mbufs[i]);
        }
    }

    /* Make out array contiguous */
    for (i = 0, j = 0; i < n; i++) {
        if (drops[i]) continue;

        hdrs[j] = hdrs[i];
        mbufs[j] = mbufs[i];

        mbufs[j]->ol_flags = PKT_TX_L4_NO_CKSUM;
        mbufs[j]->tx_offload = 0;
        mbufs[j]->l2_len = 14;
        mbufs[j]->l3_len = 20;
        mbuf_adjust(mbufs[j], msglens[i]);

        hdrs[j]->ether.d_addr = hdrs[j]->ether.s_addr;
        hdrs[j]->ether.s_addr = mymac;

#if !DPDK_IPV6
        hdrs[j]->ipv4.dst_addr = hdrs[j]->ipv4.src_addr;
        hdrs[j]->ipv4.src_addr = htonl(MY_IPADDR);
        hdrs[j]->ipv4.total_length = htons(msglens[i] -
                offsetof(struct iokvs_message, ipv4));
        hdrs[j]->ipv4.time_to_live = 64;
        hdrs[j]->ipv4.hdr_checksum = 0;
#if DPDK_V4_HWXSUM
        mbufs[j]->ol_flags |= PKT_TX_IP_CKSUM | PKT_TX_IPV4;
#else
        hdrs[j]->ipv4.hdr_checksum = rte_ipv4_cksum(&hdrs[j]->ipv4);
#endif
#endif

        hdrs[j]->udp.dst_port = hdrs[j]->udp.src_port;
        hdrs[j]->udp.src_port = htons(11211);
        hdrs[j]->udp.dgram_len = htons(msglens[i] -
                offsetof(struct iokvs_message, udp));
        hdrs[j]->udp.dgram_cksum = 0;

#if 0
        printf("Sending packet:\n");
        rte_pktmbuf_dump(stdout, mbufs[j], msglens[i]);
#endif
        j++;
    }

#if 1
    i = 0;
    while (i < j) {
        n = rte_eth_tx_burst(p, q, mbufs + i, j - i);
        i += n;
    }
#else
    for (i = 0, j = 0; i < n; i++) {
        if (!drops[i]) {
            rte_pktmbuf_free(mbufs[i]);
        }
    }
#endif

#endif
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
	return 0;
}

static void maintenance(void)
{
    size_t i, n;

    n = rte_lcore_count() - 1;
    while (1) {
        for (i = 0; i < n; i++) {
            ialloc_maintenance(iallocs[i]);
        }
        usleep(10);
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

    database_init();
    printf("Initailizing networking\n");
    rte_mempool_ops_init();
    network_init();
    printf("Networking initialized\n");
    //test_init();

    rte_eal_mp_remote_launch(test_init, NULL, SKIP_MASTER);
    while (n_ready < rte_lcore_count() - 1);
    /*while(1)
    {
        //waiting
    }*/
    rte_mempool_free(mempool);
    return 0;
}