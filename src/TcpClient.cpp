#include "TcpClient.h"
#include "tcp_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <atomic>
#include <errno.h>
#include <mutex>
#include <new>
#include <stdio.h>
#include <string.h>


struct TcpClient::Impl {
	std::atomic<int>  fd{-1};
	std::mutex        rx;                 // held by read / waitReadable / checkAlive
	std::mutex        tx;                 // held by write / waitWritable / shutdown / options
	std::atomic<bool> closing{false};     // close() was called
	std::atomic<bool> broken{false};      // peer closed or fatal error seen
	std::atomic<int>  err{0};
	esp_ip_addr_t remote_ip{}, local_ip{};
	uint16_t remote_port = 0, local_port = 0;

	bool usable() const { return fd >= 0 && !closing && !broken; }

	tcp_status_t fail(int e)                    // fatal error: stream becomes unusable
	{
		err = e;
		broken = true;
		return tcp_detail::classifyErrno(e);
	}
};


TcpClient::TcpClient() = default;
TcpClient::~TcpClient() { close(); }
TcpClient::TcpClient(TcpClient&&) noexcept = default;

TcpClient& TcpClient::operator=(TcpClient&& other) noexcept
{
	if (this != &other) {
		close();
		_s = std::move(other._s);
	}
	return *this;
}


TcpClient TcpClient::fromFd(int fd)
{
	TcpClient s;
	std::unique_ptr<Impl> p(new (std::nothrow) Impl());
	if (!p) {
		lwip_close(fd);
		return s;
	}
	p->fd = fd;

	struct sockaddr_storage a;
	socklen_t len = sizeof(a);
	memset(&a, 0, sizeof(a));
	if (lwip_getpeername(fd, (struct sockaddr*)&a, &len) == 0)
		p->remote_port = tcp_detail::sockaddrToIp(a, &p->remote_ip);
	len = sizeof(a);
	memset(&a, 0, sizeof(a));
	if (lwip_getsockname(fd, (struct sockaddr*)&a, &len) == 0)
		p->local_port = tcp_detail::sockaddrToIp(a, &p->local_ip);
	s._s = std::move(p);
	return s;
}


tcp_status_t TcpClient::connect(const char* host, uint16_t port, int timeout_ms, const TcpOptions& opts, int* err_out)
{
	int dummy = 0;
	int& err = err_out ? *err_out : dummy;
	err = 0;
	close();

	if (!host) { err = EINVAL; return tcp_status_t::Error; }

	struct addrinfo hints;
	struct addrinfo* res = nullptr;
	memset(&hints, 0, sizeof(hints));
#if CONFIG_TCPLIB_IPV6
	hints.ai_family = AF_UNSPEC;
#else
	hints.ai_family = AF_INET;
#endif
	hints.ai_socktype = SOCK_STREAM;
	char port_str[8];
	snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
	int gai = lwip_getaddrinfo(host, port_str, &hints, &res);
	if (gai != 0 || !res) {
		err = gai ? gai : EHOSTUNREACH;
		return tcp_status_t::Error;
	}
	struct sockaddr_storage addr;
	socklen_t addr_len = res->ai_addrlen;
	memcpy(&addr, res->ai_addr, addr_len);
	const int family = res->ai_family;
	lwip_freeaddrinfo(res);
#if CONFIG_TCPLIB_IPV6
	if (family == AF_INET6)
		tcp_detail::setLinkLocalScope(reinterpret_cast<struct sockaddr_in6&>(addr), opts.netif_key);
#endif

	int fd = lwip_socket(family, SOCK_STREAM, 0);
	if (fd < 0) { err = errno; return tcp_status_t::Error; }

	int e = tcp_detail::bindToNetif(fd, opts.netif_key);
	if (e != 0 || !tcp_detail::setNonBlocking(fd)) {
		err = e ? e : errno;
		lwip_close(fd);
		return tcp_status_t::Error;
	}
	tcp_detail::applySocketOptions(fd, opts);

	int rc = lwip_connect(fd, (struct sockaddr*)&addr, addr_len);
	if (rc < 0 && errno != EINPROGRESS) {
		err = errno;
		lwip_close(fd);
		return tcp_status_t::Error;
	}
	if (rc < 0) {   // handshake in progress
		int werr = 0;
		tcp_status_t w = tcp_detail::waitFd(fd, true, tcp_detail::deadlineFromTimeout(timeout_ms), nullptr, nullptr, &werr);
		if (w != tcp_status_t::Ok) {
			err = werr;
			lwip_close(fd);
			return w == tcp_status_t::Timeout ? tcp_status_t::Timeout : tcp_status_t::Error;
		}
		int so_err = 0;
		socklen_t len = sizeof(so_err);
		if (lwip_getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &len) < 0 || so_err != 0) {
			err = so_err ? so_err : errno;
			lwip_close(fd);
			return tcp_status_t::Error;
		}
	}

	*this = fromFd(fd);
	if (!isOpen()) { err = ENOMEM; return tcp_status_t::Error; }
	return tcp_status_t::Ok;
}


tcp_status_t TcpClient::read(void* buf, size_t cap, size_t& got, int timeout_ms)
{
	got = 0;
	if (!_s) return tcp_status_t::Closed;
	if (!buf || cap == 0) { _s->err = EINVAL; return tcp_status_t::Error; }

	std::lock_guard<std::mutex> lk(_s->rx);
	if (!_s->usable()) return tcp_status_t::Closed;

	const int64_t deadline = tcp_detail::deadlineFromTimeout(timeout_ms);
	for (;;) {
		int werr = 0;
		tcp_status_t w = tcp_detail::waitFd(_s->fd, false, deadline, &_s->closing, &_s->broken, &werr);
		if (w == tcp_status_t::Error) return _s->fail(werr);
		if (w != tcp_status_t::Ok) return w;

		int n = lwip_recv(_s->fd, buf, cap, MSG_DONTWAIT);
		if (n > 0) { got = (size_t)n; return tcp_status_t::Ok; }
		if (n == 0) { _s->broken = true; return tcp_status_t::Closed; }   // peer closed

		int e = errno;
		if (e == EAGAIN || e == EWOULDBLOCK || e == EINTR) {
			if (tcp_detail::expired(deadline)) return tcp_status_t::Timeout;
			continue;
		}
		return _s->fail(e);
	}
}


tcp_status_t TcpClient::write(const void* data, size_t len, int timeout_ms, size_t* written)
{
	size_t sent = 0;
	auto done = [&](tcp_status_t s) { if (written) *written = sent; return s; };

	if (!_s) return done(tcp_status_t::Closed);
	if (len == 0) return done(tcp_status_t::Ok);
	if (!data) { _s->err = EINVAL; return done(tcp_status_t::Error); }

	std::lock_guard<std::mutex> lk(_s->tx);
	if (!_s->usable()) return done(tcp_status_t::Closed);

	const uint8_t* p = static_cast<const uint8_t*>(data);
	const int64_t deadline = tcp_detail::deadlineFromTimeout(timeout_ms);
	while (sent < len) {
		int werr = 0;
		tcp_status_t w = tcp_detail::waitFd(_s->fd, true, deadline, &_s->closing, &_s->broken, &werr);
		if (w == tcp_status_t::Error) return done(_s->fail(werr));
		if (w != tcp_status_t::Ok) return done(w);

		int n = lwip_send(_s->fd, p + sent, len - sent, MSG_DONTWAIT);
		if (n > 0) { sent += (size_t)n; continue; }

		int e = (n < 0) ? errno : 0;
		if (n < 0 && (e == EAGAIN || e == EWOULDBLOCK || e == EINTR)) {
			if (tcp_detail::expired(deadline)) return done(tcp_status_t::Timeout);
			vTaskDelay(1);   // select said writable but the stack is full: don't spin
			continue;
		}
		if (n == 0) { _s->broken = true; return done(tcp_status_t::Closed); }
		return done(_s->fail(e));
	}
	return done(tcp_status_t::Ok);
}


tcp_status_t TcpClient::waitReadable(int timeout_ms)
{
	if (!_s) return tcp_status_t::Closed;
	std::lock_guard<std::mutex> lk(_s->rx);
	if (!_s->usable()) return tcp_status_t::Closed;
	int werr = 0;
	tcp_status_t w = tcp_detail::waitFd(_s->fd, false, tcp_detail::deadlineFromTimeout(timeout_ms),
	                          &_s->closing, &_s->broken, &werr);
	return w == tcp_status_t::Error ? _s->fail(werr) : w;
}


tcp_status_t TcpClient::waitWritable(int timeout_ms)
{
	if (!_s) return tcp_status_t::Closed;
	std::lock_guard<std::mutex> lk(_s->tx);
	if (!_s->usable()) return tcp_status_t::Closed;
	int werr = 0;
	tcp_status_t w = tcp_detail::waitFd(_s->fd, true, tcp_detail::deadlineFromTimeout(timeout_ms),
	                          &_s->closing, &_s->broken, &werr);
	return w == tcp_status_t::Error ? _s->fail(werr) : w;
}


bool TcpClient::isOpen() const
{
	return _s && _s->usable();
}


tcp_status_t TcpClient::checkAlive()
{
	if (!_s || !_s->usable()) return tcp_status_t::Closed;

	std::unique_lock<std::mutex> lk(_s->rx, std::try_to_lock);
	if (!lk.owns_lock())
		return tcp_status_t::Ok;           // a reader is active and will notice by itself
	if (!_s->usable()) return tcp_status_t::Closed;

	uint8_t b;
	int n = lwip_recv(_s->fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
	if (n > 0) return tcp_status_t::Ok;
	if (n == 0) { _s->broken = true; return tcp_status_t::Closed; }
	int e = errno;
	if (e == EAGAIN || e == EWOULDBLOCK || e == EINTR) return tcp_status_t::Ok;
	return _s->fail(e);
}


tcp_status_t TcpClient::shutdownWrite()
{
	if (!_s) return tcp_status_t::Closed;
	std::lock_guard<std::mutex> lk(_s->tx);
	if (!_s->usable()) return tcp_status_t::Closed;
	if (lwip_shutdown(_s->fd, SHUT_WR) < 0)
		return _s->fail(errno);
	return tcp_status_t::Ok;
}


void TcpClient::close()
{
	if (!_s) return;
	_s->closing = true;                                   // wakes blocked calls
	std::scoped_lock lk(_s->rx, _s->tx);
	int fd = _s->fd.exchange(-1);
	if (fd >= 0) lwip_close(fd);
}


tcp_status_t TcpClient::setOption(int level, int opt, const void* val, size_t len)
{
	if (!_s) return tcp_status_t::Closed;
	std::lock_guard<std::mutex> lk(_s->tx);
	if (!_s->usable()) return tcp_status_t::Closed;
	if (lwip_setsockopt(_s->fd, level, opt, val, (socklen_t)len) < 0) {
		_s->err = errno;
		return tcp_status_t::Error;
	}
	return tcp_status_t::Ok;
}


tcp_status_t TcpClient::getOption(int level, int opt, void* val, size_t* len)
{
	if (!_s) return tcp_status_t::Closed;
	if (!val || !len) return tcp_status_t::Error;
	std::lock_guard<std::mutex> lk(_s->tx);
	if (!_s->usable()) return tcp_status_t::Closed;
	socklen_t l = (socklen_t)*len;
	if (lwip_getsockopt(_s->fd, level, opt, val, &l) < 0) {
		_s->err = errno;
		return tcp_status_t::Error;
	}
	*len = (size_t)l;
	return tcp_status_t::Ok;
}


int TcpClient::lastErrno() const                { return _s ? _s->err.load() : 0; }
static esp_ip4_addr_t ip4Of(const esp_ip_addr_t& ip)
{
	return ip.type == ESP_IPADDR_TYPE_V4 ? ip.u_addr.ip4 : esp_ip4_addr_t{};
}

static std::string ipToString(const esp_ip_addr_t& ip)
{
	char buf[46];   // longest IPv6 text form
#if CONFIG_TCPLIB_IPV6
	if (ip.type == ESP_IPADDR_TYPE_V6)
		return inet_ntop(AF_INET6, ip.u_addr.ip6.addr, buf, sizeof(buf)) ? buf : "";
#endif
	esp_ip4addr_ntoa(&ip.u_addr.ip4, buf, sizeof(buf));
	return std::string(buf);
}

esp_ip_addr_t TcpClient::remoteAddr() const     { return _s ? _s->remote_ip : esp_ip_addr_t{}; }
esp_ip_addr_t TcpClient::localAddr() const      { return _s ? _s->local_ip : esp_ip_addr_t{}; }
esp_ip4_addr_t TcpClient::remoteIp() const      { return ip4Of(remoteAddr()); }
uint16_t TcpClient::remotePort() const          { return _s ? _s->remote_port : 0; }
esp_ip4_addr_t TcpClient::localIp() const       { return ip4Of(localAddr()); }
uint16_t TcpClient::localPort() const           { return _s ? _s->local_port : 0; }
std::string TcpClient::remoteIpString() const   { return ipToString(remoteAddr()); }
std::string TcpClient::localIpString() const    { return ipToString(localAddr()); }

esp_netif_t* TcpClient::netif() const
{
	if (!_s) return nullptr;
	esp_netif_t* n = nullptr;
	while ((n = esp_netif_next_unsafe(n)) != nullptr) {
#if CONFIG_TCPLIB_IPV6
		if (_s->local_ip.type == ESP_IPADDR_TYPE_V6) {
			esp_ip6_addr_t ip6[LWIP_IPV6_NUM_ADDRESSES];
			int cnt = esp_netif_get_all_ip6(n, ip6);
			for (int i = 0; i < cnt; i++)
				if (memcmp(ip6[i].addr, _s->local_ip.u_addr.ip6.addr, sizeof(ip6[i].addr)) == 0)
					return n;
			continue;
		}
#endif
		esp_netif_ip_info_t info;
		if (esp_netif_get_ip_info(n, &info) == ESP_OK &&
		    info.ip.addr != 0 && info.ip.addr == _s->local_ip.u_addr.ip4.addr)
			return n;
	}
	return nullptr;
}

