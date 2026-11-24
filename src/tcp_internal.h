#pragma once

#include <atomic>
#include <stdint.h>
#include <string>
#include "esp_netif_ip_addr.h"
#include "sdkconfig.h"
#include "tcp_options.h"
#include "tcp_status.h"

struct sockaddr_storage;   // lwip/sockets.h
struct sockaddr_in6;

namespace tcp_detail {

// Blocking waits are done in slices of this length so that close() from another
// task is noticed quickly.
constexpr int kWaitSliceMs = 100;

int64_t nowUs();
int64_t deadlineFromTimeout(int timeout_ms);   // < 0 -> INT64_MAX (no deadline)
bool    expired(int64_t deadline_us);

// ECONNRESET, EPIPE, ... -> Closed; everything else -> Error
tcp_status_t classifyErrno(int e);

// Waits until fd is readable/writable, the deadline passes, or one of the stop
// flags becomes true (-> Closed). Error: select() failed, *err_out holds errno.
tcp_status_t waitFd(int fd, bool for_write, int64_t deadline_us,
              const std::atomic<bool>* stop_a, const std::atomic<bool>* stop_b, int* err_out);

bool setNonBlocking(int fd);
void applySocketOptions(int fd, const TcpOptions& o);   // TCP_NODELAY, keepalive (best effort)
int  bindToNetif(int fd, const std::string& key);    // 0 or errno-style code

// Socket address -> IP (ip may be null), returns the port. IPv4-mapped IPv6
// addresses (IPv4 client on a dual-stack listener) come back as IPv4.
uint16_t sockaddrToIp(const struct sockaddr_storage& a, esp_ip_addr_t* ip);

#if CONFIG_TCPLIB_IPV6
// A link-local IPv6 address (fe80::/10) is only valid on one interface; without
// one, lwIP has no route (EHOSTUNREACH). If no scope is set, use the netif named
// by netif_key, or the default netif.
void setLinkLocalScope(struct sockaddr_in6& a, const std::string& netif_key);
#endif

} // namespace tcp_detail
