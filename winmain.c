/*
	winmain.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#define WINVER 0x0500
#define _WIN32_WINDOWS	WINVER
#define _WIN32_WINNT	WINVER
#define _WIN32_IE	WINVER
#include <windows.h>
#include <windowsx.h>
#include <winsock.h>

#include <commctrl.h>
#include <shlwapi.h>

#include "ms_icmp.h"
#include "shttpd.h"
#include "resource.h"

// global variable
static HMENU hMenuInit, hMenuHello, hMenuWindow;
static HMENU hMenuInitWindow, hMenuHelloWindow, hMenuWindowWindow;

static char szAppTitle[MAX_PATH];
static int iShowMode;
static int iMainWinCount = 0;
static HINSTANCE hInst;
static HICON hIcon;
static HANDLE hSem;
static HWND hMainWnd, hLogWnd;
static WSADATA wsaData;

#define SVR_PORT	8514	// shell(tcp) and syslog(udp)
#define WM_SOCKET   (WM_USER + 1000)
typedef INT SOCKERR;
typedef WORD SOCKEVENT;

SOCKERR ResetSocket( SOCKET sock )
{
	LINGER linger;

	if( sock == INVALID_SOCKET )
		return 0;

	linger.l_onoff = TRUE;
	linger.l_linger = 0;
	setsockopt( sock, SOL_SOCKET, SO_LINGER, (char *)&linger, sizeof(linger) );

	return closesocket( sock );
}

SOCKERR CreateSocket( SOCKET *pSock, int type, DWORD addr, WORD port )
{
	SOCKADDR_IN sAddr;
	SOCKET sNew;
	SOCKERR sErr;

	if( ( sNew = socket(PF_INET, type, 0) ) == INVALID_SOCKET ) {
		sErr = WSAGetLastError();
		goto error;
	}

	sAddr.sin_family = AF_INET;
	sAddr.sin_addr.s_addr = addr;
	sAddr.sin_port = port;
	if( bind(sNew, (SOCKADDR *)&sAddr, sizeof(sAddr)) != 0 ) {
		sErr = WSAGetLastError();
		goto error;
	}

	*pSock = sNew;
	return 0;

error :
	*pSock = INVALID_SOCKET;
	ResetSocket( sNew );
	return sErr;
}

typedef struct _tagLogData {
	struct _tagLogData *next;	// *prev;
	DWORD tick;
	int len;
	char buf[1];
} LogData;

typedef struct {
	WPARAM fwSizeType;
	int cxChar, cxCaps, cyChar, cxClient, cyClient;
	int iMaxWidth, iVscrollPos, iVscrollMax, iHscrollPos, iHscrollMax;

	int logNum;
	int logLongest;
	LogData* logFirst;
	LogData* logLast;

	SOCKET mainSock;
	SOCKET udpSock;
} MainWinData;

void LogDestroy( HWND hwnd )
{
	MainWinData *pData;
	LogData *curr, *prev;

	WaitForSingleObject( hSem, INFINITE );
	if( ( pData = (MainWinData*)GetWindowLong( hwnd, 0 ) ) == NULL ) {
		ReleaseSemaphore( hSem, 1, NULL );
		return ;
	}

	for( ; pData->logNum > 0; pData->logNum-- ) {
		prev = NULL;
		curr = pData->logFirst;

		while( ( curr != NULL ) && ( curr->next != NULL ) ) {
			prev = curr;
			curr = curr->next;
		}
		if( curr != NULL ) {
			HeapFree(GetProcessHeap(), 0, curr);
		}
		if( prev != NULL ) {
			prev->next = NULL;
		}
		else {
			break;  // at top, break the loop
		}
	}
	pData->logFirst =
	pData->logLast = NULL;
	pData->logNum =
	pData->logLongest = 0;
	ReleaseSemaphore(hSem, 1, NULL);
	return ;
}

void LogAppend(HWND hwnd, char* pszFormat, ...)
{
	MainWinData *pData;
	LogData *curr;
	va_list ArgList;
	int len;
	char buf[4096];

	WaitForSingleObject( hSem, INFINITE );
	if( ( pData = (MainWinData*)GetWindowLong( hwnd, 0 ) ) == NULL ) {
		ReleaseSemaphore( hSem, 1, NULL );
		return ;
	}

	va_start( ArgList, pszFormat );
	len = wvnsprintf( buf, sizeof(buf), pszFormat, ArgList );
	va_end( ArgList );

	curr = (LogData *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
			sizeof(LogData)+len+1);
	curr->len = len;
	strcpy(curr->buf, buf);
	curr->next = NULL;
	curr->tick = GetTickCount();

	if( pData->logFirst == NULL )
		pData->logFirst = curr;
	if( pData->logLast == NULL )
		pData->logLast = curr;
	else {
		pData->logLast->next = curr;
		pData->logLast = curr;
	}
	if( pData->logLongest < curr->len )
		pData->logLongest = curr->len;
	pData->logNum++;

	ReleaseSemaphore( hSem, 1, NULL );
	return ;
}

void LogUpdate( HWND hwnd )
{
	MainWinData *pData;
	RECT rect;
	int max, pos;

	WaitForSingleObject( hSem, INFINITE );
	if( ( pData = (MainWinData*)GetWindowLong( hwnd, 0 ) ) == NULL ) {
		ReleaseSemaphore( hSem, 1, NULL );
		return ;
	}

	max = pData->iVscrollMax;
	pos = pData->iVscrollPos;

	SendMessage( hwnd, WM_SIZE,
		pData->fwSizeType,
		MAKELONG( pData->cxClient, pData->cyClient ) );

    // SendMessage( hwnd, WM_VSCROLL,
    //     MAKELONG( SB_BOTTOM, 0 ),
    //     0 );

    // UpdateWindow( hwnd );
    // if( pData->logNum < pData->cyClient / pData->cyChar )
    //     InvalidateRect( hwnd, NULL, TRUE );


	pData->iVscrollPos = pData->iVscrollMax;
	if( pData->logNum < pData->cyClient / pData->cyChar ) {
        /*
        rect.top = 0;
        rect.left = 0;
        rect.bottom = pData->cyClient;
        rect.right = pData->cxClient;
        */
		InvalidateRect( hwnd, NULL, TRUE );
	}
	else {
        SetScrollPos( hwnd, SB_VERT, pData->iVscrollPos, TRUE );

        rect.top = 0;
        rect.left = 0;
        rect.bottom = pData->cyClient - ( pData->cyClient % pData->cyChar );
        rect.right = pData->cxClient;
        ScrollWindow( hwnd, 0, (pos - pData->iVscrollPos) * pData->cyChar,
            &rect, NULL );

        rect.top = rect.bottom;
        rect.left = 0;
        rect.bottom = pData->cyClient;
        rect.right = pData->cxClient;
        InvalidateRect( hwnd, &rect, TRUE );
	}
	ReleaseSemaphore( hSem, 1, NULL );
	return ;
}

BOOL WinOnCreate( HWND hwnd, CREATESTRUCT *pCS )
{
	HDC hdc;
	TEXTMETRIC tm;
	MainWinData *pData;
	SOCKET sock, usock;

	CreateSocket( &sock, SOCK_STREAM, htonl(INADDR_ANY), htons(SVR_PORT) );
	listen( sock, 5 );
	WSAAsyncSelect( sock, hwnd, WM_SOCKET, FD_ACCEPT );

	CreateSocket( &usock, SOCK_DGRAM, htonl(INADDR_ANY), htons(SVR_PORT) );
	WSAAsyncSelect( usock, hwnd, WM_SOCKET, FD_READ );

	pData = (MainWinData*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MainWinData) );
            
	hdc = GetDC( hwnd );

	GetTextMetrics( hdc, &tm );
	pData->cxChar = tm.tmAveCharWidth;
	pData->cxCaps = ( (tm.tmPitchAndFamily & 1) ? 3 : 2 ) * pData->cxChar / 2;
	pData->cyChar = tm.tmHeight + tm.tmExternalLeading;
	// pData->iMaxWidth = 40 * pData->cxChar + 22 * pData->cxCaps + 16 * pData->cxCaps;
	pData->iMaxWidth = 10 * pData->cxChar;

	ReleaseDC( hwnd, hdc );

	pData->logNum = 0;
	pData->logLongest = 10;
	pData->logFirst = NULL;

	pData->mainSock = sock;
	pData->udpSock = usock;

	SetWindowLong( hwnd, 0, (long)pData );

	LogAppend( hwnd, "listen on %d", SVR_PORT );
	LogUpdate( hwnd );
	// hMainWnd = hwnd;
	return TRUE;
}

void WinOnSize( HWND hwnd, UINT state, int cx, int cy )
{
    MainWinData *pData = (MainWinData*)GetWindowLong( hwnd, 0 );
    SCROLLINFO si;

    pData->fwSizeType = state;
    pData->cxClient = cx;
    pData->cyClient = cy;

    pData->iVscrollMax = max( 0, pData->logNum - pData->cyClient / pData->cyChar );
    pData->iVscrollPos = min( pData->iVscrollPos, pData->iVscrollMax );

    // SetScrollRange( hwnd, SB_VERT, 0, iVscrollMax, FALSE );
    // SetScrollPos( hwnd, SB_VERT, iVscrollPos, TRUE );
    // try to use SetScrollInfo instead
    si.cbSize = sizeof( si );
    si.fMask = SIF_ALL;
    si.nMin = 0;
    si.nMax = pData->logNum - ( pData->cyClient%pData->cyChar ? 0 : 1 );
    si.nPage = pData->cyClient/pData->cyChar + ( pData->cyClient%pData->cyChar ? 1 : 0 );
    si.nPos = pData->iVscrollPos;
    SetScrollInfo( hwnd, SB_VERT, &si, TRUE );

    // iHscrollMax = max( 0, (iMaxWidth - cxClient) / cxChar );
    // iHscrollPos = min( iHscrollPos, iHscrollMax );
    // SetScrollRange( hwnd, SB_HORZ, 0, iHscrollMax, FALSE );
    // SetScrollPos( hwnd, SB_HORZ, iHscrollPos, TRUE );

    // try to use SetScrollInfo instead
    pData->iMaxWidth = pData->logLongest * pData->cxChar;
    pData->iHscrollMax = max( 0, pData->iMaxWidth - pData->cxClient );
    pData->iHscrollPos = min( pData->iHscrollPos, pData->iHscrollMax );

    si.cbSize = sizeof( si );
    si.fMask = SIF_ALL;
    si.nMin = 0;
    si.nMax = pData->iMaxWidth - 1;
    si.nPage = pData->cxClient;
    si.nPos = pData->iHscrollPos;
    SetScrollInfo( hwnd, SB_HORZ, &si, TRUE );

    return ;
}

void WinOnVScroll( HWND hwnd, HWND hwndCtl, UINT code, int pos )
{
    MainWinData *pData = (MainWinData*)GetWindowLong( hwnd, 0 );
    int iVscrollInc;
    // RECT rect;

    switch( code ) {
        case SB_TOP :
            iVscrollInc = -pData->iVscrollPos;
            break;
        case SB_BOTTOM :
            iVscrollInc = pData->iVscrollMax - pData->iVscrollPos;
            break;
        case SB_LINEUP :
            iVscrollInc = -1;
            break;
        case SB_LINEDOWN :
            iVscrollInc = 1;
            break;
        case SB_PAGEUP :
            iVscrollInc = min( -1, -pData->cyClient / pData->cyChar );
            break;
        case SB_PAGEDOWN :
            iVscrollInc = max( 1, pData->cyClient / pData->cyChar );
            break;
        case SB_THUMBTRACK :
            // iVscrollInc = HIWORD(wParam) - pData->iVscrollPos;
            iVscrollInc = pos - pData->iVscrollPos;
            break;
        default :
            iVscrollInc = 0;
            break;
    }
    iVscrollInc =
        max( -pData->iVscrollPos,
        min( iVscrollInc, pData->iVscrollMax - pData->iVscrollPos ) );
    if( iVscrollInc != 0 ) {
        pData->iVscrollPos += iVscrollInc;
        /*
        rect.top = 0;
        rect.left = 0;
        rect.bottom = pData->cyClient - ( pData->cyClient % pData->cyChar );
        rect.right = pData->cxClient;
        */
        ScrollWindow( hwnd, 0, -pData->cyChar * iVscrollInc, NULL, NULL );

        SetScrollPos( hwnd, SB_VERT, pData->iVscrollPos, TRUE );
        // InvalidateRect( hwnd, NULL, TRUE );
        UpdateWindow( hwnd );
    }
    return ;
}

void WinOnHScroll( HWND hwnd, HWND hwndCtl, UINT code, int pos )
{
    MainWinData *pData = (MainWinData*)GetWindowLong( hwnd, 0 );
    int iHscrollInc;

    switch( code ) {
        case SB_LINEUP :
            iHscrollInc = -1;
            break;
        case SB_LINEDOWN :
            iHscrollInc = 1;
            break;
        case SB_PAGEUP :
            iHscrollInc = -1 * pData->cxCaps;
            break;
        case SB_PAGEDOWN :
            iHscrollInc = 1 * pData->cxCaps;
            break;
        case SB_THUMBTRACK :
        // case SB_THUMBPOSITION :
            // iHscrollInc = HIWORD(wParam) - pData->iHscrollPos;
            iHscrollInc = pos - pData->iHscrollPos;
            break;
        default :
            iHscrollInc = 0;
            break;
    }
    iHscrollInc =
        max( -pData->iHscrollPos,
        min( iHscrollInc, pData->iHscrollMax - pData->iHscrollPos ) );
    if( iHscrollInc != 0 ) {
        pData->iHscrollPos += iHscrollInc;
        ScrollWindow( hwnd, -iHscrollInc, 0, NULL, NULL );
        SetScrollPos( hwnd, SB_HORZ, pData->iHscrollPos, TRUE );
        UpdateWindow( hwnd );
    }
    return ;
}

void WinOnPaint( HWND hwnd )
{
    MainWinData *pData = (MainWinData*)GetWindowLong( hwnd, 0 );
    HDC hdc;
    PAINTSTRUCT ps;
    int iPaintBeg, iPaintEnd, i, x, y;
    LogData* curr;

    hdc = BeginPaint( hwnd, &ps );

    iPaintBeg = max( 0, pData->iVscrollPos + ps.rcPaint.top / pData->cyChar );
    iPaintEnd = min( pData->logNum, pData->iVscrollPos + ps.rcPaint.bottom / pData->cyChar + 1 );

    for( i = 0, curr = pData->logFirst;
        ( i < iPaintBeg ) && ( curr != NULL );
        i++, curr = curr->next ) {
        ;
    }
    for( ;
        i < iPaintEnd && curr != NULL;
        i++, curr = curr->next ) {
	char szBuffer[ 32 ];
        x = -pData->iHscrollPos;
        y = pData->cyChar * ( i - pData->iVscrollPos );

	sprintf( szBuffer, "%8lu", curr->tick );
        TextOut( hdc, x, y, szBuffer, 8 );
	x += 100;
        TextOut( hdc, x, y, curr->buf, curr->len );
    }
    EndPaint( hwnd, &ps );
    return ;
}

void WinOnDestroy( HWND hwnd )
{
    MainWinData *pData = (MainWinData*)GetWindowLong( hwnd, 0 );

    LogDestroy( hwnd );
    HeapFree( GetProcessHeap(), 0, pData );
    // DestroyWindow( hwnd );
    iMainWinCount--;
    if( iMainWinCount <= 0 )
        PostQuitMessage( 0 );
    return ;
}

#define HOSTSIZE    256

typedef struct {
    HWND    hwnd;
    char    host[ HOSTSIZE ];
} TraceRouteType;

TraceRouteType traceRouteData = { 0, "" };

DWORD TraceRouteThread( LPVOID param )
{
    HANDLE hIcmp;
    IPINFO ipInfo;
    ICMPECHO icmpEcho;
    struct hostent *pHostEnt;
    DWORD dstAddr;
    TraceRouteType *pTrData = (TraceRouteType *)param;
    int i;

    // LogAppend( pTrData->hwnd, "B4 pIcmpCreateFile()" );
    // LogUpdate( pTrData->hwnd );
    // Sleep( 12000 );

    dstAddr = INADDR_NONE;
    pHostEnt = gethostbyname( pTrData->host );
    if( pHostEnt != NULL )
        dstAddr = *((DWORD *)pHostEnt->h_addr);
    else
        dstAddr = inet_addr( pTrData->host );
    if( dstAddr == INADDR_NONE ) {
        LogAppend( pTrData->hwnd, "FAIL to get host %s", pTrData->host );
        LogUpdate( pTrData->hwnd );
        HeapFree( GetProcessHeap(), 0, pTrData );
        return 0;
    }
    memset( &ipInfo, 0, sizeof(ipInfo) );

    LogAppend( pTrData->hwnd, "pIcmpSendEcho : %s, %s",
        pTrData->host,
        inet_ntoa( *((struct in_addr*)&dstAddr) ) );
    LogUpdate( pTrData->hwnd );

    hIcmp = pIcmpCreateFile();    
    for( i = 1; i <= 64; i++ ) {

        ipInfo.Ttl = (u_char)i;
        memset( &icmpEcho, 0, sizeof(icmpEcho) );

        pIcmpSendEcho( hIcmp, dstAddr, 0, 0,
            &ipInfo, &icmpEcho, sizeof(icmpEcho), 16000 );

        pHostEnt = gethostbyaddr( (char*)&icmpEcho.Address, 4, PF_INET );
        LogAppend( pTrData->hwnd, "%s:%2d: %8lu ms,  %s(%s)",
            pTrData->host, i, icmpEcho.RTTime,
            ( pHostEnt == NULL ) ? "???" : pHostEnt->h_name,
            inet_ntoa( *((struct in_addr*)&icmpEcho.Address) ) );
        LogUpdate( pTrData->hwnd );

        if( icmpEcho.Options.Ttl != 0 )
            break;
    }
    pIcmpCloseHandle( hIcmp );
    LogAppend( pTrData->hwnd, "pIcmpCloseHandle : %s, %s, i=%2d",
        pTrData->host, inet_ntoa( *((struct in_addr*)&dstAddr) ), i );
    LogUpdate( pTrData->hwnd );
    HeapFree( GetProcessHeap(), 0, pTrData );
    return 0;
}

BOOL CALLBACK TraceRouteDialog( HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam )
{
    switch( iMsg ) {
        case WM_INITDIALOG :
            SetDlgItemText( hwnd, IDD_HOST_TEXT, traceRouteData.host );
            SetFocus( GetDlgItem( hwnd, IDD_HOST_TEXT ) );
            break;
        case WM_COMMAND :
            switch( LOWORD( wParam ) ) {
                case IDCANCEL :
                    EndDialog( hwnd, 0 );
                    return TRUE;
                case IDOK :
                    GetDlgItemText( hwnd, IDD_HOST_TEXT,
                        traceRouteData.host, HOSTSIZE-1 );
                    EndDialog( hwnd, 1 );

            }
            break;
    }
    return FALSE;
}

void TraceRouteSetup( HWND hwnd )
{
    DWORD threadId;
    TraceRouteType *pTrData;

    if( DialogBox( hInst, "HostDialog", hwnd, TraceRouteDialog ) == FALSE )
        return ;

    traceRouteData.hwnd = hwnd;

    pTrData = (TraceRouteType *)HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TraceRouteType) );
    memcpy( pTrData, &traceRouteData, sizeof(TraceRouteType) );

    CreateThread( NULL, 0,
        (LPTHREAD_START_ROUTINE)TraceRouteThread, (LPVOID)pTrData,
        0, &threadId );

    return ;
}

LRESULT WinOnSocket( HWND hwnd, SOCKET sock, SOCKERR sErr, SOCKEVENT sEvent )
{
    SOCKET newSock;
    MainWinData *pData = (MainWinData*)GetWindowLong( hwnd, 0 );

    if( sErr != 0 ) {
        return -1;
    }
    switch( sEvent ) {
        case FD_CLOSE :
            if( pData->mainSock != sock ) {
                LogAppend( hwnd, "close socket %d", sock );
                closesocket( sock );
                break;
            }
            if( pData->mainSock != INVALID_SOCKET ) {
                LogAppend( hwnd, "close main socket %d", sock );
                closesocket( pData->mainSock );
                pData->mainSock = INVALID_SOCKET;
            }
            break;
        case FD_ACCEPT : {
            SOCKADDR_IN addr;
            INT cbAddr;

            cbAddr = sizeof( addr );
            newSock = accept( sock, (SOCKADDR *)&addr, &cbAddr );
            // if( newSock == INVALID_SOCKET )
            LogAppend( hwnd, "accept socket %d from %s %u",
                newSock,
                inet_ntoa( *((struct in_addr *)&addr.sin_addr.s_addr) ),
                ntohs( addr.sin_port ) );

            WSAAsyncSelect( newSock, hwnd, WM_SOCKET, FD_READ | FD_CLOSE );
            break;
        }
        case FD_READ : {
		struct sockaddr_in addr;
		int rc, siz=sizeof(addr);
		char buf[ BUFSIZ ];
#if 0
		rc = recv( sock, buf, sizeof(buf), 0 );
		if( rc < 0 ) {
			LogAppend( hwnd, "sock=%d, rc=%d.", sock, rc );
			if( sock != pData->udpSock )
				closesocket( sock );	// socket error or eof meet
		}
		else {
			buf[ rc ] = '\0';
			LogAppend( hwnd, "sock=%d, buf=%s.", sock, buf );
		}
#else
		if( sock != pData->udpSock )
			rc = recv( sock, buf, sizeof(buf), 0 );
		else
			rc = recvfrom( sock, buf, sizeof(buf), 0, (struct sockaddr*)&addr, &siz );

		if( rc >= 0 )
			buf[ rc ] = '\0';

		if( sock != pData->udpSock ) {
            		if( rc >= 0 )
				LogAppend( hwnd, "sock=%d, buf=%s.", sock, buf );
			else
				closesocket( sock );	// socket error or eof meet
		} else {
			if( rc >= 0 )
				LogAppend( hwnd, "udp %s:%u:%s.",
				inet_ntoa(((struct sockaddr_in*)&addr)->sin_addr),
				ntohs(((struct sockaddr_in*)&addr)->sin_port),
				buf );
			else
				LogAppend( hwnd, "udp %s:%u: rc=%d",
				inet_ntoa(((struct sockaddr_in*)&addr)->sin_addr),
				ntohs(((struct sockaddr_in*)&addr)->sin_port),
				rc );
		}
#endif
        }
        case FD_WRITE :
        default :
            break ;
    }
    LogUpdate( hwnd );
    return 0;
}

const char appLog[] = TEXT("appLog");
LRESULT CALLBACK appLogProc( HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam )
{
    switch( iMsg ) {
        HANDLE_MSG( hwnd, WM_CREATE,    WinOnCreate );
        HANDLE_MSG( hwnd, WM_SIZE,      WinOnSize );
        HANDLE_MSG( hwnd, WM_VSCROLL,   WinOnVScroll );
        HANDLE_MSG( hwnd, WM_HSCROLL,   WinOnHScroll );
        HANDLE_MSG( hwnd, WM_PAINT,     WinOnPaint );
        HANDLE_MSG( hwnd, WM_DESTROY,   WinOnDestroy );
        // HANDLE_MSG( hwnd, WM_COMMAND,   WinOnCommand );
	case WM_COMMAND: //	void WinOnCommand( HWND hwnd, int id, HWND hwndCtl, int codeNotify )
		switch( LOWORD(wParam) ) {
			case IDM_FILE_NEW :
				LogDestroy( hwnd );
				LogUpdate( hwnd );
				break;
			case IDM_FILE_OPEN :
				TraceRouteSetup( hwnd );
				break;
			case IDM_FILE_PRINT :
				LogAppend( hwnd, "wVersion: %X", wsaData.wVersion );
				LogAppend( hwnd, "wHighVersion: %X", wsaData.wHighVersion );
				LogAppend( hwnd, "szDescription: %s", wsaData.szDescription );
				LogAppend( hwnd, "iMaxSocket: %d", wsaData.iMaxSockets );
				LogAppend( hwnd, "iMaxUdpDg: %d", wsaData.iMaxUdpDg );
				LogAppend( hwnd, "lpVendorInfo: %lX", *((DWORD *)&(wsaData.lpVendorInfo)) );
				LogUpdate( hwnd );
				break;
    		}
		break;
        case WM_SOCKET: // HANDLE_MSG( hwnd, WM_SOCKET,    WinOnSocket );
            return WinOnSocket( hwnd, (SOCKET)wParam,
                (SOCKERR)WSAGETSELECTERROR(lParam),
                (SOCKEVENT)WSAGETSELECTEVENT(lParam) );
        // case WM_RBUTTONDOWN :
        // case WM_LBUTTONDOWN :
        case WM_CHAR :{
            MainWinData *pData = (MainWinData*)GetWindowLong( hwnd, 0 );

            LogAppend( hwnd, "%04d, %04d, %08lX, %08lX",
                pData->logNum, iMsg, wParam, lParam );
            LogUpdate( hwnd );
            return 0;
        }
	case WM_CLOSE:
		ShowWindow(hwnd, SW_HIDE);
		return 0;
    }
    return DefWindowProc( hwnd, iMsg, wParam, lParam );
}

const char widgetPanel[] = TEXT("widgetPanel");
LRESULT CALLBACK widgetPanelProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static COLORREF crCustClr[16];
	CREATESTRUCT* ptr = (CREATESTRUCT*)GetWindowLong(hwnd, 0);

	switch(msg) {
		case WM_CREATE: {
			CREATESTRUCT* ptr = (CREATESTRUCT*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(CREATESTRUCT));
 			memcpy((char*)ptr, (char*)lParam, sizeof(CREATESTRUCT));
			SetWindowLong(hwnd, 0, (long)ptr); // ->lpszClass );
			break;
		}
		case WM_LBUTTONUP: {
			char szmsg[80];
			sprintf(szmsg, "%s, %s, %06lX clicked.\n",
				ptr->lpszClass, ptr->lpszName, ptr->lpCreateParams );
			MessageBox(hwnd, TEXT(szmsg), TEXT("Hello, World!"),MB_OK);
			// Beep(50, 40);
			break;
		}
		case WM_RBUTTONUP: {
			CHOOSECOLOR cc;

			ZeroMemory(&cc, sizeof(cc));
			cc.lStructSize = sizeof(cc);
			cc.hwndOwner = hwnd;
			cc.lpCustColors = (LPDWORD)crCustClr;
			cc.rgbResult = (COLORREF)ptr->lpCreateParams;
			cc.Flags = CC_FULLOPEN | CC_RGBINIT;
			ChooseColor(&cc);

			ptr->lpCreateParams = (PVOID)cc.rgbResult;
			InvalidateRect(hwnd, NULL, TRUE);
			break;
		}
		case WM_USER: {
			ptr->lpCreateParams = (PVOID)lParam;
			InvalidateRect(hwnd, NULL, TRUE);
			break;
		}
		case WM_PAINT: {
			HDC hdc;
			PAINTSTRUCT ps;
			RECT rect;
			TCHAR label[32];
			
			// HBRUSH hBrush[2];
			// HPEN hPen[2];

			hdc = BeginPaint(hwnd, &ps);
			GetClientRect(hwnd, &rect);
#if 0
			hBrush[1] = CreateSolidBrush(ptr->color);
			hBrush[0] = SelectObject(hdc, hBrush[1]);
			hPen[1] = CreatePen(PS_NULL, 1, RGB(0,0,0));
			hPen[0] = SelectObject(hdc, hPen[1]);

			Rectangle(hdc, gbox[2].x+220, gbox[2].y+140,
				gbox[2].x+220+100, gbox[2].y+140+50);
#endif
			SetBkColor(hdc, (COLORREF)ptr->lpCreateParams /* gColor */);
			ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rect, TEXT(""), 0, NULL);

			GetWindowText(hwnd, label, sizeof(label)-1);
			TextOut(hdc, 8, (rect.bottom-rect.top)/2,
				label, strlen(label)); //	ptr->lpszName, strlen(ptr->lpszName));
#if 0
			SelectObject(hdc, hPen[0]);
			SelectObject(hdc, hBrush[0]);
			DeleteObject(hPen[1]);
			DeleteObject(hBrush[1]);
#endif
			EndPaint(hwnd, &ps);
			break;
		}
		case WM_DESTROY:
			HeapFree(GetProcessHeap(), 0, (char*)GetWindowLong(hwnd, 0));
			break;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

const char widgetBurning[] = TEXT("wigetBurning");
LRESULT CALLBACK widgetBurningProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static const TCHAR* cap[] = {TEXT("75"), TEXT("150"), TEXT("225"),
		TEXT("300"), TEXT("375"), TEXT("450"), TEXT("525"), TEXT("600"),
		TEXT("675")};
	CREATESTRUCT* ptr = (CREATESTRUCT*)GetWindowLong(hwnd, 0);

	switch(msg) {
		case WM_CREATE: {
			CREATESTRUCT* ptr = (CREATESTRUCT*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(CREATESTRUCT));
 			memcpy((char*)ptr, (char*)lParam, sizeof(CREATESTRUCT));
			SetWindowLong(hwnd, 0, (long)ptr); // ->lpszClass );
			break;
		}
		case WM_HSCROLL:
			ptr->lpCreateParams = (LPVOID)lParam;
			InvalidateRect(hwnd, NULL, TRUE);
			break;
		case WM_PAINT: {
			HDC hdc;
			PAINTSTRUCT ps;
			RECT rect, rect2;
			HFONT hFont[2];
			HBRUSH hBrush[3];
			HPEN hPen[2];
			int i, till, step, full;
			CREATESTRUCT* ptr = (CREATESTRUCT*)GetWindowLong(hwnd, 0);

			hdc = BeginPaint(hwnd, &ps);
			GetClientRect(hwnd, &rect);

			hBrush[1] = CreateSolidBrush(RGB(255,255,184)); // Yellow
			hBrush[2] = CreateSolidBrush(RGB(255,110,110)); // Red
			hBrush[0] = SelectObject(hdc, hBrush[1]);
			hPen[1] = CreatePen(PS_NULL, 1, RGB(0,0,0));
			hPen[0] = SelectObject(hdc, hPen[1]);
			hFont[1] = CreateFont(13,0,0,0,FW_MEDIUM,0,0,0,0,0,0,0,0,TEXT("Tahoma"));
			hFont[0] = SelectObject(hdc, hFont[1]);

			till = (rect.right/750.0) * (int)ptr->lpCreateParams;
			step = rect.right / 10.0;
			full = (rect.right/750.0) * 700;

			if(till > full) {
				SelectObject(hdc, hBrush[1]);
				Rectangle(hdc, 0, 0, full, 30);

				SelectObject(hdc, hBrush[2]);
				Rectangle(hdc, full, 0, till, 30);
			}
			else {
				SelectObject(hdc, hBrush[1]);
				Rectangle(hdc, 0, 0, till, 30);
			}
			SelectObject(hdc, hPen[0]);
			for( i = 1; i < 10; i++ ) {
				MoveToEx(hdc, i*step, 0, NULL);
				LineTo(hdc, i*step, 7);

				SetBkMode(hdc, TRANSPARENT);
				rect2.bottom = 28;
				rect2.top = 8;
				rect2.left = i*step-10;
				rect2.right = i*step+10;
				DrawText(hdc, cap[i-1], strlen(cap[i-1]), &rect2, DT_CENTER);
			}

			// cleanup...

			SelectObject(hdc, hBrush[0]);
			DeleteObject(hBrush[1]);
			DeleteObject(hBrush[2]);

			SelectObject(hdc, hPen[0]);
			DeleteObject(hPen[1]);

			SelectObject(hdc, hFont[0]);
			DeleteObject(hFont[1]);

			EndPaint(hwnd, &ps);
			break;
		}
		case WM_DESTROY:
			HeapFree(GetProcessHeap(), 0, (char*)GetWindowLong(hwnd, 0));
			break;
	}
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK mdiDefaultProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch( msg ) {
        case WM_QUERYENDSESSION :
        case WM_CLOSE :
            if( MessageBox( hwnd, "OK to close window?", "Hello",
                MB_ICONQUESTION | MB_OKCANCEL ) != IDOK )
                return 0;
            break;  // i.e., call DefMdiChildProc()
	case WM_KEYDOWN:
		if( wParam == VK_ESCAPE ) {
               		SendMessage(hwnd, WM_CLOSE, (WPARAM)0, (LPARAM)0 );
			// PostQuitMessage(0);
			return 0;
		}
		// continue to WM_CHAR
	case WM_CHAR :
		switch((char)wParam) {
			case ' ':
	                        PlaySound("hellowin.wav", NULL,
					SND_FILENAME | SND_ASYNC );
		}
		break; // let DefMDIChildProc() handle
	case WM_COMMAND: {
		if( GetWindow(hwnd, GW_OWNER) )
			return TRUE;    // not in icon state
		switch(LOWORD(wParam)) {
			case IDM_WINDOW_RESTORE:
				SendMessage( GetParent(hwnd), WM_MDIRESTORE, (WPARAM)hwnd, 0 );
				return 0;
			case IDM_WINDOW_DESTROY:
				SendMessage( GetParent(hwnd), WM_MDIDESTROY, (WPARAM)hwnd, 0 );
				return 0;
			case IDM_WINDOW_CASCADE:
			case IDM_WINDOW_TILE: // send to parent
				SendMessage( GetParent(GetParent(hwnd)), msg, wParam, lParam);
				return 0;
		}
		break;
	}
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc;

		InvalidateRect( hwnd, NULL, TRUE );
		hdc = BeginPaint( hwnd, &ps );
		EndPaint( hwnd, &ps );
		return 0;
	}
        case WM_MDIACTIVATE : {
            HWND hwndClient = GetParent(hwnd);
            HWND hwndFrame = GetParent(hwndClient);

            if( lParam == (LPARAM)hwnd )
                SendMessage( hwndClient, WM_MDISETMENU,
                    (WPARAM)hMenuWindow, (LPARAM)hMenuWindowWindow );
#if 0
            else
                SendMessage( hwndClient, WM_MDISETMENU,
                    (WPARAM)hMenuInit, (LPARAM)hMenuInitWindow );
#endif
            DrawMenuBar( hwndFrame );
            return 0;
	}
	case WM_RBUTTONUP: {
		HMENU hMenu;
		POINT point;

		point.x = LOWORD(lParam);
		point.y = HIWORD(lParam);

		hMenu = CreatePopupMenu();
		ClientToScreen(hwnd, &point);

		AppendMenu(hMenu, MF_STRING, IDM_WINDOW_RESTORE, "&Restore");
		AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
		AppendMenu(hMenu, MF_STRING, IDM_WINDOW_TILE, "&Tile");
		AppendMenu(hMenu, MF_STRING, IDM_WINDOW_CASCADE, "C&ascade");
		AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
		AppendMenu(hMenu, MF_STRING, IDM_WINDOW_DESTROY, "&Close");

		TrackPopupMenu(hMenu, TPM_RIGHTBUTTON,
			point.x, point.y, 0, hwnd, NULL);
		DestroyMenu(hMenu);
		break;
	}

    }
    return DefMDIChildProc(hwnd, msg, wParam, lParam);
}

const char mdiHello[] = TEXT("mdiHello");
LRESULT CALLBACK mdiHelloProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	typedef struct {
		UINT iColor;
		COLORREF clrText;
		char szText[80];
	} HELLODATA, *LPHELLODATA;

	/* shared among windows based on this class */
	static const COLORREF clrTextArray[] = {
		RGB(  0,  0,  0), RGB(255, 0,   0),
		RGB(  0,255,  0), RGB(  0, 0, 255),
		RGB(255,255,255) };
	static HWND hwndClient, hwndFrame;

	HMENU hMenu;
	LPHELLODATA lpHelloData = (LPHELLODATA)GetWindowLong(hwnd, 0);

    switch( iMsg ) {
        case WM_CREATE :
            // allocate memory for window private data
            lpHelloData = (LPHELLODATA)HeapAlloc( GetProcessHeap(),
                HEAP_ZERO_MEMORY, sizeof( HELLODATA ) );
            lpHelloData->iColor = IDM_BLACK;
            lpHelloData->clrText = RGB( 0, 0, 0 );
            lpHelloData->szText[0] = '\0';
            SetWindowLong( hwnd, 0, (long)lpHelloData );

            // Start the timer going
            SetTimer( hwnd, 1, 1000, NULL );

            // save some window handles
            hwndClient = GetParent( hwnd );
            hwndFrame = GetParent( hwndClient );
            return 0;
	case WM_TIMER: {	/* timer went off */
		struct tm* tm_p;
		time_t tm_v;

		time( &tm_v );
		tm_p = localtime( &tm_v );

		/* display the new time */
		strcpy( lpHelloData->szText, asctime(tm_p) );
		lpHelloData->szText[ strlen(lpHelloData->szText)-1 ] = '\0';
		InvalidateRect( hwnd, NULL, 0 );	/* update screen */
		break;
	}
        case WM_COMMAND :
            switch( LOWORD( wParam ) ) {
		case IDM_WINDOW_FIRST ... IDM_WINDOW_LAST:
			break;
		case IDM_COLOR_FIRST ... IDM_COLOR_LAST:
                    hMenu = GetMenu( hwndFrame );
                    CheckMenuItem( hMenu, lpHelloData->iColor, MF_UNCHECKED );
                    lpHelloData->iColor = LOWORD( wParam );
                    CheckMenuItem( hMenu, lpHelloData->iColor, MF_CHECKED );

                    lpHelloData->clrText = clrTextArray[ LOWORD(wParam) - IDM_BLACK ];
                    InvalidateRect( hwnd, NULL, FALSE );
                    return 0;
            }
            break;
	case WM_SIZE: {
		RECT rect;

		GetClientRect(hwnd, &rect);
		InvalidateRect(hwnd, &rect, TRUE);
		break;
	}
        case WM_PAINT: {
		HDC hdc;
		PAINTSTRUCT ps;
		RECT rect;

		hdc = BeginPaint( hwnd, &ps );

		SetTextColor( hdc, lpHelloData->clrText );
		GetClientRect( hwnd, &rect );

		// TextOut( hdc, X, Y, lpHelloData->szText, strlen(lpHelloData->szText) );

		DrawText( hdc, lpHelloData->szText, -1, &rect,
			DT_SINGLELINE | DT_CENTER | DT_VCENTER );

		EndPaint( hwnd, &ps );
		return 0;
	}
        case WM_MDIACTIVATE :
            // Set the Hello menu if gaining focus
            if( lParam != (LPARAM)hwnd ) {
                // SendMessage( hwndClient, WM_MDISETMENU, (WPARAM)hMenuInit, (LPARAM)hMenuInitWindow);
                SendMessage( hwndClient, WM_MDISETMENU, (WPARAM)hMenuWindow, (LPARAM)hMenuWindowWindow);
                CheckMenuItem( hMenuHello, lpHelloData->iColor,
                    MF_UNCHECKED );
            }
            else {
                SendMessage( hwndClient, WM_MDISETMENU,
                    (WPARAM)hMenuHello, (LPARAM)hMenuHelloWindow );
                CheckMenuItem( hMenuHello, lpHelloData->iColor,
                    MF_CHECKED );
            }
            DrawMenuBar( hwndFrame );
            return 0;
        case WM_DESTROY :
            KillTimer( hwnd, 1 );
            HeapFree( GetProcessHeap(), 0, lpHelloData );
            return 0;
    }
    return mdiDefaultProc( hwnd, iMsg, wParam, lParam );
}

const char mdiRect[]  = TEXT("mdiRect");
LRESULT CALLBACK mdiRectProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	typedef struct {
		short int cxClient;
		short int cyClient;
	} RECTDATA, *LPRECTDATA;

	static HWND hwndClient, hwndFrame;
	HBRUSH hBrush;
	HDC hdc;
	LPRECTDATA lpRectData;
	int xLeft, xRight, yTop, yBottom;
	short nRed, nGreen, nBlue;

    switch( iMsg ) {
        case WM_CREATE :
            // Allocate memory for window private data
            lpRectData = (LPRECTDATA)HeapAlloc( GetProcessHeap(),
                HEAP_ZERO_MEMORY, sizeof( RECTDATA ) );
            SetWindowLong( hwnd, 0, (long)lpRectData );

            // Start the timer going
            SetTimer( hwnd, 1, 250, NULL );

            // Save some window handles
            hwndClient = GetParent( hwnd );
            hwndFrame = GetParent( hwndClient );
            return 0;
        case WM_SIZE :
            if( wParam == SIZE_MINIMIZED )
                break;
            lpRectData = (LPRECTDATA)GetWindowLong( hwnd, 0 );
            lpRectData->cxClient = LOWORD( lParam );
            lpRectData->cyClient = HIWORD( lParam );
            break;
        case WM_TIMER :
            lpRectData = (LPRECTDATA)GetWindowLong( hwnd, 0 );
            xLeft = rand() % lpRectData->cxClient;
            xRight = rand() % lpRectData->cxClient;
            yTop = rand() % lpRectData->cyClient;
            yBottom = rand() % lpRectData->cyClient;
            nRed = rand() % 256;
            nGreen = rand() % 256;
            nBlue = rand() % 256;

            hdc = GetDC( hwnd );
            hBrush = CreateSolidBrush( RGB( nRed, nGreen, nBlue ) );
            SelectObject( hdc, hBrush );

            Rectangle( hdc,
                min(xLeft,xRight), min(yTop,yBottom),
                max(xLeft,xRight), max(yTop,yBottom) );

            ReleaseDC( hwnd, hdc );
            DeleteObject( hBrush );
            return 0;
        case WM_DESTROY :
            lpRectData = (LPRECTDATA)GetWindowLong( hwnd, 0 );
            HeapFree( GetProcessHeap(), 0, lpRectData );
            KillTimer( hwnd, 1 );
            return 0;
    }
    return mdiDefaultProc( hwnd, iMsg, wParam, lParam );
}

const char mdiGDIDemo[] = TEXT("mdiGDIDemo");
LRESULT CALLBACK mdiGDIDemoProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
#define IDM_GDI_RESET		200
#define	IDM_GDI_PIXEL		201
#define IDM_GDI_RECTANGLE	202
#define IDM_GDI_PEN		203
#define IDM_GDI_BRUSH		204
#define IDM_GDI_HATCH		205
#define IDM_GDI_SHAPE		206
#define IDM_GDI_FONT		207

	static const TBBUTTON tbButtons[] = {
		{0, IDM_GDI_PIXEL,	TBSTATE_ENABLED, TBSTYLE_BUTTON, 0L, 0},
		{1, IDM_GDI_RECTANGLE,	TBSTATE_ENABLED, TBSTYLE_BUTTON, 0L, 0},
		{2, IDM_GDI_PEN,	TBSTATE_ENABLED, TBSTYLE_BUTTON, 0L, 0},
		{3, IDM_GDI_BRUSH,	TBSTATE_ENABLED, TBSTYLE_BUTTON, 0L, 0},
		{0, 0,			TBSTATE_ENABLED, TBSTYLE_SEP,	 0L, 0},
		{4, IDM_GDI_HATCH,	TBSTATE_ENABLED, TBSTYLE_BUTTON, 0L, 0},
		{0, IDM_GDI_SHAPE,	TBSTATE_ENABLED, TBSTYLE_BUTTON, 0L, 0},
		{0, 0,			TBSTATE_ENABLED, TBSTYLE_SEP,	 0L, 0},
		{1, IDM_GDI_FONT,	TBSTATE_ENABLED, TBSTYLE_BUTTON, 0L, 0},
		{0, 0,			TBSTATE_ENABLED, TBSTYLE_SEP,	 0L, 0},
		{2, IDM_GDI_RESET,	TBSTATE_ENABLED, TBSTYLE_BUTTON, 0L, 0},
	};
	typedef struct {
		HWND tbwnd, ttwnd;
		int cmd;
		LOGFONT lf;
		COLORREF textColor;
	} DATA;
	DATA* ptr = (DATA*)GetWindowLong(hwnd, 0);

	switch(msg) {
		case WM_CREATE:
			ptr = (DATA*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DATA));
			SetWindowLong( hwnd, 0, (long)ptr );
			ptr->cmd = IDM_GDI_RESET;
			ptr->tbwnd = CreateToolbarEx(hwnd,
				WS_VISIBLE|WS_CHILD|/* WS_BORDER|*/TBSTYLE_TOOLTIPS,
				IDTB_TOOLBAR,
				sizeof(tbButtons)/sizeof(TBBUTTON),
				hInst, IDTB_BMP,
				tbButtons,
				sizeof(tbButtons)/sizeof(TBBUTTON),
				0, 0, 16, 16, sizeof(TBBUTTON));
			ptr->ttwnd = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
				WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,
				0, 0, 0, 0, ptr->tbwnd, NULL, NULL, NULL);
			{
				RECT rect;
				TOOLINFO ti;
				GetClientRect(ptr->tbwnd, &rect);

				ti.cbSize = sizeof(ti);
				ti.uFlags = TTF_SUBCLASS;
				ti.hwnd = ptr->tbwnd;
				ti.hinst = NULL;
				ti.uId = 0;
				ti.lpszText = "This is TOOLTIPS";
				memcpy(&(ti.rect), &rect, sizeof(rect));
				SendMessage(ptr->ttwnd, TTM_ADDTOOL, 0, (LPARAM)&ti);
			}
			ptr->textColor = RGB(0,0,0);
			return 0;
		case WM_COMMAND :
			switch(LOWORD(wParam)) {
				case IDM_WINDOW_FIRST ... IDM_WINDOW_LAST:
					return mdiDefaultProc(hwnd,msg,wParam,lParam);
				case IDM_GDI_FONT: {
					CHOOSEFONT cf;
					BOOL rc;

					ZeroMemory(&cf, sizeof(cf));
					cf.lStructSize = sizeof(cf);
					cf.hwndOwner = hwnd;
					cf.lpLogFont = &ptr->lf;
					cf.rgbColors = ptr->textColor;
					cf.Flags = CF_EFFECTS|CF_SCREENFONTS|CF_INITTOLOGFONTSTRUCT;

					if((rc = ChooseFont(&cf))==TRUE)
						ptr->textColor = cf.rgbColors;
					LogAppend(hLogWnd, "%s(%d) ChooseFont()=%d", __FILE__, __LINE__, rc );
					LogUpdate(hLogWnd);
				}
				default:
					ptr->cmd = LOWORD(wParam);
					InvalidateRect(hwnd, NULL, TRUE);
			}
			return 0;
		case WM_SIZE: {
			// RECT rect;
			// GetClientRect(hwnd, &rect);
			// InvalidateRect(hwnd, &rect, TRUE);
			InvalidateRect(hwnd, NULL, TRUE);
			SendMessage(ptr->tbwnd, msg, wParam, lParam);
			break;
		}
		case WM_PAINT: {
			HDC hdc;
			PAINTSTRUCT ps;
			HPEN hPen[8];
			HBRUSH hBrush[8];
			HFONT hFont[4];
			int i, w, h, x1, y1, x2, y2;

			hdc = BeginPaint(hwnd, &ps);
			switch(ptr->cmd) {
				case IDM_GDI_PIXEL:
					w = ps.rcPaint.right - ps.rcPaint.left;
					h = ps.rcPaint.bottom - ps.rcPaint.top;
					i = w*h; // (w > h) ? w : h;
					for(; i>0; i-- ) {
						SetPixel(hdc,
							ps.rcPaint.left+(rand()%w),
							ps.rcPaint.top+(rand()%h),
							RGB(rand()%255,rand()%255,rand()%255));
					}
					break;
				case IDM_GDI_RECTANGLE:
					GetClientRect(hwnd, &ps.rcPaint);
					w = (ps.rcPaint.right - ps.rcPaint.left)/2;
					h = (ps.rcPaint.bottom - ps.rcPaint.top)/2;
					Rectangle(hdc,w/2,h/2,w*3/2,h*3/2);
					break;
				case IDM_GDI_PEN:
					GetClientRect(hwnd, &ps.rcPaint);
					x1 = (ps.rcPaint.right - ps.rcPaint.left)/4;
					y1 = (ps.rcPaint.bottom - ps.rcPaint.top)/4;
					x2 = x1 * 3;
					y2 = y1 * 3;
					h = (y2 - y1) / 4;

					hPen[1] = CreatePen(PS_SOLID, 1, RGB(0,0,0));
					hPen[2] = CreatePen(PS_DASH, 1, RGB(0,0,0));
					hPen[3] = CreatePen(PS_DOT, 1, RGB(0,0,0));
					hPen[4] = CreatePen(PS_DASHDOT, 1, RGB(0,0,0));
					hPen[5] = CreatePen(PS_DASHDOTDOT, 1, RGB(0,0,0));

					hPen[0] = SelectObject(hdc, hPen[1]);
					MoveToEx(hdc, x1, y1, NULL);
					LineTo(hdc, x2, y1);

					for(i=2; i<=5; i++) {
						SelectObject(hdc, hPen[i]);
						MoveToEx(hdc, x1, y1+h*(i-1), NULL);
						LineTo(hdc, x2, y1+h*(i-1));
					}
					SelectObject(hdc, hPen[0]);
					for(i=1; i<=5; i++)
						DeleteObject(hPen[i]);
					break;
				case IDM_GDI_BRUSH:
					hPen[1] = CreatePen(PS_NULL, 1, RGB(0,0,0));
					hPen[0] = SelectObject(hdc, hPen[1]);

					hBrush[1] = CreateSolidBrush(RGB(121,90,0));
					hBrush[2] = CreateSolidBrush(RGB(240,63,19));
					hBrush[3] = CreateSolidBrush(RGB(240,210,18));
					hBrush[4] = CreateSolidBrush(RGB(9,189,21));
					hBrush[0] = SelectObject(hdc, hBrush[1]);

					GetClientRect(hwnd, &ps.rcPaint);
					w = (ps.rcPaint.right - ps.rcPaint.left)/3;
					h = (ps.rcPaint.bottom - ps.rcPaint.top)/3;
					x1 = w/2;
					y1 = h/2;
					x2 = x1 + w;
					y2 = y1 + h;
					i = w/32;
					Rectangle(hdc, x1+i, y1+i, x2-i, y2-i);

					SelectObject(hdc, hBrush[2]);
					Rectangle(hdc, x1+w+i, y1+i, x2+w-i, y2-i);

					SelectObject(hdc, hBrush[3]);
					Rectangle(hdc, x1+i, y1+h+i, x2-i, y2+h-i);

					SelectObject(hdc, hBrush[4]);
					Rectangle(hdc, x1+w+i, y1+h+i, x2+w-i, y2+h-i);

					SelectObject(hdc, hPen[0]);
					SelectObject(hdc, hBrush[0]);

					DeleteObject(hPen[1]);
					for(i=1; i<=4; i++)
						DeleteObject(hBrush[i]);
					break;
				case IDM_GDI_HATCH:
					hPen[1] = CreatePen(PS_NULL, 1, RGB(0,0,0));
					hPen[0] = SelectObject(hdc, hPen[1]);

					hBrush[1] = CreateHatchBrush(HS_BDIAGONAL, RGB(0,0,0));
					hBrush[2] = CreateHatchBrush(HS_FDIAGONAL, RGB(0,0,0));
					hBrush[3] = CreateHatchBrush(HS_CROSS, RGB(0,0,0));
					hBrush[4] = CreateHatchBrush(HS_HORIZONTAL, RGB(0,0,0));
					hBrush[5] = CreateHatchBrush(HS_DIAGCROSS, RGB(0,0,0));
					hBrush[6] = CreateHatchBrush(HS_VERTICAL, RGB(0,0,0));

					hBrush[0] = SelectObject(hdc, hBrush[1]);

					GetClientRect(hwnd, &ps.rcPaint);
					w = (ps.rcPaint.right - ps.rcPaint.left)/4;
					h = (ps.rcPaint.bottom - ps.rcPaint.top)/3;
					x1 = w/2;
					y1 = h/2;
					x2 = x1 + w;
					y2 = y1 + h;
					i = w/32;
					Rectangle(hdc, x1+i, y1+i, x2-i, y2-i);

					SelectObject(hdc, hBrush[2]);
					Rectangle(hdc, x1+w+i, y1+i, x2+w-i, y2-i);

					SelectObject(hdc, hBrush[3]);
					Rectangle(hdc, x1+2*w+i, y1+i, x2+2*w-i, y2-i);

					SelectObject(hdc, hBrush[4]);
					Rectangle(hdc, x1+i, y1+h+i, x2-i, y2+h-i);

					SelectObject(hdc, hBrush[5]);
					Rectangle(hdc, x1+w+i, y1+h+i, x2+w-i, y2+h-i);

					SelectObject(hdc, hBrush[6]);
					Rectangle(hdc, x1+2*w+i, y1+h+i, x2+2*w-i, y2+h-i);

					SelectObject(hdc, hPen[0]);
					SelectObject(hdc, hBrush[0]);

					DeleteObject(hPen[1]);
					for(i=1; i<=6; i++)
						DeleteObject(hBrush[i]);
					break;
				case IDM_GDI_SHAPE: {
#if 0
					const POINT polygon[5] = {30, 145,
						85, 165, 105, 110,
						65, 125, 30, 105};
					const POINT bezier[4] = {280, 160,
						320, 160, 325, 110, 350, 110};
#endif
					POINT pt[5];

					GetClientRect(hwnd, &ps.rcPaint);
					w = (ps.rcPaint.right - ps.rcPaint.left)/4;
					h = (ps.rcPaint.bottom - ps.rcPaint.top)/3;
					i = w/16;
					x1 = w/2 + i;
					y1 = h/2 + i;
					x2 = x1 + w - 2*i;
					y2 = y1 + h - 2*i;

					// Ellipse(hdc, 30, 30, 120, 90);
					Ellipse(hdc, x1, y1, x2, y2);

					// RoundRect(hdc, 150, 30, 240, 90, 15, 20);
					RoundRect(hdc, x1+w, y1, x2+w, y2, w/4, h/4);

					// Chord(hdc, 270, 30, 360, 90, 270, 45, 360, 45);
					Chord(hdc, x1+2*w, y1, x2+2*w, y2, x1+2*w, 4*i, x2+2*w, 4*i);

					// Polygon(hdc, polygon, sizeof(polygon)/sizeof(POINT));
					pt[0].x = x1, pt[0].y = y1+h+i;
					pt[1].x = x1+w/2-i, pt[1].y = y1+h+3*i;
					pt[2].x = x2-i, pt[2].y = y1+h;
					pt[3].x = x2-2*i, pt[3].y = y2+h;
					pt[4].x = x1+i, pt[4].y = y2+h-3*i;
					Polygon(hdc, pt, 5);

					// Rectangle(hdc, 150, 110, 230, 160);
					Rectangle(hdc, x1+w, y1+h, x2+w, y2+h);
					
					// PolyBezier(hdc, bezier, sizeof(bezier)/sizeof(POINT));
					pt[0].x=x1+2*w, pt[0].y=y2+h;	  // begin
					pt[1].x=x2+2*w-8*i, pt[1].y=y2+h; // control
					pt[2].x=x2+2*w-2*i, pt[2].y=y1+h; // control
					pt[3].x=x2+2*w, pt[3].y=y1+h;	  // end
					PolyBezier(hdc, pt, 4);

					break;
				}
				case IDM_GDI_FONT: {
					static const TCHAR *verse[] = {
	TEXT("Not marble, nor the gilded monuments"),
	TEXT("Of princes, shall outlive this powerful rhyme;"),
	TEXT("But you shall shine more bright in these contents"),
	TEXT("Than unswept stone, besmear'd with sluttish time."),
	TEXT("When wasteful war shall statues overturn,"),
	TEXT("And broils root out the work of masonry,"),
	TEXT("Nor Mars his sword, nor war's quick fire shall burn"),
	TEXT("The living record of your memory."),
	TEXT("'Gainst death, and all oblivious enmity"),
	TEXT("Shall you pace forth; your praise shall still find room "),
	TEXT("Even in the eyes of all posterity"),
	TEXT("That wear this world out to the ending doom."),
	TEXT("So, till the judgment that yourself arise,"),
	TEXT("You live in this, and dwell in lovers' eyes.") };
					int i;

					SetTextColor(hdc, ptr->textColor);
					SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
					SetBkMode(hdc, TRANSPARENT /*OPAQUE*/ );
					// hFont[1] = CreateFont(15, 0, 0, 0, FW_MEDIUM, 0, 0, 0, 0, 0, 0, 0, 0, TEXT("Georgia"));
					hFont[1] = CreateFontIndirect(&ptr->lf);
					hFont[0] = SelectObject(hdc, hFont[1]);

					for(i=0; i<sizeof(verse)/sizeof(char*); i++) {
						TextOut(hdc, 50, 30+i*30,  verse[i],  lstrlen(verse[i]));
					}
            
					SelectObject(hdc, hFont[0]);
					DeleteObject(hFont[1]);
					break;
				}

			}
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_DESTROY:
			HeapFree(GetProcessHeap(), 0, ptr);
			break;
	}
	return mdiDefaultProc(hwnd, msg, wParam, lParam);
}

static int appDialogQuit;
const char appDialog[] = TEXT("appDialog");
LRESULT CALLBACK appDialogProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
#define ID_TABCTRL	201
#define ID_COMBO	202
#define ID_LIST		203
#define ID_STATIC2	204
#define ID_EDIT2	205
#define ID_CHKBOX	206
#define ID_DATE		207
#define ID_TIMER2	208
#define BTN_ADD		210
#define BTN_DEL		211
#define BTN_DELALL	212
#define	BTN_START	213
#define	BTN_BLUE	214
#define	BTN_YELLOW	215
#define	BTN_ORANGE	216

	static struct {
		int x, y, w, h;
		TCHAR* title;
	} gbox[] = {
		{ 0, 0, 0, 0, "Tab Control"},
		{ 0, 0, 0, 0, "Setting #2"},
		{ 0, 0, 0, 0, "ListBox #3"},
		{ 0, 0, 0, 0, "Calendar #4"},
	};
	static struct {
		TCHAR name[30];
		TCHAR job[20];
		int age;
	} friends[] = {
		{TEXT("Erika"), TEXT("waitress"), 18},
		{TEXT("Thomas"), TEXT("programmer"), 25},
		{TEXT("George"), TEXT("police officer"), 26},
		{TEXT("Michael"), TEXT("producer"), 38},
		{TEXT("Jane"), TEXT("steward"), 28},
		{TEXT("FreeBSD"), TEXT("ucb unix"), 60},
		{TEXT("Ubuntu"), TEXT("debian"), 13},
		{TEXT("Solaris"), TEXT("sun micro"), 50},
		{TEXT("Fedora"), TEXT("redhat"), 31},
	};
	typedef struct {
		TCHAR title[32];
		int Pval;
		HWND hTab, hCombo, hTrack, hBurning, hList, hStatic, hDate, hPbar,
			hPanel, hStatic2, hEdit2, hButton2, hTrack2, hLLbl, hRLbl;
	} DATA;
	DATA* ptr = (DATA*)GetWindowLong(hwnd, 0);
	int sel;

	switch(msg) {
		case WM_CREATE: {
			RECT rc;
			int i, gbG, gbW, gbH;

			ptr = (DATA*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DATA));
			SetWindowLong(hwnd, 0, (long)ptr);

			GetWindowText(hwnd, ptr->title, sizeof(ptr->title)-1);
			GetWindowRect(hwnd, &rc);
			SetWindowPos(hwnd, 0,
				(GetSystemMetrics(SM_CXSCREEN)-rc.right+rc.left)/2,
				(GetSystemMetrics(SM_CYSCREEN)-rc.bottom+rc.top)/2,
				0, 0, SWP_NOZORDER|SWP_NOSIZE );

			GetClientRect(hwnd, &rc);
			gbG=10, gbW=rc.right-rc.left, gbH=rc.bottom-rc.top;

			gbox[0].x=gbG, gbox[0].y=gbG;
			gbox[1].x=gbW/2+gbG, gbox[1].y=gbG;
			gbox[2].x=gbG, gbox[2].y=gbH/2+gbG;
			gbox[3].x=gbW/2+gbG, gbox[3].y=gbH/2+gbG;
			for(i=0; i<4; i++) {
				gbox[i].w=gbW/2-gbG*2, gbox[i].h=gbH/2-gbG*2;
				if(i)
				CreateWindow(TEXT("button"), gbox[i].title, WS_CHILD|WS_VISIBLE|BS_GROUPBOX,
					gbox[i].x, gbox[i].y, gbox[i].w, gbox[i].h, hwnd, (HMENU)0, hInst, NULL);
				gbox[i].x += 3*gbG, gbox[i].y += 4*gbG;
				gbox[i].w -= 6*gbG, gbox[i].h -= 8*gbG;
			}

			ptr->hTab = CreateWindow(WC_TABCONTROL, NULL, WS_CHILD|WS_VISIBLE,
				gbox[0].x, gbox[0].y, 200, 120, hwnd, (HMENU)ID_TABCTRL, hInst, NULL);
			ptr->hCombo = CreateWindow("combobox", NULL, WS_CHILD|WS_VISIBLE|CBS_DROPDOWN,
				gbox[0].x+220, gbox[0].y+10, 100, 100, hwnd, (HMENU)ID_COMBO, hInst, NULL);
			CreateWindow("button", "add", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
				gbox[0].x+220, gbox[0].y+40, 100, 25, hwnd, (HMENU)BTN_ADD, hInst, NULL);
			CreateWindow("button", "del", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
				gbox[0].x+220, gbox[0].y+70, 100, 25, hwnd, (HMENU)BTN_DEL, hInst, NULL);
			CreateWindow("button", "delall", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
				gbox[0].x+220, gbox[0].y+100, 100, 25, hwnd, (HMENU)BTN_DELALL, hInst, NULL);

			ptr->hTrack = CreateWindowEx(0, TRACKBAR_CLASS, NULL, WS_CHILD|WS_VISIBLE|TBS_FIXEDLENGTH|TBS_NOTICKS,
				gbox[0].x, gbox[0].y+140, gbox[0].w, 25, hwnd, (HMENU)0, hInst, NULL);
			ptr->hBurning = CreateWindowEx(WS_EX_STATICEDGE, widgetBurning, NULL, WS_CHILD|WS_VISIBLE,
				gbox[0].x, gbox[0].y+180, gbox[0].w, 30, hwnd, (HMENU)0, hInst, (void*)150);

			SendMessage(ptr->hTrack, TBM_SETRANGE, TRUE, MAKELONG(0,750));
			SendMessage(ptr->hTrack, TBM_SETPAGESIZE, 0, 20);
			SendMessage(ptr->hTrack, TBM_SETTICFREQ, 20, 0);
			SendMessage(ptr->hTrack, TBM_SETPOS, TRUE, 150);

			ptr->hList = CreateWindow(TEXT("listbox"), NULL, WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY,
				gbox[2].x, gbox[2].y, 200, 120, hwnd, (HMENU)ID_LIST, hInst, NULL);
			for(sel=0; sel<sizeof(friends)/sizeof(friends[0]); sel++) {
				SendMessage(ptr->hCombo, CB_ADDSTRING, 0, (LPARAM)friends[sel].name);
				SendMessage(ptr->hList, LB_ADDSTRING, 0, (LPARAM)friends[sel].name);
			}
			ptr->hStatic = CreateWindow(TEXT("static"), NULL, WS_CHILD|WS_VISIBLE,
				gbox[2].x, gbox[2].y+140, 200, 60, hwnd, (HMENU)ID_STATIC2, hInst, NULL);
			CreateWindow("button", "Blue", WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON,
				gbox[2].x+230, gbox[2].y+10, 100, 25, hwnd, (HMENU)BTN_BLUE, hInst, NULL);
			CreateWindow("button", "Yellow", WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON,
				gbox[2].x+230, gbox[2].y+40, 100, 25, hwnd, (HMENU)BTN_YELLOW, hInst, NULL);
			CreateWindow("button", "Orange", WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON,
				gbox[2].x+230, gbox[2].y+70, 100, 25, hwnd, (HMENU)BTN_ORANGE, hInst, NULL);

			ptr->hPanel = CreateWindow(widgetPanel, TEXT("color"), WS_CHILD|WS_VISIBLE,
				gbox[2].x+220, gbox[2].y+140, 100, 50, hwnd, (HMENU)NULL, hInst, (PVOID)RGB(255,255,255));
			CheckDlgButton(hwnd, BTN_BLUE, BST_CHECKED);
			SendMessage(hwnd, WM_COMMAND, BTN_BLUE, 0);

			ptr->hStatic2 = CreateWindow(TEXT("static"), "l=?\tt=?\tr=?\tb=?", WS_CHILD|WS_VISIBLE,
				gbox[1].x, gbox[1].y, 200, 30, hwnd, (HMENU)ID_STATIC2, hInst, NULL);

			SetLayeredWindowAttributes(hwnd, RGB(255,0,0), 255, LWA_COLORKEY|LWA_ALPHA);
			ptr->hTrack2 = CreateWindowEx(0, TRACKBAR_CLASS, "Trackbar control",
				WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS|TBS_ENABLESELRANGE,
				gbox[1].x+80, gbox[1].y+40, 150, 30, hwnd, (HMENU)0, hInst, NULL);
			ptr->hLLbl = CreateWindow("static", "Transparent", WS_CHILD|WS_VISIBLE,
				0, 0, 80, 30, hwnd, (HMENU)0, hInst, NULL);
			ptr->hRLbl = CreateWindow("static", "Opaque", WS_CHILD|WS_VISIBLE,
				0, 0, 50, 30, hwnd, (HMENU)0, hInst, NULL);

			SendMessage(ptr->hTrack2, TBM_SETRANGE, TRUE, MAKELONG(0,255));
			SendMessage(ptr->hTrack2, TBM_SETPAGESIZE, 0, 20);
			SendMessage(ptr->hTrack2, TBM_SETTICFREQ, FALSE, 255);
			SendMessage(ptr->hTrack2, TBM_SETPOS, FALSE, 255);
			SendMessage(ptr->hTrack2, TBM_SETBUDDY, TRUE, (LPARAM)ptr->hLLbl);
			SendMessage(ptr->hTrack2, TBM_SETBUDDY, FALSE, (LPARAM)ptr->hRLbl);

			ptr->hEdit2 = CreateWindow("edit", NULL, WS_CHILD|WS_VISIBLE|WS_BORDER,
				gbox[1].x, gbox[1].y+80, 280, 25, hwnd, (HMENU)ID_EDIT2, hInst, NULL);

			CreateWindow("button", TEXT("show original title"), WS_CHILD|WS_VISIBLE|BS_CHECKBOX,
				gbox[1].x+150, gbox[1].y+110, 150, 25, hwnd, (HMENU)ID_CHKBOX, hInst, NULL);
			CheckDlgButton(hwnd, ID_CHKBOX, BST_CHECKED);

			ptr->hPbar = CreateWindowEx(0, PROGRESS_CLASS, NULL,
				WS_CHILD|WS_VISIBLE|PBS_SMOOTH,
				gbox[1].x, gbox[1].y+150, 280, 25, hwnd, (HMENU)NULL, hInst, NULL);
			SendMessage(ptr->hPbar, PBM_SETRANGE, 0, MAKELPARAM(0,150));
			SendMessage(ptr->hPbar, PBM_SETSTEP, 1, 0);

			CreateWindow("button", TEXT("Start"), WS_CHILD|WS_VISIBLE,
				gbox[1].x+200, gbox[1].y+190, 80, 25, hwnd, (HMENU)BTN_START, hInst, NULL);

			ptr->hDate = CreateWindowEx(0, MONTHCAL_CLASS, TEXT("bonjour"),
				WS_BORDER | WS_CHILD | WS_VISIBLE | MCS_DAYSTATE,
				gbox[3].x+(gbox[3].w-240)/2, gbox[3].y, /*225*/240, gbox[3].h, hwnd, (HMENU)ID_DATE, hInst, NULL);
			// SendMessage(hwnd, WM_COMMAND, ID_DATE, 0);
			break;
		}
		case WM_HSCROLL: {
			LRESULT res;
			res = SendMessage((HWND)lParam, TBM_GETPOS, 0, 0);
			if((HWND)lParam == ptr->hTrack)
				SendMessage(ptr->hBurning, WM_HSCROLL, wParam, (LPARAM)res);
			else if((HWND)lParam == ptr->hTrack2)
				SetLayeredWindowAttributes(hwnd, RGB(255,0,0), (BYTE)res, LWA_COLORKEY|LWA_ALPHA);
		}
		case WM_COMMAND: {
			/* if(HIWORD(wParam)==BN_CLICKED) {
			}
			else */
			switch(LOWORD(wParam)) {
				case ID_STATIC2: {
					TCHAR text[250];
					sprintf(text,TEXT("Name: %s.\nJob: %s.\nAge: %d."),
						friends[lParam].name, friends[lParam].job, friends[lParam].age);
					SetWindowText(ptr->hStatic, text);
					break;
				}
				case ID_COMBO:
					if(HIWORD(wParam)==BN_CLICKED) {
						SendMessage(ptr->hCombo, CB_SHOWDROPDOWN, (WPARAM)TRUE, 0);
					}
					else if(HIWORD(wParam)==CBN_SELCHANGE) {
						SetFocus(hwnd);
						sel = (int)SendMessage(ptr->hCombo, CB_GETCURSEL, 0, 0);
						SendMessage(hwnd, WM_COMMAND, ID_STATIC2, sel);
					}
					break;
				case ID_LIST: {
					if(HIWORD(wParam)==LBN_SELCHANGE) {
						sel = (int)SendMessage(ptr->hList, LB_GETCURSEL, 0, 0);
						SendMessage(hwnd, WM_COMMAND, ID_STATIC2, sel);
					}
					break;
				}
				case BTN_ADD: {
					TCHAR text[250];
					LRESULT rc;
					GetWindowText(ptr->hCombo, text, sizeof(text));
					if(lstrlen(text) != 0) {
						TCITEM tie;
						tie.mask = TCIF_TEXT;
						tie.pszText = text;
						rc = SendMessage(ptr->hTab, TCM_GETITEMCOUNT, 0, 0);
						SendMessage(ptr->hTab, TCM_INSERTITEM, rc, (LPARAM)&tie);
						LogAppend(hLogWnd, "tab append %d", rc );
						LogUpdate(hLogWnd);
					}
					break;
				}
				case BTN_DEL: {
					LRESULT rc;
					rc = SendMessage(ptr->hTab, TCM_GETCURSEL, 0, 0);
					if(rc != -1)
						SendMessage(ptr->hTab, TCM_DELETEITEM, rc, 0);
					LogAppend(hLogWnd, "tab delete %d", rc );
					LogUpdate(hLogWnd);
					break;
				}
				case BTN_DELALL:
					SendMessage(ptr->hTab, TCM_DELETEALLITEMS, 0, 0);
					break;
				case BTN_START:
					ptr->Pval = 1;
					SendMessage(ptr->hPbar, PBM_SETPOS, 0, 0);
					SetTimer(hwnd, ID_TIMER2, 100/*5*/, NULL);
					break;
				case BTN_BLUE:
					SendMessage(ptr->hPanel, WM_USER, 0, (LPARAM)RGB(0, 76, 255));
					break;
				case BTN_YELLOW:
					SendMessage(ptr->hPanel, WM_USER, 0, (LPARAM)RGB(255, 255, 0));
					break;
				case BTN_ORANGE:
					SendMessage(ptr->hPanel, WM_USER, 0, (LPARAM)RGB(255, 123, 0));
					break;
				case ID_EDIT2: {
//					int len = GetWindowTextLength(ptr->hEdit2)+1;
					TCHAR text[32];
					CheckDlgButton(hwnd, ID_CHKBOX, BST_UNCHECKED);
					GetWindowText(ptr->hEdit2, text, sizeof(text)-1);
					SetWindowText(hwnd, text);
					break;
				}
				case ID_CHKBOX: {
					BOOL checked = IsDlgButtonChecked(hwnd, ID_CHKBOX); 
					if(checked) {
						CheckDlgButton(hwnd, ID_CHKBOX, BST_UNCHECKED);
						// SetWindowText(hwnd, TEXT(""));
						SendMessage(hwnd, WM_COMMAND, ID_EDIT2, 0);
					}
					else {
						CheckDlgButton(hwnd, ID_CHKBOX, BST_CHECKED);
						SetWindowText(hwnd, ptr->title);
					}
					break;
				}
				case ID_DATE: {
					SYSTEMTIME tm;
					TCHAR ds[50];

					ZeroMemory(&tm, sizeof(tm));
					SendMessage(ptr->hDate, MCM_GETCURSEL, 0, (LPARAM)&tm);

					sprintf(ds, "%04d/%02d/%02d", tm.wYear, tm.wMonth, tm.wDay);
					SetWindowText(ptr->hEdit2, ds);
					break;
				}
			}
			break;
		}
		case WM_NOTIFY: {
			if(((LPNMHDR)lParam)->code==MCN_SELECT)
				SendMessage(hwnd, WM_COMMAND, ID_DATE, 0);
			break;
		}
		case WM_MOVE: {
			RECT rect;
			char buf[80];

			GetWindowRect(hwnd, &rect);
			sprintf(buf, "l:%4d, t:%4d, r:%4d, b:%4d.",
				rect.left, rect.top, rect.right, rect.bottom );
			SetWindowText(ptr->hStatic2, buf);
			break;
		}
		case WM_TIMER:
			SendMessage(ptr->hPbar, PBM_STEPIT, 0, 0);
			if(++(ptr->Pval)>=150)
				KillTimer(hwnd, ID_TIMER2);
			break;
		case WM_DESTROY:
			appDialogQuit = TRUE;
			KillTimer(hwnd, ID_TIMER2);
			HeapFree(GetProcessHeap(), 0, ptr);
			break;
	}
	return DefWindowProc( hwnd, msg, wParam, lParam );
}

int appDialogMain(HWND parent, int type)
{
	MSG msg;
	HWND hwnd;

	appDialogQuit = FALSE;
	hwnd = CreateWindowEx(WS_EX_LAYERED|WS_EX_DLGMODALFRAME/*|WS_EX_TOPMOST*/,
		appDialog, TEXT("Dialog Box"), 
		/*WS_OVERLAPPEDWINDOW|*/ WS_VISIBLE | WS_SYSMENU | WS_CAPTION,
		0, 0, 800, 600, 
		parent, NULL, NULL/*ghInstance*/, NULL);
	return 0;

	while(!appDialogQuit && GetMessage(&msg, NULL, 0, 0) ) {
		if(msg.hwnd==hwnd) {
			LogAppend(hLogWnd, "msg=%d", msg.message);
			LogUpdate(hLogWnd);
		}
#if 0
		else switch(msg.message) {
			case WM_MOUSEFIRST ... WM_MOUSELAST :
				continue;
		}
#endif
		if(!IsDialogMessage(hwnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return 0;
}

const char appDialog1[] = TEXT("appDialog1");
LRESULT CALLBACK appDialogProc1( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
	typedef struct {
		int w, h, gap;
		HWND hGB[4];
	} DATA;
	DATA* ptr = (DATA*)GetWindowLong(hwnd, 0);

	switch(msg) {
		case WM_CREATE: {
			RECT rc;
			int i;

			ptr = (DATA*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DATA));
			SetWindowLong(hwnd, 0, (long)ptr);

			GetWindowRect(hwnd, &rc);
			SetWindowPos(hwnd, 0,
				(GetSystemMetrics(SM_CXSCREEN)-rc.right+rc.left)/2,
				(GetSystemMetrics(SM_CYSCREEN)-rc.bottom+rc.top)/2,
				0, 0, SWP_NOZORDER|SWP_NOSIZE );

			ptr->hGB[0] = CreateWindow(TEXT("button"), TEXT("GroupBox 1"), WS_CHILD|WS_VISIBLE|BS_GROUPBOX,
				10, 10, 120, 120, hwnd, (HMENU)0, hInst, NULL);
			ptr->hGB[1] = CreateWindow(TEXT("button"), TEXT("GroupBox 2"), WS_CHILD|WS_VISIBLE|BS_GROUPBOX,
				330, 10, 120, 120, hwnd, (HMENU)0, hInst, NULL);
			ptr->hGB[2] = CreateWindow(TEXT("button"), TEXT("GroupBox 3"), WS_CHILD|WS_VISIBLE|BS_GROUPBOX,
				10, 250, 120, 120, hwnd, (HMENU)0, hInst, NULL);
			ptr->hGB[3] = CreateWindow(TEXT("button"), TEXT("GroupBox 4"), WS_CHILD|WS_VISIBLE|BS_GROUPBOX,
				330, 250, 120, 120, hwnd, (HMENU)0, hInst, NULL);

			break;
		}
		case WM_SIZE: 
			ptr->w = LOWORD(lParam);
			ptr->h = HIWORD(lParam);
			ptr->gap = 10;
			SetWindowPos(ptr->hGB[0], 0, ptr->gap, ptr->gap, ptr->w/2-ptr->gap*2, ptr->h/2-ptr->gap*2, SWP_NOZORDER);
			SetWindowPos(ptr->hGB[1], 0, ptr->w/2+ptr->gap, ptr->gap, ptr->w/2-ptr->gap*2, ptr->h/2-ptr->gap*2, SWP_NOZORDER);
			SetWindowPos(ptr->hGB[2], 0, ptr->gap, ptr->h/2+ptr->gap, ptr->w/2-ptr->gap*2, ptr->h/2-ptr->gap*2, SWP_NOZORDER);
			SetWindowPos(ptr->hGB[3], 0, ptr->w/2+ptr->gap, ptr->h/2+ptr->gap, ptr->w/2-ptr->gap*2, ptr->h/2-ptr->gap*2, SWP_NOZORDER);
			break;
		case WM_COMMAND: 
			break;
		case WM_DESTROY :
			HeapFree(GetProcessHeap(), 0, ptr);
			break;
	}
	return DefWindowProc( hwnd, msg, wParam, lParam );
}

BOOL CALLBACK CloseEnumProc( HWND hwnd, LPARAM lParam )
{
    if( GetWindow( hwnd, GW_OWNER ) )   // check for icon title
        return TRUE;

    SendMessage( GetParent( hwnd ), WM_MDIRESTORE, (WPARAM)hwnd, 0 );

    if( !SendMessage( hwnd, WM_QUERYENDSESSION, 0, 0 ) )
        return TRUE;

    SendMessage( GetParent( hwnd ), WM_MDIDESTROY, (WPARAM)hwnd, 0 );
    return TRUE;
}

BOOL CALLBACK RestoreEnumProc( HWND hwnd, LPARAM lParam )
{
    if( GetWindow( hwnd, GW_OWNER ) )
        return TRUE;    // not in icon state

    SendMessage( GetParent( hwnd ), WM_MDIRESTORE, (WPARAM)hwnd, 0 );
    return TRUE;
}

const char appFrame[] = TEXT("appFrame");
LRESULT CALLBACK appFrameProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
#define ID_FRAME_CLIENT	1
#define ID_FRAME_STATUS	2
	static HWND hClient, hStatus;
	HWND hwndChild;

	switch(msg) {
		case WM_CREATE : {
			CLIENTCREATESTRUCT ccs;
			int statWidths[] = {100, -1};

			ccs.hWindowMenu = hMenuWindowWindow;
			ccs.idFirstChild = IDM_FIRSTCHILD;

			hClient = CreateWindowEx(WS_EX_CLIENTEDGE, "MDICLIENT", NULL,
				WS_CHILD|WS_CLIPCHILDREN|WS_VISIBLE/*|WS_VSCROLL|WS_HSCROLL*/,
				CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
				hwnd, (HMENU)ID_FRAME_CLIENT, hInst, (LPVOID)&ccs);

			hStatus = CreateWindow(STATUSCLASSNAME, NULL,
				WS_CHILD|WS_VISIBLE|SBARS_SIZEGRIP, 0,0,0,0,
				hwnd, (HMENU)ID_FRAME_STATUS, hInst, NULL);
			SendMessage(hStatus, SB_SETPARTS, sizeof(statWidths)/sizeof(int), (LPARAM)statWidths);
			SendMessage(hStatus, SB_SETTEXT, 0, (LPARAM)"Hi there...");

			break;
		}
		case WM_SIZE : {
			RECT rcClient, rcStatus;

			// hStatus = GetDlgItem(hwnd, ID_FRAME_STATUS);
			SendMessage(hStatus, WM_SIZE, 0, 0);
			GetWindowRect(hStatus, &rcStatus);

			GetClientRect(hwnd, &rcClient);
			// hClient = GetDlgItem(hwnd, ID_FRAME_CLIENT);
			SetWindowPos(hClient, NULL, 0, 0,
				rcClient.right-rcClient.left, rcClient.bottom-rcClient.top-rcStatus.bottom+rcStatus.top,
				SWP_NOZORDER);
			return 0;	// wkliang:20090312 - don't leave it for DefFrameProc()
		}
        case WM_COMMAND : {
            MDICREATESTRUCT mdicreate = { .hOwner=hInst,
		.x=CW_USEDEFAULT, .y=CW_USEDEFAULT,
		.cx=CW_USEDEFAULT, .cy=CW_USEDEFAULT,
		.style=0, .lParam=0 };

            switch( LOWORD(wParam) ) {
                case IDM_DEMO_HELLO :
                    mdicreate.szClass = mdiHello;
                    mdicreate.szTitle = "Hello";
                    hwndChild = (HWND)SendMessage(hClient, WM_MDICREATE,
                        0, (LPARAM)(LPMDICREATESTRUCT)&mdicreate );
                    return 0;
                case IDM_DEMO_RECT :
                    mdicreate.szClass = mdiRect;
                    mdicreate.szTitle = "Rectangles";
                    hwndChild = (HWND)SendMessage(hClient, WM_MDICREATE,
                        0, (LPARAM)(LPMDICREATESTRUCT)&mdicreate );
                    return 0;
                case IDM_DEMO_GDI :
                    mdicreate.szClass = mdiGDIDemo;
                    mdicreate.szTitle = "GDI Demo";
                    hwndChild = (HWND)SendMessage(hClient, WM_MDICREATE,
                        0, (LPARAM)(LPMDICREATESTRUCT)&mdicreate );
                    return 0;
		case IDM_DEMO_DIALOG:
			LogAppend(hLogWnd, "before appDialogMain");
			appDialogMain(hwnd, LOWORD(wParam));
			LogAppend(hLogWnd, "after appDialogMain");
			return 0;
		case IDM_DEMO_DIALOG+1 ... IDM_DEMO_DIALOG+8:
			LogAppend(hLogWnd, "appDialog%d", LOWORD(wParam)-IDM_DEMO_DIALOG);
			CreateWindow(/* WS_EX_DLGMODALFRAME|WS_EX_TOPMOST*/
				appDialog1, TEXT("Dialog Box"), 
				WS_OVERLAPPEDWINDOW|/*WS_CLIPCHILDREN|*/WS_VISIBLE|WS_SYSMENU| WS_CAPTION,
				0, 0, 640, 480, 
				hwnd, NULL, NULL/*ghInstance*/, NULL);
			return 0;
                case IDM_FILE_EXIT :
			SendMessage(hwnd, WM_QUERYENDSESSION, 0, 0L);
#if 0
			hwndChild = (HWND)SendMessage(hClient, WM_MDIGETACTIVE, 0, 0);
			if( SendMessage( hwndChild, WM_QUERYENDSESSION, 0, 0 ) )
				SendMessage(hClient, WM_MDIDESTROY, (WPARAM)hwndChild, 0);
#endif
                    return 0;
                case IDM_WINDOW_TILE :
                    EnumChildWindows( hClient, RestoreEnumProc, 0 );
                    SendMessage( hClient, WM_MDITILE, 0, 0 );
                    return 0;
                case IDM_WINDOW_CASCADE :
                    SendMessage( hClient, WM_MDICASCADE, 0, 0 );
                    return 0;
                case IDM_WINDOW_ARRANGE :
                    SendMessage( hClient, WM_MDIICONARRANGE, 0, 0 );
                    return 0;
                case IDM_WINDOW_PREV :
                    hwndChild = (HWND)SendMessage(hClient, WM_MDIGETACTIVE, 0, 0);
                    SendMessage(hClient, WM_MDINEXT, (WPARAM)hwndChild, 1 );
                    return 0;
                case IDM_WINDOW_NEXT :
                    hwndChild = (HWND)SendMessage(hClient, WM_MDIGETACTIVE, 0, 0);
                    SendMessage(hClient, WM_MDINEXT, (WPARAM)hwndChild, 0 );
                    return 0;
                case IDM_WINDOW_CLOSEALL :
                    EnumChildWindows(hClient, CloseEnumProc, 0);
                    return 0;
                default :
			if(LOWORD(wParam) < IDM_FIRSTCHILD) {
				// wkliang: 20090101 - DefFrameProc() will not hand over WM_COMMAND!?
				hwndChild = (HWND)SendMessage(hClient, WM_MDIGETACTIVE, 0, 0);
				if( IsWindow(hwndChild) )
					SendMessage(hwndChild, WM_COMMAND, wParam, lParam);
			}
                    break;
            }
            break;
        }
        case WM_QUERYENDSESSION :
		SendMessage(hwnd, WM_COMMAND, IDM_WINDOW_CLOSEALL, 0);
		if( GetWindow(hClient, GW_CHILD) != NULL )
			return 0;
		DestroyWindow(hwnd);
		return 0;
	case WM_CLOSE :
		iShowMode = FALSE;
		ShowWindow(hwnd, SW_HIDE);
		return 0;
	case WM_SHOWWINDOW:
	case WM_ACTIVATE:
    		LogAppend( hLogWnd, "%s(%d) msg=%d, w=%04X, l=%08lX",
			__FILE__, __LINE__, msg, wParam, lParam );
		LogUpdate( hLogWnd );
//		if(HIWORD(wParam) && LOWORD(wParam)==0x00) {
//			iShowMode = FALSE;
//			ShowWindow(hwnd, SW_HIDE);
//		}
		break;
        case WM_DESTROY :
            PostQuitMessage( 0 );
            return 0;
    }
    return DefFrameProc(hwnd, hClient, msg, wParam, lParam);
}

#define	SHTTPD_ID_TIMER	22

static BOOL CALLBACK
dlgShttpdProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
extern void example_init(struct shttpd_ctx*);
	static struct shttpd_ctx *ctx;
	static char *argv[] = {"", "-ports", "8080", NULL};
	int argc = sizeof(argv) / sizeof(argv[0]) - 1;

	switch (msg) {
	case WM_CLOSE:
		shttpd_fini(ctx);
		KillTimer(hDlg, SHTTPD_ID_TIMER);
		DestroyWindow(hDlg);
		break;

	case WM_TIMER:
		shttpd_poll(ctx, 0);
		break;

	case WM_INITDIALOG:
		SetTimer(hDlg, SHTTPD_ID_TIMER, 250, NULL);	// poll every 250 ms
		ctx = shttpd_init(argc, argv);
		example_init(ctx);
		ShellExecute(hDlg, "open", "http://127.0.0.1:8080/", "", "", SW_SHOW);
		break;
	default:
		break;
	}
	return FALSE;
}


const char appSystray[] = TEXT("appSystray");
LRESULT CALLBACK appSystrayProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam )
{
#define ID_GROUP	100
#define ID_SAVE		101
#define	ID_STATUS	102
#define	ID_STATIC	103
#define	ID_SHTTPD	104
#define	ID_QUIT		105
#define	ID_TRAYICON	106
#define	ID_TIMER	107
#define	ID_ICON		108
#define	ID_ADVANCED	109
#define	ID_SHOWLOG	110
#define	ID_LOG		111
#define ID_APP_SHOW	112
#define ID_APP_HIDE	113
#define ID_EXEC_NOTEPAD	120
#define ID_EXEC_GOOGLE	121
#define TRAY_TIMER	130

	static	NOTIFYICONDATA	ni = {0};

	switch (msg) {
	case WM_CREATE: {
//		memset(&ni, 0, sizeof(ni));
		ni.cbSize = sizeof(ni);
		ni.uID = ID_TRAYICON;
		ni.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
		ni.hIcon = hIcon;
		ni.hWnd = hwnd;
		strncpy(ni.szTip, szAppTitle, sizeof(ni.szTip));
		ni.uCallbackMessage = WM_USER;
		Shell_NotifyIcon(NIM_ADD, &ni);
#if 0
		ctx->ev[0] = CreateEvent(0, TRUE, FALSE, 0);
		ctx->ev[1] = CreateEvent(0, TRUE, FALSE, 0);
		_beginthread(run_server, 0, ctx);
#endif
		SetTimer(hwnd, TRAY_TIMER, 60*1000, NULL);
		break;
	}
	case WM_TIMER:
		ni.uFlags = NIF_INFO;
		ni.uTimeout = 1000;
		ni.dwInfoFlags = NIIF_INFO; // _NONE, _ERROR, _WARNING
		sprintf(ni.szInfoTitle, "Hello, Ballon!");
		sprintf(ni.szInfo, "Hello, World!");
		Shell_NotifyIcon(NIM_MODIFY, &ni);
		return 0;
	case WM_CLOSE:
		KillTimer(hwnd, TRAY_TIMER);
		Shell_NotifyIcon(NIM_DELETE, &ni);
		PostQuitMessage(0); // wkliang:20090312 - terminate msg loop
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case ID_EXEC_NOTEPAD :
			ShellExecute(hwnd, "open", "notepad.exe", "", "", SW_SHOW);
			return 0;
		case ID_EXEC_GOOGLE :
			ShellExecute(hwnd, "open", "http://www.google.com/", "", "", SW_SHOW);
			return 0;

		case ID_SHTTPD :
			return DialogBox(hInst, MAKEINTRESOURCE(IDI_MyDLG), NULL, dlgShttpdProc);
		case ID_QUIT :
			SendMessage(hwnd, WM_CLOSE, wParam, lParam);
			// wkliang:20090312 - Let WM_CLOSE post it
			// PostQuitMessage(0);
			return 0;
		case ID_SHOWLOG :
			ShowWindow(hLogWnd, SW_RESTORE);
			break;
		case ID_APP_HIDE:
			iShowMode = FALSE;
			ShowWindow(hMainWnd, SW_HIDE);
			break;
		case ID_APP_SHOW:
			iShowMode = TRUE;
			// ShowWindow(hMainWnd, SW_RESTORE);
			// ShowWindow(hMainWnd, SW_RESTORE);
			ShowWindow(hMainWnd, SW_SHOW);
			// SetActiveWindow(hMainWnd);
			// show_log_window(ctx);
			break;
		}
		break;
	case WM_USER: {
		switch (lParam) {
		case WM_LBUTTONUP:
		case WM_LBUTTONDBLCLK:
    			SendMessage(hwnd, WM_COMMAND,
				iShowMode ? ID_APP_HIDE : ID_APP_SHOW, 0L);
			break;
		case WM_RBUTTONUP: {
			HMENU hMenu;
			POINT pt;
			hMenu = CreatePopupMenu();
			AppendMenu(hMenu, 0, ID_EXEC_NOTEPAD, "Notepad");
			AppendMenu(hMenu, 0, ID_EXEC_GOOGLE, "Google");
			AppendMenu(hMenu, 0, ID_SHTTPD, "sHTTPd");
			AppendMenu(hMenu, 0, ID_SHOWLOG, "Show Log");
			if(iShowMode)
				AppendMenu(hMenu, 0, ID_APP_HIDE, "Hide Application");
			else
				AppendMenu(hMenu, 0, ID_APP_SHOW, "Show Application");
			AppendMenu(hMenu, 0, ID_QUIT, "Exit Appliction");
			GetCursorPos(&pt);
			TrackPopupMenu(hMenu, 0, pt.x, pt.y, 0, hwnd, NULL);
			DestroyMenu(hMenu);
			break;
		}
		}
		break;
	}
	}
	return DefWindowProc( hwnd, msg, wParam, lParam );
}

int WINAPI WinMain( HINSTANCE hThis, HINSTANCE hPrev, PSTR szCmdLine, int iCmdShow )
{
	HMODULE hIcmpDll;
	HACCEL hAccel;
	HWND hwnd, hwndClient;
	MSG msg;
	WNDCLASSEX wndclass = {0};

	hInst = hThis;	// == GetModuleHandle(NULL)
	iShowMode = iCmdShow;

	GetModuleFileName(hThis, szAppTitle, sizeof(szAppTitle));
	hIcon = LoadIcon(hThis/*GetModuleHandle(NULL)*/, MAKEINTRESOURCE(IDI_MyICO));
	if( hIcon == NULL )
		hIcon = LoadIcon( NULL, IDI_APPLICATION );

	hSem = CreateSemaphore( NULL, 1, 1, NULL );

	WSAStartup( MAKEWORD(1,1), &wsaData );

	hIcmpDll = LoadLibrary( "ICMP.DLL" );
	pIcmpCreateFile = (procIcmpCreateFile)GetProcAddress( hIcmpDll, "IcmpCreateFile" );
	pIcmpCloseHandle = (procIcmpCloseHandle)GetProcAddress( hIcmpDll, "IcmpCloseHandle" );
	pIcmpSendEcho = (procIcmpSendEcho)GetProcAddress( hIcmpDll, "IcmpSendEcho" );

	// (void)memset(&wndclass, 0, sizeof(wndclass));
	wndclass.cbSize = sizeof(wndclass);
	wndclass.style = 0;
	wndclass.cbClsExtra = 0;
	// wndclass.cbWndExtra = 0;
	wndclass.cbWndExtra = sizeof(void *);
	wndclass.hInstance = hInst;
//	wndclass.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wndclass.hbrBackground = (HBRUSH)GetSysColorBrush(COLOR_BTNFACE);
	wndclass.hCursor = LoadCursor( NULL, IDC_ARROW );
	wndclass.lpszMenuName = NULL;
	wndclass.hIcon = hIcon;
	wndclass.hIconSm = hIcon;

	wndclass.lpszClassName = widgetPanel;
	wndclass.lpfnWndProc = widgetPanelProc;
	RegisterClassEx(&wndclass);

	wndclass.lpszClassName = widgetBurning;
	wndclass.lpfnWndProc = widgetBurningProc;
	RegisterClassEx(&wndclass);

	wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);

	wndclass.lpszClassName = mdiHello;
	wndclass.lpfnWndProc = mdiHelloProc;
	RegisterClassEx( &wndclass );

	wndclass.lpszClassName = mdiRect;
	wndclass.lpfnWndProc = mdiRectProc;
	RegisterClassEx( &wndclass );

	wndclass.lpszClassName = mdiGDIDemo;
	wndclass.lpfnWndProc = mdiGDIDemoProc;
	RegisterClassEx(&wndclass);

	wndclass.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
	wndclass.lpszClassName = appFrame;
	wndclass.lpfnWndProc = appFrameProc;
	RegisterClassEx(&wndclass);

	wndclass.hbrBackground = (HBRUSH)GetSysColorBrush(COLOR_3DFACE);
	wndclass.lpszClassName = appDialog;
	wndclass.lpfnWndProc = appDialogProc;
	RegisterClassEx(&wndclass);

	wndclass.lpszClassName = appDialog1;
	wndclass.lpfnWndProc = appDialogProc1;
	RegisterClassEx(&wndclass);

	wndclass.lpszClassName = appSystray;
	wndclass.lpfnWndProc = appSystrayProc;
	RegisterClassEx(&wndclass);

	wndclass.style = CS_HREDRAW | CS_VREDRAW;
	wndclass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wndclass.lpszClassName = appLog;
	wndclass.lpfnWndProc = appLogProc;
	RegisterClassEx(&wndclass);

	hMenuInit = LoadMenu( hInst, "MdiMenuInit" );
	hMenuHello = LoadMenu( hInst, "MdiMenuHello" );
	hMenuWindow = LoadMenu( hInst, "MdiMenuWindow" );

	hMenuInitWindow = GetSubMenu( hMenuInit, INIT_MENU_POS );
	hMenuWindowWindow = GetSubMenu( hMenuWindow, WINDOW_MENU_POS );
	hMenuHelloWindow = GetSubMenu( hMenuHello, HELLO_MENU_POS );

	hAccel = LoadAccelerators( hInst, "myAccel" );

	/* activate the common controls */
	{
		INITCOMMONCONTROLSEX iccex;

		iccex.dwSize = sizeof(iccex);

		iccex.dwICC = ICC_WIN95_CLASSES;	// for tooltips
		InitCommonControlsEx(&iccex);
		
		iccex.dwICC = ICC_TAB_CLASSES;
		InitCommonControlsEx(&iccex);

		iccex.dwICC = ICC_BAR_CLASSES;
		InitCommonControlsEx(&iccex);

		iccex.dwICC = ICC_DATE_CLASSES;
		InitCommonControlsEx(&iccex);

		iccex.dwICC = ICC_PROGRESS_CLASS;
		InitCommonControlsEx(&iccex);
	}

	hwnd = hLogWnd = CreateWindow(appLog, szAppTitle,
		WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, hMenuInit, hInst, NULL );
	ShowWindow(hwnd, SW_HIDE);
	UpdateWindow( hwnd );
	iMainWinCount++;

	hwnd = CreateWindow(appSystray, szAppTitle,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, NULL, hInst, NULL );
	// ShowWindow( hwnd, iShowMode );
	// UpdateWindow( hwnd );
	iMainWinCount++;

	hwnd = hMainWnd = CreateWindow(appFrame, szAppTitle,
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL, hMenuInit, hInst, NULL );
	ShowWindow( hwnd, iCmdShow );
	UpdateWindow( hwnd );
	iMainWinCount++;

	hwndClient = GetWindow( hwnd, GW_CHILD );

	while(GetMessage( &msg, NULL, 0, 0 ) > 0) {
		if( TranslateMDISysAccel(hwndClient, &msg) )
			continue;
		if( TranslateAccelerator(hwnd, hAccel, &msg) )
			continue;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	DestroyMenu( hMenuHello );
	DestroyMenu( hMenuWindow );

	FreeLibrary( hIcmpDll );
	WSACleanup();

	return msg.wParam;
}

