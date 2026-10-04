#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/poll.h>
#include <linux/timekeeping.h>

#include "../include/sentinel_uapi.h"

#define DEVICE_NAME "sentinel"
#define EVENT_QUEUE_SIZE 16

/* =========================================================
 * CLIENT / HEARTBEAT STATE
 * ========================================================= */

static DEFINE_MUTEX(client_lock);

static bool client_registered;
static bool client_expired;
static pid_t client_pid;
static unsigned int timeout_ms;
static unsigned long last_heartbeat;

/*
 * Exact file opened by the registered client.
 * Used for reliable death detection in release().
 */
static struct file *client_file;
static u32 heartbeat_count;

/* =========================================================
 * EVENT QUEUE
 * ========================================================= */

static struct sentinel_event event_queue[EVENT_QUEUE_SIZE];

static unsigned int event_head;
static unsigned int event_tail;
static unsigned int event_count;

/* Protects event queue */
static DEFINE_SPINLOCK(event_lock);

/* Used by read()/poll() to wait for events */
static wait_queue_head_t event_waitq;


/* =========================================================
 * WATCHDOG WORK
 * ========================================================= */

static struct delayed_work watchdog_work;


/* =========================================================
 * CHECK WHETHER AN EVENT EXISTS
 * ========================================================= */

static bool sentinel_has_event(void)
{
    bool available;

    spin_lock(&event_lock);

    available = (event_count > 0);

    spin_unlock(&event_lock);

    return available;
}


/* =========================================================
 * ADD EVENT TO QUEUE
 * ========================================================= */

static void sentinel_push_event(unsigned int type,
                                pid_t pid,
                                const char *msg)
{
    struct sentinel_event *event;

    spin_lock(&event_lock);

    /* Queue full */
    if (event_count >= EVENT_QUEUE_SIZE)
    {
        printk(KERN_WARNING
               "Sentinel: event queue full\n");

        spin_unlock(&event_lock);
        return;
    }

    event = &event_queue[event_head];

    event->type = type;
    event->pid = pid;
    event->timestamp_ns = ktime_get_ns();

    strscpy(event->message,
            msg,
            sizeof(event->message));

    event_head =
        (event_head + 1) % EVENT_QUEUE_SIZE;

    event_count++;

    printk(KERN_INFO
           "Sentinel: EVENT QUEUED type=%u pid=%d count=%u\n",
           type,
           pid,
           event_count);

    spin_unlock(&event_lock);

    /*
     * Wake userspace processes waiting in poll()/read()
     */
    wake_up_interruptible(&event_waitq);
}


/* =========================================================
 * WATCHDOG
 * ========================================================= */

static void sentinel_watchdog(struct work_struct *work)
{
    bool timeout_detected = false;
    pid_t expired_pid = 0;

    mutex_lock(&client_lock);

    if (client_registered && !client_expired)
    {
        unsigned long timeout_jiffies =
            msecs_to_jiffies(timeout_ms);

        if (time_after(jiffies,
                       last_heartbeat + timeout_jiffies))
        {
            client_expired = true;

            expired_pid = client_pid;
            timeout_detected = true;

            printk(KERN_WARNING
                   "Sentinel: HEARTBEAT TIMEOUT - PID %d\n",
                   client_pid);
        }
    }

    mutex_unlock(&client_lock);

    /*
     * Generate the event outside the client mutex.
     */
    if (timeout_detected)
    {
        sentinel_push_event(
            SENTINEL_EVENT_HEARTBEAT_TIMEOUT,
            expired_pid,
            "Heartbeat timeout");
    }

    /*
     * Continue checking every 500 ms.
     */
    schedule_delayed_work(
        &watchdog_work,
        msecs_to_jiffies(500));
}


/* =========================================================
 * OPEN
 * ========================================================= */

static int sentinel_open(struct inode *inode,
                         struct file *file)
{
    printk(KERN_INFO
           "Sentinel: device opened\n");

    return 0;
}


/* =========================================================
 * READ EVENT
 * ========================================================= */

static ssize_t sentinel_read(struct file *file,
                             char __user *buffer,
                             size_t length,
                             loff_t *offset)
{
    struct sentinel_event event;

    if (length < sizeof(event))
        return -EINVAL;

    /*
     * If no event exists, wait.
     */
    if (!sentinel_has_event())
    {
        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;

        if (wait_event_interruptible(
                event_waitq,
                sentinel_has_event()))
        {
            return -ERESTARTSYS;
        }
    }

    /* Take one event from the queue */
    spin_lock(&event_lock);

    if (event_count == 0)
    {
        spin_unlock(&event_lock);
        return -EAGAIN;
    }

    event = event_queue[event_tail];

    event_tail =
        (event_tail + 1) % EVENT_QUEUE_SIZE;

    event_count--;

    printk(KERN_INFO
           "Sentinel: EVENT READ count=%u\n",
           event_count);

    spin_unlock(&event_lock);

    /* Copy event to userspace */
    if (copy_to_user(buffer,
                     &event,
                     sizeof(event)))
    {
        return -EFAULT;
    }

    return sizeof(event);
}


/* =========================================================
 * WRITE = HEARTBEAT
 * ========================================================= */

static ssize_t sentinel_write(struct file *file,
                              const char __user *buffer,
                              size_t length,
                              loff_t *offset)
{
    char heartbeat_msg[64];
    size_t copy_len;

    mutex_lock(&client_lock);

    if (!client_registered)
    {
        mutex_unlock(&client_lock);
        return -ENODEV;
    }

    /*
     * Only the registered process can send heartbeat.
     */
    if (task_pid_nr(current) != client_pid)
    {
        mutex_unlock(&client_lock);
        return -EPERM;
    }

    copy_len =
        min(length,
            sizeof(heartbeat_msg) - 1);

    if (copy_from_user(heartbeat_msg,
                       buffer,
                       copy_len))
    {
        mutex_unlock(&client_lock);
        return -EFAULT;
    }

    heartbeat_msg[copy_len] = '\0';

    /*
     * Update heartbeat timestamp.
     */
    last_heartbeat = jiffies;
    heartbeat_count++;

    /*
     * A valid heartbeat means the client is alive again.
     */
    client_expired = false;

    printk(KERN_INFO
           "Sentinel: heartbeat from PID %d: %s\n",
           client_pid,
           heartbeat_msg);

    mutex_unlock(&client_lock);

    return length;
}


/* =========================================================
 * IOCTL
 * ========================================================= */

static long sentinel_ioctl(struct file *file,
                           unsigned int cmd,
                           unsigned long arg)
{
    unsigned int new_timeout;

    switch (cmd)
    {
    case SENTINEL_IOC_REGISTER:

        /*
         * Get timeout value from userspace.
         */
        if (copy_from_user(
                &new_timeout,
                (unsigned int __user *)arg,
                sizeof(new_timeout)))
        {
            return -EFAULT;
        }

        /*
         * Allowed timeout:
         * 100 ms to 60 seconds.
         */
        if (new_timeout < 100 ||
            new_timeout > 60000)
        {
            return -EINVAL;
        }

        mutex_lock(&client_lock);

        client_registered = true;
        client_expired = false;

        /*
         * Kernel records the real caller PID.
         */
        client_pid =
            task_pid_nr(current);

        /*
         * Store the exact file opened by
         * the registered client.
         */
        client_file = file;

        timeout_ms = new_timeout;

        /*
         * Registration itself counts
         * as the first heartbeat.
         */
        last_heartbeat = jiffies;
heartbeat_count=0;
        mutex_unlock(&client_lock);

        printk(KERN_INFO
               "Sentinel: registered PID %d, timeout %u ms\n",
               client_pid,
               timeout_ms);

        return 0;


    case SENTINEL_IOC_UNREGISTER:

        mutex_lock(&client_lock);

        if (client_registered &&
            task_pid_nr(current) == client_pid)
        {
            printk(KERN_INFO
                   "Sentinel: unregistered PID %d\n",
                   client_pid);

            client_registered = false;
            client_expired = false;
            client_file = NULL;
            client_pid = 0;
        }

        mutex_unlock(&client_lock);

        return 0;
case SENTINEL_IOC_STATUS:
{
    struct sentinel_status status;

    mutex_lock(&client_lock);

    status.registered = client_registered;
    status.expired = client_expired;
    status.heartbeat_count = heartbeat_count;
    status.pid = client_pid;
    status.timeout_ms = timeout_ms;

    mutex_unlock(&client_lock);

    if (copy_to_user(
            (struct sentinel_status __user *)arg,
            &status,
            sizeof(status)))
    {
        return -EFAULT;
    }

    return 0;
}

    default:
        return -EINVAL;
    }
}


/* =========================================================
 * POLL
 * ========================================================= */

static __poll_t sentinel_poll(struct file *file,
                              poll_table *wait)
{
    __poll_t mask = 0;
    unsigned int count;

    /*
     * Tell Linux:
     * "Wake this process when event_waitq changes."
     */
    poll_wait(file,
              &event_waitq,
              wait);

    spin_lock(&event_lock);

    count = event_count;

    if (event_count > 0)
    {
        mask |= POLLIN;
        mask |= POLLRDNORM;
    }

    printk(KERN_INFO
           "Sentinel: poll called, event_count=%u\n",
           count);

    spin_unlock(&event_lock);

    return mask;
}


/* =========================================================
 * RELEASE / CLOSE
 * ========================================================= */

static int sentinel_release(struct inode *inode,
                            struct file *file)
{
    bool client_died = false;
    pid_t dead_pid = 0;

    mutex_lock(&client_lock);

    /*
     * Only the registered client's exact file
     * can generate CLIENT_DIED.
     *
     * event_reader/supervisor closing their
     * own file will NOT trigger this event.
     *
     * If heartbeat timeout already occurred,
     * suppress duplicate CLIENT_DIED.
     */
    if (client_registered &&
        !client_expired &&
        file == client_file)
    {
        dead_pid = client_pid;

        client_registered = false;
        client_expired = false;
        client_file = NULL;
        client_pid = 0;

        client_died = true;
    }

    mutex_unlock(&client_lock);

    if (client_died)
    {
        printk(KERN_WARNING
               "Sentinel: CLIENT DIED - PID %d\n",
               dead_pid);

        sentinel_push_event(
            SENTINEL_EVENT_CLIENT_DIED,
            dead_pid,
            "Client process died");
    }
    else
    {
        printk(KERN_INFO
               "Sentinel: device closed\n");
    }

    return 0;
}


/* =========================================================
 * FILE OPERATIONS
 * ========================================================= */

static const struct file_operations sentinel_fops =
{
    .owner = THIS_MODULE,
    .open = sentinel_open,
    .read = sentinel_read,
    .write = sentinel_write,
    .unlocked_ioctl = sentinel_ioctl,
    .poll = sentinel_poll,
    .release = sentinel_release,
};


/* =========================================================
 * CHARACTER DEVICE
 * ========================================================= */

static struct miscdevice sentinel_device =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &sentinel_fops,
    .mode = 0666,
};


/* =========================================================
 * MODULE INIT
 * ========================================================= */

static int __init sentinel_init(void)
{
    int ret;

    /*
     * Initialize event wait queue.
     */
    init_waitqueue_head(&event_waitq);

    /*
     * Register /dev/sentinel
     */
    ret = misc_register(&sentinel_device);

    if (ret)
    {
        printk(KERN_ERR
               "Sentinel: device registration failed\n");

        return ret;
    }

    /*
     * Initialize watchdog work.
     */
    INIT_DELAYED_WORK(
        &watchdog_work,
        sentinel_watchdog);

    /*
     * Start watchdog.
     */
    schedule_delayed_work(
        &watchdog_work,
        msecs_to_jiffies(500));

    printk(KERN_INFO
           "Sentinel: device registered\n");

    return 0;
}


/* =========================================================
 * MODULE EXIT
 * ========================================================= */

static void __exit sentinel_exit(void)
{
    /*
     * Stop watchdog.
     */
    cancel_delayed_work_sync(
        &watchdog_work);

    /*
     * Remove /dev/sentinel
     */
    misc_deregister(&sentinel_device);

    printk(KERN_INFO
           "Sentinel: device unregistered\n");
}


module_init(sentinel_init);
module_exit(sentinel_exit);


/* =========================================================
 * MODULE INFORMATION
 * ========================================================= */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chinmay");
MODULE_DESCRIPTION(
    "Sentinel Linux Watchdog Character Driver");
