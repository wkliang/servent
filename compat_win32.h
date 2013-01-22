/*
 * Copyright (c) 2004-2007 Sergey Lyubka <valenok@gmail.com>
 * All rights reserved
 *
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

/* Tip from Justin Maximilian, suppress errors from winsock2.h */
#define _WINSOCKAPI_

#include <windows.h>
#include <winsock2.h>
#include <commctrl.h>
#include <winnls.h>
#include <shlobj.h>
#include <shellapi.h>

#ifndef _WIN32_WCE

#include <process.h>
#include <direct.h>
#include <io.h>

#else /* _WIN32_WCE */

/* Windows CE-specific definitions */
#define NO_CGI	/* WinCE has no pipes */
#define NO_GUI	/* temporarily until it is fixed */
#pragma comment(lib,"ws2")
/* WinCE has both Unicode and ANSI versions of GetProcAddress */
#undef GetProcAddress
#define GetProcAddress GetProcAddressA
#include "compat_wince.h"

#endif /* _WIN32_WCE */

#define	ERRNO			GetLastError()
#define	NO_SOCKLEN_T
#define	SSL_LIB			L"ssleay32.dll"
#define	DIRSEP			'\\'
#define	IS_DIRSEP_CHAR(c)	((c) == '/' || (c) == '\\')
#define	O_NONBLOCK		0
#define	EWOULDBLOCK		WSAEWOULDBLOCK
#define	snprintf		_snprintf
#define	vsnprintf		_vsnprintf
#define	mkdir(x,y)		_mkdir(x)
#define	popen(x,y)		_popen(x, y)
#define	pclose(x)		_pclose(x)
#define	dlopen(x,y)		LoadLibraryW(x)
#define	dlsym(x,y)		(void *) GetProcAddress(x,y)
#define	_POSIX_

#ifdef __LCC__
#include <stdint.h>
#endif /* __LCC__ */

#ifdef _MSC_VER /* MinGW already has these */
typedef unsigned int		uint32_t;
typedef unsigned short		uint16_t;
typedef __int64			uint64_t;
#define S_ISDIR(x)		((x) & _S_IFDIR)
#endif /* _MSC_VER */

/* wkliang:20110525 borrow from iaxclient_lib */

#define THREAD HANDLE
#define THREADID unsigned
#define THREADCREATE(func, args, thread, id) \
(thread = (HANDLE)CreateThread(NULL, 0, func, (PVOID)args, 0, &id))
#define THREADCREATE_ERROR NULL
#define THREADFUNCDECL(func) unsigned __stdcall func(PVOID args)
#define THREADFUNCRET(r) int r = 0
#define THREADJOIN(t)
/* causes deadlock with wx GUI on MSW */
/* #define THREADJOIN(t) WaitForSingleObject(t, INFINITE) */
#define MUTEX CRITICAL_SECTION
#define MUTEXINIT(m) InitializeCriticalSection(m)
#define MUTEXLOCK(m) EnterCriticalSection(m)
#define MUTEXUNLOCK(m) LeaveCriticalSection(m)
#define MUTEXDESTROY(m) DeleteCriticalSection(m)

/*
 * POSIX dirent interface
 */
struct dirent {
	char	d_name[FILENAME_MAX];
};

typedef struct DIR {
	HANDLE			handle;
	WIN32_FIND_DATAW	info;
	struct dirent		result;
	char			*name;
} DIR;

extern DIR *opendir(const char *name);
extern int closedir(DIR *dir);
extern struct dirent *readdir(DIR *dir);
