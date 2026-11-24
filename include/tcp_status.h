/**
 * @file tcp_status.h
 * @brief Result code returned by every tcplib operation.
 */
#pragma once

/**
 * @brief Result of a tcplib operation.
 * @ingroup tcplib
 */
enum class tcp_status_t {
	Ok,        ///< Operation completed. For TcpClient::read(): at least one byte was read.
	Timeout,   ///< Nothing happened within the timeout; the connection is still fine.
	Closed,    ///< Closed by the peer, by close(), or after an earlier fatal error. Permanent until the next connect().
	Error      ///< Any other failure; see lastErrno() or the @p err_out argument.
};

/**
 * @brief Printable name of a status code.
 * @ingroup tcplib
 * @param s Status code.
 * @return "Ok", "Timeout", "Closed" or "Error" (static string, never null).
 */
inline const char* tcpStatusToString(tcp_status_t s)
{
	switch (s) {
		case tcp_status_t::Ok:      return "Ok";
		case tcp_status_t::Timeout: return "Timeout";
		case tcp_status_t::Closed:  return "Closed";
		default:                    return "Error";
	}
}
