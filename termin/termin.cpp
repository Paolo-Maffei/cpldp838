/*
    project        : termin, terminal soft for CPLDP-8
    author         : omiokone
    termin.cpp     : termin main
*/

#include <windows.h>
#include <commdlg.h>
#include "resource.h"

// transmit/receive loop time, mainly for cursor response
const DWORD loop_time   =30;

#define FONTFACE "Courier New"
const int   string_size =100;

#ifdef STRICT
typedef WNDPROC PROCTYPE;
#else
typedef FARPROC PROCTYPE;
#endif

static HWND             main_window =NULL;
static HWND             main_edit   =NULL;
static HINSTANCE        instance    =NULL;
static HANDLE           com         =NULL;
static HFONT            font        =NULL;
static char             home_dir[MAX_PATH];
static char             fontface[MAX_PATH];
static DWORD            char_interval;  // interval between text characters (ms)
static PROCTYPE         orgproc;
static unsigned char    tch;            // character to be sent
static int              tch_exist;      // TRUE: character to be sent exists
static int              cursor;         // TRUE: cursor is shown on edit control
static int              rmode;          // 0:normal, 1:save text, 2:save raw
static int              tmode;          // 0:normal, 1:send text, 2:send raw
                                        // 3:send binary (leader)
                                        // 4:send binary (body)
static HANDLE           rfile, tfile;

void error(UINT id)
{
    char  caption[string_size];
    char  text   [string_size];

    LoadString(instance, IDS_ERR_CAPTION, caption, string_size);
    LoadString(instance, id,              text,    string_size);
    MessageBox(main_window, text, caption, MB_OK);
}

LRESULT CALLBACK EditWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg){                       // inhibit focus on edit control
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:    return 0;
    }
    return CallWindowProc(orgproc, hWnd, uMsg, wParam, lParam);
}

static HANDLE select_file(int write)
{
    OPENFILENAME    fs={sizeof(OPENFILENAME)};
    char            filename[MAX_PATH];
    HANDLE          file;
    BOOL            r;

    filename[0]=NULL;
    fs.hwndOwner        =main_window;
    fs.lpstrFile        =filename;
    fs.nMaxFile         =MAX_PATH;
    fs.lpstrInitialDir  =home_dir;
    fs.Flags            =OFN_PATHMUSTEXIST|OFN_LONGNAMES;
    if(write){
        r=GetSaveFileName(&fs);
    }else{
        fs.Flags|=OFN_FILEMUSTEXIST;
        r=GetOpenFileName(&fs);
    }
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
    HMENU       menu;
    UINT        enable, disable;

    menu=GetMenu(main_window);
    disable=MF_BYCOMMAND|MF_GRAYED|MF_DISABLED;
    enable =MF_BYCOMMAND|MF_ENABLED;
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

void cursor_on()
{
    SendMessage(main_edit, WM_CHAR, '_', 0); cursor=TRUE;
}

static LOGFONT lfont={
    0, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
    DEFAULT_PITCH|FF_DONTCARE, FONTFACE};

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    HDC         dc;
    TEXTMETRIC  tm;

    switch(uMsg){
    case WM_KEYDOWN:
        if(wParam==VK_DELETE){
            if(!tmode && !tch_exist){
                tch=0x7f; tch_exist=TRUE;
            }
            return 0;
        }
        break;
    case WM_CHAR:
        if(!tmode && !tch_exist){
            tch=(unsigned char)wParam; tch_exist=TRUE;
        }
        return 0;
    case WM_COMMAND:
        switch(LOWORD(wParam)){
        case IDM_EXIT:
            PostMessage(hWnd, WM_CLOSE, 0, 0);
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
        case IDM_CLEAR:
            SetWindowText(main_edit, ""); cursor_on();
            break;
        }
        return 0;
    case WM_CREATE:
        main_edit=CreateWindow("EDIT", "",
                               WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE,
                               0, 0, 0, 0, hWnd, NULL, instance, NULL);
        orgproc=(PROCTYPE)SetWindowLongPtr(main_edit, GWLP_WNDPROC,
                                           (LONG_PTR)EditWndProc);
        dc=GetDC(main_edit); GetTextMetrics(dc, &tm); ReleaseDC(main_edit, dc);
        lfont.lfHeight=tm.tmHeight+tm.tmExternalLeading;
        if(*fontface) lstrcpy(lfont.lfFaceName, fontface);
        font=CreateFontIndirect(&lfont);
        SendMessage(main_edit, WM_SETFONT, (WPARAM)font, FALSE);
        cursor_on();
        return 0;
    case WM_SIZE:
        MoveWindow(main_edit, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        return 0;
    case WM_DESTROY:
        DeleteObject(font);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static int init_com(char *def)
{
    char            comname[MAX_PATH], *p, *q;
    DCB             dcb;
    COMMTIMEOUTS    ctmo={0};

    for(p=comname, q=def; *q && *q!=':'; *p++=*q++);
    *p++=':'; *p=NULL;
    com=CreateFile(comname, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                   FILE_FLAG_OVERLAPPED, NULL);
    if(com==INVALID_HANDLE_VALUE || !GetCommState(com, &dcb)) return 1;
    if(!BuildCommDCB(def, &dcb)) return 1;
    dcb.fParity         =dcb.Parity!=NOPARITY;
    dcb.fOutxCtsFlow    =TRUE;
    dcb.fOutxDsrFlow    =FALSE;
    dcb.fDtrControl     =DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity =FALSE;
    dcb.fOutX           =FALSE;
    dcb.fInX            =FALSE;
    dcb.fErrorChar      =FALSE;
    dcb.fNull           =FALSE;
    dcb.fRtsControl     =RTS_CONTROL_HANDSHAKE;
    dcb.fAbortOnError   =FALSE;
    SetCommState(com, &dcb);
    SetCommTimeouts(com, &ctmo);
    return 0;
}

void message_loop()
{
    OVERLAPPED      ros={0}, tos={0};
    HANDLE          event[2];
    int             rwait, twait;
    MSG             msg;
    unsigned char   c;
    DWORD           n;

    event[0]=ros.hEvent=CreateEvent(NULL, TRUE, FALSE, NULL);
    event[1]=tos.hEvent=CreateEvent(NULL, TRUE, FALSE, NULL);
    rwait=twait=FALSE;
    while(TRUE){
        while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)){
            if(msg.message==WM_QUIT) return;
                // as single thread, events must be left till closing com
                // or else error occurs on Win9x
            TranslateMessage(&msg);
            DispatchMessage (&msg);
        }

        if(rwait){
            if(!cursor) cursor_on();
        }else{
            if(ReadFile(com, &c, 1, &n, &ros)) SetEvent(ros.hEvent);
            rwait=TRUE;
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

        if(!twait && tch_exist){
            if(tmode<2) tch|=0x80;
            if(tmode==1 && tch==0x8a) tch_exist=FALSE;
                // supress LF in send text file mode
            else{
                if(WriteFile(com, &tch, 1, &n, &tos)) SetEvent(tos.hEvent);
                // byte by byte transmission to check CTS steadily
                twait=TRUE;
            }
        }

        switch(MsgWaitForMultipleObjects(twait?2:1, event, FALSE, loop_time,
                                         QS_ALLINPUT))
        {
        case WAIT_OBJECT_0:
            rwait=FALSE;
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
            break;
        case WAIT_OBJECT_0+1:
            twait=tch_exist=FALSE;
            if(tmode==1) Sleep(char_interval);
            break;
        }
    }
}

static void get_arg(LPSTR &s, LPSTR d)
{
    char    t;

    while(*s && *s++!='\"');
    while(*s){
        t=*s++; if(t=='\"') break;
        *d++=t;
    }
    *d=NULL;
}

static const char * const CLASS_NAME="termin";

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nShow)
{
    char        comdef[MAX_PATH], interval[MAX_PATH], title[string_size];
    WNDCLASS    wc;

    instance=hInstance;

    get_arg(lpCmdLine, comdef);
    if(!*comdef){
        error(IDS_ERR_ARG); return 0;
    }
    if(init_com(comdef)){
        error(IDS_ERR_COM); return 0;
    }
    get_arg(lpCmdLine, home_dir);
    get_arg(lpCmdLine, interval); char_interval=strtoul(interval, NULL, 10);
    get_arg(lpCmdLine, fontface);

    wc.style            =CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc      =WndProc;
    wc.cbClsExtra       =0;
    wc.cbWndExtra       =0;
    wc.hInstance        =hInstance;
    wc.hIcon            =NULL;
    wc.hCursor          =NULL;
    wc.hbrBackground    =NULL;
    wc.lpszMenuName     =MAKEINTRESOURCE(IDM_MAIN);
    wc.lpszClassName    =CLASS_NAME;
    if(!RegisterClass(&wc)) return 0;
    LoadString(hInstance, IDS_APP_TITLE, title, string_size);
    main_window=CreateWindow(CLASS_NAME, title, WS_OVERLAPPEDWINDOW,
                             0, 0, CW_USEDEFAULT, CW_USEDEFAULT,
                             NULL, NULL, hInstance, NULL);
    if(!main_window) return 0;
    ShowWindow(main_window, nShow);
    UpdateWindow(main_window);

    rmode=tmode=0; tch_exist=FALSE;
    change_menu(0, 0); change_menu(1, 0);
    message_loop();
    if(rmode) CloseHandle(rfile);
    if(tmode) CloseHandle(tfile);
    CloseHandle(com);
    return 1;
}
