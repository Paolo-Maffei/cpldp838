/*
    project        : VT52 terminal emulator for CPLDP-8/VISTA
    author         : omiokone
    vt52.cpp       : vt52 main
*/

#include <windows.h>
#include <commdlg.h>
#include "resource.h"

// transmit/receive loop time, mainly for cursor response
const DWORD loop_time   =30;
const int   width       =80;            // screen character size
const int   height      =24;
const int   string_size =100;
#define FONTFACE "Courier New"

static HWND             main_window =NULL;
static HINSTANCE        instance    =NULL;
static HANDLE           com         =NULL;
static HFONT            font        =NULL;
static char             home_dir[MAX_PATH];
static char             fontface[MAX_PATH];
static DWORD            char_interval;  // interval between text characters (ms)
static unsigned char    tch[3];         // characters to be sent
static DWORD            tchs;           // number of characters
static int              cursor;         // TRUE: character cursor is shown
static int              rmode;          // 0:normal, 1:save text, 2:save raw
static int              tmode;          // 0:normal, 1:send text, 2:send raw
                                        // 3:send binary (leader)
                                        // 4:send binary (body)
static HANDLE           rfile, tfile;
static WCHAR            screen[width*height];
static int              posx;           // cursor position left is 0
static int              posy;           //                 top  is 0
static int              fontw;          // font width
static int              fonth;          // font height
static int              escs;           // escape sequence state
static int              graph;          // TRUE: graphics mode

void error(UINT id)
{
    char  caption[string_size];
    char  text   [string_size];

    LoadString(instance, IDS_ERR_CAPTION, caption, string_size);
    LoadString(instance, id,              text,    string_size);
    MessageBox(main_window, text, caption, MB_OK);
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

// character screen

static void clear_screen()
{
    int     i;

    for(i=0; i<height*width; i++) screen[i]=' ';
    cursor=FALSE; posx=posy=0;
    escs=0; graph=FALSE;
}

static void scroll_up()
{
    int     i;

    for(i=0; i<width*(height-1); i++) screen[i]=screen[i+width];
    for(; i<width*height; i++) screen[i]=' ';
}

static void scroll_down()
{
    int     i;

    for(i=width*height-1; width<=i; i--) screen[i]=screen[i-width];
    for(; 0<=i; i--) screen[i]=' ';
}

static void draw_cursor()
{
    HDC     dc;
    RECT    r;

    r.left=fontw*posx; r.right =r.left+fontw-1;
    r.top =fonth*posy; r.bottom=r.top +fonth-1;
    dc=GetDC(main_window); InvertRect(dc, &r); ReleaseDC(main_window, dc);
}

static void row_out(int row)
{
    HDC     dc;

    dc=GetDC(main_window);
    SetTextColor(dc, RGB(255, 255, 255)); SetBkColor(dc, RGB(0, 0, 0));
    SelectObject(dc, font);
    TextOutW(dc, 0, fonth*row, (LPCWSTR)(screen+width*row), width);
    ReleaseDC(main_window, dc);
}

static void cursor_off()
{
    if(cursor) draw_cursor();
    cursor=FALSE;
}

static void erase_eol()
{
    WCHAR  *p;
    int     i;

    p=screen+width*posy+posx;
    for(i=posx; i<width; i++) *p++=' ';
    cursor_off(); row_out(posy);
}

static void erase_eos()
{
    int     i;

    for(i=width*posy+posx; i<width*height; i++) screen[i]=' ';
    cursor_off();
    for(i=posy; i<height; i++) row_out(i);
}

static void screen_out(WCHAR c)
{
    static unsigned cy;

    if(escs==0){
        if(0x20<=c && c<=0x7e){
            if(graph){
                switch(c){
                case 'a':   c=0x2588; break;
                case 'h':   c=0x2192; break;
                case 'i':   c=0x2026; break;
                case 'k':   c=0x2193; break;
                }
            }
            screen[width*posy+posx]=c;
            cursor_off(); row_out(posy);
            if(posx<width-1) posx++;
        }else{
            switch(c){
            case 0x08:  if(0<posx){cursor_off(); posx--;}
                        break;
            case 0x09:  if(posx<width-1){
                            cursor_off();
                            if(posx<width-8) posx=(posx&~7)+8;
                            else  posx++;
                        }
                        break;
            case 0x0a:  if(height-1<=posy){
                            scroll_up();
                            InvalidateRect(main_window, NULL, TRUE);
                            UpdateWindow(main_window);
                        }else{
                            cursor_off(); posy++;
                        }
                        break;
            case 0x0d:  cursor_off(); posx=0;
                        break;
            case 0x1b:  escs=1;
                        break;
            }
        }
    }else
    if(escs==1){
        switch(c){
        case 'A':   if(0<posy)       {cursor_off(); posy--;} break;
        case 'B':   if(posy<height-1){cursor_off(); posy++;} break;
        case 'C':   if(posx<width -1){cursor_off(); posx++;} break;
        case 'D':   if(0<posx)       {cursor_off(); posx--;} break;
        case 'F':   graph=TRUE;                              break;
        case 'G':   graph=FALSE;                             break;
        case 'H':   cursor_off(); posx=posy=0;               break;
        case 'I':   if(posy<=0){
                        scroll_down();
                        InvalidateRect(main_window, NULL, TRUE);
                        UpdateWindow(main_window);
                    }else{
                        cursor_off(); posy--;
                    }
                    break;
        case 'K':   erase_eol(); break;
        case 'J':   erase_eos(); break;
        case 'Y':   escs=2;      break;
        }
        if(escs==1) escs=0;
    }else
    if(escs==2){
        cy=c; escs=3;
    }else{
        cursor_off();
        posx=c-0x20; if(width<=posx) posx=width-1;
        if(cy<height+0x20) posy=cy-0x20;
        escs=0;
    }
}

struct vk_table{
    WPARAM          vk;
    unsigned char   code[2][3];
} key_table[]={     // normal            shift
    {VK_UP,       {{0x1b, 'A', 0},   {0x1b, '?', 'M'}}},
    {VK_DOWN,     {{0x1b, 'B', 0},   {0x1b, '?', 't'}}},
    {VK_RIGHT,    {{0x1b, 'C', 0},   {0x1b, '?', 's'}}},
    {VK_LEFT,     {{0x1b, 'D', 0},   {0x1b, '?', 'q'}}},
    {VK_DIVIDE,   {{0x1b, 'P', 0},   {0x1b, 'P', 0}}},
    {VK_MULTIPLY, {{0x1b, 'Q', 0},   {0x1b, 'Q', 0}}},
    {VK_SUBTRACT, {{0x1b, 'R', 0},   {0x1b, 'R', 0}}},
    {VK_NUMPAD0,  {{0x1b, '?', 'p'}, {0x1b, '?', 'p'}}},
    {VK_NUMPAD1,  {{0x1b, '?', 'q'}, {0x1b, '?', 'q'}}},
    {VK_NUMPAD2,  {{0x1b, '?', 'r'}, {0x1b, '?', 'r'}}},
    {VK_NUMPAD3,  {{0x1b, '?', 's'}, {0x1b, '?', 's'}}},
    {VK_NUMPAD4,  {{0x1b, '?', 't'}, {0x1b, '?', 't'}}},
    {VK_NUMPAD5,  {{0x1b, '?', 'u'}, {0x1b, '?', 'u'}}},
    {VK_NUMPAD6,  {{0x1b, '?', 'v'}, {0x1b, '?', 'v'}}},
    {VK_NUMPAD7,  {{0x1b, '?', 'w'}, {0x1b, '?', 'w'}}},
    {VK_NUMPAD8,  {{0x1b, '?', 'x'}, {0x1b, '?', 'x'}}},
    {VK_NUMPAD9,  {{0x1b, '?', 'y'}, {0x1b, '?', 'y'}}},
    {VK_DECIMAL,  {{0x1b, '?', 'n'}, {0x1b, '?', 'n'}}},
    {VK_ADD,      {{0x1b, '?', 'M'}, {0x1b, '?', 'M'}}},
    {VK_INSERT,   {{0x1b, '?', 'p'}, {0x1b, '?', 'v'}}},
    {VK_DELETE,   {{0x7f, 0,   0},   {0x1b, '?', 'u'}}},
    {VK_HOME,     {{0x1b, '?', 'n'}, {0x1b, '?', 'w'}}},
    {VK_END,      {{0x1b, 'R', 0},   {0x1b, '?', 'y'}}},
    {VK_PRIOR,    {{0x1b, '?', 'r'}, {0x1b, 'Q', 0}}},
    {VK_NEXT,     {{0x1b, '?', 'x'}, {0x1b, 'P', 0}}},
    {0,           {{0,    0,   0},   {0,    0,   0}}}};

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    struct vk_table *p;
    HDC             dc;
    PAINTSTRUCT     ps;
    unsigned char   *d, *s;
    int             i;

    switch(uMsg){
    case WM_PAINT:
        dc=BeginPaint(hWnd, &ps);
        SetTextColor(dc, RGB(255, 255, 255)); SetBkColor(dc, RGB(0, 0, 0));
        SelectObject(dc, font);
        for(i=0; i<height; i++)
            TextOutW(dc, 0, fonth*i, (LPCWSTR)(screen+width*i), width);
        EndPaint(hWnd, &ps);
        cursor=FALSE;
        return 0;
    case WM_KILLFOCUS:
        cursor_off();
        return 0;
    case WM_KEYDOWN:
        for(p=key_table; p->vk; p++)
            if(p->vk==wParam){
                if(!tmode && !tchs){
                    d=tch;
                    s=p->code[GetKeyState(VK_SHIFT)<0?1:0];
                    for(; tchs<=2 && *s; tchs++) *d++=*s++;
                }
                return 0;
            }
        break;
    case WM_CHAR:
        if(!tmode && !tchs){
            *tch=(unsigned char)wParam; tchs=1;
        }
        return 0;
    case WM_COMMAND:
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
        case IDM_CLEAR:
            clear_screen();
            InvalidateRect(hWnd, NULL, TRUE);
            UpdateWindow(hWnd);
            break;
        }
        return 0;
    case WM_DESTROY:
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
            if(!cursor && GetFocus()){
                draw_cursor(); cursor=TRUE;
            }
        }else{
            if(ReadFile(com, &c, 1, &n, &ros)) SetEvent(ros.hEvent);
            rwait=TRUE;
        }

        if(tmode && !tchs){
            ReadFile(tfile, tch, 1, &n, NULL);
            if(n==0 || (tmode==4 && *tch==0x80)){
                CloseHandle(tfile); tmode=0; change_menu(0, 0);
            }else{
                tchs=1;
                if(tmode==3 && (*tch&0x40)) tmode=4;
            }
        }

        if(!twait && tchs){
            if(tmode<2) *tch|=0x80;
            if(tmode==1 && *tch==0x8a) tchs=0;
                // supress LF in send text file mode
            else{
                if(WriteFile(com, tch, tchs, &n, &tos)) SetEvent(tos.hEvent);
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
                screen_out(c);
            }
            break;
        case WAIT_OBJECT_0+1:
            twait=FALSE; tchs=0;
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

static LOGFONT lfont={
    0, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
    OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
    DEFAULT_PITCH|FF_DONTCARE, FONTFACE};

static const char * const CLASS_NAME="VT52";

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR lpCmdLine, int nShow)
{
    char        comdef[MAX_PATH], interval[MAX_PATH], title[string_size];
    WNDCLASS    wc;
    HDC         dc;
    TEXTMETRIC  tm;
    RECT        r;

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
    wc.hCursor          =LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground    =(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszMenuName     =MAKEINTRESOURCE(IDM_MAIN);
    wc.lpszClassName    =CLASS_NAME;
    if(!RegisterClass(&wc)) return 0;
    LoadString(hInstance, IDS_APP_TITLE, title, string_size);
    main_window=CreateWindow(CLASS_NAME, title, WS_OVERLAPPEDWINDOW,
                             0, 0, CW_USEDEFAULT, CW_USEDEFAULT,
                             NULL, NULL, hInstance, NULL);
    if(!main_window) return 0;
    clear_screen();
    dc=GetDC(main_window);
    GetTextMetrics(dc, &tm); lfont.lfHeight=tm.tmHeight+tm.tmExternalLeading;
    if(*fontface) lstrcpy(lfont.lfFaceName, fontface);
    font=CreateFontIndirect(&lfont);
    SelectObject(dc, font); GetTextMetrics(dc, &tm);
    ReleaseDC(main_window, dc);
    fonth=tm.tmHeight+tm.tmExternalLeading; fontw=tm.tmAveCharWidth;
    r.top=r.left=1; r.bottom=fonth*height; r.right=fontw*width;
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, TRUE);
    MoveWindow(main_window, 0, 0, r.right-r.left+1, r.bottom-r.top+1, FALSE);
    ShowWindow(main_window, nShow);
    UpdateWindow(main_window);

    rmode=tmode=0; tchs=0;
    change_menu(0, 0); change_menu(1, 0);
    message_loop();
    if(rmode) CloseHandle(rfile);
    if(tmode) CloseHandle(tfile);
    DeleteObject(font);
    CloseHandle(com);
    return 1;
}
