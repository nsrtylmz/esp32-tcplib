# esp32-tcplib

**English** | [Türkçe](README.tr.md)

`tcplib`: a thin C++ wrapper around lwIP TCP sockets for **ESP-IDF** (>= 5.0).

It does one job: create sockets, move bytes, report what happened. No tasks, no
callbacks, no protocol. Build those on top, the way your application needs them.

| Type | What it is |
|---|---|
| `TcpClient` | one TCP connection: `connect`, `read`, `write`, `close` |
| `TcpListener` | a listening socket: `listen`, `accept` (hands out `TcpClient`s) |
| `TcpOptions` | socket options applied on creation, including the network interface to use |
| `tcp_status_t` | result of every operation: `Ok`, `Timeout`, `Closed`, `Error` |

- Every blocking call takes a timeout (`> 0` wait, `0` try once, `< 0` forever)
- Move-only RAII objects: the destructor closes the socket
- Clients and listeners can be bound to one network interface (Wi-Fi, Ethernet, ...)
- Optional IPv6 (dual-stack listeners)
- The file descriptor is never exposed, so nobody can switch it to blocking mode behind your back
- No logging, no global state, no namespace (everything is prefixed with `Tcp` / `tcp`)

Tested on ESP32 with ESP-IDF 5.5, over Wi-Fi and W5500 SPI Ethernet.

## Install

### With the IDF component manager (recommended)

In your component's `idf_component.yml` (e.g. `main/idf_component.yml`):

```yaml
dependencies:
  tcplib:
    git: https://github.com/nsrtylmz/esp32-tcplib.git
    version: v0.1.0        # a release tag
```

### As a plain component

ESP-IDF names a component after its folder, so put the library in a folder
called **`tcplib`**:

```
git clone https://github.com/nsrtylmz/esp32-tcplib.git components/tcplib
```

(or `git submodule add https://github.com/nsrtylmz/esp32-tcplib.git components/tcplib`)

### Then

```cmake
idf_component_register(SRCS "main.cpp"
                       PRIV_REQUIRES tcplib)
```

```cpp
#include "tcplib.h"     // or the individual headers: "TcpClient.h", "TcpListener.h", ...
```

Layout:

```
esp32-tcplib/
  include/tcplib.h            umbrella header
  include/*.h                 public headers
  src/                        implementation and private headers
  Kconfig                     menuconfig → tcplib
  idf_component.yml           component manager manifest
  Doxyfile                    API documentation (run `doxygen`)
  examples/tcp_client/        client example (+ tools/tcp_echo_server.py)
  examples/tcp_multi_client/  three clients, each on a chosen interface (+ tools/tcp_multi_client_test.py)
  examples/tcp_listener/      three servers: Wi-Fi only, Ethernet only, both (+ tools/tcp_listener_test.py)
```

## Usage

### Client

```cpp
TcpClient client;
int err = 0;
tcp_status_t st = client.connect("192.168.1.10", 5000, 3000, TcpOptions(), &err);
if (st == tcp_status_t::Ok) {
    client.write("hello", 5, 1000);

    char buf[128];
    size_t got = 0;
    st = client.read(buf, sizeof(buf), got, 5000);
    if (st == tcp_status_t::Ok)           { /* got bytes in buf */ }
    else if (st == tcp_status_t::Timeout) { /* nothing yet, connection is fine */ }
    else                                  { /* Closed or Error: peer is gone */ }
}
```

To reconnect, call `client.connect(...)` again on the same object (a previous
connection is closed first).

`connect()` cannot be cancelled: `close()` from another task does not wake it.
With a negative timeout it waits until the handshake succeeds or fails, so prefer
a finite timeout and retry in a loop.

### Server

```cpp
TcpListener listener;
listener.listen(5000, /*backlog=*/4);

TcpClient client;
if (listener.accept(client, 1000) == tcp_status_t::Ok) {
    // 'client' is a normal TcpClient: hand it to a task, a list, ...
}
```

`accept(out, 0)` is non-blocking, `accept(out, -1)` waits until someone connects
or `close()` is called from another task.

### Choosing the network interface

```cpp
TcpOptions o;
o.netif_key = "ETH_DEF";            // esp_netif key: "WIFI_STA_DEF", "WIFI_AP_DEF", "ETH_DEF", ...
client.connect("192.168.1.100", 5000, 3000, o);   // leaves through Ethernet only
listener.listen(5000, 4, o);                       // accepts Ethernet connections only
```

A client bound to an interface sends through it, so the server must be reachable
on that network. A listener bound to an interface only sees connections arriving
on it; the others are refused. `TcpClient::netif()` tells which interface a
connection runs on. An unknown key makes `connect()` / `listen()` fail with
`ENODEV`.

## Status codes

| tcp_status_t | Meaning |
|---|---|
| `Ok` | Done. For `read`: `got > 0`. |
| `Timeout` | Nothing happened in time. The connection is still fine. |
| `Closed` | The peer closed the connection (or reset it), `close()` was called, or a fatal error happened earlier. Permanent until the next `connect()`. |
| `Error` | Anything else. `lastErrno()` has the errno. |

`read()` delivers all buffered data first and reports `Closed` afterwards.
`write()` sends everything or fails; `*written` says how much was accepted
(useful after a `Timeout`). `tcpStatusToString()` gives a printable name.

`connect()` and `listen()` report the error code through their optional
`err_out` argument: an errno value, or a `getaddrinfo()` code (`EAI_...`) when
name resolution failed.

## Detecting a dead connection

- `isOpen()`: local state only, no I/O.
- `checkAlive()`: peeks the socket. Detects a peer that closed or reset the connection.
- A link that died silently (cable pulled, power loss) cannot be detected by any
  socket call. Keepalive (on by default; ~45 s with default `TcpOptions`) or your own
  application-level heartbeat handles that.

## Options

```cpp
TcpOptions o;
o.no_delay    = true;
o.keepalive   = true;  o.keep_idle_s = 30;  o.keep_interval_s = 5;  o.keep_count = 3;
o.netif_key   = "WIFI_STA_DEF";   // bind to one interface, empty = any
client.connect(host, port, 3000, o);
```

Options given to `TcpListener::listen()` are applied to every accepted connection.
Anything else: `setOption(level, opt, val, len)` / `getOption(...)` (include
`lwip/sockets.h` for the constants). Some lwIP options only work if enabled in
`menuconfig` (e.g. `SO_RCVBUF` needs `CONFIG_LWIP_SO_RCVBUF`). TCP options in
`TcpOptions` are best effort; read them back with `getOption()` if it matters.

## IPv6

Off by default. Enable it in `idf.py menuconfig` → `tcplib` → "Enable IPv6 support"
(`CONFIG_TCPLIB_IPV6`, needs `CONFIG_LWIP_IPV6`). Then:

- `connect()` also takes IPv6 strings (`"2001:db8::10"`, without a `%zone`
  suffix); hostnames prefer IPv4.
- Link-local addresses (`fe80::...`) are sent out on `TcpOptions::netif_key`, or on
  the default interface when it is empty (lwIP has no route for them otherwise).
- `TcpListener` opens one dual-stack socket that accepts IPv4 and IPv6 clients.
- `remoteAddr()` / `localAddr()` return `esp_ip_addr_t` (IPv4 or IPv6) and the
  `...IpString()` functions print either form. `remoteIp()` / `localIp()` stay
  IPv4 only and return `0.0.0.0` for an IPv6 address.

Link-local IPv6 needs working neighbor discovery (multicast) between the two
devices. Some Wi-Fi access points do not forward it between clients; connections
then time out although ping works one way. See `examples/tcp_client/README.md`.

## Thread rules

| Call | Rule |
|---|---|
| `write`, `shutdownWrite`, `waitWritable` | any task; calls are serialized, so concurrent writes never interleave |
| `read`, `waitReadable` | one task at a time |
| `TcpListener::accept` | one task at a time |
| `close` | any task; blocked calls on other tasks return `Closed` within ~100 ms (not `connect()`, see above) |

Do not `connect()`, `listen()`, move or destroy an object while another task is using it.

## Notes and limits

- IPv4 only unless `CONFIG_TCPLIB_IPV6` is enabled (see above). No TLS.
- `connect()`'s timeout covers the TCP handshake, not the DNS lookup, and
  `connect()` cannot be cancelled.
- Max simultaneous sockets: `CONFIG_LWIP_MAX_SOCKETS` (default 10).
- `TcpClient::netif()` uses `esp_netif_next_unsafe()` (ESP-IDF >= 5.0).
- Blocking waits run in 100 ms slices (that is how `close()` can interrupt them).

## Examples

| Example | What it shows | PC tool |
|---|---|---|
| [tcp_client](examples/tcp_client) | one client, reconnects, sends lines typed in the serial monitor, IPv4/IPv6 | `tools/tcp_echo_server.py` |
| [tcp_multi_client](examples/tcp_multi_client) | three clients at once, each on a chosen interface | `tools/tcp_multi_client_test.py` (pass/fail test) |
| [tcp_listener](examples/tcp_listener) | three servers: Wi-Fi only, Ethernet only, both; reports the incoming interface | `tools/tcp_listener_test.py` (pass/fail test) |

Each example builds straight from the repository (it finds the library two
folders up), whatever the repository folder is called:

```
cd examples/tcp_listener
idf.py set-target esp32
idf.py menuconfig        # Wi-Fi SSID/password, example settings
idf.py build flash monitor
```

`sdkconfig` is not part of the repository (it holds your Wi-Fi password); every
example starts from its `sdkconfig.defaults`. The multi-client and listener
examples also ship `sdkconfig.w5500` with the W5500 Ethernet settings they were
tested with.

## Testing

The Python tools in the examples are end-to-end tests that run against a real
board (Python 3.8+, standard library only). They print a table and exit with 0
(PASS) or 1 (FAIL), so they can be scripted:

```
python examples/tcp_multi_client/tools/tcp_multi_client_test.py --expect-netif 2=WIFI_STA_DEF
python examples/tcp_listener/tools/tcp_listener_test.py --wifi 172.16.10.65 --eth 192.168.1.101
```

See each example's README for the options and the results on an ESP32 + W5500.

## API documentation

The public headers carry Doxygen comments. With [Doxygen](https://www.doxygen.nl)
installed, run `doxygen` in this folder and open `docs/html/index.html`.

## License

[MIT](LICENSE)
