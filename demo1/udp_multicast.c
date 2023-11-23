#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <errno.h>
#include <string.h>

#include "global.h"

extern void udp_process(int fd, aciga_events_t events, void *user_data);

int udp_server_init(aciga_global_t *global, char *interface)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int ret = setsockopt(fd, SOL_SOCKET,SO_BINDTODEVICE, interface, strlen(interface));
    if ( ret < 0 ) {
        perror("SO_BINDTODEVICE");
        return -1;
    }

    // allow multiple sockets to use the same PORT number
    //
    uint32_t yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof(yes)) < 0) {
        perror("Reusing ADDR failed");
        return -1;
    }

    // set up destination address
    //
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // differs from sender
    addr.sin_port = htons(UDP_PORT);

    // bind to receive address, listen on all address.
    //
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return -1;
    }

    // use setsockopt() to request that the kernel join a multicast group
    //
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(UDP_MCAST_GROUP);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    // mreq.imr_interface.s_addr = interface_ip; //network endian.
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0) {
        perror("setsockopt");
        return -1;
    }

    mainloop_add_fd(global->loop, fd, ACIGA_EVENT_IN, udp_process, NULL, (void *)global);
    global->udp_fd = fd;

    return 0;
}

int udp_server_finish(aciga_global_t *global)
{
    mainloop_remove_fd(global->loop, global->udp_fd);
    close(global->udp_fd);
    global->udp_fd = -1;
    return 0;
}