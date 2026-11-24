#!/usr/bin/env python3
"""Test client for the tcp_listener example.

Connects to every server of the ESP32 over every interface you give it and
checks who accepts what:

    server 1 (port 5001)  Wi-Fi only    -> accepted via Wi-Fi, refused via Ethernet
    server 2 (port 5002)  Ethernet only -> refused via Wi-Fi, accepted via Ethernet
    server 3 (port 5003)  any           -> accepted via both

An accepted connection must greet with "server<N> hello <ip> via <KEY>", where
KEY is the interface the connection arrived on (WIFI_STA_DEF / ETH_DEF), and
must echo a test line. With --max-clients it also opens one connection too many
to every reachable server and expects "server<N> busy".

    python tcp_listener_test.py --wifi 172.16.10.65 --eth 192.168.1.101
    python tcp_listener_test.py --wifi 172.16.10.65                  # Wi-Fi only
    python tcp_listener_test.py --wifi ... --eth ... --expect 1=wifi,2=eth,3=both
    python tcp_listener_test.py --wifi ... --eth ... --max-clients 3

Prints a table and exits with 0 (PASS) or 1 (FAIL). Standard library only (3.8+).
"""
import argparse
import socket
import sys
import uuid

IFACE_KEY = {"wifi": "WIFI_STA_DEF", "eth": "ETH_DEF"}


def recv_line(sock, timeout):
    sock.settimeout(timeout)
    data = b""
    while not data.endswith(b"\n"):
        chunk = sock.recv(256)
        if not chunk:
            break
        data += chunk
    return data.decode("utf-8", "replace").rstrip("\r\n")


def try_server(host, port, timeout):
    """Returns (outcome, detail, socket-or-None). outcome: accepted/refused/timeout/error."""
    try:
        s = socket.create_connection((host, port), timeout=timeout)
    except ConnectionRefusedError:
        return "refused", "connection refused (RST)", None
    except socket.timeout:
        return "timeout", f"no answer in {timeout:g} s", None
    except OSError as e:
        return "error", str(e), None
    try:
        hello = recv_line(s, timeout)
    except (socket.timeout, OSError) as e:
        s.close()
        return "error", f"connected, but no greeting ({e})", None
    return "accepted", hello, s


def check_accepted(sid, path, hello, sock, timeout):
    """Validates greeting and echo. Returns a list of problems."""
    problems = []
    parts = hello.split()
    # "server3 hello 172.16.10.30 via WIFI_STA_DEF"
    if len(parts) != 5 or parts[0] != f"server{sid}" or parts[1] != "hello" or parts[3] != "via":
        return [f"bad greeting {hello!r}"]
    if parts[4] != IFACE_KEY[path]:
        problems.append(f"says via {parts[4]}, expected {IFACE_KEY[path]}")
    probe = f"ping {uuid.uuid4().hex[:8]}"
    try:
        sock.sendall((probe + "\n").encode())
        echo = recv_line(sock, timeout)
        if echo != probe:
            problems.append(f"echo {echo!r} != {probe!r}")
    except (socket.timeout, OSError) as e:
        problems.append(f"no echo ({e})")
    return problems


def check_busy(sid, host, port, limit, timeout):
    """Opens limit connections, then one more; that one must get 'busy'."""
    held = []
    try:
        for _ in range(limit):
            outcome, hello, s = try_server(host, port, timeout)
            if outcome != "accepted":
                return [f"connection {len(held) + 1} of {limit}: {outcome} ({hello})"]
            held.append(s)
        outcome, line, s = try_server(host, port, timeout)
        if s:
            held.append(s)
        if line != f"server{sid} busy":
            return [f"connection {limit + 1} got {outcome}: {line!r}, expected 'server{sid} busy'"]
        return []
    finally:
        for s in held:
            s.close()


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--wifi", help="ESP32 address on the Wi-Fi network")
    p.add_argument("--eth", help="ESP32 address on the Ethernet network")
    p.add_argument("--ports", default="5001,5002,5003", help="ports of servers 1,2,3")
    p.add_argument("--expect", default="1=wifi,2=eth,3=both",
                   help="where each server accepts: wifi, eth, both or none")
    p.add_argument("--timeout", type=float, default=3, help="seconds per step (default 3)")
    p.add_argument("--max-clients", type=int, default=0,
                   help="also test the per-server limit (Max connections per server)")
    a = p.parse_args()

    paths = {k: v for k, v in (("wifi", a.wifi), ("eth", a.eth)) if v}
    if not paths:
        p.error("give at least one of --wifi / --eth")
    ports = [int(x) for x in a.ports.split(",")]
    expect = {}
    for item in a.expect.split(","):
        sid, _, where = item.partition("=")
        expect[int(sid)] = {"wifi": {"wifi"}, "eth": {"eth"}, "both": {"wifi", "eth"}, "none": set()}[where]

    rows, failures = [], []
    for sid, port in enumerate(ports, start=1):
        for path, host in paths.items():
            want = "accepted" if path in expect.get(sid, set()) else "refused"
            outcome, detail, sock = try_server(host, port, a.timeout)
            problems = []
            if outcome != want:
                problems.append(f"expected {want}, got {outcome}")
            elif sock:
                problems += check_accepted(sid, path, detail, sock, a.timeout)
            if sock:
                sock.close()
            ok = not problems
            rows.append((f"server{sid}", port, path, host, want, outcome, "ok" if ok else "FAIL", detail))
            failures += [f"server{sid} via {path}: {x}" for x in problems]

    if a.max_clients:
        for sid, port in enumerate(ports, start=1):
            for path in sorted(expect.get(sid, set()) & set(paths)):
                problems = check_busy(sid, paths[path], port, a.max_clients, a.timeout)
                rows.append((f"server{sid}", port, path, paths[path], f"busy after {a.max_clients}",
                             "busy" if not problems else "-", "ok" if not problems else "FAIL", ""))
                failures += [f"server{sid} limit via {path}: {x}" for x in problems]
                break   # one interface per server is enough

    print(f"{'server':<9}{'port':<6}{'via':<6}{'address':<17}{'expected':<16}{'result':<10}{'':<6}detail")
    for r in rows:
        print(f"{r[0]:<9}{r[1]:<6}{r[2]:<6}{r[3]:<17}{r[4]:<16}{r[5]:<10}{r[6]:<6}{r[7]}")
    print()
    if failures:
        print("FAIL:\n  " + "\n  ".join(failures))
        return 1
    print("PASS: every server accepted and refused exactly as expected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
