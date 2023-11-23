#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

#include "aciga/net.h"
#include "aciga/timer.h"


#include "global.h"
#include "aciga/log.h"
#include "udp_server.h"

#include "dora_election.hpp"

aciga_global_t *global;
on_recv_fn g_udp_rx;


#define MAX_ADDRESS_CNT 16

struct local_nic_info {
    struct in_addr addr[MAX_ADDRESS_CNT];
    char if_name[MAX_ADDRESS_CNT][64];
    int cnt;
};
struct local_nic_info g_local_nic;

static int address_is_myself(struct in_addr *ip_addr)
{
    // printf("check source ip: %u \n", ip_addr->s_addr );

    for(int i=0; i<g_local_nic.cnt; ++i) {
        if( ip_addr->s_addr == g_local_nic.addr[i].s_addr ) {
            return 1;
        }
    }

    return 0;
}

uint32_t get_ipv4_by_interface_name(char *name)
{
    for(int i=0; i<g_local_nic.cnt; ++i) {
        if( !strcmp(g_local_nic.if_name[i], name) ) {
            return g_local_nic.addr[i].s_addr;
        }
    }

    return 0;
}

void get_local_ip_address()
{
    struct ifaddrs *ifAddrStruct = NULL;
    struct ifaddrs *ifa = NULL;
    void *tmpAddrPtr = NULL;

    getifaddrs(&ifAddrStruct);

    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET) { // check it is IP4
            // is a valid IP4 Address
            tmpAddrPtr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            char addressBuffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);

            printf("%s IP Address %s ", ifa->ifa_name, addressBuffer);

            memcpy( &g_local_nic.addr[g_local_nic.cnt], tmpAddrPtr, sizeof(struct in_addr )) ;
            snprintf( g_local_nic.if_name[g_local_nic.cnt], 64,  "%s", ifa->ifa_name);

            printf(" s_addr: %u  index:%d \n", g_local_nic.addr[g_local_nic.cnt].s_addr, g_local_nic.cnt);

            ++g_local_nic.cnt;

        } else if (ifa->ifa_addr->sa_family == AF_INET6) { // check it is IP6
            // is a valid IP6 Address
            tmpAddrPtr = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
            char addressBuffer[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
            printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer);
        }
    }

    if (ifAddrStruct != NULL) {
        freeifaddrs(ifAddrStruct);
    }
}



void udp_process(int fd, aciga_events_t events, void *user_data)
{
    aciga_global_t *global = (aciga_global_t *)user_data;

    if (!(events | ACIGA_EVENT_IN)) {
        return;
    }

    char buf[2096] = {0x00};
    struct sockaddr_in paddr = {0x00};
    socklen_t addrlen = sizeof(paddr);
    int ret = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&paddr, &addrlen);
    if (ret <= 0) {
        if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            aciga_log_debug("udp recvfrom busy.");
        } else {
            aciga_log_err("udp server read error: %s", strerror(errno));
        }
        return;
    }
    buf[ret] = 0;

    if( address_is_myself(&paddr.sin_addr) ) {
        // aciga_log_debug("ignore msg from ourself");
        return;
    }

    aciga_log_debug("udp recv(%s:%d) message(%d): %s", inet_ntoa(paddr.sin_addr), htons(paddr.sin_port), ret, buf);

    // if (ntohs(paddr.sin_port) != 22991) {
    //     paddr.sin_port = htons(22991);
    // }

    if (g_udp_rx) {
        g_udp_rx(buf, ret);
    }

    return;
}



int wrap_udp_send(const char *pkt, uint16_t len)
{
    struct sockaddr_in s;

#ifdef USE_BROADCAST
    //broadcast
    memset(&s, 0, sizeof(struct sockaddr_in));
    s.sin_family = AF_INET;
    s.sin_port = htons(UDP_PORT);
    s.sin_addr.s_addr = INADDR_BROADCAST; /* This is not correct : htonl(INADDR_BROADCAST); */
#else
    //multicast
    memset(&s, 0, sizeof(struct sockaddr_in));
    s.sin_family = AF_INET;
    s.sin_port = htons(UDP_PORT);
    s.sin_addr.s_addr = inet_addr(UDP_MCAST_GROUP);
#endif

    if (global->udp_fd != -1) {
        aciga_log_debug("UDP_SEND: %s\n", pkt);

        int r = sendto(global->udp_fd, pkt, len, 0, (struct sockaddr *)&s, sizeof(struct sockaddr_in));
        if (r < 0) {
            aciga_log_err("sendto failed, ret=%d err=%s", r, strerror(errno));
        }

        return r;
    }

    aciga_log_err("udp socket not ready");
    return -1;
}

int wrap_register_recv_cb(on_recv_fn cb)
{
    g_udp_rx = cb;
    return 0;
}

void *wrap_create_timer(void *user_arg, uint32_t msec, on_timeout_fn cb)
{
    mainloop_timeout_t *timer = mainloop_add_timeout(global->loop, msec, (mainloop_timeout_func)cb, NULL, user_arg);
    return timer;
}

int wrap_modify_timer(void *timer, uint32_t msec)
{
    return mainloop_modify_timeout(global->loop, timer, msec);
}

void wrap_destory_timer(void *timer)
{
    mainloop_remove_timeout(global->loop, timer);
}

struct elec_adaptor adaptor = {
    .broadcast_send_pkt = wrap_udp_send,
    .register_recv_cb = wrap_register_recv_cb,
    .create_timer = wrap_create_timer,
    .modify_timer = wrap_modify_timer,
    .destory_timer = wrap_destory_timer,
};

struct gateway_info info = {
    .did = NULL,
    .did_len = 0,
    .dev_type = GW_P5,
    .data_ver = 0,
};

void usage()
{
    aciga_log_err("usage: cmd -d did_string");
}

#include <unistd.h>
int main(int argc, char *argv[])
{
#ifdef ANDROID
    aciga_log_init("client", "/data/conf/log/msg_lan", 512 * 1024);
#else
    aciga_log_init("client", "/tmp/log/msg_client", 512 * 1024);
#endif

    aciga_log_set_level(ACIGA_LOG_DEBUG);

    char *nic_name = NULL;

    int c;
    while ((c = getopt (argc, argv, "d:i:")) != -1) {
        switch(c) {
            case 'd':
                info.did_len = strlen(optarg);
                info.did = malloc(info.did_len + 1);
                memcpy(info.did, optarg, info.did_len );
                info.did[ info.did_len ] = '\0';
                break;
            case 'i':
                nic_name = optarg;
                break;
            default:
                break;
        }
    }

    if( info.did_len == 0 || !nic_name ) {
        usage();
        return 0;
    }

    global = global_new();
    if (!global) {
        return -1;
    }

    get_local_ip_address();

    uint32_t nic_addr = get_ipv4_by_interface_name(nic_name);

    aciga_log_info("interface name:%s  addr:0x%x", nic_name, nic_addr);

    int ret = udp_server_init(global, nic_name);
    if (ret < 0) {
        aciga_log_err("FATAL, failed to init udp server");
        return 0;
    }


    elec_init(&adaptor, &info);

    elec_start();

    mainloop_run(global->loop);
}