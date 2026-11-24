/**
 * @file tcp_options.h
 * @brief Socket options applied when a socket is created.
 */
#pragma once

#include <string>

/**
 * @brief Socket options applied by TcpClient::connect() and TcpListener::listen(),
 *        and to every connection accepted by a TcpListener.
 * @ingroup tcplib
 *
 * TCP-level options are best effort: read one back with
 * TcpClient::getOption() if it matters.
 */
struct TcpOptions {
	bool no_delay        = true;    ///< TCP_NODELAY: send small writes immediately (no Nagle delay).
	bool keepalive       = true;    ///< SO_KEEPALIVE: detect a silently dead peer.
	int  keep_idle_s     = 30;      ///< TCP_KEEPIDLE: idle seconds before the first keepalive probe.
	int  keep_interval_s = 5;       ///< TCP_KEEPINTVL: seconds between keepalive probes.
	int  keep_count      = 3;       ///< TCP_KEEPCNT: unanswered probes before the connection is dropped.

	/**
	 * @brief Bind the socket to one network interface, by esp_netif key.
	 *
	 * For example "WIFI_STA_DEF" (Wi-Fi station), "WIFI_AP_DEF" (Wi-Fi soft-AP)
	 * or "ETH_DEF" (Ethernet). Empty = any interface (routing decides).
	 * An unknown key makes connect()/listen() fail with ENODEV.
	 * Link-local IPv6 destinations are also sent out on this interface.
	 */
	std::string netif_key;

	bool reuse_addr = true;         ///< SO_REUSEADDR (TcpListener only).
};
