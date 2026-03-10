#ifndef USER_H
#define USER_H

#include "langext.h"

#if __GNUC__
#define DLLIMPORT(RETURN) __attribute__((dllimport,stdcall)) RETURN
#else
#define DLLIMPORT(RETURN) _declspec(dllimport) RETURN stdcall
#endif

#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_ADD 0x6B
#define VK_SUBTRACT 0x6D
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_LSHIFT 0xA0
#define VK_LEFT	 0x25	
#define VK_UP	 0x26	
#define VK_RIGHT 0x27	
#define VK_DOWN	 0x28	
#define VK_OEM_3 0xC0
#define VK_F1	0x70	
#define VK_F2	0x71	
#define VK_F3	0x72	
#define VK_F4	0x73	
#define VK_F5	0x74	
#define VK_F6	0x75	
#define VK_F7	0x76	
#define VK_F8	0x77	
#define VK_F9	0x78	
#define VK_F10	0x79	
#define VK_F11	0x7A	
#define VK_F12	0x7B		

#define WM_CREATE    0x0001
#define WM_KEYDOWN   0x0100
#define WM_COMMAND   0x0111
#define WM_HSCROLL   0x0114
#define WM_CLOSE     0x0010
#define WM_DESTROY   0x0002
#define WM_QUIT      0x0012
#define WM_SIZE      0x0005
#define WM_MOUSEMOVE 0x0200
#define WM_INPUT     0x00FF
#define WM_ACTIVATE  0x0006

#define IMAGE_BITMAP        0
#define IMAGE_ICON          1
#define IMAGE_CURSOR        2

#define LR_DEFAULTCOLOR     0x00000000
#define LR_MONOCHROME       0x00000001
#define LR_COLOR            0x00000002
#define LR_COPYRETURNORG    0x00000004
#define LR_COPYDELETEORG    0x00000008
#define LR_LOADFROMFILE     0x00000010
#define LR_LOADTRANSPARENT  0x00000020
#define LR_DEFAULTSIZE      0x00000040
#define LR_VGACOLOR         0x00000080
#define LR_LOADMAP3DCOLORS  0x00001000
#define LR_CREATEDIBSECTION 0x00002000
#define LR_COPYFROMRESOURCE 0x00004000
#define LR_SHARED           0x00008000

#define TBM_GETPOS 0x0400
#define TBM_SETRANGE 0x0406
#define TBM_SETPOS (0x0400 + 5) 

#define TBS_AUTOTICKS 0x0001

#define CBS_DROPDOWNLIST 0x0003

#define CB_ADDSTRING 0x0143
#define CBS_HASSTRINGS 0x2000

#define LB_ADDSTRING    0x0180
#define LB_GETCURSEL    0x0188
#define LB_GETTEXT      0x0189
#define LB_DELETESTRING	0x0182
#define LB_GETCOUNT     0x018B

#define BS_CHECKBOX        0x00000002
#define BS_AUTOCHECKBOX    0x00000003
#define BS_PUSHLIKE        0x00001000

#define BM_GETCHECK 0x00F0
#define BM_SETCHECK 0x00F1

#define BST_UNCHECKED 0x0000
#define BST_CHECKED 0x0001

#define LBS_NOTIFY        0x0001

#define LBN_SELCHANGE 1

#define WS_OVERLAPPED   0x00000000L
#define WS_POPUP        0x80000000L
#define WS_CHILD        0x40000000L
#define WS_MINIMIZE     0x20000000L
#define WS_VISIBLE      0x10000000L
#define WS_DISABLED     0x08000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_MAXIMIZE     0x01000000L
#define WS_CAPTION      0x00C00000L
#define WS_BORDER       0x00800000L
#define WS_DLGFRAME     0x00400000L
#define WS_VSCROLL      0x00200000L
#define WS_HSCROLL      0x00100000L
#define WS_SYSMENU      0x00080000L
#define WS_THICKFRAME   0x00040000L
#define WS_GROUP        0x00020000L
#define WS_MINIMIZEBOX  0x00020000L
#define WS_TABSTOP      0x00010000L
#define WS_MAXIMIZEBOX  0x00010000L

#define WM_LBUTTONDOWN  0x0201
#define WM_LBUTTONUP    0x0202
#define WM_RBUTTONDOWN  0x0204
#define WM_MBUTTONDOWN  0x0207
#define WM_MOUSEWHEEL   0x020A

#define RID_HEADER 0x10000005
#define RID_INPUT  0x10000003

#define HID_USAGE_PAGE_GENERIC 0x01
#define HID_USAGE_PAGE_GAME    0x05
#define HID_USAGE_PAGE_LED     0x08
#define HID_USAGE_PAGE_BUTTON  0x09

#define HID_USAGE_GENERIC_POINTER               0x01
#define HID_USAGE_GENERIC_MOUSE                 0x02
#define HID_USAGE_GENERIC_JOYSTICK              0x04
#define HID_USAGE_GENERIC_GAMEPAD               0x05
#define HID_USAGE_GENERIC_KEYBOARD              0x06
#define HID_USAGE_GENERIC_KEYPAD                0x07
#define HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER 0x08

#define RIM_TYPEMOUSE    0
#define RIM_TYPEKEYBOARD 1
#define RIM_TYPEHID      2

#define SW_HIDE            0
#define SW_NORMAL          1
#define SW_SHOWMINIMIZED   2
#define SW_MAXIMIZE        3
#define SW_SHOWNOACTIVATE  4
#define SW_SHOW            5
#define SW_MINIMIZE        6
#define SW_SHOWMINNOACTIVE 7
#define SW_SHOWNA          8
#define SW_RESTORE         9
#define SW_SHOWDEFAULT     10
#define SW_FORCEMINIMIZE   11

#if _WIN64
typedef unsigned long long Wparam;
typedef long long Lparam;
typedef long long Lresult;
#else
typedef unsigned Wparam;
typedef int Lparam;
typedef int Lresult;
#endif

typedef struct{
    int left;
    int top;
    int right;
    int bottom;
} Rect;

typedef struct{
    int x;
    int y;
} Point;

typedef struct{
    void* hwnd;
    unsigned message;
    Wparam w_param;
    Lparam l_param;
    unsigned time;
    Point pt;
} Msg;

typedef struct{
    unsigned style;
    Lresult (stdcall* wnd_proc)(void*,unsigned,Wparam,Lparam);
    int cls_extra;
    int wnd_extra;
    void* instance;
    void* window_icon;
    void* cursor;
    void* background;
    const char* menu_name;
    const char* class_name;
} WndClassA;

typedef struct{
    int is_icon;
    unsigned hotspot_x;
    unsigned hotspot_y;
    void* bitmap_mask;
    void* bitmap_color;
} IconInfo;

typedef struct{
    unsigned type;
    unsigned size;
    void* device;
    Wparam wparam;
} RawInputHeader;

typedef struct{
    uint16 flags;
    union{
        unsigned buttons;
        struct{
            uint16 button_flags;
            uint16 button_data;
        };
    };
    unsigned raw_buttons;
    int      last_x;
    int      last_y;
    unsigned extra_information;
} RawMouse;

typedef struct{
    uint16 MakeCode;
    uint16 Flags;
    uint16 Reserved;
    uint16 VKey;
    unsigned Message;
    unsigned ExtraInformation;
} RawKeyboard;

typedef struct{
    unsigned hid_size;
    unsigned count;
    uint8    raw_data[1];
} RawHid;

typedef struct{
    RawInputHeader header;
    union{
        RawMouse    mouse;
        RawKeyboard keyboard;
        RawHid      hid;
    } data;
} RawInput;

typedef struct{
    uint16 usage_page;
    uint16 usage;
    unsigned flags;
    void* target;
} RawInputDevice;

#ifdef __cplusplus
extern "C"{
#endif

DLLIMPORT(int) ShowCursor(int show);
DLLIMPORT(uint16) RegisterClassA(WndClassA *g_windowclass);
DLLIMPORT(int) SetWindowPos(void* window,void* window_insert_after,int x,int y,int cx,int cy,unsigned flags);
DLLIMPORT(int) GetCursorPos(Point* point);
DLLIMPORT(int) SetCursorPos(int x,int y);
DLLIMPORT(int) ClipCursor(Rect* rect);
DLLIMPORT(void*) LoadImageA(void* instance,const char* name,unsigned type,int cx,int cy,unsigned fuLoad);
DLLIMPORT(int) GetClientRect(void* hwnd,Rect* rect);
DLLIMPORT(void*) GetDC(void* wnd);
DLLIMPORT(int) ReleaseDC(void* window,void* dc);
DLLIMPORT(void*) GetDlgItem(void* dlg,int identifier);
DLLIMPORT(uint16) GetKeyState(int virt_key);
DLLIMPORT(int) GetKeyboardState(uint8* key_state);
DLLIMPORT(Lresult) DefWindowProcA(void* wnd,unsigned msg,Wparam wparam,Lparam l_param);
DLLIMPORT(void*) CreateWindowExA(
    unsigned ex_style,
    const char* class_name,
    const char* window_name,
    int style,
    int x,
    int y,
    int width,
    int height,
    void* wnd_parent,
    void* menu,
    void* instance,
    void* param
);
DLLIMPORT(int) MessageBoxA(void* wnd,char* text,char* caption,unsigned type);
DLLIMPORT(int) DestroyWindow(void* window);
DLLIMPORT(int) SendMessageA(void* hwnd,unsigned msg,Wparam w_param,Lparam l_param);
DLLIMPORT(int) DispatchMessageA(Msg* msg);
DLLIMPORT(unsigned) GetMessageA(
    Msg* msg,
    void*  wnd,
    unsigned msg_filter_min,
    unsigned msg_filter_max
);
DLLIMPORT(int) PeekMessageA(
    Msg* msg,
    void*  wnd,
    unsigned msg_filter_min,
    unsigned msg_filter_max,
    unsigned remove_msg
);
DLLIMPORT(void*) GetForegroundWindow(void);
DLLIMPORT(int) ScreenToClient(void* hwnd,Point* point);
DLLIMPORT(int) ClientToScreen(void* hwnd,Point* point);
DLLIMPORT(void*) CreateIconIndirect(IconInfo* icon_info);
DLLIMPORT(int) FillRect(void* dc,Rect* rc,void* brush);
DLLIMPORT(unsigned) GetRawInputData(void* raw_input,unsigned command,void* data,unsigned* cb_size,unsigned header_size);
DLLIMPORT(int) RegisterRawInputDevices(RawInputDevice* raw_input_devices,unsigned n_devices,unsigned cb_size);
DLLIMPORT(int) GetWindowRect(void* window,Rect* rect);
DLLIMPORT(int) MapWindowPoints(void* window_from,void* window_to,Point* points,unsigned n_points);
DLLIMPORT(int) ShowWindow(void* window,int cmd_show);
DLLIMPORT(int) IsZoomed(void* window);

#ifdef __cplusplus
}
#endif

#endif
