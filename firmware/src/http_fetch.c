#include "http_fetch.h"
#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(http_fetch, LOG_LEVEL_INF);

static uint8_t  *_buf;
static size_t    _buf_size;
static size_t    _received;
static int       _status_code;
static char      _etag_buf[128];
static bool      _etag_found;

static int response_cb(struct http_response *rsp,
                       enum http_final_call final_data,
                       void *user_data)
{
	_status_code = rsp->http_status_code;

	/* On the first call that contains headers, recv_buf holds headers +
	 * start of body. body_frag_start > recv_buf means headers are present. */
	if (!_etag_found && rsp->body_frag_start > rsp->recv_buf) {
		/* Search for ETag in the header region */
		size_t header_len = rsp->body_frag_start - rsp->recv_buf;
		char *hdr = (char *)rsp->recv_buf;
		char *etag = NULL;

		/* Case-insensitive scan for "etag:" */
		for (size_t i = 0; i + 5 < header_len; i++) {
			if ((hdr[i]   == 'E' || hdr[i]   == 'e') &&
			    (hdr[i+1] == 'T' || hdr[i+1] == 't') &&
			    (hdr[i+2] == 'a' || hdr[i+2] == 'a') &&
			    (hdr[i+3] == 'g' || hdr[i+3] == 'g') &&
			     hdr[i+4] == ':') {
				etag = hdr + i + 5;
				break;
			}
		}
		if (etag) {
			while (*etag == ' ') etag++;
			size_t len = 0;
			while (etag[len] && etag[len] != '\r' && etag[len] != '\n') len++;
			if (len > 0 && len < sizeof(_etag_buf) - 1) {
				memcpy(_etag_buf, etag, len);
				_etag_buf[len] = '\0';
				_etag_found = true;
			}
		}
	}

	if (rsp->body_frag_len > 0 && _buf) {
		size_t to_copy = rsp->body_frag_len;
		if (_received + to_copy > _buf_size) {
			to_copy = _buf_size - _received;
			LOG_WRN("PNG buffer full — truncating");
		}
		memcpy(_buf + _received, rsp->body_frag_start, to_copy);
		_received += to_copy;
	}

	return 0;
}

int http_fetch_display(const char *etag_in,
                       uint8_t *buf, size_t buf_size, size_t *bytes_out,
                       char *etag_out)
{
	_buf        = buf;
	_buf_size   = buf_size;
	_received   = 0;
	_status_code = 0;
	_etag_found = false;
	memset(_etag_buf, 0, sizeof(_etag_buf));

	/* Build optional If-None-Match header array (null-terminated) */
	char if_none_match_hdr[128] = "";
	const char *opt_hdrs[2] = {NULL, NULL};

	if (etag_in && etag_in[0]) {
		snprintf(if_none_match_hdr, sizeof(if_none_match_hdr),
		         "If-None-Match: %s\r\n", etag_in);
		opt_hdrs[0] = if_none_match_hdr;
	}

	/* Resolve host */
	struct zsock_addrinfo hints = {.ai_socktype = SOCK_STREAM};
	struct zsock_addrinfo *addr;
	char port_str[8];
	snprintf(port_str, sizeof(port_str), "%d", SERVER_PORT);

	int ret = zsock_getaddrinfo(SERVER_HOST, port_str, &hints, &addr);
	if (ret) {
		LOG_ERR("DNS lookup failed: %d", ret);
		return HTTP_FETCH_ERR;
	}

	int sock = zsock_socket(addr->ai_family, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		zsock_freeaddrinfo(addr);
		return HTTP_FETCH_ERR;
	}

	ret = zsock_connect(sock, addr->ai_addr, addr->ai_addrlen);
	zsock_freeaddrinfo(addr);
	if (ret) {
		LOG_ERR("TCP connect failed: %d", ret);
		zsock_close(sock);
		return HTTP_FETCH_ERR;
	}

	static uint8_t recv_buf[4096];
	struct http_request req = {
		.method       = HTTP_GET,
		.url          = SERVER_PATH,
		.host         = SERVER_HOST,
		.protocol     = "HTTP/1.1",
		.response     = response_cb,
		.recv_buf     = recv_buf,
		.recv_buf_len = sizeof(recv_buf),
		.optional_headers = (etag_in && etag_in[0]) ? opt_hdrs : NULL,
	};

	ret = http_client_req(sock, &req, 10000, NULL);
	zsock_close(sock);

	if (ret < 0) {
		LOG_ERR("HTTP request failed: %d", ret);
		return HTTP_FETCH_ERR;
	}

	LOG_INF("HTTP %d — %zu bytes", _status_code, _received);

	if (_status_code == 304) {
		return HTTP_FETCH_NOT_MODIFIED;
	}
	if (_status_code == 200) {
		*bytes_out = _received;
		if (etag_out && _etag_found) {
			strncpy(etag_out, _etag_buf, 63);
			etag_out[63] = '\0';
		}
		return HTTP_FETCH_OK;
	}

	LOG_ERR("Unexpected HTTP status %d", _status_code);
	return HTTP_FETCH_ERR;
}
