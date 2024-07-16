#include "event2/event-config.h"

#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/buffer.h>
#include <event2/ws.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#define VERIFY(cond) do {                       \
	if (!(cond)) {                              \
		fprintf(stderr, "[error] %s\n", #cond); \
		exit(EXIT_FAILURE);                     \
	}                                           \
} while (0);                                    \

#define URL_MAX 4096

struct connect_base
{
	struct evhttp_connection *evcon;
	struct evhttp_uri *location;
};

static struct evhttp_uri* uri_parse(const char *str)
{
	struct evhttp_uri *uri;
	VERIFY(uri = evhttp_uri_parse(str));
	VERIFY(evhttp_uri_get_host(uri));
	VERIFY(evhttp_uri_get_port(uri) > 0);
	return uri;
}
static char* uri_path(struct evhttp_uri *uri, char buffer[URL_MAX])
{
	struct evhttp_uri *path;

	VERIFY(evhttp_uri_join(uri, buffer, URL_MAX));

	path = evhttp_uri_parse(buffer);
	evhttp_uri_set_scheme(path, NULL);
	evhttp_uri_set_userinfo(path, 0);
	evhttp_uri_set_host(path, NULL);
	evhttp_uri_set_port(path, -1);
	VERIFY(evhttp_uri_join(path, buffer, URL_MAX));
	return buffer;
}
static char* uri_hostport(struct evhttp_uri *uri, char buffer[URL_MAX])
{
	VERIFY(evhttp_uri_join(uri, buffer, URL_MAX));
	VERIFY(evhttp_uri_get_host(uri));
	VERIFY(evhttp_uri_get_port(uri) > 0);
	evutil_snprintf(buffer, URL_MAX, "%s:%d",
		evhttp_uri_get_host(uri), evhttp_uri_get_port(uri));
	return buffer;
}

static void get_cb(struct evhttp_request *req, void *arg)
{
	ev_ssize_t len;
	struct evbuffer *evbuf;

	VERIFY(req);

	evbuf = evhttp_request_get_input_buffer(req);
	len = evbuffer_get_length(evbuf);
	fwrite(evbuffer_pullup(evbuf, len), len, 1, stdout);
	evbuffer_drain(evbuf, len);
}

static void connect_cb(struct evhttp_request *proxy_req, void *arg)
{
	struct connect_base *base = arg;
	struct evhttp_connection *evcon = base->evcon;
	struct evhttp_uri *location = base->location;
	struct evhttp_request *req;
	char buffer[URL_MAX];

	VERIFY(proxy_req);
	VERIFY(evcon);

	req = evhttp_request_new(get_cb, NULL);
	evhttp_add_header(req->output_headers, "Connection", "close");
	evhttp_add_header(req->output_headers, "Host", evhttp_uri_get_host(location));
	VERIFY(!evhttp_make_request(evcon, req, EVHTTP_REQ_GET,
		uri_path(location, buffer)));
}

int main(int argc, const char **argv)
{
	char hostport[URL_MAX];

	struct evhttp_uri *host;

	struct event_base *base;
	struct evhttp_connection *evcon;
	struct evhttp_request *req;

	struct connect_base connect_base;

	if (argc != 2) {
		printf("Usage: %s proxy url\n", argv[0]);
		return 1;
	}
	host   = uri_parse(argv[1]);

	#ifdef _WIN32
	{
		WORD wVersionRequested;
		WSADATA wsaData;
		int err;

		wVersionRequested = MAKEWORD(2, 2);

		err = WSAStartup(wVersionRequested, &wsaData);
		if (err != 0) {
			printf("WSAStartup failed with error: %d\n", err);
			VERIFY(err);
		}
	}
#endif // _WIN32

	VERIFY(base = event_base_new());
	VERIFY(evcon = evhttp_connection_base_new(base, NULL,
		evhttp_uri_get_host(host), evhttp_uri_get_port(host)));
	connect_base.evcon = evcon;

	VERIFY(req = evhttp_request_new(connect_cb, &connect_base));

	uri_hostport(host, hostport);
	evhttp_add_header(req->output_headers, "Upgrade", "websocket");
	evhttp_add_header(req->output_headers, "Connection", "Upgrade");
	evhttp_add_header(req->output_headers, "Host", hostport);
	evhttp_make_request(evcon, req, EVHTTP_REQ_CONNECT, hostport);

	event_base_dispatch(base);

	evhttp_connection_free(evcon);
	event_base_free(base);
	evhttp_uri_free(host);

	return 0;
}
