#include "new_scsdio.h"
#include <cstdio>

extern bool isSDHC;

uint8_t _SD_CRC7(uint8_t *pBuf, int len);
bool _SCSD_readData (void* buffer);

void _SD_CRC16 (u8* buff, int buffLength, u8* crc16buff);
void WriteSector(u16 *buff, u32 sector, u32 writenum)
{
    u8 crc16[8];//??为什么是8个
    sc_mode(en_sdcard);
    sc_sdcard_reset();
    // auto param = isSDHC ? sector : (sector << 9);
    auto param = (sector << 9);
    SDCommand(25, 0, param);
    get_resp_drop();
    send_clk(0x10);
    for (u32 j = 0; j < writenum; j++)
    {
        _SD_CRC16((u8 *)((u32)buff + j * 512), 512, (u8 *)crc16);
        sd_data_write((u16 *)((u32)buff + j * 512), (u16 *)crc16);
        send_clk(0x10);
    }
    SDCommand(12, 0, 0);
    get_resp_drop();
    send_clk(0x10);
    vu16 *wait_busy = (vu16 *)sd_dataadd;
    while (((*wait_busy) & 0x0100) == 0)
        ;
    return;
}

void ReadSector(uint16_t *buff, uint32_t sector, uint8_t readnum)
{
    SDCommand(0x12, 0, sector << 9); // R0 = 0x12, R1 = 0, R2 as calculated above
    for(int i=0;i<readnum;i++)
    {
        _SCSD_readData(buff); // Add R6, left shifted by 9, to R4 before casting
        buff += 512 / 2;
    }
    SDCommand(0xC, 0, 0); // Command to presumably stop reading
    get_resp_drop();           // Get response from SD card
    send_clk(0x10);       // Send clock signal
}

#define BUSY_WAIT_TIMEOUT 500000
#define SCSD_STS_BUSY 0x100

void sd_data_write(u16 *buff, u16 *crc16buff)
{
    vu16 *const wait_busy = (vu16 *)sd_dataadd;
    vu16 *const data_write_u16 = (vu16 *)sd_dataadd;
    vu32 *const data_write_u32 = (vu32 *)sd_dataadd;
    while (!((*wait_busy) & SCSD_STS_BUSY))
        ; // Note:两边的等待是不一致的
    *wait_busy;

    *data_write_u16 = 0; // start bit

    auto writeU16 = [data_write_u32](uint32_t data)//lambda Function
        {
            data |= (data << 20);
            *data_write_u32 = data;
            *data_write_u32 = (data >> 8);
        };
    
    if((u32)buff & 1){//unaligned
        u8* buff_u8 = (u8*)buff;
        u16 byteHI;
        u16 byteLo;
        for (int i = 0; i < 512; i += 2){
            byteLo = *buff_u8++;
            byteHI = *buff_u8++;
            writeU16((byteHI << 8) | byteLo);
        }
    }else{
        for (int i = 0; i < 512; i += 2){
            writeU16(*buff++);
        }
    }

    
    if ((u32)crc16buff & 1)
    {   
        u8* crc_u8 = (u8*)crc16buff;
        u16 byteHI;
        u16 byteLo;
        for (int i = 0; i < 512; i += 2){
            byteLo = *crc_u8++;
            byteHI = *crc_u8++;
            writeU16((byteHI << 8) | byteLo);
        }
    }else{
        for (int i = 0; i < 4; i++)
        {
            writeU16(*crc16buff++);
        }
    }
    *data_write_u16 = 0xFF; // end bit
    while (((*wait_busy) & SCSD_STS_BUSY))
        ; // Note:这个部分与上个部分是不一样的
}
void sc_mode(u16 mode)
{
    vu16 *sc_mode_addr = (vu16 *)0x09FFFFFE;
    *sc_mode_addr = 0xA55A;
    *sc_mode_addr = 0xA55A;
    *sc_mode_addr = mode;
    *sc_mode_addr = mode;
}

void sc_sdcard_reset(void)
{
    vu16 *reset_addr = (vu16 *)sd_reset;
    *reset_addr = 0xFFFF;
}

void send_clk(u32 num)
{
    vu16 *cmd_addr = (vu16 *)sd_comadd;
    while (num--)
    {
        *cmd_addr;
    }
}

#define REG_SCSD_CMD (*(vu16 *)(0x09800000))
void SDCommand(u8 command, uint8_t num, u32 argument)
{
    u8 databuff[6];
    u8 *tempDataPtr = databuff;

    *tempDataPtr++ = command | 0x40;
    *tempDataPtr++ = argument >> 24;
    *tempDataPtr++ = argument >> 16;
    *tempDataPtr++ = argument >> 8;
    *tempDataPtr++ = argument;
    *tempDataPtr = _SD_CRC7(databuff, 5);

    while (((REG_SCSD_CMD & 0x01) == 0)){}

    REG_SCSD_CMD;

    tempDataPtr = databuff;
    volatile uint32_t *send_command_addr = (volatile uint32_t *)(0x09800000); // 假设sd_comadd也是数据写入地址

    int length = 6;
    while (length--)
    {
        uint32_t data = *tempDataPtr++;
        data = data | (data << 17);
        *send_command_addr = data;
        *send_command_addr = data << 2;
        *send_command_addr = data << 4;
        *send_command_addr = data << 6;
        // sd_dataadd[0] ~ [3]至少目前证明都是镜像的，可以随便用，可以用stmia来加速
        // 本质上是将U16的写合并成U32的写
    }
}
void get_resp_drop()
{
    int byteNum = 6 + 1; // 6resp + 8 clocks

    // Wait for the card to be non-busy
    vu16 *const cmd_addr_u16 = (vu16 *)(0x09800000);
    vu32 *const cmd_addr_u32 = (vu32 *)(0x09800000);
    while ((((*cmd_addr_u16) & 0x01) != 0));

    // 实际上，当跳出这个循环的时候，已经读了一个bit了，后续会多读一个bit，但是这是抛弃的rsp,因此多读一个bit也就是多一个时钟周期罢了
    while (byteNum--)
    {
        *cmd_addr_u32;
        *cmd_addr_u32;
        *cmd_addr_u32;
        *cmd_addr_u32;
    }
}

inline uint8_t CRC7_one(uint8_t crcIn, uint8_t data)
{
    crcIn ^= data;
    return crc7_lut[crcIn];
}
uint8_t _SD_CRC7(uint8_t *pBuf, int len)
{
    uint8_t crc = 0;
    while (len--)
        crc = CRC7_one(crc, *pBuf++);
    crc |= 1;
    return crc;
}

vu16* const REG_SCSD_DATAREAD_ADDR	=	((vu16*)(0x09100000));
vu32* const REG_SCSD_DATAREAD_32_ADDR	=	((vu32*)(0x09100000));
bool  __attribute__((optimize("Ofast")))  _SCSD_readData (void* buffer) {
	u8* buff_u8 = (u8*)buffer;
	u16* buff = (u16*)buffer;
	int i;
	
	i = BUSY_WAIT_TIMEOUT;
	while (((*REG_SCSD_DATAREAD_ADDR) & SCSD_STS_BUSY) && (--i));
	if (i == 0) {
		return false;
	}

	i=256;
	if ((u32)buff_u8 & 0x01) {
		u32 temp;
		while(i--) {
			*REG_SCSD_DATAREAD_32_ADDR;
			temp = (*REG_SCSD_DATAREAD_32_ADDR) >> 16;
			*buff_u8++ = (u8)temp;
			*buff_u8++ = (u8)(temp >> 8);
		}
	} else {
		while(i--) {
			*REG_SCSD_DATAREAD_32_ADDR;
			*buff++ = (*REG_SCSD_DATAREAD_32_ADDR) >> 16; 
		}
	}

	for (i = 0; i < 8; i++) {
		*REG_SCSD_DATAREAD_32_ADDR;
	}
	*REG_SCSD_DATAREAD_ADDR;

	return true;
}

bool MemoryCard_IsInserted(void) {
    uint16_t status = *(vu16*)sd_comadd; // 读取状态寄存器的值
    return (status & 0x300)==0; 
}
void _SD_CRC16 (u8* buff, int buffLength, u8* crc16buff) {
	u32 a, b, c, d;
	u32 bitPattern = 0x80808080;	// 分成4部分，每部分8bit
	const u32 crcConst = 0x1021;	// r8
	u8 dataByte = 0;	// r2

	a = 0;	// r3
	b = 0;	// r4
	c = 0;	// r5
	d = 0;	// r6
	
	
	
	while (buffLength--){
        dataByte = *buff++;
		a = a << 1;
		if ( a & 0x10000) a ^= crcConst;
		if (dataByte & (bitPattern >> 24)) a ^= crcConst;
		
		b = b << 1;
		if (b & 0x10000) b ^= crcConst;
		if (dataByte & (bitPattern >> 25)) b ^= crcConst;
	
		c = c << 1;
		if (c & 0x10000) c ^= crcConst;
		if (dataByte & (bitPattern >> 26)) c ^= crcConst;
		
		d = d << 1;
		if (d & 0x10000) d ^= crcConst;
		if (dataByte & (bitPattern >> 27)) d ^= crcConst;
		
		bitPattern = (bitPattern >> 4) | (bitPattern << 28);
        
		a = a << 1;
		if ( a & 0x10000) a ^= crcConst;
		if (dataByte & (bitPattern >> 24)) a ^= crcConst;
		
		b = b << 1;
		if (b & 0x10000) b ^= crcConst;
		if (dataByte & (bitPattern >> 25)) b ^= crcConst;
	
		c = c << 1;
		if (c & 0x10000) c ^= crcConst;
		if (dataByte & (bitPattern >> 26)) c ^= crcConst;
		
		d = d << 1;
		if (d & 0x10000) d ^= crcConst;
		if (dataByte & (bitPattern >> 27)) d ^= crcConst;
		
		bitPattern = (bitPattern >> 4) | (bitPattern << 28);
	} 
	
	int count = 8;	// buf是8 byte
	while(count--){
		bitPattern = 0;
		if (a & 0x8000) bitPattern |= (1<<7);
		if (b & 0x8000) bitPattern |= (1<<6);
		if (c & 0x8000) bitPattern |= (1<<5);
		if (d & 0x8000) bitPattern |= (1<<4);

		if (a & 0x4000) bitPattern |= (1<<3);
		if (b & 0x4000) bitPattern |= (1<<2);
		if (c & 0x4000) bitPattern |= (1<<1);
		if (d & 0x4000) bitPattern |= (1<<0);
		a = a << 2;
		b = b << 2;
		c = c << 2;
		d = d << 2;
		
		*crc16buff++ = bitPattern;
	}
	
	return;
}