// Runs up to three TCP echo servers, each listening on its own port and,
// optionally, on one network interface only:
//
//   server 1  port 5001  Wi-Fi station only   (netif_key "WIFI_STA_DEF")
//   server 2  port 5002  Ethernet only        (netif_key "ETH_DEF")
//   server 3  port 5003  any interface        (netif_key "")
//
// A listener bound to an interface only sees connections arriving on it; lwIP
// refuses the others. Every accepted connection gets its own task (it owns the
// TcpClient), is greeted with the interface it arrived on, and is echoed:
//
//   server3 hello 172.16.10.30 via WIFI_STA_DEF
//
// tools/tcp_listener_test.py connects to every server over every interface and
// checks who accepts what. Configure in `idf.py menuconfig`:
//   "Listener Configuration"            per server: enable, port, interface
//   "Example Connection Configuration"  Wi-Fi and Ethernet (W5500)
#include <atomic>
#include <errno.h>
#include <stdio.h>
#include <string>
#include <utility>
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
#include "server_config.h"

static std::atomic<int> s_active[4];   // open connections per server id (1..3)

// Name of the interface a connection runs on, e.g. "WIFI_STA_DEF".
static const char* iface_of(const TcpClient& c)
{
	esp_netif_t* n = c.netif();
	return n ? esp_netif_get_ifkey(n) : "unknown";
}

// One accepted connection. The session task owns it and deletes it.
struct Session {
	const ServerConfig* cfg;
	TcpClient client;
};

static void session_task(void* arg)
{
	Session* s = static_cast<Session*>(arg);
	const int id = s->cfg->id;
	char tag[12];
	snprintf(tag, sizeof(tag), "server%d", id);
	TcpClient& c = s->client;

	const std::string peer = c.remoteIpString();
	const char* iface = iface_of(c);
	ESP_LOGI(tag, "connection from %s port %u on %s (to %s port %u), %d open",
	         peer.c_str(), c.remotePort(), iface, c.localIpString().c_str(), c.localPort(),
	         s_active[id].load());

	char hello[96];
	int n = snprintf(hello, sizeof(hello), "server%d hello %s via %s\n", id, peer.c_str(), iface);
	c.write(hello, (size_t)n, 1000);

	char buf[256];
	unsigned bytes = 0;
	int64_t last_rx = esp_timer_get_time();
	while (c.isOpen()) {
		size_t got = 0;
		tcp_status_t st = c.read(buf, sizeof(buf), got, 1000);
		if (st == tcp_status_t::Ok) {
			last_rx = esp_timer_get_time();
			bytes += got;
			if (c.write(buf, got, 2000) != tcp_status_t::Ok) break;   // echo
		} else if (st == tcp_status_t::Timeout) {
			if (CONFIG_LISTENER_IDLE_TIMEOUT_S > 0 &&
			    esp_timer_get_time() - last_rx > (int64_t)CONFIG_LISTENER_IDLE_TIMEOUT_S * 1000000) {
				ESP_LOGI(tag, "%s idle for %d s, closing", peer.c_str(), CONFIG_LISTENER_IDLE_TIMEOUT_S);
				break;
			}
		} else {
			break;   // Closed or Error
		}
	}

	const int left = --s_active[id];   // one atomic step: no race with other sessions
	ESP_LOGI(tag, "%s port %u on %s closed after %u bytes, %d open",
	         peer.c_str(), c.remotePort(), iface, bytes, left);
	delete s;          // closes the socket
	vTaskDelete(nullptr);
}

static void server_task(void* arg)
{
	const ServerConfig& cfg = *static_cast<const ServerConfig*>(arg);
	char tag[12];
	snprintf(tag, sizeof(tag), "server%d", cfg.id);
	const char* where = cfg.netif_key[0] ? cfg.netif_key : "any interface";

	TcpOptions opts;
	opts.netif_key = cfg.netif_key;

	TcpListener listener;
	while (1) {
		int err = 0;
		if (listener.listen(cfg.port, CONFIG_LISTENER_MAX_CLIENTS, opts, &err) == tcp_status_t::Ok)
			break;
		if (err == ENODEV)
			ESP_LOGE(tag, "interface \"%s\" does not exist (enable it in Example Connection Configuration)", cfg.netif_key);
		else
			ESP_LOGE(tag, "listen on port %u failed, errno %d", cfg.port, err);
		vTaskDelay(pdMS_TO_TICKS(5000));
	}
	ESP_LOGI(tag, "listening on port %u, %s", listener.port(), where);

	while (1) {
		TcpClient client;
		tcp_status_t st = listener.accept(client, -1);
		if (st != tcp_status_t::Ok) {
			ESP_LOGW(tag, "accept: %s (errno %d)", tcpStatusToString(st), listener.lastErrno());
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}

		if (s_active[cfg.id] >= CONFIG_LISTENER_MAX_CLIENTS) {
			char busy[48];
			int n = snprintf(busy, sizeof(busy), "server%d busy\n", cfg.id);
			client.write(busy, (size_t)n, 500);
			ESP_LOGW(tag, "%s refused: %d connections already open", client.remoteIpString().c_str(), CONFIG_LISTENER_MAX_CLIENTS);
			continue;   // client goes out of scope: closed
		}

		Session* s = new Session{&cfg, std::move(client)};
		s_active[cfg.id]++;   // counted here, before the session task starts and logs it
		char name[16];
		snprintf(name, sizeof(name), "s%d_conn", cfg.id);
		if (xTaskCreate(session_task, name, 4096, s, 5, nullptr) != pdPASS) {
			ESP_LOGE(tag, "no memory for a session task");
			s_active[cfg.id]--;
			delete s;
		}
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

	for (const ServerConfig& s : kServers) {
		if (s.id == 0) break;
		char name[12];
		snprintf(name, sizeof(name), "server%d", s.id);
		xTaskCreate(server_task, name, 4096, (void*)&s, 5, nullptr);
	}
}
