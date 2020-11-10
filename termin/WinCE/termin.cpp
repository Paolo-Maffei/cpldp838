/*
    project        : termin, terminal soft for CPLDP-8
    author         : omiokone
    termin.cpp     : termin main
*/

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include "resource.h"

#define FONTFACE TEXT("Courier New")

const DWORD send_timeout    =10;        // transmit timeout (ms)
const int   string_size     =100;

#ifdef STRICT
#define PROCTYPE WNDPROC
#else
#define PROCTYPE FARPROC
#endif

#ifndef _tcstoul
#define _tcstoul(s, t, u) strtoul(s, t, u)
#endif

static HWND             main_window =NULL;
static HWND             main_edit   =NULL;
static HINSTANCE        instance    =NULL;
static HWND             cmdbar      =NULL;
static HANDLE           com         =NULL;
static HFONT            font        =NULL;
static TCHAR            home_dir[MAX_PATH];
static TCHAR            fontface[MAX_PATH];
static DWORD            char_interval;  // interval between text characters (ms)
static WNDPROC          orgproc;
static HANDLE           rfile, tfile;
static unsigned char    tch;            // character to be sent
static int              tch_exist;      // TRUE: character to be sent exists
static int              cursor;         // TRUE: cursor is shown on edit control
static int              rmode;          // 0:normal, 1:save text, 2:save raw
static int              tmode;          // 0:normal, 1:send text, 2:send raw
                                        // 3:send binary (leader)
                                        // 4:send binary (body)

LRESULT CALLBACK EditWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg){                       // inhibit focus on edit control
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:    return 0;
    }
    return CallWindowProc((PROCTYPE)orgproc, hWnd, uMsg, wParam, lParam);
}

void error(int errno)
{
    TCHAR caption[string_size];
    TCHAR text   [string_size];

    LoadString(instance, IDS_ERR_CAPTION, caption, string_size);
    LoadString(instance, errno,           text,    string_size);
    MessageBox(main_window, text, caption, MB_OK);
}

static HANDLE select_file(int write)
{
    TCHAR           filename[MAX_PATH];
    OPENFILENAME    fs;
    HANDLE          file;
    BOOL            r;

    filename[0]=NULL;
    memset((void *)&fs, 0, sizeof(OPENFILENAME));
    fs.lStructSize      =sizeof(OPENFILENAME);
    fs.hwndOwner        =main_window;
    fs.lpstrFile        =filename;
    fs.nMaxFile         =MAX_PATH;
    fs.lpstrInitialDir  =home_dir;
    fs.Flags            =OFN_PATHMUSTEXIST;
    if(write){
        r=GetSaveFileName(&fs);
    }else{
        fs.Flags|=OFN_FILEMUSTEXIST;
        r=GetOpenFileName(&fs);
    }
    UpdateWindow(main_window);          // for WindowsCE
    if(!r) return INVALID_HANDLE_VALUE;
    file=CreateFile(filename, write?GENERIC_WRITE:GENERIC_READ, 0, NULL,
                    write?CREATE_ALWAYS:OPEN_EXISTING, 0, NULL);
    if(file==INVALID_HANDLE_VALUE) error(IDS_ERR_FILE);
    return file;
}

static void change_menu(
    int     receive,                    // I 0:send 1:receive
    int     open                        // I 0:gray 1:enable [End] item 
    )
{
    HANDLE      menu;
    UINT        enable, disable;

#ifdef UNDER_CE
    menu=CommandBar_GetMenu(cmdbar, 0);
    disable=MF_BYCOMMAND|MF_GRAYED;
#else
    menu=GetMenu(main_window);
    disable=MF_BYCOMMAND|MF_GRAYED|MF_DISABLED;
#endif
    enable=MF_BYCOMMAND|MF_ENABLED;
    if(receive){
        if(open){
            EnableMenuItem(menu, IDM_RECV_RAW, disable);
            EnableMenuItem(menu, IDM_RECV_TXT, disable);
            EnableMenuItem(menu, IDM_RECV_END, enable);
        }else{
            EnableMenuItem(menu, IDM_RECV_RAW, enable);
            EnableMenuItem(menu, IDM_RECV_TXT, enable);
            EnableMenuItem(menu, IDM_RECV_END, disable);
        }
    }else{
        if(open){
            EnableMenuItem(menu, IDM_SEND_RAW, disable);
            EnableMenuItem(menu, IDM_SEND_BIN, disable);
            EnableMenuItem(menu, IDM_SEND_TXT, disable);
            EnableMenuItem(menu, IDM_SEND_END, enable);
        }else{
            EnableMenuItem(menu, IDM_SEND_RAW, enable);
            EnableMenuItem(menu, IDM_SEND_BIN, enable);
            EnableMenuItem(menu, IDM_SEND_TXT, enable);
            EnableMenuItem(menu, IDM_SEND_END, disable);
        }
    }
}

static LOGFONT lfont={
    0, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
    DEFAULT_PITCH, FONTFACE};

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    RECT        r;
    HDC         dc;
    TEXTMETRIC  tm;
    int         h;

    switch(uMsg){
    case WM_KEYDOWN:
        if(wParam==VK_DELETE){
            if(!tch_exist){
                tch=0x7f; tch_exist=TRUE;
            }
            return 0;
        }
        break;
    case WM_CHAR:
        if(!tch_exist){
            tch=(unsigned char)wParam; tch_exist=TRUE;
        }
        return 0;
    case WM_COMMAND:
        UpdateWindow(main_window);      // for WindowsCE
        switch(LOWORD(wParam)){
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        case IDM_SEND_RAW:
            if(tmode) break;
            tfile=select_file(0);
            if(tfile!=INVALID_HANDLE_VALUE){
                tmode=2; change_menu(0, 1);
            }
            break;
        case IDM_SEND_BIN:
            if(tmode) break;
            tfile=select_file(0);
            if(tfile!=INVALID_HANDLE_VALUE){
                tmode=3; change_menu(0, 1);
            }
            break;
        case IDM_SEND_TXT:
            if(tmode) break;
            tfile=select_file(0);
            if(tfile!=INVALID_HANDLE_VALUE){
                tmode=1; change_menu(0, 1);
            }
            break;
        case IDM_SEND_END:
            if(tmode){
                CloseHandle(tfile); tmode=0; change_menu(0, 0);
            }
            break;
        case IDM_RECV_RAW:
            if(rmode) break;
            rfile=select_file(1);
            if(rfile!=INVALID_HANDLE_VALUE){
                rmode=2; change_menu(1, 1);
            }
            break;
        case IDM_RECV_TXT:
            if(rmode) break;
            rfile=select_file(1);
            if(rfile!=INVALID_HANDLE_VALUE){
                rmode=1; change_menu(1, 1);
            }
            break;
        case IDM_RECV_END:
            if(rmode){
                CloseHandle(rfile); rmode=0; change_menu(1, 0);
            }
            break;
        case IDM_CLEAR   :
            SetWindowText(main_edit, TEXT(""));
            SendMessage(main_edit, WM_CHAR, TEXT('_'), 0); cursor=TRUE;
            break;
        }
        return 0;
    case WM_CREATE:
        GetClientRect(hWnd, &r); h=0;
#ifdef UNDER_CE
        cmdbar=CommandBar_Create(instance, hWnd, 1);
        CommandBar_InsertMenubar(cmdbar, instance, IDM_MAIN, 0);
        CommandBar_AddAdornments(cmdbar, 0, 0);
        h=CommandBar_Height(cmdbar);
#endif
        main_edit=CreateWindow(TEXT("EDIT"), TEXT(""),
                    WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_AUTOVSCROLL|ES_MULTILINE,
                    0, h, r.right, r.bottom-h, hWnd, NULL, instance, NULL);
        orgproc=(WNDPROC)GetWindowLong(main_edit, GWL_WNDPROC);
        SetWindowLong(main_edit, GWL_WNDPROC, (LONG)EditWndProc);
        dc=GetDC(main_edit); GetTextMetrics(dc, &tm); ReleaseDC(main_edit, dc);
        lfont.lfHeight=tm.tmHeight+tm.tmExternalLeading;
        if(*fontface) lstrcpy(lfont.lfFaceName, fontface);
        font=CreateFontIndirect(&lfont);
        SendMessage(main_edit, WM_SETFONT, (WPARAM)font, FALSE);
        SendMessage(main_edit, WM_CHAR, TEXT('_'), 0); cursor=TRUE;
        return 0;
#ifndef UNDER_CE
    case WM_SIZE:
        if(wParam==SIZE_MAXIMIZED || wParam==SIZE_RESTORED)
            SetWindowPos(main_edit, NULL, 0, 0, LOWORD(lParam), HIWORD(lParam),
                         SWP_NOMOVE);
        break;
#endif
    case WM_DESTROY:
        if(rmode) CloseHandle(rfile);
        if(tmode) CloseHandle(tfile);
        DeleteObject(font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static int init_com(TCHAR *def)
{
    TCHAR           comname[MAX_PATH], *p, *q;
    DCB             dcb, tdcb;
    COMMTIMEOUTS    ctmo;

    for(p=comname, q=def; *q && *q!=TEXT(':'); *p++=*q++);
    *p++=TEXT(':'); *p=NULL;
    com=CreateFile(comname, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                   0, NULL);
    if(com==INVALID_HANDLE_VALUE || !GetCommState(com, &dcb)) return 1;
#ifdef UNDER_CE
    dcb.BaudRate        =19200;
    dcb.Parity          =NOPARITY;
    dcb.ByteSize        =8;
    dcb.StopBits        =ONESTOPBIT;
#else
    if(!BuildCommDCB(def, &tdcb)) return 1;
    dcb.BaudRate        =tdcb.BaudRate;
    dcb.Parity          =tdcb.Parity;
    dcb.ByteSize        =tdcb.ByteSize;
    dcb.StopBits        =tdcb.StopBits;
#endif
    dcb.fParity         =dcb.Parity!=NOPARITY;
    dcb.fOutxCtsFlow    =TRUE;
    dcb.fOutxDsrFlow    =FALSE;
    dcb.fDtrControl     =DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity =FALSE;
    dcb.fOutX           =FALSE;
    dcb.fInX            =FALSE;
    dcb.fNull           =FALSE;
    dcb.fRtsControl     =RTS_CONTROL_HANDSHAKE;
    SetCommState(com, &dcb);
    ctmo.ReadIntervalTimeout        =MAXDWORD;
    ctmo.ReadTotalTimeoutMultiplier =0;
    ctmo.ReadTotalTimeoutConstant   =0;
    ctmo.WriteTotalTimeoutMultiplier=0;
    ctmo.WriteTotalTimeoutConstant  =send_timeout;
    SetCommTimeouts(com, &ctmo);
    return 0;
}

static void get_arg(LPTSTR &s, LPTSTR d)
{
    TCHAR   t;

    while(*s && *s++!=TEXT('\"'));
    while(*s){
        t=*s++; if(t==TEXT('\"')) break;
        *d++=t;
    }
    *d=NULL;
}

static const TCHAR * const CLASS_NAME=TEXT("termin");

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR lpCmdLine, int nShow)
{
    TCHAR           comdef[MAX_PATH], interval[MAX_PATH], title[string_size];
    WNDCLASS        wc;
    MSG             msg;
    unsigned char   c;
    DWORD           style, n;

    instance=hInstance;

    get_arg(lpCmdLine, comdef);
    if(!*comdef){
        error(IDS_ERR_ARG); return FALSE;
    }
    if(init_com(comdef)){
        error(IDS_ERR_COM); return FALSE;
    }
    get_arg(lpCmdLine, home_dir);
    get_arg(lpCmdLine, interval); char_interval=_tcstoul(interval, NULL, 10);
    get_arg(lpCmdLine, fontface);

    wc.style            =CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc      =(WNDPROC)WndProc;
    wc.cbClsExtra       =0;
    wc.cbWndExtra       =0;
    wc.hInstance        =hInstance;
    wc.hIcon            =NULL;
    wc.hCursor          =NULL;
    wc.hbrBackground    =(HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName    =CLASS_NAME;
#ifdef UNDER_CE
    wc.lpszMenuName     =NULL;
    style               =WS_VISIBLE;
#else
    wc.lpszMenuName     =MAKEINTRESOURCE(IDM_MAIN);
    style               =WS_OVERLAPPEDWINDOW;
#endif
    if(!RegisterClass(&wc)) return FALSE;

    LoadString(hInstance, IDS_APP_TITLE, title, string_size);
    main_window=CreateWindow(CLASS_NAME, title, style,
                             0, 0, CW_USEDEFAULT, CW_USEDEFAULT,
                             NULL, NULL, hInstance, NULL);
    if(!main_window) return FALSE;
    ShowWindow(main_window, nShow);
    UpdateWindow(main_window);

    rmode=tmode=0; tch_exist=FALSE;
    change_menu(0, 0); change_menu(1, 0);
    while(TRUE){
        if(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)){
            if(msg.message==WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage (&msg);
        }

        ReadFile(com, &c, 1, &n, NULL);
        if(0<n){
            if(rmode==2)  WriteFile(rfile, &c, 1, &n, NULL);
            else{
                c&=0x7f;
                if(rmode) WriteFile(rfile, &c, 1, &n, NULL);
                if(cursor){
                    SendMessage(main_edit, WM_CHAR, 0x08, 0);
                    cursor=FALSE;
                }
                if(c==0x09 || c==0x0d || (0x20<=c && c<=0x7e))
                    SendMessage(main_edit, WM_CHAR, c, 0);
            }
        }else{
            if(!cursor){
                SendMessage(main_edit, WM_CHAR, TEXT('_'), 0);
                cursor=TRUE;
            }
            if(!tmode) Sleep(1);
        }

        if(!tch_exist && tmode){
            ReadFile(tfile, &tch, 1, &n, NULL);
            if(n==0 || (tmode==4 && tch==0x80)){
                CloseHandle(tfile); tmode=0; change_menu(0, 0);
            }else{
                tch_exist=TRUE;
                if(tmode==3 && (tch&0x40)) tmode=4;
            }
        }
        if(tch_exist){
            if(tmode<2) tch|=0x80;
            if(tmode==1 && tch==0x8a) tch_exist=FALSE;
                // supress LF in send text file mode
            else{
                WriteFile(com, &tch, 1, &n, NULL);
                if(0<n){
                    tch_exist=FALSE;
                    if(tmode==1) Sleep(char_interval);
                }
            }
        }
    }
    CloseHandle(com);
    return msg.wParam;
}
