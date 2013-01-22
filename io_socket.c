/*
 * Copyright (c) 2004-2005 Sergey Lyubka <valenok@gmail.com>
 * All rights reserved
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

#include "defs.h"

static int read_socket(struct stream *stream, void *buf, size_t len)
{
	int n;

	assert(stream->chan.sock != -1);
	// return (recv(stream->chan.sock, buf, len, 0));
	n = recv(stream->chan.sock, buf, len, 0);
	MY_DEBUG("n=%d\n", n);
	return n;
}

static int write_socket(struct stream *stream, const void *buf, size_t len)
{
	int n;

	assert(stream->chan.sock != -1);
	// return (send(stream->chan.sock, buf, len, 0));
	n = send(stream->chan.sock, buf, len, 0);
	MY_DEBUG("n=%d\n", n);
	return n;
}

static void close_socket(struct stream *stream)
{
	assert(stream->chan.sock != -1);
	(void) closesocket(stream->chan.sock);
}

const struct io_class	_shttpd_io_socket =  {
	"socket",
	read_socket,
	write_socket,
	close_socket
};
