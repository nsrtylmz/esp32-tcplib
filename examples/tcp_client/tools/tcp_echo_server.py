#!/usr/bin/env python3
"""TCP echo server for testing the tcp_client example on a PC.

Listens on IPv4 and IPv6 at the same time (one dual-stack socket), prints what
each client sends and sends it back. Standard library only.

    python tcp_echo_server.py [port]        (default port: 3333)

Stop with Ctrl+C.
"""
import socket
import sys
import threading


def handle(conn, addr):
    host, port = addr[0], addr[1]
    if host.startswith("::ffff:"):          # IPv4 client on the dual-stack socket
        host = host[7:]
    print(f"[+] {host} port {port} connected", flush=True)
    with conn:
        while True:
            try:
                data = conn.recv(1024)
            except OSError:
                break
            if not data:
                break
            print(f"[{host}] {data!r}", flush=True)
            try:
                conn.sendall(data)
            except OSError:
                break
    print(f"[-] {host} port {port} disconnected", flush=True)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 3333
    if socket.has_dualstack_ipv6():
        srv = socket.create_server(("", port), family=socket.AF_INET6, dualstack_ipv6=True)
        print(f"Listening on port {port} (IPv4 + IPv6)", flush=True)
    else:
        srv = socket.create_server(("", port))
        print(f"Listening on port {port} (IPv4 only: this system has no dual-stack IPv6)", flush=True)

    srv.settimeout(1.0)                     # lets Ctrl+C stop the server on Windows
    with srv:
        try:
            while True:
                try:
                    conn, addr = srv.accept()
                except socket.timeout:
                    continue
                threading.Thread(target=handle, args=(conn, addr), daemon=True).start()
        except KeyboardInterrupt:
            print("Stopped.")


if __name__ == "__main__":
    main()
