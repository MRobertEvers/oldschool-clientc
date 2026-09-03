/* A parse-only stand-in for windows.h. Enough of the API surface that
 * torirs_chrome_exec_gdi.c can be syntax-checked on a machine with no Windows
 * SDK. It proves the C parses and that our own calls agree with our own
 * declarations -- it does NOT prove the Win32 calls are correct. */
#ifndef WINSTUB_H
#define WINSTUB_H
#include <stdint.h>
typedef void* HWND; typedef void* HDC; typedef void* HFONT; typedef void* HBRUSH;
typedef void* HMENU; typedef void* HINSTANCE; typedef void* HDWP; typedef void* HCURSOR;
typedef void* HICON; typedef void* HBITMAP; typedef void* HGDIOBJ; typedef void* HANDLE;
typedef void* HMODULE; typedef void (*FARPROC)(void);
typedef unsigned UINT; typedef unsigned long DWORD; typedef unsigned long ULONG; typedef unsigned short WORD;
typedef uintptr_t WPARAM; typedef intptr_t LPARAM; typedef intptr_t LRESULT;
typedef intptr_t INT_PTR; typedef intptr_t LONG_PTR; typedef uintptr_t ULONG_PTR; typedef int BOOL; typedef char CHAR; typedef long LONG;
#define CALLBACK
#define WINAPI
#define TRUE 1
#define FALSE 0
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate; unsigned char rgbReserved[32]; }
  PAINTSTRUCT;
typedef struct {
  UINT cbSize, fMask; int nMin, nMax; UINT nPage; int nPos, nTrackPos;
} SCROLLINFO;
typedef struct {
  DWORD biSize; LONG biWidth, biHeight; WORD biPlanes, biBitCount;
  DWORD biCompression, biSizeImage; LONG biXPelsPerMeter, biYPelsPerMeter;
  DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER;
typedef struct { DWORD dummy; } RGBQUAD;
typedef struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO;
typedef struct {
  DWORD bV5Size; LONG bV5Width, bV5Height; WORD bV5Planes, bV5BitCount;
  DWORD bV5Compression, bV5SizeImage; LONG bV5XPelsPerMeter, bV5YPelsPerMeter;
  DWORD bV5ClrUsed, bV5ClrImportant, bV5RedMask, bV5GreenMask, bV5BlueMask, bV5AlphaMask;
} BITMAPV5HEADER;
typedef struct { BOOL fIcon; DWORD xHotspot, yHotspot; HBITMAP hbmMask, hbmColor; } ICONINFO;
typedef struct { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG;
/* WM_DRAWITEM's payload: what an owner-drawn control is handed instead of a
 * default paint. `hDC` and `rcItem` are the whole of what the drawing below
 * needs; `itemState` carries the pressed bit a button reads. */
typedef struct {
  UINT CtlType, CtlID; UINT itemID, itemAction, itemState;
  HWND hwndItem; HDC hDC; RECT rcItem; ULONG_PTR itemData;
} DRAWITEMSTRUCT;
typedef struct { UINT CtlType, CtlID, itemID; UINT itemWidth, itemHeight; ULONG_PTR itemData; }
  MEASUREITEMSTRUCT;
typedef struct { LONG lfHeight; char lfFaceName[32]; } LOGFONTA;
typedef struct { UINT cbSize; LOGFONTA lfMessageFont; } NONCLIENTMETRICSA;
typedef struct {
  UINT style; LRESULT (CALLBACK *lpfnWndProc)(HWND,UINT,WPARAM,LPARAM);
  int cbClsExtra, cbWndExtra; HINSTANCE hInstance; HICON hIcon; HCURSOR hCursor;
  HBRUSH hbrBackground; const char* lpszMenuName; const char* lpszClassName;
} WNDCLASSA;
typedef LRESULT (CALLBACK *WNDPROC)(HWND,UINT,WPARAM,LPARAM);
#define LOWORD(x) ((int)((x)&0xFFFF))
#define HIWORD(x) ((int)(((x)>>16)&0xFFFF))
#define WS_CHILD 1
#define WS_BORDER 2
#define WS_VSCROLL 4
#define WS_OVERLAPPED 8
#define WS_CAPTION 16
#define WS_SYSMENU 32
#define WS_THICKFRAME 64
#define WS_EX_TOOLWINDOW 128
#define WS_EX_COMPOSITED 256
#define WS_POPUP 256
#define WS_CLIPCHILDREN 512
#define WS_CLIPSIBLINGS 1024
#define WS_MAXIMIZE 2048
#define WS_OVERLAPPEDWINDOW 4096
#define BS_AUTOCHECKBOX 1
#define BS_PUSHBUTTON 2
#define ES_AUTOHSCROLL 4
/* The multiline EDIT's three, for the chrome's TEXTAREA row. Values are
 * arbitrary here -- this stub proves the C parses and that our calls match our
 * own declarations, never that a Win32 constant is the right number. */
#define ES_MULTILINE 1024
#define ES_AUTOVSCROLL 2048
#define ES_WANTRETURN 4096
#define SS_LEFT 8
#define SS_ETCHEDHORZ 16
#define SS_NOTIFY 256
#define SS_OWNERDRAW 512
#define CBS_DROPDOWNLIST 32
#define CW_USEDEFAULT 0
#define SW_SHOWNOACTIVATE 1
#define SWP_NOZORDER 1
#define SWP_SHOWWINDOW 2
#define SWP_HIDEWINDOW 4
#define SWP_NOMOVE 8
#define SWP_NOSIZE 16
#define SWP_NOREDRAW 32
#define SWP_NOACTIVATE 64
#define WM_COMMAND 1
#define WM_SIZE 2
#define WM_CLOSE 3
#define WM_SETFONT 4
#define WM_CTLCOLORSTATIC 5
#define WM_CTLCOLORBTN 6
#define BM_GETCHECK 7
#define BM_SETCHECK 8
#define BST_CHECKED 1
#define BST_UNCHECKED 0
#define BN_CLICKED 1
#define EN_KILLFOCUS 2
#define EN_SETFOCUS 4
#define STN_CLICKED 0
#define CBN_SELCHANGE 3
#define CBN_DROPDOWN 7
#define CBN_CLOSEUP 8
#define CB_RESETCONTENT 10
#define CB_ADDSTRING 11
#define CB_SETCURSEL 12
#define CB_GETCURSEL 13
#define CB_GETLBTEXT 14
#define CB_SHOWDROPDOWN 15
#define CB_SETITEMHEIGHT 16
#define CB_GETITEMHEIGHT 17
#define COLOR_BTNFACE 15
#define TRANSPARENT 1
#define IDC_ARROW ((const char*)32512)
#define ERROR_CLASS_ALREADY_EXISTS 1410
#define SPI_GETNONCLIENTMETRICS 41
#define BS_OWNERDRAW 0x0B
#define CBS_OWNERDRAWFIXED 0x10
#define CBS_HASSTRINGS 0x0200
#define WM_DRAWITEM 20
#define WM_MEASUREITEM 21
#define WM_ERASEBKGND 22
#define WM_CTLCOLOREDIT 24
#define WM_CTLCOLORLISTBOX 25
#define WM_NCHITTEST 26
#define WM_VSCROLL 27
#define WM_MOUSEWHEEL 28
#define WM_LBUTTONDOWN 29
#define WM_LBUTTONUP 30
#define WM_MOUSEMOVE 31
#define WM_CAPTURECHANGED 32
#define WM_PAINT 33
#define WM_SETREDRAW 34
#define WM_PRINTCLIENT 35
#define WM_GETDLGCODE 36
#define WM_CHAR 37
#define WM_KEYDOWN 38
#define WM_KEYUP 39
#define WM_SYSKEYDOWN 40
#define WM_SYSKEYUP 41
#define WM_KILLFOCUS 42
#define WM_TOUCH 43
#define WM_DESTROY 44
#define WM_RBUTTONDOWN 45
#define WM_RBUTTONUP 46
#define WM_MBUTTONDOWN 47
#define WM_MBUTTONUP 48
#define GWLP_WNDPROC (-4)
#define GWLP_USERDATA (-21)
#define GWL_STYLE (-16)
#define HTCLIENT 1
#define HTCAPTION 2
#define DLGC_WANTALLKEYS 4
#define DLGC_WANTCHARS 8
#define ODS_SELECTED 0x0001
#define ODS_COMBOBOXEDIT 0x1000
#define ODT_COMBOBOX 3
#define SB_VERT 1
#define SB_LINEUP 0
#define SB_LINEDOWN 1
#define SB_PAGEUP 2
#define SB_PAGEDOWN 3
#define SB_THUMBPOSITION 4
#define SB_THUMBTRACK 5
#define SB_TOP 6
#define SB_BOTTOM 7
#define SIF_RANGE 1
#define SIF_PAGE 2
#define SIF_POS 4
#define SIF_TRACKPOS 16
#define SIF_ALL (SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS)
#define WHEEL_DELTA 120
#define GET_WHEEL_DELTA_WPARAM(wp) ((short)HIWORD(wp))
#define SRCCOPY 0x00CC0020
#define BI_RGB 0
#define BI_BITFIELDS 3
#define DIB_RGB_COLORS 0
#define DT_LEFT 0
#define DT_CENTER 0x01
#define DT_VCENTER 0x04
#define DT_SINGLELINE 0x20
#define DT_END_ELLIPSIS 0x8000
#define NULL_BRUSH 5
#define BLACK_BRUSH 4
#define DEFAULT_GUI_FONT 17
#define SIZE_MINIMIZED 1
#define CS_OWNDC 0x20
#define CS_HREDRAW 0x02
#define CS_VREDRAW 0x01
#define WM_QUIT 0x12
#define GCLP_HBRBACKGROUND (-10)
#define GCL_STYLE (-26)
#define PM_REMOVE 1
#define SW_HIDE 0
#define SW_SHOW 5
#define WS_VISIBLE 0x10000000
#define MK_LBUTTON 1
#define VK_ESCAPE 27
#define VK_RETURN 13
#define VK_BACK 8
#define VK_INSERT 45
#define VK_DELETE 46
#define VK_SHIFT 16
#define VK_LSHIFT 160
#define VK_RSHIFT 161
#define VK_CONTROL 17
#define VK_LCONTROL 162
#define VK_RCONTROL 163
#define VK_MENU 18
#define VK_TAB 9
#define VK_SPACE 32
#define VK_LEFT 37
#define VK_UP 38
#define VK_RIGHT 39
#define VK_DOWN 40
#define VK_HOME 36
#define VK_END 35
#define VK_F1 112
#define VK_F12 123
#define VK_PRIOR 33
#define VK_NEXT 34
#define VK_OEM_COMMA 188
#define XBUTTON1 1
#define XBUTTON2 2
#define GET_XBUTTON_WPARAM(wp) HIWORD(wp)
#define HGDI_ERROR ((HGDIOBJ)(intptr_t)-1)
#define COLORONCOLOR 3
#define HALFTONE 4
#define RDW_INVALIDATE 1
#define RDW_ALLCHILDREN 0x80
HWND CreateWindowExA(DWORD,const char*,const char*,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,void*);
BOOL DestroyWindow(HWND); BOOL ShowWindow(HWND,int);
LRESULT SendMessageA(HWND,UINT,WPARAM,LPARAM);
LRESULT DefWindowProcA(HWND,UINT,WPARAM,LPARAM);
BOOL RegisterClassA(const WNDCLASSA*); DWORD GetLastError(void);
LONG_PTR SetWindowLongPtrA(HWND,int,LONG_PTR);
LONG_PTR GetWindowLongPtrA(HWND,int);
#define SetWindowLongPtr SetWindowLongPtrA
#define GetWindowLongPtr GetWindowLongPtrA
HINSTANCE GetModuleHandleA(const char*); HCURSOR LoadCursor(HINSTANCE,const char*);
#define GetModuleHandle GetModuleHandleA
FARPROC GetProcAddress(HMODULE,const char*);
BOOL GetClientRect(HWND,RECT*); HDWP BeginDeferWindowPos(int);
HDWP DeferWindowPos(HDWP,HWND,HWND,int,int,int,int,UINT); BOOL EndDeferWindowPos(HDWP);
BOOL SetWindowTextA(HWND,const char*); int GetWindowTextA(HWND,char*,int);
HWND GetFocus(void); HFONT CreateFontIndirectA(const LOGFONTA*); BOOL DeleteObject(void*);
BOOL SystemParametersInfoA(UINT,UINT,void*,UINT); int SetBkMode(HDC,int);
HBRUSH GetSysColorBrush(int);
typedef DWORD COLORREF;
#define RGB(r, g, b) ((COLORREF)(((DWORD)(r)) | (((DWORD)(g)) << 8) | (((DWORD)(b)) << 16)))
HBRUSH CreateSolidBrush(COLORREF);
BOOL InvalidateRect(HWND, const RECT*, BOOL);
HWND SetFocus(HWND);
HDC GetDC(HWND); int ReleaseDC(HWND,HDC); BOOL UpdateWindow(HWND);
BOOL AdjustWindowRect(RECT*,DWORD,BOOL); BOOL SetWindowTextA(HWND,const char*);
HBITMAP CreateBitmap(int,int,UINT,UINT,const void*); HICON CreateIconIndirect(ICONINFO*);
LRESULT DefWindowProc(HWND,UINT,WPARAM,LPARAM);
BOOL PeekMessage(MSG*,HWND,UINT,UINT,UINT); BOOL TranslateMessage(const MSG*); LRESULT DispatchMessage(const MSG*);
short GetKeyState(int); DWORD GetTickCount(void);
BOOL SetEnvironmentVariableA(const char*,const char*);
ULONG_PTR GetClassLongPtrA(HWND,int); HWND GetParent(HWND);
/* GDI32, for the owner-drawn half. No msimg32: the alpha compositing is done
 * in software, which is why AlphaBlend is absent from this list. */
HDC CreateCompatibleDC(HDC);
HDC BeginPaint(HWND, PAINTSTRUCT*);
BOOL EndPaint(HWND, const PAINTSTRUCT*);
HBITMAP CreateDIBSection(HDC, const BITMAPINFO*, UINT, void**, void*, DWORD);
HGDIOBJ SelectObject(HDC, HGDIOBJ);
BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
BOOL StretchBlt(HDC,int,int,int,int,HDC,int,int,int,int,DWORD);
int SetStretchBltMode(HDC,int); BOOL SetBrushOrgEx(HDC,int,int,POINT*);
BOOL DeleteDC(HDC);
int FillRect(HDC, const RECT*, HBRUSH);
HBRUSH CreatePatternBrush(HBITMAP);
HGDIOBJ GetStockObject(int);
COLORREF SetTextColor(HDC, COLORREF);
int DrawTextA(HDC, const char*, int, RECT*, UINT);
BOOL GetWindowRect(HWND, RECT*);
BOOL ScreenToClient(HWND, POINT*);
BOOL GetCursorPos(POINT*);
BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT);
BOOL RedrawWindow(HWND, const RECT*, void*, UINT);
int SetScrollInfo(HWND, int, const SCROLLINFO*, BOOL);
BOOL GetScrollInfo(HWND, int, SCROLLINFO*);
HWND SetCapture(HWND);
BOOL ReleaseCapture(void);
HWND GetCapture(void);
#endif
