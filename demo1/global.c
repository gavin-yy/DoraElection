#include <errno.h>
#include "aciga/log.h"
#include "aciga/mem.h"
#include "aciga/net.h"

#include "global.h"

aciga_global_t* global_new()
{
    aciga_global_t *global = (aciga_global_t *)aciga_malloc0(sizeof(aciga_global_t));
	if (!global) {
		aciga_log_err("OOM at alloc aciga global struct(%d).", sizeof(aciga_global_t));
		errno = ENOMEM;
		return NULL;
	}

    memset(global,0,sizeof(aciga_global_t));

    global->log_level = ACIGA_LOG_INFO;
	global->running = 1;
	global->udp_fd = -1;

    global->loop = mainloop_init(NULL);

	if (!global->loop) {
		aciga_log_err("mainloop init error: %s", strerror(errno));
		aciga_global_finish(global);
        return NULL;
	}

    return global;
}


static void global_reset(aciga_global_t* global)
{
    if(global->udp_fd >= 0) {
        udp_server_finish(global);
    }

    if (global->loop) {
        //释放
        mainloop_destroy(global->loop);

		aciga_xfree(global->loop);
	}
}


void aciga_global_finish(aciga_global_t *global)
{
	if (!global)
		return;

	global_reset(global);
    aciga_xfree(global);

	return;
}
