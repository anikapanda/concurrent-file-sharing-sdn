
# Concurrent File Sharing System using SDN

## Project Overview
This project implements a concurrent file-sharing system using C socket programming and Software-Defined Networking (SDN).

The system supports multiple clients communicating with a file server over a Mininet network controlled by an OS-Ken OpenFlow 1.3 controller.

## Technologies Used
- C Programming
- TCP Socket Programming
- Ubuntu Linux
- Mininet
- Open vSwitch
- OpenFlow 1.3
- OS-Ken SDN Controller
- Python
- iperf

## Network Topology

| Device | Role | IP Address |
|---|---|---|
| h1 | Client 1 | 10.0.0.1 |
| h2 | Client 2 | 10.0.0.2 |
| h3 | File Server | 10.0.0.3 |
| s1 | OpenFlow Switch | — |

SDN Controller: 127.0.0.1:6653

File Server Port: 9000

## Features
- Client authentication
- File upload and download
- File listing
- Multiple concurrent TCP client connections
- OpenFlow-based packet forwarding
- Dynamic traffic monitoring
- Automatic temporary blocking of high-traffic clients

## Project Files

- `client.c` — TCP file-sharing client
- `server.c` — Concurrent TCP file server
- `controller/start_controller.py` — Controller launcher
- `controller/sdn_controller.py` — Active SDN controller
- `controller/sdn_controller_dynamic.py` — Dynamic traffic-monitoring implementation
- `controller/sdn_controller_original.py` — Original controller backup

## Running the Project

### 1. Compile the C programs

```bash
gcc -o server server.c -pthread
gcc -o client client.c
```

### 2. Start the SDN controller

Activate the Python environment containing OS-Ken, then run:

```bash
cd controller
python -u start_controller.py
```

### 3. Start Mininet

In a separate terminal:

```bash
sudo mn --topo single,3 --mac --switch ovsk,protocols=OpenFlow13 --controller remote,ip=127.0.0.1,port=6653
```

### 4. Verify connectivity

At the Mininet prompt:

```bash
pingall
```

### 5. Test dynamic SDN traffic monitoring

At the Mininet prompt:

```bash
h3 iperf -s &
h1 iperf -c 10.0.0.3 -t 15 -i 1
```

Observe the controller terminal for high-traffic detection messages.

## Experimental Results

- Mininet connectivity: 0% packet loss (6/6 received)
- Successful client authentication and file transfers
- Concurrent TCP client sessions observed
- Baseline virtual-network throughput: approximately 64–65 Gbit/s
- Throughput with a 2 Mbit/s rate limit: approximately 1.91 Mbit/s
- High-traffic detection recorded at 20,085.14 Mbit/s
- Temporary 10-second traffic blocking triggered
- iperf throughput dropped to 0 bits/sec during blocking and subsequently recovered

## Contributors

- Anika Panda
- Praguna Krishna A
- Yuvaraja C S

## Notes

The project was tested in an Ubuntu virtual machine using Mininet. Throughput values reflect the virtualized environment rather than physical network performance.
