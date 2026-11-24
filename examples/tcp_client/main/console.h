#pragma once
#include "tcplib.h"


// Lines typed in the serial monitor are queued here and sent by the task that
// owns the TcpClient, so the client is never shared between tasks.

// Starts the task that reads lines from the console UART.
void console_start();

// Only used for the "not connected" hint when a line is typed.
void console_set_connected(bool connected);

// Sends every queued line as typed (no '\n' is added). false = the write failed.
bool console_send_queued(TcpClient& client);
