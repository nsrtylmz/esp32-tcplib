// Runs up to three TCP clients at the same time, each in its own task with its
// own TcpClient, optionally bound to a chosen network interface.
//
// Every client sends a numbered line ("client2 #17") each period and checks
// that the server echoes it back unchanged. The first line of each connection
// also reports the interface ("client2 #0 hello netif=WIFI_STA_DEF").
// tools/tcp_multi_client_test.py is the matching server and test.
//
// Configure in `idf.py menuconfig`:
//   "Multi Client Configuration"       server, period, and per client: enable,
//                                      address, port, network interface
//   "Example Connection Configuration" Wi-Fi and/or Ethernet
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tcplib.h"
#include "client_config.h"

static int64_t now_ms() { return esp_timer_get_time() / 1000; }

struct Stats {
	unsigned connects = 0;
	unsigned sent = 0;
	unsigned echoed = 0;
	unsigned mismatched = 0;
};

// Exchanges numbered lines with the server until the connection drops.
static void run_session(const ClientConfig& cfg, const char* tag, TcpClient& client, Stats& st)
{
	char prefix[16];                      // "client2 #" - every line starts with it
	snprintf(prefix, sizeof(prefix), "client%d #", cfg.id);

	unsigned next_seq = 0, expect_seq = 0;
	int64_t next_send = now_ms();
	std::string rx;                       // bytes received, not yet a full line
	char buf[128];

	while (client.isOpen()) {
		// send
		if (now_ms() >= next_send) {
			char line[96];
			int n;
			if (next_seq == 0)
				n = snprintf(line, sizeof(line), "%s0 hello netif=%s\n", prefix, cfg.netif_key[0] ? cfg.netif_key : "any");
			else
				n = snprintf(line, sizeof(line), "%s%u\n", prefix, next_seq);
			tcp_status_t ws = client.write(line, (size_t)n, 1000);
			if (ws != tcp_status_t::Ok) {
				ESP_LOGW(tag, "write: %s (errno %d)", tcpStatusToString(ws), client.lastErrno());
				return;
			}
			next_seq++;
			st.sent++;
			next_send += CONFIG_MULTI_SEND_PERIOD_MS;
		}

		// receive
		size_t got = 0;
		tcp_status_t s = client.read(buf, sizeof(buf), got, 50);
		if (s == tcp_status_t::Timeout) continue;
		if (s != tcp_status_t::Ok) {
			ESP_LOGW(tag, "read: %s (errno %d)", tcpStatusToString(s), client.lastErrno());
			return;
		}
		rx.append(buf, got);

		// check every complete line: it must be the echo of the next expected seq
		size_t nl;
		while ((nl = rx.find('\n')) != std::string::npos) {
			std::string line = rx.substr(0, nl);
			rx.erase(0, nl + 1);
			char want[24];
			int wn = snprintf(want, sizeof(want), "%s%u", prefix, expect_seq);
			bool ok = line.compare(0, wn, want) == 0 && (line.size() == (size_t)wn || line[wn] == ' ');
			if (ok) {
				st.echoed++;
				expect_seq++;
				if (st.echoed % 10 == 0)
					ESP_LOGI(tag, "sent %u, echoed %u, mismatched %u", st.sent, st.echoed, st.mismatched);
			} else {
				st.mismatched++;
				ESP_LOGW(tag, "unexpected echo \"%s\" (expected \"%s\")", line.c_str(), want);
			}
		}
	}
}

static void client_task(void* arg)
{
	const ClientConfig& cfg = *static_cast<const ClientConfig*>(arg);
	char tag[12];
	snprintf(tag, sizeof(tag), "client%d", cfg.id);
	const char* iface = cfg.netif_key[0] ? cfg.netif_key : "any interface";

	TcpOptions opts;
	opts.netif_key = cfg.netif_key;
	Stats st;

	while (1) {
		ESP_LOGI(tag, "connecting to %s port %u via %s", cfg.host, cfg.port, iface);
		TcpClient client;
		int err = 0;
		tcp_status_t s = client.connect(cfg.host, cfg.port, CONFIG_MULTI_CONNECT_TIMEOUT_MS, opts, &err);
		if (s != tcp_status_t::Ok) {
			if (err == ENODEV)
				ESP_LOGE(tag, "interface \"%s\" does not exist (enable it in Example Connection Configuration)", cfg.netif_key);
			else
				ESP_LOGW(tag, "connect failed: %s (errno/code %d)", tcpStatusToString(s), err);
			vTaskDelay(pdMS_TO_TICKS(CONFIG_MULTI_RETRY_DELAY_MS));
			continue;
		}

		esp_netif_t* n = client.netif();
		st.connects++;
		ESP_LOGI(tag, "connected #%u: %s port %u -> %s port %u, interface %s", st.connects,
		         client.localIpString().c_str(), client.localPort(),
		         client.remoteIpString().c_str(), client.remotePort(),
		         n ? esp_netif_get_ifkey(n) : "?");

		run_session(cfg, tag, client, st);

		ESP_LOGW(tag, "disconnected (sent %u, echoed %u, mismatched %u), retrying",
		         st.sent, st.echoed, st.mismatched);
		vTaskDelay(pdMS_TO_TICKS(CONFIG_MULTI_RETRY_DELAY_MS));
	}
}

extern "C" void app_main(void)
{
	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	ESP_ERROR_CHECK(example_connect());
#if CONFIG_EXAMPLE_CONNECT_WIFI
	esp_wifi_set_ps(WIFI_PS_NONE);   // lower latency; remove on battery-powered devices
#endif

	int started = 0;
	for (const ClientConfig& c : kClients) {
		if (c.id == 0) break;
		char name[12];
		snprintf(name, sizeof(name), "client%d", c.id);
		xTaskCreate(client_task, name, 4096, (void*)&c, 5, nullptr);
		started++;
	}
	ESP_LOGI("multi", "%d client(s) started", started);
}
