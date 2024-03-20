#include "new_scsdio.h"
#include <cstdio>

bool isSDHC;

uint8_t _SD_CRC7(uint8_t *pBuf, int len);
extern "C" bool _SCSD_readData (void* buffer);

void _SD_CRC16 (u8* buff, int buffLength, u8* crc16buff);

uint64_t sdio_crc16_4bit_checksum(uint32_t *data, uint32_t num_words);
inline uint16_t setFastCNT(uint16_t originData){
/*  2-3   32-pin GBA Slot ROM 1st Access Time (0-3 = 10, 8, 6, 18 cycles)
    4     32-pin GBA Slot ROM 2nd Access Time (0-1 = 6, 4 cycles)*/
    const uint16_t mask = ~(7<<2);//~ 000011100, clear bit 2-3 + 4
    const uint16_t setVal = ((2) << 2) | (1<<4);
    return (originData & mask) | setVal;
}
vu16* const EXMEMCNT_ADDR = (vu16*)0x4000204;

extern "C" void get_resp_drop(int dropBytes=6);
void WriteSector(u8 *buff, u32 sector, u32 writenum)
{
    u64 crc16;//并行4个
    sc_mode(en_sdcard);
    sc_sdcard_reset();
    auto param = isSDHC ? sector : (sector << 9);
    if(writenum == 1){
        SDCommand(24, param);
        get_resp_drop();
        crc16 = sdio_crc16_4bit_checksum((u32 *)(buff),512/sizeof(u32));
        sd_data_write((u16 *)(buff), (u16 *)(&crc16));
    }else
    {
        SDCommand(25, param);
        get_resp_drop();
        for (auto buffEnd = buff + writenum * 512 ; buff < buffEnd; buff += 512)
        {
            crc16 = sdio_crc16_4bit_checksum((u32 *)(buff),512/sizeof(u32));
            sd_data_write((u16 *)(buff), (u16 *)(&crc16));
            send_clk(0x10);
        }
        SDCommand(12, 0);
        get_resp_drop();
    }
    send_clk(0x10);
    vu16 *wait_busy = (vu16 *)sd_dataadd;
    while (((*wait_busy) & 0x0100) == 0)
        ;
    return;
}

void ReadSector(uint8_t *buff, uint32_t sector, uint32_t readnum)
{
    u16 originMemStat = *EXMEMCNT_ADDR;
    *EXMEMCNT_ADDR = setFastCNT(originMemStat);   
    auto param = isSDHC ? sector : (sector << 9);
    if(readnum == 1){
        SDCommand(17, param);
        _SCSD_readData(buff);
    }else{
        SDCommand(0x12,param); // R0 = 0x12, R1 = 0, R2 as calculated above
    
        for(auto buffer_end = buff + readnum*(512);buff<buffer_end;buff+=512)
        {
            _SCSD_readData(buff); // Add R6, left shifted by 9, to R4 before casting
        }
        SDCommand(0xC, 0); // Command to presumably stop reading
        get_resp_drop();           // Get response from SD card
    }

    send_clk(0x10);       // Send clock signal
    *EXMEMCNT_ADDR = originMemStat;
}

#define BUSY_WAIT_TIMEOUT 500000
#define SCSD_STS_BUSY 0x100

void sd_data_write(u16 *buff, u16 *crc16buff)
{
    u16 originMemStat = *EXMEMCNT_ADDR;
    *EXMEMCNT_ADDR = setFastCNT(originMemStat);   
    vu16 *const wait_busy = (vu16 *)sd_dataadd;
    vu16 *const data_write_u16 = (vu16 *)sd_dataadd;
    vu32 *const data_write_u32 = (vu32 *)sd_dataadd;
    while (!((*wait_busy) & SCSD_STS_BUSY))
        ; // Note:两边的等待是不一致的
    *wait_busy;

    *data_write_u16 = 0; // start bit

    auto writeU16 = [data_write_u32](uint32_t data)//lambda Function
        {
            // data |= (data << 20);    //strange, it works without
            *data_write_u32 = data;
            *data_write_u32 = (data >> 8);
        };
    auto writeU32 = [data_write_u32](uint32_t data)//lambda Function
        {
            *data_write_u32 = data;
            *data_write_u32 = (data >> 8);
            *data_write_u32 = (data >> 16);
            *data_write_u32 = (data >> 24);//居然能用
        };

#define WRITE_U16 \
    "ldrh r0, [%0], #2\n" \
    "lsr r1, r0, #8\n" \
    "stmia %1, {r0-r1}\n"
#define WRITE_U32 \
    "ldr r0, [%0], #4\n"   \
    "lsr r1, r0, #8\n"     \
    "lsr r2, r0, #16\n"    \
    "lsr r3, r0, #24\n"    \
    "stmia %1, {r0-r3}\n"

    if((u32)buff & 1){//unaligned
        u8* buff_u8 = (u8*)buff;
        u16 byteHI;
        u16 byteLo;
        for (int i = 0; i < 512; i += 2){
            byteLo = *buff_u8++;
            byteHI = *buff_u8++;
            writeU16((byteHI << 8) | byteLo);
        }
    }
    // else if((u32)buff & 2){//u16 aligned
    //     asm volatile(
    //         WRITE_U16
    //         WRITE_U32
    //     "1:\n"
    //         WRITE_U32
    //         WRITE_U32
    //         "cmp %0, %2\n"
    //         "blt 1b\n"
    //         WRITE_U16
    //         : // 没有输出
    //         : "r"((u32)buff),"r"((u32)data_write_u32),"r"(((u32)buff) + 510/*512-2*/)
    //         : "r0", "r1", "r2", "r3", "cc"// 破坏列表
    //     );
    // }
    else{//u32 aligned
        asm volatile(
        "2:\n"
            WRITE_U16
            WRITE_U16
            "cmp %0, %2\n"
            "blt 2b"
            : // 没有输出
            : "r"((u32)buff),"r"((u32)data_write_u32),"r"(((u32)buff) + 512)
            : "r0", "r1", "r2", "r3", "cc"// 破坏列表
        );
        // u32 *buff_u32 = (u32*)buff;
        // for (int i = 0; i < 512; i += 4){
        //     writeU32(*buff_u32++);
        // }//or?
        // for (int i = 0; i < 512; i += 2){
        //     writeU16(*buff++);
        // }
    }

    
    if ((u32)crc16buff & 1)
    {   
        u8* crc_u8 = (u8*)crc16buff;
        u16 byteHI;
        u16 byteLo;
        for (int i = 0; i < 4; i ++){
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
    *EXMEMCNT_ADDR = originMemStat;
}
void sc_mode(u16 mode)
{
    vu16 *sc_mode_addr = (vu16 *)0x09FFFFFE;
    *sc_mode_addr = 0xA55A;
    *sc_mode_addr = 0xA55A;
    *sc_mode_addr = mode;
    *sc_mode_addr = mode;
}

inline void sc_sdcard_reset(void)
{
    vu16 *reset_addr = (vu16 *)sd_reset;
    *reset_addr = 0xFFFF;
}
inline void sc_sdcard_enable_lite(void)//what was that?
{
    vu16 *reset_addr = (vu16 *)sd_reset;
    *reset_addr = 0;
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
void SDCommand(u8 command, u32 argument)
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

    #define SEND_ONE_COMMAND_BYTE \
        "ldrb r0, [%0], #1 \n"         /* uint32_t data = *tempDataPtr++; */ \
        "orr  r0, r0, r0, lsl #17 \n"  /* data = data | (data << 17); */ \
        "mov  r1, r0, lsl #2 \n"       \
        "mov  r2, r0, lsl #4 \n"       \
        "mov  r3, r0, lsl #6 \n"       \
        "stmia %1, {r0-r3}\n"
    asm volatile(//发送6次
    "1:\n"
        SEND_ONE_COMMAND_BYTE
        SEND_ONE_COMMAND_BYTE
        "cmp %0, %2\n"
        "blt 1b"
        : // 没有输出
        : "r"((u32)databuff),"r"((u32)send_command_addr),"r"(((u32)databuff) + 6)
        : "r0", "r1", "r2", "r3"// 破坏列表
    );
    // int length = 6;
    // while (length--)
    // {
    //     uint32_t data = *tempDataPtr++;
    //     data = data | (data << 17);
    //     *send_command_addr = data;
    //     *send_command_addr = data << 2;
    //     *send_command_addr = data << 4;
    //     *send_command_addr = data << 6;
    //     // sd_dataadd[0] ~ [3]至少目前证明都是镜像的，可以随便用，可以用stmia来加速
    //     // 本质上是将U16的写合并成U32的写
    // }
}
void inline  get_resp_drop(int byteNum)
{
    byteNum++; // +  8 clocks

    // Wait for the card to be non-busy
    vu16 *const cmd_addr_u16 = (vu16 *)(0x09800000);
    vu32 *const cmd_addr_u32 = (vu32 *)(0x09800000);
    while ((((*cmd_addr_u16) & 0x01) != 0));

    // 实际上，当跳出这个循环的时候，已经读了一个bit了，后续会多读一个bit，但是这是抛弃的rsp,因此多读一个bit也就是多一个时钟周期罢了
    
   asm volatile(
        "2: \n\t"
            "ldmia %0, {r0-r3} \n\t"   
            "subs %1, %1, #1 \n\t"   // byteNum--
            "bne 2b \n\t"            // 如果byteNum不为0，则继续循环
        : // 没有输出
        : "r" (cmd_addr_u32), "r" (byteNum) // 输入
        : "r0", "r1", "r2", "r3", "cc" // 破坏列表
    );
    // while (byteNum--)
    // {
    //     *cmd_addr_u32;
    //     *cmd_addr_u32;
    //     *cmd_addr_u32;
    //     *cmd_addr_u32;
    // }
}

inline uint8_t CRC7_one(uint8_t crcIn, uint8_t data)
{
    crcIn ^= data;
    return crc7_lut[crcIn];
}
inline uint8_t _SD_CRC7(uint8_t *pBuf, int len)
{
    uint8_t crc = 0;
    while (len--)
        crc = CRC7_one(crc, *pBuf++);
    crc |= 1;
    return crc;
}

vu16* const REG_SCSD_DATAREAD_ADDR	=	((vu16*)(0x09100000));
vu32* const REG_SCSD_DATAREAD_32_ADDR	=	((vu32*)(0x09100000));
bool  _SCSD_readData (void* buffer) {
	u8* buff_u8 = (u8*)buffer;
	int i;
	
	i = BUSY_WAIT_TIMEOUT;
	while (((*REG_SCSD_DATAREAD_ADDR) & SCSD_STS_BUSY));
	if (i == 0) {
		return false;
	}

    const u32 maskHi = 0xFFFF0000;
    #define LOAD_U32_ALIGNED_2WORDS \
            "ldmia  %2, {r0-r7} \n"   /*从REG_SCSD_DATAREAD_32_ADDR读取8个32位值到r0-r7*/  \
            "and    r3, r3, %3 \n"     /*r3 &= maskHi*/                                     \
            "and    r7, r7, %3 \n"     \
            "orr    r3, r3, r1, lsr #16 \n"     /*r3 |= (r1 >> 16)*/                        \
            "orr    r7, r7, r5, lsr #16 \n"     \
            "stmia  %0!, {r3,r7} \n"  /*将r3和r7的值存储到buff_u32指向的位置，并将buff_u32增加8字节*/

    #define LOAD_U32_ALIGNED_1WORDS \
            "ldmia  %2, {r0-r3} \n"   /*从REG_SCSD_DATAREAD_32_ADDR读取8个32位值到r0-r7*/  \
            "and    r3, r3, %3 \n"     /*r3 &= maskHi*/                                     \
            "orr    r3, r3, r1, lsr #16 \n"     /*r3 |= (r1 >> 16)*/                        \
            "str    r3, [%0], #4 \n"  /*将r3和r7的值存储到buff_u32指向的位置，并将buff_u32增加8字节*/

    #define LOAD_U32_ALIGNED_U16 \
            "ldmia  %2, {r0-r1} \n"   /*从REG_SCSD_DATAREAD_32_ADDR读取8个32位值到r0-r7*/  \
            "lsr r1, r1, #16\n"     \
            "strh r1, [%0], #2\n"       //u16


	if (((u32)buff_u8 & 0x03) == 0){//u32 aligned
        asm volatile(
        "1: \n"
            LOAD_U32_ALIGNED_2WORDS
            LOAD_U32_ALIGNED_2WORDS
            "cmp    %0, %1 \n"
            "blt    1b \n"              // if buffer<bufferEnd continue;
            :
            : "r" (buffer), "r" ((u32)buffer+512), "r" (REG_SCSD_DATAREAD_32_ADDR), "r" (maskHi)
            : "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "memory","cc"
        );
	    // i=256;
        // u16* buff = (u16*)buffer;
		// while(i--) {

		// 	*(REG_SCSD_DATAREAD_32_ADDR);
		// 	*buff++ = (*(REG_SCSD_DATAREAD_32_ADDR)) >> 16; 
		// }
	}else if ((((u32)buff_u8 & 0x01) == 0)) {//u16 aligned
		asm volatile(
            LOAD_U32_ALIGNED_U16
            LOAD_U32_ALIGNED_1WORDS
            LOAD_U32_ALIGNED_2WORDS
        "2: \n"
            LOAD_U32_ALIGNED_2WORDS
            LOAD_U32_ALIGNED_2WORDS
            "cmp    %0, %1 \n"
            "blt    2b \n"              // if buffer<bufferEnd continue;
            LOAD_U32_ALIGNED_U16
            :
            : "r" (buffer), "r" ((u32)buffer+510/*512-2*/), "r" (REG_SCSD_DATAREAD_32_ADDR), "r" (maskHi)
            : "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "memory","cc"
        );
	} else 
    {
		u32 temp;
	    i=256;
		while(i--) {
			*REG_SCSD_DATAREAD_32_ADDR;
			temp = (*REG_SCSD_DATAREAD_32_ADDR) >> 16;
			*buff_u8++ = (u8)temp;
			*buff_u8++ = (u8)(temp >> 8);
		}
	}

    asm volatile(
            "ldmia  %0, {r0-r7} \n"   // drop the crc16
            "ldrh   r1, [%0] \n"   // end
            :
            : "r" ((u32)REG_SCSD_DATAREAD_32_ADDR)
            : "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7"
        );
	// for (i = 0; i < 8; i++) {
	// 	*REG_SCSD_DATAREAD_32_ADDR;
	// }
	// *REG_SCSD_DATAREAD_ADDR;

	return true;
}

bool MemoryCard_IsInserted(void) {
    uint16_t status = *(vu16*)sd_comadd; // 读取状态寄存器的值
    return (status & 0x300)==0; 
}

uint64_t inline calSingleCRC16(uint64_t crc,uint32_t data_in){
    // Shift out 8 bits for each line
    uint32_t data_out = crc >> 32;
    crc <<= 32;

    // XOR outgoing data to itself with 4 bit delay
    data_out ^= (data_out >> 16);

    // XOR incoming data to outgoing data with 4 bit delay
    data_out ^= (data_in >> 16);

    // XOR outgoing and incoming data to accumulator at each tap
    uint64_t xorred = data_out ^ data_in;
    crc ^= xorred;
    crc ^= xorred << (5 * 4);
    crc ^= xorred << (12 * 4);
    return crc;
}

uint32_t loadBigEndU32_u8(u8* &dataBuf){
    u32 data;
    data = (*dataBuf++) << 24;
    data |= (*dataBuf++) << 16;
    data |= (*dataBuf++) << 8;
    data |= (*dataBuf++);
    return data;
}
uint64_t sdio_crc16_4bit_checksum(uint32_t *dataBuf, uint32_t num_words)
{
    uint64_t crc = 0;
    if((uintptr_t)dataBuf & 3){//u8 align
        uint8_t *data = (u8*)dataBuf;
        uint8_t *end = (u8*)(dataBuf + num_words);
        while (data < end)
        {
            uint32_t data_in = loadBigEndU32_u8(data);
            crc = calSingleCRC16(crc,data_in);
        }
    }else{
        uint32_t *data = dataBuf;
        uint32_t *end = dataBuf + num_words;
        while (data < end)
        {
            uint32_t data_in = __builtin_bswap32(*data++);
            crc = calSingleCRC16(crc,data_in);
        }
    }


    return __builtin_bswap64(crc);
}
bool get_resp (u8* dest, u32 length) {
	u32 i;	
	int numBits = length * 8;
	
	i = BUSY_WAIT_TIMEOUT;
	while (((REG_SCSD_CMD & 0x01) != 0) && (--i));
	if (i == 0) {
		return false;
	}
	
	// The first bit is always 0
	vu16* const cmd_addr_u16 = (vu16*)(0x09800000);
	vu32* const cmd_addr_u32 = (vu32*)(0x09800000);

	u32 partial_result = ((*cmd_addr_u16) & 0x01) << 16 ;
	numBits-=2;
	// Read the remaining bits in the response.
	// It's always most significant bit first
	const u32 mask_2bit = 0x10001;
	while (numBits) {
		numBits-=2;
		partial_result = (partial_result << 2) | ((*cmd_addr_u32) & mask_2bit);
		if ((numBits & 0x7) == 0) {
			//_1_3_5_7 _0_2_4_6 
			*dest++ = ((partial_result >> 16) | (partial_result<<1));
			partial_result = 0;
		}
	}
	for (i = 0; i < 4; i++) {//8clock
		*cmd_addr_u32;
	}
	return true;
}
#define NUM_STARTUP_CLOCKS 30000
#define GO_IDLE_STATE 0
#define CMD8 8
#define APP_CMD 55
#define CMD58 58
#define SD_APP_OP_COND 41
#define MAX_STARTUP_TRIES 5000
#define SD_OCR_VALUE 0x00030000
//2.8V to 3.0V
#define ALL_SEND_CID 2
#define SEND_RELATIVE_ADDR 3
#define SD_STATE_STBY 3
#define SEND_CSD 9
#define SELECT_CARD 7
#define SET_BUS_WIDTH 6
#define SET_BLOCKLEN 16
#define RESPONSE_TIMEOUT 256
#define SEND_STATUS 13
#define SD_STATE_TRAN 4
#define READY_FOR_DATA 1
u32 relativeCardAddress = 0;
bool cmd_and_response (u32 respLenth,u8* responseBuffer, u8 command, u32 data) {
	SDCommand (command, data);
	return get_resp (responseBuffer, respLenth);
}
void cmd_and_response_drop (u32 respLenth,u8* responseBuffer, u8 command, u32 data) {
	SDCommand (command, data);
	get_resp_drop (respLenth);
}


bool init_sd(){
    sc_sdcard_reset();//do we really need that?
	send_clk (NUM_STARTUP_CLOCKS);
    SDCommand (GO_IDLE_STATE, 0);
	send_clk (NUM_STARTUP_CLOCKS);
    int i;

	u8 responseBuffer[17] = {0};
    isSDHC = false;
    bool cmd8Response = cmd_and_response(17,responseBuffer, CMD8, 0x1AA);
    if (cmd8Response && responseBuffer[0] == CMD8 && responseBuffer[1] == 0 && responseBuffer[2] == 0 && responseBuffer[3] == 0x1 && responseBuffer[4] == 0xAA) {
        isSDHC = true;//might be
    }
    for (i = 0; i < MAX_STARTUP_TRIES; i++) {
        cmd_and_response(6,responseBuffer, APP_CMD, 0);
		if (responseBuffer[0] != APP_CMD) {	
			return false;
		}

		// u32 arg = SD_OCR_VALUE;
		u32 arg = 0;
        arg |= (1<<28); //Max performance
        arg |= (1<<20); //3.3v
        if(isSDHC)
            arg |= (1<<30); // Set HCS bit,Supports SDHC

		if (cmd_and_response(6,responseBuffer, SD_APP_OP_COND, arg) &&//ACMD41
			((responseBuffer[1] & (1<<7)) != 0)/*Busy:0b:initing 1b:init completed*/) {
            bool CCS = responseBuffer[1] & (1<<6);//0b:SDSC  1b:SDHC/SDXC
            if(!CCS && isSDHC)
                isSDHC = false;
			break; // Card is ready
		}
	    send_clk (NUM_STARTUP_CLOCKS);
	}

	if (i >= MAX_STARTUP_TRIES) {
		return false;
	}

    // The card's name, as assigned by the manufacturer
	cmd_and_response_drop (17,responseBuffer, ALL_SEND_CID, 0);
	// Get a new address
	for (i = 0; i < MAX_STARTUP_TRIES ; i++) {
		cmd_and_response (6,responseBuffer, SEND_RELATIVE_ADDR, 0);
		relativeCardAddress = (responseBuffer[1] << 24) | (responseBuffer[2] << 16);
		if ((responseBuffer[3] & 0x1e) != (SD_STATE_STBY << 1)) {
			break;
		}
	}
 	if (i >= MAX_STARTUP_TRIES) {
		return false;
	}

	// Some cards won't go to higher speeds unless they think you checked their capabilities
	cmd_and_response_drop (17,responseBuffer, SEND_CSD, relativeCardAddress);
 
	// Only this card should respond to all future commands
	cmd_and_response_drop (6,responseBuffer, SELECT_CARD, relativeCardAddress);
 
    // Set a 4 bit data bus
    cmd_and_response_drop (6,responseBuffer, APP_CMD, relativeCardAddress);
    cmd_and_response_drop (6,responseBuffer, SET_BUS_WIDTH, 2); // 4-bit mode.
	
	return true;
}