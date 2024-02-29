#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include "nds/ndstypes.h"
// #define sd_comadd 0x9800000
// #define sd_dataadd 0x9000000  
// #define sd_dataradd 0x9100000
// #define sd_reset 0x9440000

// #define en_fireware 0
// #define en_sdram 1
// #define en_sdcard 2
// #define en_write 4
// #define en_rumble 8
// #define en_rumble_user_flash 1
extern void sc_ReadSector (u16 *buff,u32 sector,u8 readnum);
extern void sc_WriteSector (u16 *buff,u32 sector,u8 writenum);
extern void sc_InitSCMode (void);
extern bool sc_MemoryCard_IsInserted (void);
extern void sc_sdcard_reset(void);

#ifdef __cplusplus
}
#endif