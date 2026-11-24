#include "console.h"
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char* TAG = "console";

struct Line { char text[128]; };
static QueueHandle_t s_lines;
static volatile bool s_connected;

// Removes control characters (Ctrl key presses, Esc sequences, CR/LF...) in place.
// Bytes >= 0x80 stay, so UTF-8 text such as Turkish letters is kept.
static void strip_control_chars(char* s)
{
	char* out = s;
	for (const char* in = s; *in; in++) {
		unsigned char c = (unsigned char)*in;
		if (c >= 0x20 && c != 0x7f)
			*out++ = (char)c;
	}
	*out = '\0';
}

static void console_task(void*)
{
#if CONFIG_ESP_CONSOLE_UART
	// Blocking stdin reads need the UART driver; the default console is output-only.
	ESP_ERROR_CHECK(uart_driver_install((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, nullptr, 0));
	uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
	setvbuf(stdin, nullptr, _IONBF, 0);
#endif
	Line line;
	while (1) {
		if (!fgets(line.text, sizeof(line.text), stdin)) {
			vTaskDelay(pdMS_TO_TICKS(100));
			continue;
		}
		strip_control_chars(line.text);
		if (line.text[0] == '\0') continue;
		if (!s_connected)
			ESP_LOGW(TAG, "not connected, \"%s\" will be sent after connecting", line.text);
		if (xQueueSend(s_lines, &line, 0) != pdTRUE)
			ESP_LOGW(TAG, "queue full, \"%s\" dropped", line.text);
	}
}

void console_start()
{
	s_lines = xQueueCreate(8, sizeof(Line));
	xTaskCreate(console_task, "console", 3072, nullptr, 5, nullptr);
}

void console_set_connected(bool connected)
{
	s_connected = connected;
}

bool console_send_queued(TcpClient& client)
{
	Line line;
	while (xQueueReceive(s_lines, &line, 0) == pdTRUE) {
		size_t len = strlen(line.text);
//		line.text[len++] = '\n';                   // fits: fgets left room for '\0'
		if (client.write(line.text, len, 1000) != tcp_status_t::Ok) return false;
//		line.text[len - 1] = '\0';
		ESP_LOGI(TAG, "sent: %s", line.text);
	}
	return true;
}
