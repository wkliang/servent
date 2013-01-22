/*
    mdi02.h
*/

#define EXPORT      __declspec( dllexport )

#define INIT_MENU_POS	0
#define WINDOW_MENU_POS	1
#define HELLO_MENU_POS	1

#define IDM_DEMO_HELLO	0x10
#define IDM_DEMO_RECT	0x11
#define IDM_DEMO_GDI	0x12
#define IDM_DEMO_DIALOG	0x13

#define IDM_COLOR_FIRST	0x20
#define IDM_BLACK       0x20
#define IDM_RED         0x21
#define IDM_GREEN       0x22
#define IDM_BLUE        0x23
#define IDM_WHITE       0x24
#define IDM_COLOR_LAST	0x2F

#define IDM_FIRSTCHILD		0x8000
#define IDM_NOTHING		0xFF

#define IDM_FILE_NEW		0x110
#define IDM_FILE_OPEN		0x111
#define IDM_FILE_SAVE		0x112
#define IDM_FILE_SAVEAS		0x113
#define IDM_FILE_PRINTER	0x114
#define IDM_FILE_PAGE		0x115
#define IDM_FILE_PRINT		0x116
#define IDM_FILE_EXIT		0x11F

#define IDM_EDIT_UNDO		0x120
#define IDM_EDIT_REDO		0x121
#define IDM_EDIT_CUT		0x122
#define IDM_EDIT_COPY		0x123
#define IDM_EDIT_PASTE		0x124

#define IDM_WINDOW_FIRST	0x130
#define IDM_WINDOW_TILE		0x130
#define IDM_WINDOW_CASCADE	0x131
#define IDM_WINDOW_ARRANGE	0x132
#define IDM_WINDOW_PREV		0x133
#define IDM_WINDOW_NEXT		0x134
#define IDM_WINDOW_CLOSEALL	0x135
#define IDM_WINDOW_RESTORE	0x136
#define IDM_WINDOW_DESTROY	0x137
#define IDM_WINDOW_LAST		0x13F

#define IDM_HELP_TOPICS		0x190
#define IDM_HELP_WHAT		0x191
#define IDM_HELP_ABOUT		0x192

#define IDTB_TOOLBAR	201
#define IDTB_BMP	202

#define IDD_HOST	200
#define IDD_HOST_LABEL	201
#define IDD_HOST_TEXT	202

#define IDI_MyICO	500
#define IDI_MyDLG	501
