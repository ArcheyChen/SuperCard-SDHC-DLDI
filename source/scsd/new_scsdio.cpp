#include "new_scsdio.h"
#include <cstdio>

extern bool isSDHC;
void WriteSector(u16 *buff,u32 sector,u32 writenum)
{
    u32 crc16[5];   
    sc_mode(en_sdcard);
    sc_sdcard_reset();
    // auto param = isSDHC ? sector : (sector << 9);
    auto param = (sector << 9);
	SDCommand(25,0,param); 
	get_resp_drop();
	send_clk(0x10); 
    for (u32 j=0;j<writenum ; j++)
	{
		sd_crc16_s((u16*)((u32)buff+j*512),512,(u16*)crc16);
		sd_data_write((u16*)((u32)buff+j*512),(u16*)crc16);
		send_clk(0x10); 
	}
	SDCommand(12,0,0); 
	get_resp_drop();
	send_clk(0x10);
    vu16* wait_busy = (vu16*)sd_dataadd;
	while(((*wait_busy) &0x0100)==0);
    return;
}


#define BUSY_WAIT_TIMEOUT 500000
#define SCSD_STS_BUSY			0x100


void sd_data_write(u16 *buff,u16* crc16buff) {
    vu16* const wait_busy = (vu16*)sd_dataadd;
    vu16* const data_write_u16 = (vu16*)sd_dataadd;
    vu32* const data_write_u32 = (vu32*)sd_dataadd;
	while(!((*wait_busy) &0x0100));//Note:两边的等待是不一致的
    *wait_busy;

    *data_write_u16 = 0;//start bit

    auto writeU16 = [data_write_u32](uint32_t data) {
        data |= (data << 20);
        *data_write_u32 = data;
        *data_write_u32 = (data >> 8);
    };
    for(int i=0;i<512;i+=2){
        writeU16(*buff++);
    }
    if(crc16buff){
        for(int i=0;i<4;i++){
            writeU16(*crc16buff++);
        }
    }
    *data_write_u16 = 0xFF;//end bit
	while(((*wait_busy) &0x0100));//Note:这个部分与上个部分是不一样的
}
void  sc_mode(u16 mode) {
   // if(currentMode == mode)
   //    return;
	vu16 *sc_mode_addr = (vu16*)0x09FFFFFE;
	*sc_mode_addr = 0xA55A ;
	*sc_mode_addr = 0xA55A ;
	*sc_mode_addr = mode ;
	*sc_mode_addr = mode ;
   // currentMode = mode;
} 

void sc_sdcard_reset(void){
    vu16* reset_addr = (vu16*)sd_reset;
    *reset_addr = 0xFFFF;
}

void send_clk(u32 num){
    vu16* cmd_addr = (vu16*)sd_comadd;
    while(num--){
        *cmd_addr;
    }
}

#define REG_SCSD_CMD	(*(vu16*)(0x09800000))
void SDCommand (u8 command, uint8_t num, u32 argument) {
	u8 databuff[6];
	u8 *tempDataPtr = databuff;

	*tempDataPtr++ = command | 0x40;
	*tempDataPtr++ = argument>>24;
	*tempDataPtr++ = argument>>16;
	*tempDataPtr++ = argument>>8;
	*tempDataPtr++ = argument;
	*tempDataPtr = sd_crc7_s ((u16*)databuff, 5);


	while (((REG_SCSD_CMD & 0x01) == 0));
		
	REG_SCSD_CMD;

	tempDataPtr = databuff;
	volatile uint32_t* send_command_addr = (volatile uint32_t*)(0x09800000); // 假设sd_comadd也是数据写入地址

	int length = 6;
	while (length--) {
		uint32_t data = *tempDataPtr++;
        data = data | (data << 17);
        *send_command_addr = data; 
        *send_command_addr = data<<2; 
        *send_command_addr = data<<4; 
        *send_command_addr = data<<6; 
		//sd_dataadd[0] ~ [3]至少目前证明都是镜像的，可以随便用，可以用stmia来加速
		//本质上是将U16的写合并成U32的写
	}
}
void get_resp_drop() {
	int byteNum = 6 + 1;//6resp + 8 clocks
	
	// Wait for the card to be non-busy
	vu16* const cmd_addr_u16 = (vu16*)(0x09800000);
	vu32* const cmd_addr_u32 = (vu32*)(0x09800000);
	while ((((*cmd_addr_u16) & 0x01) != 0));
    //实际上，当跳出这个循环的时候，已经读了一个bit了，后续会多读一个bit，但是这是抛弃的rsp,因此多读一个bit也就是多一个时钟周期罢了
	while(byteNum--){
        *cmd_addr_u32;
        *cmd_addr_u32;
        *cmd_addr_u32;
        *cmd_addr_u32;
    }
}