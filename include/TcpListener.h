/**
 * @file TcpListener.h
 * @brief TcpListener: a listening (server) socket.
 */
#pragma once

#include <memory>
#include <stddef.h>
#include <stdint.h>
#include "TcpClient.h"
#include "tcp_options.h"
#include "tcp_status.h"

/**
 * @brief A listening TCP socket that hands out TcpClient connections.
 * @ingroup tcplib
 *
 * Move-only; the destructor closes it.
 *
 * @code
 * TcpListener listener;
 * listener.listen(5000, 4);
 * TcpClient client;
 * if (listener.accept(client, 1000) == tcp_status_t::Ok) { ... }
 * @endcode
 *
 * accept() may be used by one task at a time. close() may be called from any
 * task and wakes a blocked accept() (it returns Closed within ~100 ms).
 * Accepted connections get the TcpOptions given to listen().
 * With `CONFIG_TCPLIB_IPV6` one dual-stack socket accepts IPv4 and IPv6 clients.
 */
class TcpListener {
public:
	/** @brief Creates an object that is not listening. */
	TcpListener();
	/** @brief Closes the socket, if any. */
	~TcpListener();
	/**
	 * @brief Takes over another listener's socket.
	 * @param other Source; stops listening.
	 */
	TcpListener(TcpListener&& other) noexcept;
	/**
	 * @brief Closes the current socket first, then takes over another listener's socket.
	 * @param other Source; stops listening.
	 * @return *this
	 */
	TcpListener& operator=(TcpListener&& other) noexcept;
	TcpListener(const TcpListener&) = delete;
	TcpListener& operator=(const TcpListener&) = delete;

	/**
	 * @brief Starts listening.
	 *
	 * Binds to all addresses, or to one interface when TcpOptions::netif_key is
	 * set. Any previous socket held by this object is closed first.
	 *
	 * @param port    Port; 0 lets the system choose (see port()).
	 * @param backlog Pending connections the stack may queue (at least 1 is used).
	 * @param opts    Options for the listening socket and every accepted connection.
	 * @param err_out Optional. On Error: the errno (ENODEV for an unknown interface).
	 * @return Ok or Error.
	 */
	tcp_status_t listen(uint16_t port, int backlog, const TcpOptions& opts = TcpOptions(), int* err_out = nullptr);

	/**
	 * @brief Waits for a client.
	 * @param[out] out    Receives the new connection (a connection it held is closed).
	 * @param timeout_ms  0 = non-blocking, < 0 = wait until a client connects or
	 *                    close() is called.
	 * @retval tcp_status_t::Ok      @p out holds the new connection.
	 * @retval tcp_status_t::Timeout Nobody connected.
	 * @retval tcp_status_t::Closed  The listener is closed.
	 * @retval tcp_status_t::Error   See lastErrno().
	 */
	tcp_status_t accept(TcpClient& out, int timeout_ms);

	/** @brief Tells whether the listener is open. @return True while listening. */
	bool isOpen() const;
	/** @brief The port actually listened on. @return The port (useful after listen(0, ...)); 0 if not listening. */
	uint16_t port() const;
	/** @brief Stops listening. Safe to call from any task and more than once. */
	void close();

	/**
	 * @brief Sets a raw socket option on the listening socket, as setsockopt().
	 * @param level Option level, e.g. SOL_SOCKET (include lwip/sockets.h).
	 * @param opt   Option name.
	 * @param val   Option value.
	 * @param len   Size of @p val.
	 * @return Ok, Closed, or Error (see lastErrno()).
	 */
	tcp_status_t setOption(int level, int opt, const void* val, size_t len);
	/**
	 * @brief Reads a raw socket option, as getsockopt().
	 * @param level         Option level.
	 * @param opt           Option name.
	 * @param[out] val      Receives the value.
	 * @param[in,out] len   In: size of @p val. Out: size of the value.
	 * @return Ok, Closed, or Error (see lastErrno()).
	 */
	tcp_status_t getOption(int level, int opt, void* val, size_t* len);
	/** @brief Error code of the last failure. @return errno value, 0 if none. */
	int lastErrno() const;

private:
	struct Impl;
	std::unique_ptr<Impl> _s;
};
