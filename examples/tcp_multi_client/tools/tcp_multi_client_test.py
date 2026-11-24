#!/usr/bin/env python3
"""Test server for the tcp_multi_client example.

Listens on IPv4 and IPv6, echoes every line back, and checks what the ESP32
clients send:

    client<N> #0 hello netif=<key>     first line of every connection
    client<N> #<seq>                   then seq = 1, 2, 3, ... without gaps

The test passes when every expected client has connected and sent at least
--min-lines numbered lines without a gap. It stops as soon as that happens, or
after --duration seconds, prints a summary and exits with 0 (pass) or 1 (fail).

    python tcp_multi_client_test.py                       # 3 clients, port 3333, up to 60 s
    python tcp_multi_client_test.py --clients 1,3 --min-lines 20
    python tcp_multi_client_test.py --expect-netif 2=WIFI_STA_DEF
    python tcp_multi_client_test.py --serve               # echo and log forever, no verdict

Standard library only (Python 3.8+).
"""
import argparse
import asyncio
import re
import sys
import time
from dataclasses import dataclass, field

LINE_RE = re.compile(r"^client(\d+) #(\d+)(?: hello netif=(\S+))?$")


@dataclass
class ClientStats:
    peers: set = field(default_factory=set)      # remote addresses seen
    netifs: set = field(default_factory=set)     # interfaces the client reported
    connections: int = 0
    lines: int = 0                               # numbered lines (seq >= 1)
    gaps: int = 0                                # seq jumps / duplicates
    last_seen: float = 0.0


class Tester:
    def __init__(self, args):
        self.args = args
        self.stats = {}                          # client id -> ClientStats
        self.bad_lines = 0
        self.done = asyncio.Event()
        self.writers = set()                     # open connections, closed at the end

    def client(self, cid):
        return self.stats.setdefault(cid, ClientStats())

    def passed(self):
        for cid in self.args.clients:
            st = self.stats.get(cid)
            if not st or st.lines < self.args.min_lines or st.gaps:
                return False
            want = self.args.expect_netif.get(cid)
            if want and st.netifs != {want}:
                return False
        return True

    async def handle(self, reader, writer):
        peer = writer.get_extra_info("peername")
        host = peer[0][7:] if peer[0].startswith("::ffff:") else peer[0]
        where = f"{host} port {peer[1]}"
        cid, expect = None, 0
        self.writers.add(writer)
        log(f"[+] {where} connected")
        try:
            while True:
                raw = await reader.readline()
                if not raw:
                    break
                writer.write(raw)                # echo first, check afterwards
                await writer.drain()
                text = raw.decode("utf-8", "replace").rstrip("\r\n")
                m = LINE_RE.match(text)
                if not m:
                    self.bad_lines += 1
                    log(f"[?] {where}: unexpected line {text!r}")
                    continue
                n, seq, netif = int(m.group(1)), int(m.group(2)), m.group(3)
                if cid is None:
                    cid = n
                    st = self.client(cid)
                    st.connections += 1
                    st.peers.add(host)
                    log(f"[{cid}] connection {st.connections} from {where}")
                st = self.client(cid)
                st.last_seen = time.monotonic()
                if n != cid:
                    self.bad_lines += 1
                    log(f"[?] {where}: client{n} line on client{cid}'s connection")
                    continue
                if seq != expect:
                    st.gaps += 1
                    log(f"[{cid}] expected #{expect}, got #{seq}")
                expect = seq + 1
                if seq == 0:
                    if netif:
                        st.netifs.add(netif)
                    log(f"[{cid}] hello, netif={netif}")
                else:
                    st.lines += 1
                    if self.args.verbose:
                        log(f"[{cid}] #{seq}")
                if not self.args.serve and self.passed():
                    self.done.set()
        except (ConnectionError, OSError) as e:
            log(f"[!] {where}: {e}")
        finally:
            self.writers.discard(writer)
            writer.close()
            log(f"[-] {where} disconnected" + (f" (client{cid})" if cid else ""))

    def summary(self):
        print()
        print(f"{'client':<8}{'conns':>6}{'lines':>7}{'gaps':>6}  {'netif':<16}address")
        for cid in sorted(set(self.args.clients) | set(self.stats)):
            st = self.stats.get(cid)
            if not st:
                print(f"client{cid:<2}{'-':>6}{'-':>7}{'-':>6}  {'-':<16}never connected")
                continue
            print(f"client{cid:<2}{st.connections:>6}{st.lines:>7}{st.gaps:>6}  "
                  f"{','.join(sorted(st.netifs)) or '?':<16}{', '.join(sorted(st.peers))}")
        if self.bad_lines:
            print(f"unrecognised lines: {self.bad_lines}")
        problems = []
        for cid in self.args.clients:
            st = self.stats.get(cid)
            if not st:
                problems.append(f"client{cid} never connected")
                continue
            if st.lines < self.args.min_lines:
                problems.append(f"client{cid} sent {st.lines} of {self.args.min_lines} lines")
            if st.gaps:
                problems.append(f"client{cid} had {st.gaps} sequence gap(s)")
            want = self.args.expect_netif.get(cid)
            if want and st.netifs != {want}:
                problems.append(f"client{cid} netif {sorted(st.netifs)} != {want}")
        print()
        if problems:
            print("FAIL: " + "; ".join(problems))
        else:
            print(f"PASS: clients {', '.join(map(str, self.args.clients))} each sent "
                  f">= {self.args.min_lines} lines and got them echoed")
        return not problems


def log(msg):
    print(time.strftime("%H:%M:%S ") + msg, flush=True)


def parse_netifs(items):
    out = {}
    for item in items or []:
        cid, _, key = item.partition("=")
        out[int(cid)] = key
    return out


async def main(args):
    tester = Tester(args)
    server = await asyncio.start_server(tester.handle, host=args.bind or None, port=args.port)
    addrs = ", ".join(f"[{s.getsockname()[0]}]:{s.getsockname()[1]}" for s in server.sockets)
    if args.serve:
        log(f"listening on {addrs} (echo mode, Ctrl+C to stop)")
    else:
        log(f"listening on {addrs}; waiting for clients {args.clients}, "
            f"{args.min_lines} lines each, up to {args.duration} s")
    try:
        if args.serve:
            await asyncio.Event().wait()
        else:
            await asyncio.wait_for(tester.done.wait(), timeout=args.duration)
    except asyncio.TimeoutError:
        log("time is up")
    finally:
        # Close the clients' connections ourselves: since Python 3.12 the server
        # waits for every connection to end, and the ESP32 keeps them open.
        server.close()
        for w in list(tester.writers):
            w.close()
        try:
            await asyncio.wait_for(server.wait_closed(), timeout=2)
        except asyncio.TimeoutError:
            pass
    return tester.summary()


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--port", type=int, default=3333, help="TCP port (default 3333)")
    p.add_argument("--bind", default="", help="address to listen on (default: all)")
    p.add_argument("--clients", default="1,2,3",
                   help="expected client ids, comma separated (default 1,2,3)")
    p.add_argument("--min-lines", type=int, default=5,
                   help="numbered lines each client must send (default 5)")
    p.add_argument("--duration", type=float, default=60, help="give up after N s (default 60)")
    p.add_argument("--expect-netif", action="append", metavar="ID=KEY",
                   help="require a client to report this interface, e.g. 2=WIFI_STA_DEF")
    p.add_argument("--serve", action="store_true", help="echo and log forever, no verdict")
    p.add_argument("-v", "--verbose", action="store_true", help="log every line")
    a = p.parse_args()
    a.clients = [int(x) for x in a.clients.split(",") if x.strip()]
    a.expect_netif = parse_netifs(a.expect_netif)
    try:
        ok = asyncio.run(main(a))
    except KeyboardInterrupt:
        ok = False
    sys.exit(0 if ok else 1)
