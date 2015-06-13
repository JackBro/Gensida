#ifndef G_MAIN_H
#define G_MAIN_H

#define X_RES 320
#define Y_RES 240

#define TIMER_ID    1
#define TIMER_RATE  30

#ifdef __cplusplus
extern "C" {
#endif /* #ifdef __cplusplus */

extern HINSTANCE ghInstance;
extern WNDCLASS WndClass;
extern HWND HWnd;
extern HMENU Gens_Menu;
extern HACCEL hAccelTable;

extern int Paused;
extern int Net_Play;
extern int Current_State;
extern int Show_LED;
extern int Auto_Pause;
extern int Auto_Fix_CS;
extern int Language;
extern int Country;
extern int Country_Order[3];
extern int WinNT_Flag;
extern int Gens_Priority;
extern int Kaillera_Client_Running;
extern int Intro_Style;
extern int SegaCD_Accurate;

extern POINT Window_Pos;

extern char Str_Tmp[1024];
extern char Gens_Path[1024];
extern char Language_Path[1024];
extern char CGOffline_Path[1024];
extern char Manual_Path[1024];


int Set_Render(HWND hWnd, int Full, int Num, int Force);
int Change_68K_Type(HWND hWnd, int Num, int Reset_SND);
int Change_Sound(HWND hWnd);
HMENU Build_Main_Menu(void);

#ifdef __cplusplus
}
#endif /* #ifdef __cplusplus */

#endif