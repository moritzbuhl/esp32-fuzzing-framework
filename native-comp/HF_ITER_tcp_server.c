/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define PORT 8888


#define STACK_DATA_SIZE 65
#define HEAP_DATA_SIZE 65

#define ESP_LOGI		LOG
#define ESP_LOGE		LOG
#define ESP_LOGW		LOG
#define LOG(tag, str, ...)	printf("[%s]" str "\n", tag, ##__VA_ARGS__)
#define vTaskDelete(_)

static const char *TAG = "example";
static void
badprintfwrap(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf(fmt, ap);
	va_end(ap);
}

static void processData(int len, char *rx_buffer) {

    if (len>=5 &&
        rx_buffer[0]=='G' &&
        rx_buffer[1]=='E' &&
        rx_buffer[2]=='T' &&
        rx_buffer[3]==' ' &&
        rx_buffer[4]=='/' ) {
	//strncmp("GET /", rx_buffer, len > 5? 5 : len)  == 0) {

        uint32_t stackData[STACK_DATA_SIZE];
        memset(stackData, 0, STACK_DATA_SIZE * sizeof(uint32_t));
        uint32_t *heapData = malloc(HEAP_DATA_SIZE * sizeof(uint32_t));

        switch (rx_buffer[5]) {
            case 's':
                for (uint8_t i = 0; i < (uint8_t) rx_buffer[6]; i++)
                    stackData[i] = 0xDEADBEEF;
                break;
            case 'h': {
                uint8_t i = (uint8_t) rx_buffer[6];
                heapData[i] = 0xDEADBEEF;
                break;
		}
            case 'n': {
                char *buff = NULL;
                badprintfwrap("%s", (const char *) buff);
                break;
		}
            case 'd':
                free(heapData);
                break;
            case 'p':
                badprintfwrap(rx_buffer);
                break;
        }
        free(heapData);
    }
}

static void do_retransmit(const int sock)
{
    int len;
    char rx_buffer[128];


    len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
    if (len < 0) {
        ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
    } else if (len == 0) {
        ESP_LOGW(TAG, "Connection closed");
    } else {
        rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
        ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);

        processData(len, rx_buffer);

        // send() can return less bytes than supplied length.
        // Walk-around for robust implementation.
        int to_write = len;
        while (to_write > 0) {
            int written = send(sock, rx_buffer + (len - to_write), to_write, 0);
            if (written < 0) {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            }
            to_write -= written;
        }
    }

}

int
main(void)
{
	for (;;) {
		size_t len;
		uint8_t *buf;

		HF_ITER(&buf, &len);

		processData(len, buf);
	}
}

#if 0
    char *addr_str; //addr_str[128];
    int addr_family;
    int ip_protocol;

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;
    /* inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1); */
    addr_str = inet_ntoa(dest_addr.sin_addr);

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return 1;
    }
    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
        uint addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, (socklen_t *)&addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Convert ip address to string
        if (source_addr.sin6_family == PF_INET) {
            /* inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1); */
            addr_str = inet_ntoa(((struct sockaddr_in *)&source_addr)->sin_addr);
        } /* else if (source_addr.sin6_family == PF_INET6) {
            inet6_ntoa(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
        } */
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        do_retransmit(sock);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}
#endif
