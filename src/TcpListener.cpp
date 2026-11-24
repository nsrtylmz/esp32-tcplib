#include "TcpListener.h"
#include "TcpClient.h"
#include "tcp_internal.h"
#include "lwip/sockets.h"
#include <atomic>
#include <errno.h>
#include <mutex>
#include <new>
#include <string.h>


struct TcpListener::Impl {
	std::atomic<int>  fd{-1};
	std::mutex        m;                  // held by accept / options
	std::atomic<bool> closing{false};
	std::atomic<int>  err{0};
	uint16_t          port = 0;
	TcpOptions        opts;
};


TcpListener::TcpListener() = default;
TcpListener::~TcpListener() { close(); }
TcpListener::TcpListener(TcpListener&&) noexcept = default;

TcpListener& TcpListener::operator=(TcpListener&& other) noexcept
{
	if (this != &other) {
		close();
		_s = std::move(other._s);
	}
	return *this;
}


tcp_status_t TcpListener::listen(uint16_t port, int backlog, const TcpOptions& opts, int* err_out)
{
	int dummy = 0;
	int& err = err_out ? *err_out : dummy;
	err = 0;
	close();

#if CONFIG_TCPLIB_IPV6
	const int family = AF_INET6;   // dual-stack: also accepts IPv4 clients
#else
	const int family = AF_INET;
#endif
	int fd = lwip_socket(family, SOCK_STREAM, 0);
	if (fd < 0) { err = errno; return tcp_status_t::Error; }

	if (opts.reuse_addr) {
		int one = 1;
		lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	}

	int e = tcp_detail::bindToNetif(fd, opts.netif_key);
	if (e != 0) { err = e; lwip_close(fd); return tcp_status_t::Error; }

	struct sockaddr_storage addr;   // all zero = any address (0.0.0.0 / ::)
	memset(&addr, 0, sizeof(addr));
	socklen_t addr_len;
#if CONFIG_TCPLIB_IPV6
	struct sockaddr_in6& a = reinterpret_cast<struct sockaddr_in6&>(addr);
	a.sin6_family = AF_INET6;
	a.sin6_port = htons(port);
	addr_len = sizeof(a);
#else
	struct sockaddr_in& a = reinterpret_cast<struct sockaddr_in&>(addr);
	a.sin_family = AF_INET;
	a.sin_port = htons(port);
	addr_len = sizeof(a);
#endif
	if (lwip_bind(fd, (struct sockaddr*)&addr, addr_len) < 0 ||
	    lwip_listen(fd, backlog > 0 ? backlog : 1) < 0 ||
	    !tcp_detail::setNonBlocking(fd)) {
		err = errno;
		lwip_close(fd);
		return tcp_status_t::Error;
	}

	std::unique_ptr<Impl> p(new (std::nothrow) Impl());
	if (!p) { err = ENOMEM; lwip_close(fd); return tcp_status_t::Error; }
	p->fd = fd;
	p->opts = opts;
	p->port = port;
	socklen_t len = sizeof(addr);
	memset(&addr, 0, sizeof(addr));
	if (lwip_getsockname(fd, (struct sockaddr*)&addr, &len) == 0)
		p->port = tcp_detail::sockaddrToIp(addr, nullptr);

	_s = std::move(p);
	return tcp_status_t::Ok;
}


tcp_status_t TcpListener::accept(TcpClient& out, int timeout_ms)
{
	if (!_s) return tcp_status_t::Closed;
	std::lock_guard<std::mutex> lk(_s->m);
	if (_s->fd < 0 || _s->closing) return tcp_status_t::Closed;

	const int64_t deadline = tcp_detail::deadlineFromTimeout(timeout_ms);
	while (1)
	{
		int werr = 0;
		tcp_status_t w = tcp_detail::waitFd(_s->fd, false, deadline, &_s->closing, nullptr, &werr);
		if (w == tcp_status_t::Error) { _s->err = werr; return tcp_status_t::Error; }
		if (w != tcp_status_t::Ok) return w;

		struct sockaddr_storage ca;
		socklen_t len = sizeof(ca);
		int cfd = lwip_accept(_s->fd, (struct sockaddr*)&ca, &len);
		if (cfd < 0) {
			int e = errno;
			if (e == EAGAIN || e == EWOULDBLOCK || e == EINTR || e == ECONNABORTED) {
				if (tcp_detail::expired(deadline)) return tcp_status_t::Timeout;
				continue;
			}
			_s->err = e;
			return tcp_status_t::Error;
		}

		if (!tcp_detail::setNonBlocking(cfd)) {
			_s->err = errno;
			lwip_close(cfd);
			return tcp_status_t::Error;
		}
		tcp_detail::applySocketOptions(cfd, _s->opts);

		out = TcpClient::fromFd(cfd);
		if (!out.isOpen()) { _s->err = ENOMEM; return tcp_status_t::Error; }
		return tcp_status_t::Ok;
	}
}


bool TcpListener::isOpen() const
{
	return _s && _s->fd >= 0 && !_s->closing;
}

uint16_t TcpListener::port() const
{
	return _s ? _s->port : 0;
}

void TcpListener::close()
{
	if (!_s) return;
	_s->closing = true;
	std::lock_guard<std::mutex> lk(_s->m);
	int fd = _s->fd.exchange(-1);
	if (fd >= 0) lwip_close(fd);
}


tcp_status_t TcpListener::setOption(int level, int opt, const void* val, size_t len)
{
	if (!_s) return tcp_status_t::Closed;
	std::lock_guard<std::mutex> lk(_s->m);
	if (_s->fd < 0) return tcp_status_t::Closed;
	if (lwip_setsockopt(_s->fd, level, opt, val, (socklen_t)len) < 0) {
		_s->err = errno;
		return tcp_status_t::Error;
	}
	return tcp_status_t::Ok;
}

tcp_status_t TcpListener::getOption(int level, int opt, void* val, size_t* len)
{
	if (!_s) return tcp_status_t::Closed;
	if (!val || !len) return tcp_status_t::Error;
	std::lock_guard<std::mutex> lk(_s->m);
	if (_s->fd < 0) return tcp_status_t::Closed;
	socklen_t l = (socklen_t)*len;
	if (lwip_getsockopt(_s->fd, level, opt, val, &l) < 0) {
		_s->err = errno;
		return tcp_status_t::Error;
	}
	*len = (size_t)l;
	return tcp_status_t::Ok;
}

int TcpListener::lastErrno() const
{
	return _s ? _s->err.load() : 0;
}

