#include <windows.h>

#ifndef ROM_H
#define ROM_H


#define GENESIS_ROM      2
#define _32X_ROM         4
#define SEGACD_IMAGE     6
#define SEGACD_32X_IMAGE 8


#ifdef __cplusplus
extern "C" {
#endif

struct Rom {
char Console_Name[17];
char Copyright[17];
char Rom_Name[49];
char Rom_Name_W[49];
char Type[3];
char Version[13];
unsigned int Checksum;
char IO_Support[17];
unsigned int Rom_Start_Adress;
unsigned int Rom_End_Adress;
unsigned int R_Size;
char Ram_Infos[13];
unsigned int Ram_Start_Adress;
unsigned int Ram_End_Adress;
char Modem_Infos[13];
char Description[41];
char Countries[4];
};

extern int File_Type_Index;
extern struct Rom *Game;
extern char Recent_Rom[9][1024];
extern char IPS_Dir[1024];
extern char Rom_Dir[1024];
extern char Rom_Name[512];
extern char US_CD_Bios[1024];
extern char EU_CD_Bios[1024];
extern char JA_CD_Bios[1024];
extern char _32X_Genesis_Bios[1024];
extern char _32X_Master_Bios[1024];
extern char _32X_Slave_Bios[1024];
extern char Genesis_Bios[1024];

void Get_Name_From_Path(char *Full_Path, char *Name);
void Get_Dir_From_Path(char *Full_Path, char *Dir);
void Update_CD_Rom_Name(char *Name);
int Detect_Format(const char *Name);
int Get_Rom(HWND hWnd);
int Run_Rom(HWND hWnd, const char * Name, int File_Type_Index);
int Pre_Load_Rom(HWND hWnd, const char *Name);
int Load_Rom_CC(char *Name, int Size);
struct Rom *Load_Bios(HWND hWnd, char *Name);
struct Rom *Load_Rom(HWND hWnd, const char *Name, int inter);
struct Rom *Load_Rom_Zipped(HWND hWnd, const char *Name, int inter);
void Fix_Checksum(void);
unsigned int Calculate_CRC32(void);
int IPS_Patching();
void Free_Rom(struct Rom *Rom_Name);

#ifdef __cplusplus
};
#endif

#endif