/**
 * @file TcpClient.h
 * @brief TcpClient: one TCP connection.
 */
#pragma once

#include <memory>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include "esp_netif.h"
#include "tcp_options.h"
#include "tcp_status.h"

class TcpListener;

/**
 * @brief One TCP connection (a connected socket).
 * @ingroup tcplib
 *
 * Created by connect() or handed out by TcpListener::accept(). Move-only; the
 * destructor closes the socket.
 *
 * @code
 * TcpClient client;
 * if (client.connect("192.168.1.10", 5000, 3000) == tcp_status_t::Ok)
 *     client.write("hello", 5, 1000);
 * @endcode
 *
 * **Timeouts.** Every blocking operation takes `timeout_ms`:
 * `> 0` waits up to that long, `0` tries once and never waits, `< 0` waits forever.
 * The socket is always non-blocking internally and the file descriptor is not
 * exposed, so the blocking mode cannot be changed behind the library's back.
 *
 * **Thread rules.**
 * | Call | Rule |
 * |---|---|
 * | write(), shutdownWrite(), waitWritable() | any task; calls are serialized, so concurrent writes never interleave |
 * | read(), waitReadable() | one task at a time |
 * | close() | any task; wakes blocked calls (they return Closed within ~100 ms), except connect() |
 *
 * Do not connect(), move or destroy a TcpClient while another task is using it.
 *
 * After tcp_status_t::Closed or a fatal tcp_status_t::Error the connection is
 * permanently unusable until connect() is called again.
 */
class TcpClient {
public:
	/** @brief Creates an unconnected object (isOpen() == false). */
	TcpClient();
	/** @brief Closes the socket, if any. */
	~TcpClient();
	/**
	 * @brief Takes over another object's connection.
	 * @param other Source; becomes unconnected.
	 */
	TcpClient(TcpClient&& other) noexcept;
	/**
	 * @brief Closes the current socket first, then takes over another object's connection.
	 * @param other Source; becomes unconnected.
	 * @return *this
	 */
	TcpClient& operator=(TcpClient&& other) noexcept;
	TcpClient(const TcpClient&) = delete;
	TcpClient& operator=(const TcpClient&) = delete;

	/**
	 * @brief Connects to a server.
	 *
	 * Any previous connection held by this object is closed first, so the same
	 * object can be reused to reconnect.
	 *
	 * @param host       IPv4 address or hostname. With `CONFIG_TCPLIB_IPV6` also an
	 *                   IPv6 address (without a `%zone` suffix); hostnames then prefer IPv4.
	 * @param port       Server port.
	 * @param timeout_ms Limit for the TCP handshake only, not for the DNS lookup.
	 * @param opts       Socket options, including the interface to use (TcpOptions::netif_key).
	 * @param err_out    Optional. On Error: the errno, or the getaddrinfo() code
	 *                   (`EAI_...`) when name resolution failed; ENODEV when
	 *                   TcpOptions::netif_key names an unknown interface. 0 otherwise.
	 * @retval tcp_status_t::Ok      Connected.
	 * @retval tcp_status_t::Timeout The handshake did not finish within @p timeout_ms.
	 * @retval tcp_status_t::Error   Name resolution, socket setup or the handshake failed.
	 *
	 * @warning connect() cannot be cancelled: close() from another task does not
	 *          wake it (it may not be called concurrently anyway). With
	 *          @p timeout_ms < 0 it waits until the handshake succeeds or fails, so
	 *          prefer a finite timeout and retry in a loop.
	 */
	tcp_status_t connect(const char* host, uint16_t port, int timeout_ms, const TcpOptions& opts = TcpOptions(), int* err_out = nullptr);

	/**
	 * @brief Reads 1..@p cap bytes.
	 * @param buf        Destination buffer.
	 * @param cap        Size of @p buf (must be > 0).
	 * @param[out] got   Number of bytes read (0 unless Ok).
	 * @param timeout_ms How long to wait for data.
	 * @retval tcp_status_t::Ok      @p got > 0 bytes are in @p buf.
	 * @retval tcp_status_t::Timeout No data yet; the connection is fine.
	 * @retval tcp_status_t::Closed  The peer closed the connection. Buffered data is
	 *                               delivered first, Closed comes afterwards.
	 * @retval tcp_status_t::Error   See lastErrno().
	 */
	tcp_status_t read(void* buf, size_t cap, size_t& got, int timeout_ms);

	/**
	 * @brief Writes all @p len bytes, or fails.
	 * @param data       Bytes to send.
	 * @param len        Number of bytes.
	 * @param timeout_ms Limit for the whole write.
	 * @param written    Optional. Always set to the number of bytes accepted,
	 *                   e.g. after a Timeout.
	 * @retval tcp_status_t::Ok      Everything was accepted by the stack.
	 * @retval tcp_status_t::Timeout Only part was accepted; see @p written.
	 * @retval tcp_status_t::Closed  The connection is closed.
	 * @retval tcp_status_t::Error   See lastErrno().
	 */
	tcp_status_t write(const void* data, size_t len, int timeout_ms, size_t* written = nullptr);

	/**
	 * @brief Waits until data can be read.
	 * @param timeout_ms How long to wait.
	 * @return Ok when readable (this can also mean "peer closed": the next read()
	 *         tells), Timeout, Closed or Error.
	 */
	tcp_status_t waitReadable(int timeout_ms);

	/**
	 * @brief Waits until data can be written.
	 * @param timeout_ms How long to wait.
	 * @return Ok, Timeout, Closed or Error.
	 */
	tcp_status_t waitWritable(int timeout_ms);

	/**
	 * @brief Tells whether the connection is usable. Local state only, no I/O.
	 * @return True while connected and no Closed/fatal Error has been seen.
	 */
	bool isOpen() const;

	/**
	 * @brief Checks whether the peer is still there, without consuming data.
	 *
	 * Peeks the socket. If another task is blocked in read(), returns Ok without
	 * peeking (that reader will notice by itself).
	 *
	 * @retval tcp_status_t::Ok     Looks healthy.
	 * @retval tcp_status_t::Closed The peer closed or reset the connection.
	 * @note A silently dead link (cable pulled, power loss) is only detected by
	 *       keepalive (TcpOptions::keepalive) or by your own heartbeat.
	 */
	tcp_status_t checkAlive();

	/**
	 * @brief Half-close: sends FIN. Reading is still possible.
	 * @return Ok, Closed or Error.
	 */
	tcp_status_t shutdownWrite();

	/**
	 * @brief Closes the connection. Safe to call from any task and more than once.
	 *
	 * Blocked read()/write()/wait calls in other tasks return Closed within ~100 ms.
	 */
	void close();

	/**
	 * @brief Sets a raw socket option, as setsockopt(). The blocking mode cannot be changed.
	 * @param level Option level, e.g. SOL_SOCKET or IPPROTO_TCP (include lwip/sockets.h).
	 * @param opt   Option name, e.g. SO_RCVBUF.
	 * @param val   Option value.
	 * @param len   Size of @p val.
	 * @return Ok, Closed, or Error (see lastErrno()).
	 */
	tcp_status_t setOption(int level, int opt, const void* val, size_t len);

	/**
	 * @brief Reads a raw socket option, as getsockopt().
	 * @param level         Option level, e.g. SOL_SOCKET or IPPROTO_TCP.
	 * @param opt           Option name.
	 * @param[out] val      Receives the value.
	 * @param[in,out] len   In: size of @p val. Out: size of the value.
	 * @return Ok, Closed, or Error (see lastErrno()).
	 */
	tcp_status_t getOption(int level, int opt, void* val, size_t* len);

	/**
	 * @brief Error code of the last failure on this connection.
	 * @return errno value, 0 if none.
	 */
	int lastErrno() const;

	/**
	 * @name Addresses
	 * Cached at connect/accept time, so still valid after the connection is closed.
	 * @{
	 */
	/** @brief Remote IPv4 address. @return The address; 0.0.0.0 for an IPv6 peer (use remoteAddr()). */
	esp_ip4_addr_t remoteIp() const;
	/** @brief Remote port. @return The port; 0 when never connected. */
	uint16_t       remotePort() const;
	/** @brief Local IPv4 address. @return The address; 0.0.0.0 for an IPv6 connection (use localAddr()). */
	esp_ip4_addr_t localIp() const;
	/** @brief Local port. @return The port; 0 when never connected. */
	uint16_t       localPort() const;
	/** @brief Remote address. @return IPv4 or IPv6 address (check its @c type). */
	esp_ip_addr_t  remoteAddr() const;
	/** @brief Local address. @return IPv4 or IPv6 address (check its @c type). */
	esp_ip_addr_t  localAddr() const;
	/** @brief Remote address as text. @return E.g. "192.168.1.10" or "FE80::1". */
	std::string    remoteIpString() const;
	/** @brief Local address as text. @return E.g. "192.168.1.101". */
	std::string    localIpString() const;
	/** @} */

	/**
	 * @brief Network interface this connection runs on (matched by local address).
	 * @return The interface, or nullptr if none matches. Uses esp_netif_next_unsafe().
	 */
	esp_netif_t* netif() const;

private:
	struct Impl;
	friend class TcpListener;
	static TcpClient fromFd(int fd);
	std::unique_ptr<Impl> _s;
};
