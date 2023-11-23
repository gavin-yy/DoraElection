#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <errno.h>
#include <string.h>

#include "aciga/log.h"
#include "aciga/net.h"
#include "global.h"

extern void udp_process(int fd, aciga_events_t events, void *user_data);

int udp_server_init(aciga_global_t *global, char *interface)
{
    int fd = udp_server_socket_init("0.0.0.0", UDP_PORT, NULL);

    if (fd < 0) {
        aciga_log_err("udp server init error: %s", strerror(-fd));
        return -1;
    }

    int ret = setsockopt(fd, SOL_SOCKET,SO_BINDTODEVICE, interface, strlen(interface));
    if ( ret < 0 ) {
        perror("SO_BINDTODEVICE");
        return -1;
    }

    int broadcastEnable = 1;
    ret = setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
    if (ret != 0) {
        aciga_log_err("udp server failed to set as broadcastm  error: %s", strerror(errno));
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