/* A parse-only stand-in for windows.h. Enough of the API surface that
 * torirs_chrome_exec_gdi.c can be syntax-checked on a machine with no Windows
 * SDK. It proves the C parses and that our own calls agree with our own
 * declarations -- it does NOT prove the Win32 calls are correct. */
#ifndef WINSTUB_H
#define WINSTUB_H
#include <stdint.h>
typedef void* HWND; typedef void* HDC; typedef void* HFONT; typedef void* HBRUSH;
typedef void* HMENU; typedef void* HINSTANCE; typedef void* HDWP; typedef void* HCURSOR;
typedef void* HICON; typedef void* HBITMAP; typedef void* HGDIOBJ;
typedef unsigned UINT; typedef unsigned long DWORD; typedef unsigned short WORD;
typedef uintptr_t WPARAM; typedef intptr_t LPARAM; typedef intptr_t LRESULT;
typedef intptr_t INT_PTR; typedef uintptr_t ULONG_PTR; typedef int BOOL; typedef char CHAR; typedef long LONG;
#define CALLBACK
#define TRUE 1
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
typedef struct {
  DWORD biSize; LONG biWidth, biHeight; WORD biPlanes, biBitCount;
  DWORD biCompression, biSizeImage; LONG biXPelsPerMeter, biYPelsPerMeter;
  DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER;
typedef struct { DWORD dummy; } RGBQUAD;
typedef struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO;
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
#define BS_AUTOCHECKBOX 1
#define BS_PUSHBUTTON 2
#define ES_AUTOHSCROLL 4
#define SS_LEFT 8
#define SS_ETCHEDHORZ 16
#define SS_NOTIFY 256
#define CBS_DROPDOWNLIST 32
#define CW_USEDEFAULT 0
#define SW_SHOWNOACTIVATE 1
#define SWP_NOZORDER 1
#define SWP_SHOWWINDOW 2
#define SWP_HIDEWINDOW 4
#define SWP_NOMOVE 8
#define SWP_NOSIZE 16
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
#define CBN_SELCHANGE 3
#define CB_RESETCONTENT 10
#define CB_ADDSTRING 11
#define CB_SETCURSEL 12
#define CB_GETCURSEL 13
#define CB_GETLBTEXT 14
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
#define ODS_SELECTED 0x0001
#define ODS_COMBOBOXEDIT 0x1000
#define ODT_COMBOBOX 3
#define SRCCOPY 0x00CC0020
#define BI_RGB 0
#define DIB_RGB_COLORS 0
#define DT_LEFT 0
#define DT_CENTER 0x01
#define DT_VCENTER 0x04
#define DT_SINGLELINE 0x20
#define DT_END_ELLIPSIS 0x8000
#define NULL_BRUSH 5
#define RDW_INVALIDATE 1
#define RDW_ALLCHILDREN 0x80
HWND CreateWindowExA(DWORD,const char*,const char*,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,void*);
BOOL DestroyWindow(HWND); BOOL ShowWindow(HWND,int);
LRESULT SendMessageA(HWND,UINT,WPARAM,LPARAM);
LRESULT DefWindowProcA(HWND,UINT,WPARAM,LPARAM);
BOOL RegisterClassA(const WNDCLASSA*); DWORD GetLastError(void);
HINSTANCE GetModuleHandleA(const char*); HCURSOR LoadCursor(HINSTANCE,const char*);
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
/* GDI32, for the owner-drawn half. No msimg32: the alpha compositing is done
 * in software, which is why AlphaBlend is absent from this list. */
HDC CreateCompatibleDC(HDC);
HBITMAP CreateDIBSection(HDC, const BITMAPINFO*, UINT, void**, void*, DWORD);
HGDIOBJ SelectObject(HDC, HGDIOBJ);
BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
BOOL DeleteDC(HDC);
int FillRect(HDC, const RECT*, HBRUSH);
HBRUSH CreatePatternBrush(HBITMAP);
HGDIOBJ GetStockObject(int);
COLORREF SetTextColor(HDC, COLORREF);
int DrawTextA(HDC, const char*, int, RECT*, UINT);
BOOL GetWindowRect(HWND, RECT*);
BOOL ScreenToClient(HWND, POINT*);
BOOL RedrawWindow(HWND, const RECT*, void*, UINT);
#endif
