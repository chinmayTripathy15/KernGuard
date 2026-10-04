#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

#include "../include/sentinel_uapi.h"

int main()
{
    // Open the Sentinel device for reading events
    int fd = open("/dev/sentinel",
                  O_RDONLY | O_CLOEXEC);

    if (fd < 0)
    {
        perror("open /dev/sentinel");
        return 1;
    }

    std::cout << "Waiting for Sentinel events...\n";

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    while (true)
    {
        // Wait until the driver reports an event
        int ret = poll(&pfd, 1, -1);

        if (ret < 0)
        {
            perror("poll");
            break;
        }

        if (pfd.revents & POLLIN)
        {
            sentinel_event event{};

            ssize_t n = read(fd,
                             &event,
                             sizeof(event));

            if (n == sizeof(event))
            {
                std::cout
                    << "\n=== SENTINEL EVENT ===\n"
                    << "Type: " << event.type << "\n"
                    << "PID: " << event.pid << "\n"
                    << "Message: " << event.message << "\n"
                    << "=======================\n";
            }
        }
    }

    close(fd);

    return 0;
}
