tcp_multi_client example
========================

**English** | [Türkçe](README.tr.md)

Runs up to three TCP clients at the same time. Each client has its own task and
its own `TcpClient`, and can be bound to a chosen network interface
(`TcpOptions::netif_key`). Uses the library from the repository root (`../..`).

Each client sends a numbered line every period and checks that the server echoes
it back unchanged:

```
client2 #0 hello netif=WIFI_STA_DEF     first line of each connection
client2 #1
client2 #2
...
```

Files:

- `main/main.cpp`: client tasks
- `main/client_config.h`: turns the menuconfig entries into a table
- `main/Kconfig.projbuild`: the menuconfig entries
- `tools/tcp_multi_client_test.py`: test server for the PC

## Configure

`idf.py menuconfig`:

- **Example Connection Configuration**: Wi-Fi SSID and password. To use
  Ethernet, also enable *Connect using Ethernet interface* there. Keep
  *Preferred IPv6 Type* on **Local Link**.
- **Multi Client Configuration**:
  - *Default server address* / *Default server port*: used by every client
    whose own address/port is left empty / 0
  - *Message period*, *Connect timeout*, *Delay before reconnecting*
  - **Client 1 / Client 2 / Client 3**, each with:
    - *Enable client N*
    - *Server address* and *Server port* (empty / 0 = default)
    - *Network interface*:

      | Choice | `netif_key` | |
      |---|---|---|
      | Any (routing decides) | `""` | default for clients 1 and 3 |
      | Wi-Fi station | `WIFI_STA_DEF` | default for client 2 |
      | Ethernet | `ETH_DEF` | needs Ethernet enabled above |
      | Custom esp_netif key | your text | e.g. `WIFI_AP_DEF`, or a key your own code created |

A client whose interface does not exist logs
`interface "ETH_DEF" does not exist ...` and retries; the other clients keep
running. An IPv6 server address needs `tcplib → Enable IPv6 support`.

Binding a client to an interface chooses the network its packets leave on, so
its server address must be reachable on that network. When Wi-Fi and Ethernet
are different networks, give the Ethernet client the server's Ethernet-side
address (its own *Server address*), otherwise its connects time out.

## Wi-Fi + Ethernet (W5500) setup

Tested on an ESP32 with a W5500 module on SPI2. `sdkconfig.w5500` holds these
settings; apply it to a fresh configuration with

```
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.w5500" build
```

(delete `sdkconfig` first, it only applies when the file is created), or set
them by hand: in **Example Connection Configuration** enable both *Connect using
WiFi interface* and *Connect using Ethernet interface*, then:

| Setting | Value |
|---|---|
| Ethernet Type | W5500 Module |
| SPI Host Number | 1 (SPI2) |
| SPI SCLK / MOSI / MISO / CS GPIO | 12 / 27 / 14 / 13 |
| Interrupt GPIO | 26 |
| PHY Reset GPIO | 25 |
| SPI clock speed (MHz) | 20 |

Notes:

- These pins go through the GPIO matrix, not SPI2's IOMUX pins, which limits
  reads to about 26 MHz on the ESP32; hence 20 MHz instead of the default 36.
- GPIO12 is a strapping pin (flash voltage). It must not be pulled high while
  the ESP32 boots, or modules with 3.3 V flash will not start.
- `example_connect()` waits until every enabled interface has an IP address,
  so the Ethernet cable must be plugged into a network with DHCP.
- The W5500 filters IPv6 multicast (`w5500.mac` warning at start-up); IPv4 and
  link-local IPv6 addressing still work.

Test result with this setup (PC with Wi-Fi 172.16.10.30 and Ethernet
192.168.1.100; client 3 *Server address* = 192.168.1.100, interface Ethernet):

```
I client3: connected #1: 192.168.1.101 port 60760 -> 192.168.1.100 port 3333, interface ETH_DEF
I client2: connected #1: 172.16.10.65 port 60759 -> 172.16.10.30 port 3333, interface WIFI_STA_DEF
I client1: connected #1: 172.16.10.65 port 60758 -> 172.16.10.30 port 3333, interface WIFI_STA_DEF
```

```
python tools/tcp_multi_client_test.py --min-lines 10 --expect-netif 2=WIFI_STA_DEF --expect-netif 3=ETH_DEF
...
client1      1     10     0  any             172.16.10.65
client2      1     10     0  WIFI_STA_DEF    172.16.10.65
client3      1     10     0  ETH_DEF         192.168.1.101

PASS: clients 1, 2, 3 each sent >= 10 lines and got them echoed
```

Wi-Fi power save is turned off in `app_main()` for lower latency; remove that
line on battery-powered devices.

## Test on the PC

`tools/tcp_multi_client_test.py` listens on IPv4 and IPv6, echoes every line and
checks the clients. Python 3.8+, standard library only. Set *Default server
address* to the PC's address, flash, then:

```
python tools/tcp_multi_client_test.py
```

It waits up to 60 s for clients 1, 2 and 3 to each send 5 numbered lines without
a gap, prints a summary and exits with 0 (PASS) or 1 (FAIL):

```
client   conns  lines  gaps  netif           address
client1      1     10     0  any             172.16.10.65
client2      1     10     0  WIFI_STA_DEF    172.16.10.65
client3      1     10     0  WIFI_STA_DEF    172.16.10.65

PASS: clients 1, 2, 3 each sent >= 10 lines and got them echoed
```

Options:

| Option | Meaning |
|---|---|
| `--port 3333` | port to listen on |
| `--clients 1,2,3` | client ids that must take part (e.g. `1,2` when client 3 is disabled) |
| `--min-lines 5` | numbered lines each client must send |
| `--duration 60` | give up after this many seconds |
| `--expect-netif 2=WIFI_STA_DEF` | require a client to report this interface (repeatable) |
| `--serve` | only echo and log, forever, no verdict |
| `-v` | log every line |

When the test ends it closes the connections; the clients log `read: Closed`
and reconnect, which is expected.

Windows Firewall may ask to allow Python the first time; without that permission
the ESP32 cannot connect. For IPv6 on Wi-Fi networks that block multicast between
clients, see `../tcp_client/README.md`.

## Build and flash

```
idf.py set-target esp32
idf.py menuconfig
idf.py build flash monitor
```

`sdkconfig` is not in the repository (it holds the Wi-Fi password); a fresh
configuration starts from `sdkconfig.defaults` (Wi-Fi only), plus
`sdkconfig.w5500` for Ethernet as shown above.
