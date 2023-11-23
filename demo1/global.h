#ifndef ACIGA_GLOBAL_H
#define ACIGA_GLOBAL_H

#include "aciga/timer.h"
#include "aciga/mainloop.h"

#define MAX_DID_LEN 32
#define MAX_PID_LEN 32

#define UDP_PORT 22991
#define UDP_MCAST_GROUP "239.255.255.250"

struct aciga_global {

	int log_level;

    int running;
	mainloop_t *loop;

    //与broker之间的连接状态
    int local_broker_state;

    int dev_binded;
    char did[MAX_DID_LEN];
    uint64_t pid;

    //udp server 局域网设备发现与配网
    int udp_fd;
   	void  *usr_data;

};

typedef struct aciga_global aciga_global_t;

aciga_global_t* global_new();

void aciga_global_finish(aciga_global_t *global);

#endif