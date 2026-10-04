#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>

#include "../include/sentinel_uapi.h"


/* =========================================================
 * START WORKER
 * ========================================================= */

static pid_t start_worker()
{
    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork");
        return -1;
    }

    if (pid == 0)
    {
        execl("../worker/heartbeat_worker",
              "heartbeat_worker",
              nullptr);

        perror("execl");
        _exit(1);
    }

    std::cout
        << "[SUPERVISOR] Worker started | PID="
        << pid << "\n";

    return pid;
}


/* =========================================================
 * CLEAR OLD EVENTS
 * ========================================================= */

static void clear_old_events(int fd)
{
    sentinel_event event{};

    while (true)
    {
        ssize_t n = read(fd, &event, sizeof(event));

        if (n == sizeof(event))
        {
            std::cout
                << "[SUPERVISOR] Ignoring stale event | PID="
                << event.pid
                << " | TYPE="
                << event.type
                << "\n";

            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            break;
        }

        break;
    }
}


/* =========================================================
 * SHOW CURRENT SENTINEL STATUS
 * ========================================================= */

static void show_status(int fd)
{
    sentinel_status status{};

    if (ioctl(fd, SENTINEL_IOC_STATUS, &status) < 0)
    {
        perror("[SUPERVISOR] STATUS ioctl");
        return;
    }

    const char* state;

    if (status.registered && !status.expired)
    {
        state = "HEALTHY";
    }
    else if (status.registered && status.expired)
    {
        state = "EXPIRED";
    }
    else
    {
        state = "OFFLINE";
    }

    std::cout
        << "[SUPERVISOR] SENTINEL STATUS\n"
        << "  PID         : " << status.pid << "\n"
        << "  Registered  : "
        << (status.registered ? "YES" : "NO") << "\n"
        << "  State       : " << state << "\n"
        << "  Heartbeats  : " << status.heartbeat_count << "\n"
        << "  Timeout     : " << status.timeout_ms << " ms\n";
}


/* =========================================================
 * MAIN
 * ========================================================= */

int main()
{
    /* Print output immediately */
    std::cout << std::unitbuf;

    /*
     * Non-blocking is useful during startup so we can
     * remove old events without getting stuck.
     */
    int fd = open("/dev/sentinel",
                  O_RDONLY | O_NONBLOCK | O_CLOEXEC);

    if (fd < 0)
    {
        perror("open /dev/sentinel");
        return 1;
    }

    std::cout
        << "[SUPERVISOR] Sentinel Supervisor started\n";


    /*
     * Remove events left by an older worker/supervisor session.
     */
    clear_old_events(fd);


    /* Start a fresh worker */
    pid_t worker_pid = start_worker();

    if (worker_pid < 0)
    {
        close(fd);
        return 1;
    }


    unsigned int restart_count = 0;


    /* =====================================================
     * EVENT MONITORING
     * ===================================================== */

    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;


    while (true)
    {
        int ret = poll(&pfd, 1, -1);

        if (ret < 0)
        {
            perror("[SUPERVISOR] poll");
            break;
        }


        if (pfd.revents & POLLIN)
        {
            sentinel_event event{};

            ssize_t n = read(fd,
                             &event,
                             sizeof(event));

            if (n != sizeof(event))
            {
                std::cerr
                    << "[SUPERVISOR] Failed to read Sentinel event\n";

                continue;
            }


            std::cout
                << "\n[SUPERVISOR] EVENT RECEIVED\n"
                << "  PID     : " << event.pid << "\n"
                << "  TYPE    : " << event.type << "\n"
                << "  MESSAGE : " << event.message << "\n";


            /* =================================================
             * HEARTBEAT TIMEOUT
             * ================================================= */

            if (event.type == SENTINEL_EVENT_HEARTBEAT_TIMEOUT &&
                event.pid == worker_pid)
            {
                show_status(fd);

                std::cout
                    << "[SUPERVISOR] STATUS=UNHEALTHY\n";

                std::cout
                    << "[SUPERVISOR] Terminating PID="
                    << worker_pid << "\n";


                if (kill(worker_pid, SIGKILL) == 0)
                {
                    std::cout
                        << "[SUPERVISOR] Worker terminated\n";
                }
                else
                {
                    perror("[SUPERVISOR] kill");
                }


                waitpid(worker_pid, nullptr, 0);


                restart_count++;

                std::cout
                    << "[SUPERVISOR] Restart #"
                    << restart_count
                    << "\n";

                std::cout
                    << "[SUPERVISOR] Restarting worker...\n";


                worker_pid = start_worker();
            }


            /* =================================================
             * UNEXPECTED CLIENT DEATH
             * ================================================= */

            else if (event.type == SENTINEL_EVENT_CLIENT_DIED &&
                     event.pid == worker_pid)
            {
                std::cout
                    << "[SUPERVISOR] STATUS=OFFLINE\n";

                show_status(fd);

                std::cout
                    << "[SUPERVISOR] Worker exited unexpectedly\n";


                waitpid(worker_pid, nullptr, 0);


                restart_count++;

                std::cout
                    << "[SUPERVISOR] Restart #"
                    << restart_count
                    << "\n";

                std::cout
                    << "[SUPERVISOR] Restarting worker...\n";


                worker_pid = start_worker();
            }


            /*
             * Ignore events belonging to an old PID.
             */
            else
            {
                std::cout
                    << "[SUPERVISOR] Ignoring event for PID="
                    << event.pid << "\n";
            }
        }
    }


    close(fd);

    return 0;
}
