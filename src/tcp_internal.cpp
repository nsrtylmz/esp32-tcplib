
#include "tcp_internal.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>

namespace tcp_detail {

int64_t nowUs()
{
	return esp_timer_get_time();
}

int64_t deadlineFromTimeout(int timeout_ms)
{
	return timeout_ms < 0 ? INT64_MAX : nowUs() + (int64_t)timeout_ms * 1000;
}

bool expired(int64_t deadline_us)
{
	return deadline_us != INT64_MAX && nowUs() >= deadline_us;
}

tcp_status_t classifyErrno(int e)
{
	switch (e) {
		case ECONNRESET:
		case EPIPE:
		case ECONNABORTED:
		case ENOTCONN:
			return tcp_status_t::Closed;
		default:
			return tcp_status_t::Error;
	}
}

tcp_status_t waitFd(int fd, bool for_write, int64_t deadline_us, const std::atomic<bool>* stop_a, const std::atomic<bool>* stop_b, int* err_out)
{
	for (;;) {
		if ((stop_a && *stop_a) || (stop_b && *stop_b))
			return tcp_status_t::Closed;

		int slice = kWaitSliceMs;
		if (deadline_us != INT64_MAX) {
			int64_t remain_us = deadline_us - nowUs();
			if (remain_us < 0) remain_us = 0;
			int64_t remain_ms = (remain_us + 999) / 1000;   // round up
			if (remain_ms < slice) slice = (int)remain_ms;
		}

		fd_set set;
		FD_ZERO(&set);
		FD_SET(fd, &set);
		struct timeval tv;
		tv.tv_sec = slice / 1000;
		tv.tv_usec = (slice % 1000) * 1000;

		int r = lwip_select(fd + 1, for_write ? nullptr : &set, for_write ? &set : nullptr,
		                    nullptr, &tv);
		if (r > 0)
			return tcp_status_t::Ok;
		if (r < 0) {
			if (errno == EINTR) continue;
			if (err_out) *err_out = errno;
			return tcp_status_t::Error;
		}
		if (expired(deadline_us))
			return tcp_status_t::Timeout;
	}
}

bool setNonBlocking(int fd)
{
	int flags = lwip_fcntl(fd, F_GETFL, 0);
	return flags >= 0 && lwip_fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

static void setIntOpt(int fd, int level, int opt, int value)
{
	lwip_setsockopt(fd, level, opt, &value, sizeof(value));   // best effort
}

void applySocketOptions(int fd, const TcpOptions& o)
{
	setIntOpt(fd, IPPROTO_TCP, TCP_NODELAY, o.no_delay ? 1 : 0);
	if (o.keepalive) {
		setIntOpt(fd, SOL_SOCKET,  SO_KEEPALIVE,  1);
		setIntOpt(fd, IPPROTO_TCP, TCP_KEEPIDLE,  o.keep_idle_s);
		setIntOpt(fd, IPPROTO_TCP, TCP_KEEPINTVL, o.keep_interval_s);
		setIntOpt(fd, IPPROTO_TCP, TCP_KEEPCNT,   o.keep_count);
	}
}

int bindToNetif(int fd, const std::string& key)
{
	if (key.empty())
		return 0;

	esp_netif_t* netif = esp_netif_get_handle_from_ifkey(key.c_str());
	if (!netif)
		return ENODEV;

	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	if (esp_netif_get_netif_impl_name(netif, ifr.ifr_name) != ESP_OK)
		return ENODEV;
	if (lwip_setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)) < 0)
		return errno;
	return 0;
}

uint16_t sockaddrToIp(const struct sockaddr_storage& a, esp_ip_addr_t* ip)
{
	esp_ip_addr_t out;
	memset(&out, 0, sizeof(out));
	uint16_t port = 0;
	if (a.ss_family == AF_INET) {
		const struct sockaddr_in& in = reinterpret_cast<const struct sockaddr_in&>(a);
		out.type = ESP_IPADDR_TYPE_V4;
		out.u_addr.ip4.addr = in.sin_addr.s_addr;
		port = ntohs(in.sin_port);
	}
#if CONFIG_TCPLIB_IPV6
	else if (a.ss_family == AF_INET6) {
		const struct sockaddr_in6& in6 = reinterpret_cast<const struct sockaddr_in6&>(a);
		const uint32_t* w = in6.sin6_addr.un.u32_addr;
		port = ntohs(in6.sin6_port);
		if (w[0] == 0 && w[1] == 0 && w[2] == htonl(0xffff)) {   // ::ffff:a.b.c.d
			out.type = ESP_IPADDR_TYPE_V4;
			out.u_addr.ip4.addr = w[3];
		} else {
			out.type = ESP_IPADDR_TYPE_V6;
			memcpy(out.u_addr.ip6.addr, w, sizeof(out.u_addr.ip6.addr));
			out.u_addr.ip6.zone = (uint8_t)in6.sin6_scope_id;
		}
	}
#endif
	if (ip) *ip = out;
	return port;
}

#if CONFIG_TCPLIB_IPV6
void setLinkLocalScope(struct sockaddr_in6& a, const std::string& netif_key)
{
	const uint8_t* b = a.sin6_addr.s6_addr;
	const bool link_local = b[0] == 0xfe && (b[1] & 0xc0) == 0x80;
	if (!link_local || a.sin6_scope_id != 0)
		return;
	esp_netif_t* n = netif_key.empty() ? esp_netif_get_default_netif()
	                                   : esp_netif_get_handle_from_ifkey(netif_key.c_str());
	int idx = n ? esp_netif_get_netif_impl_index(n) : -1;
	if (idx > 0)
		a.sin6_scope_id = (uint32_t)idx;
}
#endif

} // namespace tcp_detail
