/*
	io_scsd.c 

	Hardware Routines for reading a Secure Digital card
	using the SC SD
	
	Some code based on scsd_c.c, written by Amadeus 
	and Jean-Pierre Thomasset as part of DSLinux.

 Copyright (c) 2006 Michael "Chishm" Chisholm
	
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation and/or
     other materials provided with the distribution.
  3. The name of the author may not be used to endorse or promote products derived
     from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "io_scsd.h"
#include "io_sd_common.h"
#include "io_sc_common.h"
#include <stddef.h>
#include <stdint.h>
// #include <gba.h>

//---------------------------------------------------------------
// SCSD register addresses

#define REG_SCSD_CMD	(*(volatile uint16_t*)(0x09800000))
const volatile uint16_t*  REG_SCSD_CMD_ADDR =	((volatile uint16_t*)(0x09800000));
	/* bit 0: command bit to read  		*/
	/* bit 7: command bit to write 		*/

#define REG_SCSD_DATAWRITE	(*(volatile uint16_t*)(0x09000000))
#define REG_SCSD_DATAREAD	(*(volatile uint16_t*)(0x09100000))
const volatile uint16_t* REG_SCSD_DATAREAD_ADDR	=	((volatile uint16_t*)(0x09100000));
#define REG_SCSD_DATAREAD_HI	(*(volatile uint16_t*)(0x09100002))
#define REG_SCSD_DATAREAD_32	(*(volatile uint32_t*)(0x09100000))
const volatile uint32_t* REG_SCSD_DATAREAD_32_ADDR	=	((volatile uint32_t*)(0x09100000));
#define REG_SCSD_LITE_ENABLE	(*(volatile uint16_t*)(0x09440000))
#define REG_SCSD_LOCK		(*(volatile uint16_t*)(0x09FFFFFE))
	/* bit 0: 1				*/
	/* bit 1: enable IO interface (SD,CF)	*/
	/* bit 2: enable R/W SDRAM access 	*/

//---------------------------------------------------------------
// Responses
#define SCSD_STS_BUSY			0x100
#define SCSD_STS_INSERTED		0x300

//---------------------------------------------------------------
// Send / receive timeouts, to stop infinite wait loops
#define NUM_STARTUP_CLOCKS 100	// Number of empty (0xFF when sending) bytes to send/receive to/from the card
#define TRANSMIT_TIMEOUT 100000 // Time to wait for the SC to respond to transmit or receive requests
#define RESPONSE_TIMEOUT 256	// Number of clocks sent to the SD card before giving up
#define BUSY_WAIT_TIMEOUT 500000
#define WRITE_TIMEOUT	3000	// Time to wait for the card to finish writing

#define BYTES_PER_READ 512

//---------------------------------------------------------------
// Variables required for tracking SD state
static uint32_t _SCSD_relativeCardAddress = 0;	// Preshifted Relative Card Address
static bool isSDHC=false;
//---------------------------------------------------------------
// Internal SC SD functions

extern bool _SCSD_writeData_s (uint8_t *data, uint16_t* crc);

inline void _SCSD_unlock (void) {
	_SC_changeMode (SC_MODE_MEDIA);	
}

static inline void _SCSD_enable_lite (void) {
	REG_SCSD_LITE_ENABLE = 0;
}

static bool _SCSD_sendCommand (uint8_t command, uint32_t argument) {
	uint8_t databuff[6];
	uint8_t *tempDataPtr = databuff;
	int length = 6;
	uint16_t dataByte;
	int curBit;
	int i;

	*tempDataPtr++ = command | 0x40;
	*tempDataPtr++ = argument>>24;
	*tempDataPtr++ = argument>>16;
	*tempDataPtr++ = argument>>8;
	*tempDataPtr++ = argument;
	*tempDataPtr = _SD_CRC7 (databuff);

	i = BUSY_WAIT_TIMEOUT;
	while (((REG_SCSD_CMD & 0x01) == 0) && (--i));
	if (i == 0) {
		return false;
	}
		
	dataByte = REG_SCSD_CMD;

	tempDataPtr = databuff;
	
	while (length--) {
		dataByte = *tempDataPtr++;
		for (curBit = 7; curBit >=0; curBit--){
			REG_SCSD_CMD = dataByte;
			dataByte = dataByte << 1;
		}
	}
	
	return true;
}

// Returns the response from the SD card to a previous command.
static bool _SCSD_getResponse (uint8_t* dest, uint32_t length) {
	uint32_t i;	
	int dataByte;
	int numBits = length * 8;
	
	// Wait for the card to be non-busy
	i = BUSY_WAIT_TIMEOUT;
	while ((REG_SCSD_CMD & 0x01) && (--i));
	if (dest == NULL) {
		return true;
	}
	
	if (i == 0) {
		// Still busy after the timeout has passed
		return false;
	}
	
	// The first bit is always 0
	dataByte = 0;	
	numBits--;
	// Read the remaining bits in the response.
	// It's always most significant bit first
	while (numBits--) {
		dataByte = (dataByte << 1) | ((*REG_SCSD_CMD_ADDR) & 0x01);
		if ((numBits & 0x7) == 0) {
			// It's read a whole byte, so store it
			*dest++ = (uint8_t)dataByte;
			dataByte = 0;
		}
	}

	// Send 16 more clocks, 8 more than the delay required between a response and the next command
	for (i = 0; i < 16; i++) {
		dataByte = REG_SCSD_CMD;
	}
	
	return true;
}

static inline bool _SCSD_getResponse_R1 (uint8_t* dest) {
	return _SCSD_getResponse (dest, 6);
}

static inline bool _SCSD_getResponse_R1b (uint8_t* dest) {
	return _SCSD_getResponse (dest, 6);
}

static inline bool _SCSD_getResponse_R2 (uint8_t* dest) {
	return _SCSD_getResponse (dest, 17);
}

static inline bool _SCSD_getResponse_R3 (uint8_t* dest) {
	return _SCSD_getResponse (dest, 6);
}

static inline bool _SCSD_getResponse_R6 (uint8_t* dest) {
	return _SCSD_getResponse (dest, 6);
}

static void _SCSD_sendClocks (uint32_t numClocks) {
	do {
		REG_SCSD_CMD;
	} while (numClocks--);
}

bool _SCSD_cmd_6byte_response (uint8_t* responseBuffer, uint8_t command, uint32_t data) {
	_SCSD_sendCommand (command, data);
	return _SCSD_getResponse (responseBuffer, 6);
}

bool _SCSD_cmd_17byte_response (uint8_t* responseBuffer, uint8_t command, uint32_t data) {
	_SCSD_sendCommand (command, data);
	return _SCSD_getResponse (responseBuffer, 17);
}


static bool _SCSD_initCard (void) {
	_SCSD_enable_lite();
	
	// Give the card time to stabilise
	_SCSD_sendClocks (NUM_STARTUP_CLOCKS);
	
	// Reset the card
	if (!_SCSD_sendCommand (GO_IDLE_STATE, 0)) {
		return false;
	}

	_SCSD_sendClocks (NUM_STARTUP_CLOCKS);
	
	// Card is now reset, including it's address
	_SCSD_relativeCardAddress = 0;

	// Init the card
	return _SD_InitCard_SDHC (_SCSD_cmd_6byte_response, 
				_SCSD_cmd_17byte_response,
				true,
				&_SCSD_relativeCardAddress,&isSDHC);
}

static bool  __attribute__((optimize("Ofast")))  _SCSD_readData (void* buffer) {
	uint8_t* buff_uint8_t = (uint8_t*)buffer;
	uint16_t* buff = (uint16_t*)buffer;
	// volatile register uint32_t temp;
	int i;
	
	i = BUSY_WAIT_TIMEOUT;
	while (((*REG_SCSD_DATAREAD_ADDR) & SCSD_STS_BUSY) && (--i));
	if (i == 0) {
		return false;
	}

	
	i=256;
	if ((uint32_t)buff_uint8_t & 0x01) {
		uint32_t temp;
		while(i--) {
			*REG_SCSD_DATAREAD_32_ADDR;
			temp = (*REG_SCSD_DATAREAD_32_ADDR) >> 16;
			*buff_uint8_t++ = (uint8_t)temp;
			*buff_uint8_t++ = (uint8_t)(temp >> 8);
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

//---------------------------------------------------------------
// Functions needed for the external interface

bool _SCSD_startUp (void) {
	_SCSD_unlock();
	return _SCSD_initCard();
}

bool _SCSD_isInserted (void) {
	_SCSD_unlock();
	uint8_t responseBuffer [6];

	// Make sure the card receives the command
	if (!_SCSD_sendCommand (SEND_STATUS, 0)) {
		return false;
	}
	// Make sure the card responds
	if (!_SCSD_getResponse_R1 (responseBuffer)) {
		return false;
	}
	// Make sure the card responded correctly
	if (responseBuffer[0] != SEND_STATUS) {
		return false;
	}
	return true;
}

bool _SCSD_readSectors (uint32_t sector, uint32_t numSectors, void* buffer) {
	uint32_t i;
	uint8_t* dest = (uint8_t*) buffer;
	uint8_t responseBuffer[6];
	// uint32_t argument = isSDHC ? sector : sector * BYTES_PER_READ;
	uint32_t argument = isSDHC ? sector : (sector << 9);

	if (numSectors == 1) {
		// If it's only reading one sector, use the (slightly faster) READ_SINGLE_BLOCK
		if (!_SCSD_sendCommand (READ_SINGLE_BLOCK, argument)) {
			return false;
		}

		if (!_SCSD_readData (buffer)) {
			return false;
		}

	} else {
		// Stream the required number of sectors from the card
		if (!_SCSD_sendCommand (READ_MULTIPLE_BLOCK, argument)) {
			return false;
		}
	
		for(i=0; i < numSectors; i++, dest+=BYTES_PER_READ) {
			if (!_SCSD_readData(dest)) {
				return false;
			}
		}
	
		// Stop the streaming
		_SCSD_sendCommand (STOP_TRANSMISSION, 0);
		_SCSD_getResponse_R1b (responseBuffer);
	}

	_SCSD_sendClocks(0x10);
	return true;
}

bool _SCSD_writeSectors (uint32_t sector, uint32_t numSectors, const void* buffer) {
	uint16_t crc[4];	// One per data line
	uint8_t responseBuffer[6];
	uint32_t offset = isSDHC ? sector : sector * BYTES_PER_READ;
	uint8_t* data = (uint8_t*) buffer;
	int i;

	while (numSectors--) {
		// Calculate the CRC16
		_SD_CRC16 ( data, BYTES_PER_READ, (uint8_t*)crc);
		
		// Send write command and get a response
		_SCSD_sendCommand (WRITE_BLOCK, offset);
		if (!_SCSD_getResponse_R1 (responseBuffer)) {
			return false;
		}

		// Send the data and CRC
		if (! _SCSD_writeData_s (data, crc)) {
			return false;
		}
			
		// Send a few clocks to the SD card
		_SCSD_sendClocks(0x10);
		
		if(isSDHC){
			offset++;
		}else{
			offset += BYTES_PER_READ;
		}
		data += BYTES_PER_READ;
		
		// Wait until card is finished programming
		i = WRITE_TIMEOUT;
		responseBuffer[3] = 0;
		do {
			_SCSD_sendCommand (SEND_STATUS, _SCSD_relativeCardAddress);
			_SCSD_getResponse_R1 (responseBuffer);
			i--;
			if (i <= 0) {
				return false;
			}
		} while (((responseBuffer[3] & 0x1f) != ((SD_STATE_TRAN << 1) | READY_FOR_DATA)));
	}
	
	return true;
}

bool _SCSD_clearStatus (void) {
	return _SCSD_initCard ();
}

bool _SCSD_shutdown (void) {
	_SC_changeMode (SC_MODE_RAM_RO);
	return true;
}

// const DISC_INTERFACE _io_scsd = {
// 	DEVICE_TYPE_SCSD,
// 	FEATURE_MEDIUM_CANREAD | FEATURE_MEDIUM_CANWRITE | FEATURE_SLOT_GBA,
// 	(FN_MEDIUM_STARTUP)&_SCSD_startUp,
// 	(FN_MEDIUM_ISINSERTED)&_SCSD_isInserted,
// 	(FN_MEDIUM_READSECTORS)&_SCSD_readSectors,
// 	(FN_MEDIUM_WRITESECTORS)&_SCSD_writeSectors,
// 	(FN_MEDIUM_CLEARSTATUS)&_SCSD_clearStatus,
// 	(FN_MEDIUM_SHUTDOWN)&_SCSD_shutdown
// } ;


