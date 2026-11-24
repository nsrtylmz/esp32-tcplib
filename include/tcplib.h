/**
 * @file tcplib.h
 * @brief Umbrella header: includes the whole public API.
 *
 * @defgroup tcplib tcplib
 * @brief Thin C++ wrapper around lwIP TCP sockets for ESP-IDF.
 *
 * - TcpClient: one TCP connection (connect, read, write, close)
 * - TcpListener: a listening socket (listen, accept)
 * - TcpOptions: socket options applied on creation
 * - tcp_status_t: result of every operation
 */
#pragma once

#include "tcp_status.h"
#include "tcp_options.h"
#include "TcpClient.h"
#include "TcpListener.h"
