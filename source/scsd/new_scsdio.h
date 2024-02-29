#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "nds/ndstypes.h"
#define sd_comadd 0x9800000
#define sd_dataadd 0x9000000  
#define sd_dataradd 0x9100000
#define sd_reset 0x9440000

#define en_fireware 0
#define en_sdram 1
#define en_sdcard 2
#define en_write 4
#define en_rumble 8
#define en_rumble_user_flash 1
extern void sc_ReadSector (u16 *buff,u32 sector,u8 readnum);
extern void sc_WriteSector (u16 *buff,u32 sector,u8 writenum);
extern void sc_InitSCMode (void);
extern bool sc_MemoryCard_IsInserted (void);
// extern void sc_sdcard_reset(void);

extern void get_resp(void);
extern void sd_crc16_s(u16* buff,u16 num,u16* crc16buff);
extern u8 sd_crc7_s(u16* buff,u16 num);

void get_resp_drop();
void send_clk(u32 num);
void sc_mode(u16 data);
void sc_sdcard_reset(void);
void SDCommand(u8 command,u8 num,u32 sector);
void sd_data_write(u16 *buff,u16* crc16buff);
void WriteSector(u16 *buff,u32 sector,u32 writenum);
#ifdef __cplusplus
}
#endif