# Sentinel – Kernel-Assisted Process Recovery System

A Linux-based system that checks whether a running process is working properly.

The process regularly sends a small “heartbeat” message to Sentinel to show that it is still working. If these messages stop, Sentinel detects the problem and helps restart the process using a C++ supervisor.

**Core Workflow:**  
`MONITOR → DETECT → NOTIFY → RECOVER`

## What is Sentinel ?
Sentinel is a Linux-based system that monitors a running process and helps recover it when a problem is detected.

The process regularly sends a small “heartbeat” message to Sentinel. If the heartbeat stops or the process terminates unexpectedly, Sentinel detects the problem and sends an event to a C++ supervisor.

The supervisor can then restart the affected process.

### What Makes Sentinel Unique?

Unlike a simple user-space monitoring program, Sentinel uses a custom Linux kernel module to monitor the process and detect failures. The detected events are then handled by a C++ supervisor for automatic recovery.

This combination of **kernel-level monitoring and user-space recovery** is the key idea behind Sentinel.
## Problem Statement
In real-world Linux systems, a process may still be running but stop responding properly.

Simply checking whether the process is running does not always tell us if it is working correctly.

In this project, we demonstrate this situation using a sample Linux process. The process regularly sends a small heartbeat message to Sentinel. If the heartbeat stops for a configured time, Sentinel detects the problem and generates an event.

The C++ supervisor can then use this event to recover the process.


## Objectives


The main objectives of Sentinel are:

- Monitor a Linux process using periodic heartbeat messages.
- Detect when the process stops sending heartbeats within the configured time.
- Detect unexpected process termination.
- Generate an event when a failure is detected.
- Notify the C++ supervisor about the detected failure.
- Recover the affected process by restarting it.
- Demonstrate communication between a user-space application and a Linux kernel module.
## Key Features

- **Heartbeat Monitoring** – Regularly checks whether the process is responding.
- **Timeout Detection** – Detects when the process stops sending heartbeats within the configured time.
- **Process Death Detection** – Detects unexpected termination of the monitored process.
- **Kernel Event Generation** – The Linux kernel module generates an event when a failure is detected.
- **C++ Supervisor** – Receives failure events and handles the recovery process.
- **Automatic Process Recovery** – Restarts the affected process after a detected failure.
- **Character Device Interface** – User-space programs communicate with the kernel module through `/dev/sentinel`.
- **Process Status Tracking** – Tracks registration, process ID, heartbeat count, timeout and current state.


##  System Architecture


Sentinel is divided into three main parts: a Linux kernel module, C++ user-space applications, and a shared interface.

```text
                         SENTINEL
                            |
        +-------------------+-------------------+
        |                   |                   |
        v                   v                   v
   Kernel Space        User Space        Shared Interface
        |                   |                   |
        v                   v                   v
+---------------+    +----------------+    +------------------+
| driver/       |    | worker/        |    | include/         |
|               |    |                |    |                  |
| sentinel.c    |    | heartbeat_     |    | sentinel_uapi.h  |
| Makefile      |    | worker.cpp     |    |                  |
|               |    |                |    | IOCTL & Event    |
| sentinel.ko   |    | event_reader.  |    | definitions      |
|               |    | cpp            |    |                  |
+-------+-------+    +-------+--------+    +------------------+
        |                    |
        | /dev/sentinel      |
        +---------+----------+
                  |
                  v
        +---------------------+
        | Sentinel Kernel     |
        | Module              |
        |                     |
        | - Register process  |
        | - Track heartbeat   |
        | - Detect timeout    |
        | - Detect process    |
        |   death             |
        | - Generate events   |
        +----------+----------+
                   |
                   | Failure Event
                   v
        +---------------------+
        | supervisor/         |
        |                     |
        | supervisor.cpp      |
        |                     |
        | - Receive event     |
        | - Check status      |
        | - Stop failed       |
        |   process           |
        | - Restart worker    |
        +----------+----------+
                   |
                   v
             New Worker
    
```
## How It Works

Sentinel works in four simple stages:

### 1. Monitor

The C++ worker starts and registers itself with the Sentinel kernel module.

It then regularly sends a small heartbeat message through `/dev/sentinel` to show that it is still working.

### 2. Detect

The kernel module keeps track of the worker's latest heartbeat.

If a heartbeat is not received within the configured timeout, Sentinel marks the process as expired.

Sentinel can also detect when the monitored process terminates unexpectedly.

### 3. Notify

When a failure is detected, the kernel module creates an event containing information such as:

- Event type
- Process ID
- Event message

The event is made available to the user-space supervisor.

### 4. Recover

The C++ supervisor receives the failure event.

For a failed worker, the supervisor terminates the unhealthy process if required, waits for it to exit, and starts a new worker process.

The new worker then starts sending heartbeats again.## How It Works

Sentinel works in four simple stages:

### 1. Monitor

The C++ worker starts and registers itself with the Sentinel kernel module.

It then regularly sends a small heartbeat message through `/dev/sentinel` to show that it is still working.

### 2. Detect

The kernel module keeps track of the worker's latest heartbeat.

If a heartbeat is not received within the configured timeout, Sentinel marks the process as expired.

Sentinel can also detect when the monitored process terminates unexpectedly.

### 3. Notify

When a failure is detected, the kernel module creates an event containing information such as:

- Event type
- Process ID
- Event message

The event is made available to the user-space supervisor.

### 4. Recover

The C++ supervisor receives the failure event.

For a failed worker, the supervisor terminates the unhealthy process if required, waits for it to exit, and starts a new worker process.

The new worker then starts sending heartbeats again.
## Technology Stack

The project is built using the following technologies:

| Category | Technology |
|---|---|
| Operating System | Linux (Ubuntu) |
| Kernel Component | Linux Kernel Module |
| Programming Languages | C, C++ |
| User-Space | C++17 |
| Communication | Linux Character Device (`/dev/sentinel`) |
| Kernel Interface | IOCTL |
| Build System | Make, GCC, G++ |
| Version Control | Git |

## Project Structure
The project is organized into separate components for kernel-space monitoring, user-space applications, shared definitions, and documentation.

```text
Sentinel/
│
├── driver/
│   ├── sentinel.c
│   └── Makefile
│
├── include/
│   └── sentinel_uapi.h
│
├── worker/
│   ├── heartbeat_worker.cpp
│   └── event_reader.cpp
│
├── supervisor/
│   └── supervisor.cpp
│
├── config/
├── lib/
├── scripts/
├── tests/
├── tools/
└── docs/
```
## Testing & Results

The Sentinel system was tested through the following practical test cases:

### 1. Normal Monitoring

The `heartbeat_worker` was started and continuously sent heartbeat messages to Sentinel.

```text
[WORKER PID=7823] Heartbeat #82 | STATUS=HEALTHY
[WORKER PID=7823] Heartbeat #83 | STATUS=HEALTHY
[WORKER PID=7823] Heartbeat #84 | STATUS=HEALTHY
```

**Result:** Heartbeat monitoring and process health tracking worked successfully.

### 2. Heartbeat Timeout Detection

The running worker was paused using:

```bash
kill -STOP 7823
```

After the configured timeout, Sentinel detected the missing heartbeat:

```text
Sentinel: HEARTBEAT TIMEOUT - PID 7823
Sentinel: EVENT QUEUED type=1 pid=7823 count=1
```

**Result:** Timeout detection and event generation worked successfully.

### 3. Worker Resume

The paused worker was resumed using:

```bash
kill -CONT 7823
```

The worker continued sending heartbeats and returned to:

```text
[WORKER PID=7823] Heartbeat #82 | STATUS=HEALTHY
[WORKER PID=7823] Heartbeat #83 | STATUS=HEALTHY
```

**Result:** The worker successfully resumed normal operation.

### 4. Automatic Recovery

The C++ supervisor was tested with an unhealthy worker. After receiving a heartbeat timeout event, the supervisor detected the unhealthy state, terminated the affected worker, and started a new worker process.

```text
[SUPERVISOR] STATUS=UNHEALTHY
[SUPERVISOR] Worker terminated
[SUPERVISOR] Restart #1
[SUPERVISOR] Worker started | PID=<new PID>
```

The new worker started sending heartbeats with `STATUS=HEALTHY`.

**Result:** Automatic process recovery was successfully demonstrated.

### Overall Result

The tests successfully demonstrated the main Sentinel workflow:

`MONITOR → DETECT → NOTIFY → RECOVER`
##  Build & Run

Sentinel is designed to run on a Linux system. Follow the steps below in order.

### 1. Enter the Project Directory

```bash
cd ~/Sentinel
```

### 2. Build the Kernel Module

Go to the driver directory:

```bash
cd driver
```

Build the module using the provided Makefile:

```bash
make
```

After a successful build, the kernel module file `sentinel.ko` will be created.

### 3. Load the Sentinel Kernel Module

Load the module into the Linux kernel:

```bash
sudo insmod sentinel.ko
```

Check whether it is loaded:

```bash
lsmod | grep sentinel
```

You should see `sentinel` in the output.

### 4. Check the Sentinel Device

Sentinel provides a Linux character device for communication with user-space applications.

Run:

```bash
ls -l /dev/sentinel
```

The device should be available before starting the worker.

### 5. Build the Heartbeat Worker

Open the worker directory:

```bash
cd ../worker
```

Compile the worker:

```bash
g++ -std=c++17 heartbeat_worker.cpp -I../include -o heartbeat_worker
```

Run it:

```bash
./heartbeat_worker
```

The worker registers with Sentinel and starts sending periodic heartbeat messages.

Example:

```text
[WORKER PID=1234] REGISTERED | TIMEOUT=2000 ms
[WORKER PID=1234] Heartbeat #1 | STATUS=HEALTHY
[WORKER PID=1234] Heartbeat #2 | STATUS=HEALTHY
```

### 6. Build and Run the Supervisor

Open another terminal and go to the supervisor directory:

```bash
cd ~/Sentinel/supervisor
```

Compile the supervisor:

```bash
g++ -std=c++17 supervisor.cpp -I../include -o supervisor
```

Run the supervisor:

```bash
./supervisor
```

The supervisor listens for Sentinel failure events and performs recovery when the monitored worker becomes unhealthy.

### 7. Check Sentinel Kernel Logs

To view kernel-side monitoring activity:

```bash
sudo dmesg | grep -i sentinel
```

This can show heartbeat messages, timeout detection, and generated events.

### 8. Stop the Programs

Stop the running user-space programs with:

```text
Ctrl + C
```

### 9. Unload the Kernel Module

After stopping the Sentinel-related programs, unload the kernel module:

```bash
cd ~/Sentinel/driver
sudo rmmod sentinel
```

The module can be loaded again whenever you want to run the project.
## Limitations

The current version of Sentinel is a working prototype and has some limitations:

- It currently monitors one process at a time.
- The heartbeat timeout is configured when the process registers with Sentinel.
- The kernel event queue has a fixed size.
- The current recovery policy mainly focuses on restarting the failed worker process.
- Sentinel is designed for Linux systems and requires a compatible kernel environment and kernel headers to build the driver.
- The current implementation is intended as a project prototype rather than a complete production service manager.
## Future Enhancements

The current Sentinel implementation is a working prototype, but its architecture can be extended to support larger and more complex systems.

Possible future enhancements include:

- **Multiple Process Monitoring** – Extend Sentinel to monitor multiple processes at the same time.
- **Flexible Recovery Policies** – Support different recovery actions based on the type and severity of failure.
- **Resource Monitoring** – Monitor CPU and memory usage along with process health.
- **Configuration Support** – Allow monitoring and timeout settings to be managed through configuration files or command-line tools.
- **Persistent Logging** – Store monitoring and recovery events for later analysis.
- **Scalable Event Handling** – Improve the event system to handle a larger number of monitored processes and events.
- **Monitoring Dashboard** – Provide a user-space interface to view process status, failures, and recovery history.
- **Production-Ready Integration** – Extend the framework for use with larger Linux service-management or monitoring environments.
## Conclusion


Sentinel demonstrates how Linux kernel-space and user-space components can work together to monitor and recover a running process.

The system uses heartbeat monitoring to detect process failures, generates events through a custom Linux kernel module, and uses a C++ supervisor to handle process recovery.

The project brings together Linux kernel modules, character devices, IOCTL, process management, and C++ system programming in one practical implementation.

The core workflow of Sentinel is:

`MONITOR → DETECT → NOTIFY → RECOVER`