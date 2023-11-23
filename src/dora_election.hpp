#ifndef DORAELECTION_H
#define DORAELECTION_H

#include <stdint.h>

//declare all this functions can be called by C directly
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*on_recv_fn)(uint8_t *pkt, uint16_t len);

typedef void (*on_timeout_fn)(void *timer, void *user_arg);

struct elec_adaptor {
    int (*broadcast_send_pkt)(const char *pkt, uint16_t len);

    int (*register_recv_cb)(on_recv_fn cb);

    void *(*create_timer)(void *user_arg, uint32_t msec, on_timeout_fn cb);

    int (*modify_timer)(void *timer, uint32_t msec);

    void (*destory_timer)(void *timer);
};

typedef enum {
    ROLE_FOLLOWER = 0,
    ROLE_LEADER = 1,
} ROLE;

typedef enum {
    GW_P5,
    GW_P3,
    GW_P3L,
    GW_P2,
} DEV_TYPE;

struct gateway_info {
    char *did;
    int did_len;

    DEV_TYPE dev_type;
    uint32_t data_ver;
};

int elec_init(struct elec_adaptor *adaptor, struct gateway_info *info);

int elec_start();

ROLE elec_get_current_role();

#ifdef __cplusplus
}
#endif

#endif