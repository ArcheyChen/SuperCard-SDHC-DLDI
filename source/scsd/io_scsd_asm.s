    .TEXT
@ support Super card sd/mini sd/micro sd/rumble

@--------------------------------sd data--------------------------------
.equ sd_comadd,0x9800000
.equ sd_dataadd,0x9000000  
.equ sd_dataradd,0x9100000
.equ sd_reset,0x9440000

.equ en_fireware,0
.equ en_sdram,1
.equ en_sdcard,2
.equ en_write,4
.equ en_rumble,8
.equ en_rumble_user_flash,1
@rumble ver, user flash size:8Mbit,Sector Size:0x10000,map:0x8000000 - 0x80fffff
.GLOBAL sc_MemoryCard_IsInserted

@----------void sd_data_read_s(u16 *buff)-------------
    .ALIGN
   .GLOBAL sd_data_read_s	
    .CODE 32
sd_data_read_s:
	stmfd   r13!,{r4}
	mov	r1,#sd_dataradd
sd_data_read_loop1:
	ldrh	r3,[r1]   @star bit
	tst	r3,#0x100
	bne	sd_data_read_loop1
	mov	r2,#512
sd_data_read_loop:
	ldmia	r1,{r3-r4} 
	mov	r3,r4,lsr #16
	strh	r3 ,[r0],#2

	ldmia	r1,{r3-r4} 
	mov	r3,r4,lsr #16
	strh	r3 ,[r0],#2

	ldmia	r1,{r3-r4} 
	mov	r3,r4,lsr #16
	strh	r3 ,[r0],#2

	ldmia	r1,{r3-r4} 
	mov	r3,r4,lsr #16
	strh	r3 ,[r0],#2

	ldmia	r1,{r3-r4} 
	mov	r3,r4,lsr #16
	strh	r3 ,[r0],#2

	ldmia	r1,{r3-r4} 
	mov	r3,r4,lsr #16
	strh	r3 ,[r0],#2

	ldmia	r1,{r3-r4} 
	mov	r3,r4,lsr #16
	strh	r3 ,[r0],#2

	ldmia	r1,{r3-r4} 
	mov	r3,r4,lsr #16
	strh	r3 ,[r0],#2

    subs    r2, r2, #16                
    bne     sd_data_read_loop 

	ldmia	r1,{r3-r4} @crc 16
	ldmia	r1,{r3-r4}  
	ldmia	r1,{r3-r4}  
	ldmia	r1,{r3-r4}  
	ldrh	r3,[r1]    @end bit
	ldmfd	r13!,{r4}  
	bx      r14
@----------end sd_data_read_s-------------

@------void sd_crc16_s(u16* buff,u16 num,u16* crc16buff)
    .ALIGN
   .GLOBAL	sd_crc16_s 
    .CODE 32
sd_crc16_s:
	stmfd   r13!,{r4-r9}
	mov	r9,r2

	mov	r3,#0  
	mov	r4,#0  
	mov	r5,#0  
	mov	r6,#0  

	ldr	r7,=0x80808080
	ldr	r8,=0x1021
	mov	r1,r1,lsl #3
sd_crc16_loop:

	tst	r7,#0x80
	ldrneb	r2,[r0],#1

	mov	r3,r3,lsl #1
	tst	r3,#0x10000
	eorne	r3,r3,r8
	tst	r2,r7,lsr #24
	eorne	r3,r3,r8
	
	mov	r4,r4,lsl #1
	tst	r4,#0x10000
	eorne	r4,r4,r8
	tst	r2,r7,lsr #25
	eorne	r4,r4,r8
	
	mov	r5,r5,lsl #1
	tst	r5,#0x10000
	eorne	r5,r5,r8
	tst	r2,r7,lsr #26
	eorne	r5,r5,r8
	
	mov	r6,r6,lsl #1
	tst	r6,#0x10000
	eorne	r6,r6,r8
	tst	r2,r7,lsr #27
	eorne	r6,r6,r8

	mov	r7,r7,ror #4
	subs	r1,r1,#4
    bne     sd_crc16_loop 

	mov	r2,r9
	mov	r8,#16
sd_crc16_write_data:
	mov	r7,r7,lsl #4
	tst	r3,#0x8000
	orrne	r7,r7,#8
	tst	r4,#0x8000
	orrne	r7,r7,#4
	tst	r5,#0x8000
	orrne	r7,r7,#2
	tst	r6,#0x8000
	orrne	r7,r7,#1

	mov	r3,r3,lsl #1
	mov	r4,r4,lsl #1
	mov	r5,r5,lsl #1
	mov	r6,r6,lsl #1

	sub	r8,r8,#1
	tst	r8,#1
	streqb	r7,[r2],#1
	cmp	r8,#0
	bne	sd_crc16_write_data

	ldmfd	r13!,{r4-r9}
	bx      r14
@------end sd_com_crc16_s-----------------------------------

@--------------sd_com_read_s(u16* buff,u32 num)------------------
		.ALIGN
@		.GLOBAL	 sd_com_read_s 
		.CODE 32

sd_com_read_s:
	stmfd   r13!,{r4-r6}
	mov	r2,#sd_comadd
sd_com_read_loop1:
	ldrh	r3,[r2] @star bit
	tst	r3,#1
	bne	sd_com_read_loop1

sd_com_read_loop:
	ldmia   r2,{r3-r6}
    subs    r1, r1, #1                  
    bne     sd_com_read_loop  
	ldmfd	r13!,{r4-r6}
	bx      r14
@--------------end sd_com_read_s------------------

@-------------------void sd_com_write_s(u16* buff,u32 num)-----------------

		.ALIGN
@		.GLOBAL	 sd_com_write_s
		.CODE 32
sd_com_write_s:
	stmfd   r13!,{r4-r6}
	
	mov	r2,#sd_comadd
sd_com_write_busy:
	ldrh	r3,[r2]   
	tst	r3,#0x1
	beq	sd_com_write_busy

	ldrh	r3,[r2]  

sd_com_write_loop:
    ldrb   r3,[r0],#1
	add	r3,r3,r3,lsl #17
	mov	r4,r3,lsl #2
	mov	r5,r4,lsl #2
	mov	r6,r5,lsl #2
    stmia   r2,{r3-r6}  
    subs    r1, r1, #1                  
    bne     sd_com_write_loop  
	ldmfd   r13!,{r4-r6}

	bx      r14
@-------------------end sd_com_write_s-----------------

@-----------void get_resp(void)-------------------


		.ALIGN
		.GLOBAL	 get_resp @ r0:Srcp r1:num ok
		.CODE 32
get_resp:

	stmfd   r13!,{r14}
	mov	r1,#6
	bl	sd_com_read_s
	ldmfd   r13!,{r15}
@-----------end get_resp-------------------


@----------------bool MemoryCard_IsInserted(void)---------------
		.ALIGN
@		.GLOBAL	 MemoryCard_IsInserted
		.CODE 32
sc_MemoryCard_IsInserted:
	ldr	r0,=sd_comadd
	ldrh	r1,[r0]
	tst	r1,#0x300
	moveq	r0,#1
	movne	r0,#0
	bx	r14
@----------------end MemoryCard_IsInserted---------------

@-----------------------------------------------
    .END











