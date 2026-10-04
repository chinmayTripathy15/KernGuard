#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "../include/sentinel_uapi.h"

int main()
{
    int fd = open("/dev/sentinel", O_WRONLY | O_CLOEXEC);

    if (fd < 0)
    {
        perror("open /dev/sentinel");
        return 1;
    }

    unsigned int timeout_ms = 2000;

    if (ioctl(fd, SENTINEL_IOC_REGISTER, &timeout_ms) < 0)
    {
        perror("ioctl REGISTER");
        close(fd);
        return 1;
    }

    pid_t pid = getpid();

    std::cout << "[WORKER PID=" << pid
              << "] REGISTERED | TIMEOUT="
              << timeout_ms << " ms\n";

    while (true)
    {
        const char heartbeat[] = "alive";

        if (write(fd, heartbeat, sizeof(heartbeat) - 1) < 0)
        {
            perror("heartbeat");
            break;
        }

        /*
         * Read actual heartbeat count and status
         * maintained by the Sentinel kernel driver.
         */
        struct sentinel_status status{};

        if (ioctl(fd, SENTINEL_IOC_STATUS, &status) < 0)
        {
            perror("ioctl STATUS");
            break;
        }

        const char* state =
            (status.registered && !status.expired)
                ? "HEALTHY"
                : "EXPIRED";

        std::cout
            << "[WORKER PID=" << status.pid
            << "] Heartbeat #" << status.heartbeat_count
            << " | STATUS=" << state
            << "\n";

        sleep(1);
    }

    ioctl(fd, SENTINEL_IOC_UNREGISTER);
    close(fd);

    return 0;
}
