#ifndef SENTINEL_UAPI_H
#define SENTINEL_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define SENTINEL_IOC_MAGIC 'S'

/* Register a process with timeout */
#define SENTINEL_IOC_REGISTER \
    _IOW(SENTINEL_IOC_MAGIC, 1, unsigned int)

/* Unregister current process */
#define SENTINEL_IOC_UNREGISTER \
    _IO(SENTINEL_IOC_MAGIC, 2)

/* Get current Sentinel status */
#define SENTINEL_IOC_STATUS \
    _IOR(SENTINEL_IOC_MAGIC, 3, struct sentinel_status)

/* Event types */
#define SENTINEL_EVENT_HEARTBEAT_TIMEOUT 1
#define SENTINEL_EVENT_CLIENT_DIED       2


/* Event sent from kernel to userspace */
struct sentinel_event
{
    __u32 type;
    __s32 pid;
    __u64 timestamp_ns;
    char message[64];
};


/* Current watchdog status */
struct sentinel_status
{
    __u32 registered;
    __u32 expired;
    __u32 heartbeat_count;
    __s32 pid;
    __u32 timeout_ms;
};

#endif
