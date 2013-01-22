/*
 * Copyright (c) 2004-2007 Sergey Lyubka <valenok@gmail.com>
 * All rights reserved
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include <pwd.h>
#include <unistd.h>
#include <dirent.h>
#include <dlfcn.h>

#if !defined(NO_THREADS)
#include "pthread.h"
#define	_beginthread(a, b, c) do { pthread_t tid; \
	pthread_create(&tid, NULL, (void *(*)(void *))a, c); } while (0)

/* wkliang:20110525 borrow from iaxclient_lib */
#define THREAD pthread_t
#define THREADID unsigned /* unused for Posix Threads */
#define THREADCREATE(func, args, thread, id) \
pthread_create(&thread, NULL, func, args)
#define THREADCREATE_ERROR -1
#define THREADFUNCDECL(func) void * func(void *args)
#define THREADFUNCRET(r) void * r = 0
#define THREADJOIN(t) pthread_join(t, 0)
#define MUTEX pthread_mutex_t
#define MUTEXINIT(m) pthread_mutex_init(m, NULL) //TODO: check error
#define MUTEXLOCK(m) pthread_mutex_lock(m)
#define MUTEXUNLOCK(m) pthread_mutex_unlock(m)
#define MUTEXDESTROY(m) pthread_mutex_destroy(m)

#endif /* !NO_THREADS */

#define	SSL_LIB				"libssl.so"
#define	DIRSEP				'/'
#define	IS_DIRSEP_CHAR(c)		((c) == '/')
#define	O_BINARY			0
#define	closesocket(a)			close(a)
#define	ERRNO				errno

typedef unsigned char		uint8_t;
