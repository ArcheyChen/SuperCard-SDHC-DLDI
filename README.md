# Ausar's SuperCard-SDHC-DLDI 

## Code Name:**Moon Eclipse**

Original GitHub link: https://github.com/ArcheyChen/SuperCard-SDHC-DLDI

This dldi supports:
1.  SD init
2.  SDHC card (I only tested a 64GB tf card)
3.  Unaligned R/W
4.  Compatiable with TwilightMenu++ / nds-bootstrap (Can boot nds games from Supercard Mini SD now!)

The card I tested:
  `Supercard Mini SD` (if anyone tested on other Supercard GBA cards, please let me know)

Why did I do this project? Want to make a new kernel and make the Supercard less suck :p

There are only two dldis for SCSD before this project:`chishm's dldi` and `Moonlight's dldi`

The chishm's dldi is not working, the moonlight's works, but can't init SD card, which means you need to boot from stock FW(which will do the SD init for you);
and it's not open sourced, and can't support SDHC which mean you stuck at 2GB of SD card, also, it can only do aligned r/w.

## Credits
chishm & libdev : for original code, but was broken on latest toolchains [https://github.com/devkitPro/libgba/blob/master/src/disc_io/io_scsd.c]
supercard team: for the official code, but the readSector function has some bug [http://down.supercard.sc/download/supercard_io.zip]
Moonlight dldi:  I use ida pro to disassemble the scsd_moon.dldi, and get the working readSector function[https://www.chishm.com/DLDI/downloads/scsd_moon.dldi]
profi200: told me some info about faster crc16 codes, which improves the writing speed
