tcp_listener example
====================

**English** | [Türkçe](README.tr.md)

Runs up to three TCP echo servers (`TcpListener`), each on its own port and,
optionally, on one network interface only (`TcpOptions::netif_key`):

| Server | Port | Accepts connections from | `netif_key` |
|---|---|---|---|
| 1 | 5001 | Wi-Fi station only | `WIFI_STA_DEF` |
| 2 | 5002 | Ethernet only | `ETH_DEF` |
| 3 | 5003 | both | `""` (any) |

A listener bound to an interface only sees connections that arrive on that
interface; lwIP answers the others with a reset, so the client gets
"connection refused". Every accepted connection runs in its own task (the task
owns the `TcpClient`), is greeted with the interface it arrived on, and then
everything it sends is echoed back:

```
server3 hello 172.16.10.30 via WIFI_STA_DEF
```

The ESP32 logs the same:

```
I server3: connection from 192.168.1.100 port 50016 on ETH_DEF (to 192.168.1.101 port 5003), 1 open
```

Uses the library from the repository root (`../..`).

Files:

- `main/main.cpp`: server and session tasks
- `main/server_config.h`: turns the menuconfig entries into a table
- `main/Kconfig.projbuild`: the menuconfig entries
- `tools/tcp_listener_test.py`: test client for the PC

## Configure

`idf.py menuconfig`:

- **Example Connection Configuration**: enable *Connect using WiFi interface*
  (SSID, password) and *Connect using Ethernet interface* (W5500 pins: see
  `sdkconfig.w5500` and `../tcp_multi_client/README.md`). Keep *Preferred IPv6
  Type* on **Local Link**.
- **Listener Configuration**:
  - *Max connections per server* (default 3): one more is told
    `server<N> busy` and closed
  - *Close idle connections after (s)* (default 60, 0 = never)
  - **Server 1 / Server 2 / Server 3**, each with *Enable*, *Port* and
    *Accept connections from*: Any interface, Wi-Fi station only, Ethernet
    only, or a custom esp_netif key (e.g. `WIFI_AP_DEF`)

A server whose interface does not exist logs
`interface "ETH_DEF" does not exist ...` and retries every 5 s; the others run.

`sdkconfig.defaults` raises `CONFIG_LWIP_MAX_SOCKETS` to 16: three listeners
plus three connections each need 12 sockets (the default is 10).

## Test on the PC

`tools/tcp_listener_test.py` connects to every server over every interface, checks
the accept/refuse pattern, the interface named in the greeting and the echo,
then prints a table and exits with 0 (PASS) or 1 (FAIL). Python 3.8+, standard
library only. Give it the ESP32's address on each network (printed at start-up
as `example_common: - IPv4 address: ...`):

```
python tools/tcp_listener_test.py --wifi 172.16.10.65 --eth 192.168.1.101 --max-clients 3
```

Result on an ESP32 + W5500 (PC on both networks):

```
server   port  via   address          expected        result          detail
server1  5001  wifi  172.16.10.65     accepted        accepted  ok    server1 hello 172.16.10.30 via WIFI_STA_DEF
server1  5001  eth   192.168.1.101    refused         refused   ok    connection refused (RST)
server2  5002  wifi  172.16.10.65     refused         refused   ok    connection refused (RST)
server2  5002  eth   192.168.1.101    accepted        accepted  ok    server2 hello 192.168.1.100 via ETH_DEF
server3  5003  wifi  172.16.10.65     accepted        accepted  ok    server3 hello 172.16.10.30 via WIFI_STA_DEF
server3  5003  eth   192.168.1.101    accepted        accepted  ok    server3 hello 192.168.1.100 via ETH_DEF
server1  5001  wifi  172.16.10.65     busy after 3    busy      ok
server2  5002  eth   192.168.1.101    busy after 3    busy      ok
server3  5003  eth   192.168.1.101    busy after 3    busy      ok

PASS: every server accepted and refused exactly as expected
```

Options:

| Option | Meaning |
|---|---|
| `--wifi IP` / `--eth IP` | ESP32 address on each network (at least one) |
| `--ports 5001,5002,5003` | ports of servers 1, 2, 3 |
| `--expect 1=wifi,2=eth,3=both` | where each server should accept (`wifi`, `eth`, `both`, `none`); change it when you change the menuconfig choices |
| `--max-clients 3` | also check the per-server limit |
| `--timeout 3` | seconds per step |

The PC must reach the ESP32 on both networks (for example Wi-Fi on one, an
Ethernet cable on the other). Any TCP tool (`nc`, Hercules, PuTTY raw mode)
can also be used by hand: connect to port 5001..5003 and type.

## Build and flash

```
idf.py set-target esp32
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.w5500" menuconfig
idf.py build flash monitor
```

`sdkconfig` is not in the repository (it holds the Wi-Fi password).
`sdkconfig.defaults` alone gives a Wi-Fi-only build (server 2 then reports that
`ETH_DEF` does not exist); adding `sdkconfig.w5500` as above enables the tested
W5500 Ethernet. Both only apply when `sdkconfig` is created.
