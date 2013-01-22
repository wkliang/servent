

# wkliang:20090512 - md5 had defined in iaxclient
# SRCS=		string.c shttpd.c log.c auth.c md5.c cgi.c io_ssi.c
SRCS=		string.c shttpd.c log.c auth.c cgi.c io_ssi.c \
		io_file.c io_socket.c io_ssl.c io_emb.c io_dir.c io_cgi.c \
		json.c
HDRS=		defs.h llist.h shttpd.h std_includes.h io.h ssl.h \
		compat_unix.h compat_win32.h compat_rtems.h config.h \
		json.h
		
# OBJS=		$(SRCS:%.c=%.o) compat_unix.c

# SRCS:= $(SRCS) ami.c
SRCS:= $(SRCS) user.c
SRCS:= $(SRCS) compat_unix.c
# SRCS:=	$(SRCS) compat_win32.c
# OBJS=	$(SRCS:.c=.o) 
OBJS=	$(SRCS:%.c=%.o) 
OBJS:= $(OBJS) tcutil.o tchdb.o tcbdb.o tcfdb.o tctdb.o tcadb.o myconf.o md5.o

PROG=		shttpd
CL_FLAGS=	/MD /TC /nologo /DNDEBUG /Os 	# MSVC compiler flags

# Possible flags: (in brackets are rough numbers for 'gcc -O2' on i386)
# -DHAVE_MD5		- use system md5 library (-2kb)
# -DNDEBUG		- strip off all debug code (-5kb)
# -D_DEBUG		- build debug version (very noisy) (+6kb)
# -DNO_CGI		- disable CGI support (-5kb)
# -DNO_SSL		- disable SSL functionality (-2kb)
# -DNO_AUTH		- disable authorization support (-4kb)
# -DCONFIG=\"file\"	- use `file' as the default config file
# -DNO_SSI		- disable SSI support (-4kb)
# -DNO_THREADS		- disable threads support (?)

# XXX Note for the windows users. In order to build shttpd, MSVS6 is needed.
# Follow these steps:
# 1. Add c:\path_to_msvs6\bin to the system Path environment variable.
# 2. Add two new system environment variables:
#    LIB=c:\path_to_msvs6\lib
#    INCLUDE=c:\path_to_msvs6\include
# 3. start console, go to shttpd-VERSION\src\ directory
# 4. type "nmake msvc"
# 5. go to shttpd-VERSION\examples , type "nmake msvc"

TOKYOCABINET=./tokyocabinet-1.4.48

TCCFLAGS=-DNDEBUG -D__EXTENSIONS__ -D_MYNOZLIB -D_MYNOBZIP

# CFLAGS=-Wall -Wwrite-strings -Werror -std=gnu99 -D_GNU_SOURCE -D_REENTRANT -g -O2
CFLAGS=-Werror -std=gnu99 -g -O2 -fPIC -fsigned-char -pipe -I$(TOKYOCABINET) -D_GNU_SOURCE=1 -D_REENTRANT 

CC=gcc
# RC=windres
# LDFLAGS=-mno-cygwin -Wl,-subsystem,windows

# CC=i386-mingw32-gcc
# RC=i386-mingw32-windres

# CC=i686-pc-mingw32-gcc
# RC=i686-pc-mingw32-windres

# RCVARS=-O coff
# LDFLAGS=-mwindows

# CC=winegcc
# RC=wrc

LIBS=-lshttpd

unix: lib$(PROG).a
	$(CC) $(CFLAGS) unixmain.c -o $(PROG) -L. $(LIBS) -lpthread -lm -lcrypto -lssl

unix-old: lib$(PROG).a blog.o
	$(CC) $(CFLAGS) blog.o unixmain.c -o $(PROG) -L. -L/home/wkliang/lib $(LIBS) -l$(PROG) -ldl -lpthread -lm -lcrypto -lssl -lz -lbz2

all:

	@echo "make (unix|msvc|mingw|rtems)"
	@echo 'Linux: "LIBS=-ldl make unix"'
	@echo 'BSD: "LIBS=-lpthread make unix"'
	@echo 'Solaris: "LIBS="-lpthread -lnsl -lsocket" make unix"'

# .c.o:
# 	$(CC) -c $(CFLAGS) $< -o $@

%.o: %.c
	$(CC) -c $(CFLAGS) $*.c

lib$(PROG).a: $(OBJS)
	$(AR) -r lib$(PROG).a $(OBJS) 2>&1
	ranlib lib$(PROG).a 2>&1

# unix: lib$(PROG).a blog.o
# 	$(CC) $(CFLAGS) blog.o unixmain.c -o $(PROG) $(LIBS) -L. -l$(PROG) -ldl -lpthread -lm -lcrypto -lssl

# unix: lib$(PROG).a
#	$(CC) $(CFLAGS) -fpic $(SRCS) compat_unix.c -shared -o lib$(PROG).so

# blog.o: blog.c
# 	$(CC) -c $(CFLAGS) $(TCCFLAGS) -I$(TOKYOCABINET)  $<

tcutil.o: $(TOKYOCABINET)/tcutil.c
	$(CC) -c $(CFLAGS) $(TCCFLAGS) -I$(TOKYOCABINET)  $<

tchdb.o: $(TOKYOCABINET)/tchdb.c
	$(CC) -c $(CFLAGS) $(TCCFLAGS) -I$(TOKYOCABINET)  $<

tcbdb.o: $(TOKYOCABINET)/tcbdb.c
	$(CC) -c $(CFLAGS) $(TCCFLAGS) -I$(TOKYOCABINET)  $<

tcfdb.o: $(TOKYOCABINET)/tcfdb.c
	$(CC) -c $(CFLAGS) $(TCCFLAGS) -I$(TOKYOCABINET)  $<

tctdb.o: $(TOKYOCABINET)/tctdb.c
	$(CC) -c $(CFLAGS) $(TCCFLAGS) -I$(TOKYOCABINET)  $<

tcadb.o: $(TOKYOCABINET)/tcadb.c
	$(CC) -c $(CFLAGS) $(TCCFLAGS) -I$(TOKYOCABINET)  $<

myconf.o: $(TOKYOCABINET)/myconf.c
	$(CC) -c $(CFLAGS) $(TCCFLAGS) -I$(TOKYOCABINET)  $<

md5.o:$(TOKYOCABINET)/md5.c
	$(CC) -c $(CFLAGS) $(TCCFLAGS) -I$(TOKYOCABINET)  $<

rtems:
	$(CC) -c $(CFLAGS) $(SRCS) compat_rtems.c
	$(AR) -r lib$(PROG).a *.o && ranlib lib$(PROG).a 

$(PROG).lib: $(SRCS) $(HDRS) compat_win32.c
	del *.obj
	cl /c $(CL_FLAGS) compat_win32.c $(SRCS)
	lib /nologo *.obj /out:$@

msvc:	$(PROG).lib
	cl $(CL_FLAGS) unixmain.c /link /out:$(PROG).exe \
	ws2_32.lib user32.lib advapi32.lib shell32.lib $(PROG).lib

mingw: dialog-rc.o $(OBJS)
	$(CC) -c $(CFLAGS) $(SRCS) compat_win32.c
	$(AR) -r lib$(PROG).a *.o && ranlib lib$(PROG).a 
	$(CC) $(CFLAGS) $(SRCS) compat_win32.c unixmain.c \
		-o $(PROG) $(LIBS) -lws2_32 -lcomdlg32 -lcomctl32

# dialog-rc.o : dialog.rc
# 	$(RC) -i dialog.rc -o $@
# 
# shttpd.exe: dialog-rc.o $(OBJS)
# 	$(CC) $(CFLAGS) example_win32_dialog_app.c dialog-rc.o $(OBJS) \
# 		-o $@ $(LIBS) -lws2_32 -lcomdlg32 -lcomctl32
#
# mdi02.o : resource.h mdi02.c
# 	$(CC) $(CFLAGS) -c mdi02.c -o $@
# mdi02.exe : mdi02.o mdi02-rc.o
# 	$(CC) $(LDFLAGS) -mwindows -s $^ -o $@ -lgdi32 -lcomctl32 -lcomdlg32 -lshlwapi -lwinmm -lwsock32

resource.o : resource.h resource.rc
	$(RC) -i resource.rc -o $@

winmain.o: resource.h winmain.c

servent.exe: $(OBJS) winmain.o resource.o
	$(CC) $(LDFLAGS) -mwindows -s $^ -o $@ -lgdi32 -lcomctl32 -lcomdlg32 -lshlwapi -lwinmm -lwsock32

# example.mak

../src/libshttpd.a:
	cd ../src && $(MAKE) unix

ex-unix: ../src/libshttpd.a
	$(CC) $(CFLAGS) user.c -I ../src  ../src/libshttpd.a -o user

WIN32_LIBS= user32.lib shell32.lib ws2_32.lib advapi32.lib ..\src\shttpd.lib
CL_FLAGS= /MD /DNDEBUG /nologo /Zi /I ..\src

ex-msvc:
	cl $(CL_FLAGS) user.c $(WIN32_LIBS)
	rc dialog.rc
	cl $(CL_FLAGS) example_win32_dialog_app.c dialog.res $(WIN32_LIBS)

# plurc: plurc.c json.o
# 	$(CC) $(CFLAGS) -I. plurc.c json.o -o plurc -lssl -lcrypto

test1: json.o test1.c
	$(CC) $(CFLAGS) -I. json.o test1.c -o test1

test2: json.o test2.c
	$(CC) $(CFLAGS) -I. json.o test2.c -o test2

test3: json.o test3.c
	$(CC) $(CFLAGS) -I. json.o test3.c -o test3

# example.mak

man:
	cat shttpd.1 | tbl | groff -man -Tascii | col -b > shttpd.1.txt
	cat shttpd.1 | tbl | groff -man -Tascii | less

.PHONY: clean
clean:
	rm -rvf a.out* *.o *.core $(PROG) lib$(PROG).a *.lib *.obj *.exe

