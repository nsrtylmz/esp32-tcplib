tcp_client example
==================

**English** | [Türkçe](README.tr.md)

Connects to a TCP server, sends a first message, logs whatever comes back and
reconnects when the connection drops. Lines typed in the serial monitor are sent
to the server (lines typed while disconnected are queued, up to 8). Uses the
library from the repository root (`../..`).

Files:

- `main/main.cpp`: connection loop
- `main/console.cpp`: reads lines from the serial console and queues them; only
  the main task touches the `TcpClient`
- `tools/tcp_echo_server.py`: test server for the PC

## Configure

`idf.py menuconfig`:

- **Example Connection Configuration**: Wi-Fi SSID and password, and
  *Preferred IPv6 Type*: keep **Local Link** unless your network hands out other
  IPv6 addresses (`example_connect()` waits until one of that type arrives).
- **Example Configuration**:
  - *IP Version*: IPV4 or IPV6 (IPV6 also turns on `tcplib → Enable IPv6 support`)
  - *IPV4 Address* / *IPV6 Address*: the server (your PC)
  - *Port*: server port (default 3333)
  - *Connect timeout (ms)*: one connection attempt (default 10000)
  - *Interface selection*: Auto, or an esp_netif key such as `WIFI_STA_DEF`
    (passed to `TcpOptions::netif_key`)

Wi-Fi power save is turned off in `app_main()` (`esp_wifi_set_ps(WIFI_PS_NONE)`)
for lower latency; remove that line on battery-powered devices.

## Serial console input

Type a line in `idf.py monitor` and press Enter: it is sent to the server as
typed. No `\n` is added, and control characters (Ctrl key presses, escape
sequences) are removed. Exit the monitor with `Ctrl+]`, or `Ctrl+T` then `Ctrl+X`
(easier on keyboards where `]` needs AltGr).

**Note:** console input works only when the console is a UART
(`CONFIG_ESP_CONSOLE_UART`, the default on ESP32 boards with a USB-to-UART chip
such as CP210x or CH340). Boards that use the chip's built-in USB port as the
console (USB-Serial-JTAG or USB-CDC, e.g. many ESP32-C3/S3 boards) need a
different driver; there `console.cpp` installs nothing and no input arrives.

## Test server on the PC

`tools/tcp_echo_server.py` is a small echo server that listens on IPv4 and IPv6
at the same time. Python 3.8+ is enough, no packages needed:

```
python tools/tcp_echo_server.py 3333
```

Many PC tools (for example Hercules) listen on IPv4 only, so the ESP32 cannot
reach them over IPv6. Check with `netstat -an`: an IPv6 listener shows `[::]:3333`.

Find the PC's IPv6 address with `ipconfig` (Windows) or `ip -6 addr` (Linux).
A link-local address (`fe80::...`) works on the same Wi-Fi network; leave out the
`%<number>` suffix Windows shows. Windows Firewall may ask to allow Python the
first time; without that permission incoming connections are blocked.

If IPv6 connections time out although the PC can ping the ESP32, the access
point probably does not forward IPv6 multicast (neighbor discovery) between
clients. Use IPv4 on that network, or test through the PC's mobile hotspot
(the server address is then the hotspot adapter's `fe80::...` address).

## Build and flash

```
idf.py set-target esp32
idf.py menuconfig
idf.py build flash monitor
```

`sdkconfig` is not in the repository (it holds the Wi-Fi password); a fresh
configuration starts from `sdkconfig.defaults`.
