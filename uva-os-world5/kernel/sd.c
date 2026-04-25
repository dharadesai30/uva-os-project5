#define K2_DEBUG_WARN
// #define K2_DEBUG_VERBOSE

/*
 * Limitation: does not work if the kernel boots from uboot. Cf sd_init() - FL
 * 
 * Copyright (C) 2018 bzt (bztsrc@github)
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * 
 * https://github.com/bztsrc/raspi3-tutorial/blob/master/15_writesector/sd.c
 */

#include "utils.h"
#include "plat.h"
#include "spinlock.h"
#include "sleeplock.h"

// fxl: api commpatibility 
static void wait_cycles(unsigned int n) {delay(n);}
static void wait_msec(unsigned int n) {ms_delay(n);}

#if K2_ACTUAL_DEBUG_LEVEL <= 20     // "V"
void uart_puts(char *s) {printf("%s", s);}
void uart_hex(unsigned int d) {printf("0x%x", d);}
#else
void uart_puts(char *s) {}
void uart_hex(unsigned int d) {}
#endif

#define MMIO_BASE       (VA_START + 0x3F000000)     

// https://raw.githubusercontent.com/bztsrc/raspi3-tutorial/master/15_writesector/gpio.h
// TOOD clean up & merge with plat-rpi3qemu.h
#define GPFSEL0         ((volatile unsigned int*)(MMIO_BASE+0x00200000))
#define GPFSEL1         ((volatile unsigned int*)(MMIO_BASE+0x00200004))
#define GPFSEL2         ((volatile unsigned int*)(MMIO_BASE+0x00200008))
#define GPFSEL3         ((volatile unsigned int*)(MMIO_BASE+0x0020000C))
#define GPFSEL4         ((volatile unsigned int*)(MMIO_BASE+0x00200010))
#define GPFSEL5         ((volatile unsigned int*)(MMIO_BASE+0x00200014))
#define GPSET0          ((volatile unsigned int*)(MMIO_BASE+0x0020001C))
#define GPSET1          ((volatile unsigned int*)(MMIO_BASE+0x00200020))
#define GPCLR0          ((volatile unsigned int*)(MMIO_BASE+0x00200028))
#define GPLEV0          ((volatile unsigned int*)(MMIO_BASE+0x00200034))
#define GPLEV1          ((volatile unsigned int*)(MMIO_BASE+0x00200038))
#define GPEDS0          ((volatile unsigned int*)(MMIO_BASE+0x00200040))
#define GPEDS1          ((volatile unsigned int*)(MMIO_BASE+0x00200044))
#define GPHEN0          ((volatile unsigned int*)(MMIO_BASE+0x00200064))
#define GPHEN1          ((volatile unsigned int*)(MMIO_BASE+0x00200068))
#define GPPUD           ((volatile unsigned int*)(MMIO_BASE+0x00200094))
#define GPPUDCLK0       ((volatile unsigned int*)(MMIO_BASE+0x00200098))
#define GPPUDCLK1       ((volatile unsigned int*)(MMIO_BASE+0x0020009C))

#define EMMC_ARG2           ((volatile unsigned int*)(MMIO_BASE+0x00300000))
#define EMMC_BLKSIZECNT     ((volatile unsigned int*)(MMIO_BASE+0x00300004))
#define EMMC_ARG1           ((volatile unsigned int*)(MMIO_BASE+0x00300008))
#define EMMC_CMDTM          ((volatile unsigned int*)(MMIO_BASE+0x0030000C))
#define EMMC_RESP0          ((volatile unsigned int*)(MMIO_BASE+0x00300010))
#define EMMC_RESP1          ((volatile unsigned int*)(MMIO_BASE+0x00300014))
#define EMMC_RESP2          ((volatile unsigned int*)(MMIO_BASE+0x00300018))
#define EMMC_RESP3          ((volatile unsigned int*)(MMIO_BASE+0x0030001C))
#define EMMC_DATA           ((volatile unsigned int*)(MMIO_BASE+0x00300020))
#define EMMC_STATUS         ((volatile unsigned int*)(MMIO_BASE+0x00300024))
#define EMMC_CONTROL0       ((volatile unsigned int*)(MMIO_BASE+0x00300028))
#define EMMC_CONTROL1       ((volatile unsigned int*)(MMIO_BASE+0x0030002C))
#define EMMC_INTERRUPT      ((volatile unsigned int*)(MMIO_BASE+0x00300030))
#define EMMC_INT_MASK       ((volatile unsigned int*)(MMIO_BASE+0x00300034))
#define EMMC_INT_EN         ((volatile unsigned int*)(MMIO_BASE+0x00300038))
#define EMMC_CONTROL2       ((volatile unsigned int*)(MMIO_BASE+0x0030003C))
#define EMMC_SLOTISR_VER    ((volatile unsigned int*)(MMIO_BASE+0x003000FC))

// command flags
#define CMD_NEED_APP        0x80000000
#define CMD_RSPNS_48        0x00020000
#define CMD_ERRORS_MASK     0xfff9c004
#define CMD_RCA_MASK        0xffff0000

// COMMANDs
#define CMD_GO_IDLE         0x00000000
#define CMD_ALL_SEND_CID    0x02010000
#define CMD_SEND_REL_ADDR   0x03020000
#define CMD_CARD_SELECT     0x07030000
#define CMD_SEND_IF_COND    0x08020000  // fxl: set interface op condition
#define CMD_STOP_TRANS      0x0C030000
#define CMD_READ_SINGLE     0x11220010
#define CMD_READ_MULTI      0x12220032
#define CMD_SET_BLOCKCNT    0x17020000
#define CMD_WRITE_SINGLE    0x18220000
#define CMD_WRITE_MULTI     0x19220022
#define CMD_APP_CMD         0x37000000
#define CMD_SET_BUS_WIDTH   (0x06020000|CMD_NEED_APP)
#define CMD_SEND_OP_COND    (0x29020000|CMD_NEED_APP)
#define CMD_SEND_SCR        (0x33220010|CMD_NEED_APP)

// STATUS register settings
#define SR_READ_AVAILABLE   0x00000800
#define SR_WRITE_AVAILABLE  0x00000400
#define SR_DAT_INHIBIT      0x00000002
#define SR_CMD_INHIBIT      0x00000001
#define SR_APP_CMD          0x00000020

// INTERRUPT register settings
#define INT_DATA_TIMEOUT    0x00100000
#define INT_CMD_TIMEOUT     0x00010000
#define INT_READ_RDY        0x00000020
#define INT_WRITE_RDY       0x00000010
#define INT_DATA_DONE       0x00000002
#define INT_CMD_DONE        0x00000001

#define INT_ERROR_MASK      0x017E8000

// CONTROL register settings
#define C0_SPI_MODE_EN      0x00100000
#define C0_HCTL_HS_EN       0x00000004
#define C0_HCTL_DWITDH      0x00000002

#define C1_SRST_DATA        0x04000000
#define C1_SRST_CMD         0x02000000
#define C1_SRST_HC          0x01000000
#define C1_TOUNIT_DIS       0x000f0000
#define C1_TOUNIT_MAX       0x000e0000
#define C1_CLK_GENSEL       0x00000020
#define C1_CLK_EN           0x00000004
#define C1_CLK_STABLE       0x00000002
#define C1_CLK_INTLEN       0x00000001

// SLOTISR_VER values
#define HOST_SPEC_NUM       0x00ff0000
#define HOST_SPEC_NUM_SHIFT 16
#define HOST_SPEC_V3        2
#define HOST_SPEC_V2        1
#define HOST_SPEC_V1        0

// SCR flags            fxl: scr sd card reader? 
#define SCR_SD_BUS_WIDTH_4  0x00000400
#define SCR_SUPP_SET_BLKCNT 0x02000000         // fxl: support r/w multi blocks in a cmd?
// added by my driver
#define SCR_SUPP_CCS        0x00000001

#define ACMD41_VOLTAGE      0x00ff8000
#define ACMD41_CMD_COMPLETE 0x80000000
#define ACMD41_CMD_CCS      0x40000000
#define ACMD41_ARG_HC       0x51ff8000

unsigned long sd_scr[2], sd_ocr, sd_rca, sd_err, sd_hv;

/**
 * Wait for data or command ready 
 * fxl: busy wait
 */
int sd_status(unsigned int mask)
{
    int cnt = 1000000; while((*EMMC_STATUS & mask) && !(*EMMC_INTERRUPT & INT_ERROR_MASK) && cnt--) wait_msec(1);
    return (cnt <= 0 || (*EMMC_INTERRUPT & INT_ERROR_MASK)) ? SD_ERROR : SD_OK;
}

/**
 * Wait for interrupt
 */
int sd_int(unsigned int mask)
{
    unsigned int r, m=mask | INT_ERROR_MASK;
    int cnt = 1000000; while(!(*EMMC_INTERRUPT & m) && cnt--) wait_msec(1);
    r=*EMMC_INTERRUPT;
    if(cnt<=0 || (r & INT_CMD_TIMEOUT) || (r & INT_DATA_TIMEOUT) ) {E("cnt %d r %x",cnt, r); *EMMC_INTERRUPT=r; return SD_TIMEOUT; } else
    if(r & INT_ERROR_MASK) { *EMMC_INTERRUPT=r; return SD_ERROR; }
    *EMMC_INTERRUPT=mask;
    return 0;
}

/**
 * Send a command
 */
int sd_cmd(unsigned int code, unsigned int arg)
{
    int r=0;
    sd_err=SD_OK;
    if(code&CMD_NEED_APP) {
        r=sd_cmd(CMD_APP_CMD|(sd_rca?CMD_RSPNS_48:0),sd_rca);
        if(sd_rca && !r) { uart_puts("ERROR: failed to send SD APP command\n"); sd_err=SD_ERROR;return 0;}
        code &= ~CMD_NEED_APP;
    }
    if(sd_status(SR_CMD_INHIBIT)) { uart_puts("ERROR: EMMC busy\n"); sd_err= SD_TIMEOUT;return 0;}
    uart_puts("EMMC: Sending command ");uart_hex(code);uart_puts(" arg ");uart_hex(arg);uart_puts("\n");
    *EMMC_INTERRUPT=*EMMC_INTERRUPT; *EMMC_ARG1=arg; *EMMC_CMDTM=code;
    if(code==CMD_SEND_OP_COND) wait_msec(1000); else
    if(code==CMD_SEND_IF_COND || code==CMD_APP_CMD) wait_msec(100);
    if((r=sd_int(INT_CMD_DONE))) {uart_puts("ERROR: failed to send EMMC command\n");uart_hex(r);sd_err=r;return 0;}
    r=*EMMC_RESP0;
    if(code==CMD_GO_IDLE || code==CMD_APP_CMD) return 0; else
    if(code==(CMD_APP_CMD|CMD_RSPNS_48)) return r&SR_APP_CMD; else
    if(code==CMD_SEND_OP_COND) return r; else
    if(code==CMD_SEND_IF_COND) return r==arg? SD_OK : SD_ERROR; else
    if(code==CMD_ALL_SEND_CID) {r|=*EMMC_RESP3; r|=*EMMC_RESP2; r|=*EMMC_RESP1; return r; } else
    if(code==CMD_SEND_REL_ADDR) {
        sd_err=(((r&0x1fff))|((r&0x2000)<<6)|((r&0x4000)<<8)|((r&0x8000)<<8))&CMD_ERRORS_MASK;
        return r&CMD_RCA_MASK;
    }
    return r&CMD_ERRORS_MASK;
    // make gcc happy       (fxl???
    return 0;
}

/**
 * read a block from sd card and return the number of bytes read
 * returns 0 on error.
 * num: # of blocks to read. from the code: each block 512
 */
int sd_readblock(unsigned int lba, unsigned char *buffer, unsigned int num)
{
    int r,c=0,d;
    if(num<1) num=1;
    uart_puts("sd_readblock lba ");uart_hex(lba);uart_puts(" num ");uart_hex(num);uart_puts("\n");
    if(sd_status(SR_DAT_INHIBIT)) {sd_err=SD_TIMEOUT; return 0;}
    unsigned int *buf=(unsigned int *)buffer;
    if(sd_scr[0] & SCR_SUPP_CCS) {
        if(num > 1 && (sd_scr[0] & SCR_SUPP_SET_BLKCNT)) {
            sd_cmd(CMD_SET_BLOCKCNT,num);
            if(sd_err) return 0;
        }
        *EMMC_BLKSIZECNT = (num << 16) | 512;
        sd_cmd(num == 1 ? CMD_READ_SINGLE : CMD_READ_MULTI,lba);
        if(sd_err) return 0;
    } else {
        *EMMC_BLKSIZECNT = (1 << 16) | 512;
    }
    while( c < num ) {
        if(!(sd_scr[0] & SCR_SUPP_CCS)) {
            sd_cmd(CMD_READ_SINGLE,(lba+c)*512);
            if(sd_err) return 0;
        }
        if((r=sd_int(INT_READ_RDY))){uart_puts("\rERROR: Timeout waiting for ready to read\n");sd_err=r;return 0;}
        for(d=0;d<128;d++) buf[d] = *EMMC_DATA;     // fxl: serial read.. (optimize w/ dma?)
        c++; buf+=128;
    }
    if( num > 1 && !(sd_scr[0] & SCR_SUPP_SET_BLKCNT) && (sd_scr[0] & SCR_SUPP_CCS)) sd_cmd(CMD_STOP_TRANS,0);
    return sd_err!=SD_OK || c!=num? 0 : num*512;
}

/**
 * write a block to the sd card and return the number of bytes written
 * returns 0 on error.
 * num: # of blocks to write
 */
int sd_writeblock(unsigned char *buffer, unsigned int lba, unsigned int num)
{
    int r,c=0,d;
    if(num<1) num=1;
    uart_puts("sd_writeblock lba ");uart_hex(lba);uart_puts(" num ");uart_hex(num);uart_puts("\n");
    if(sd_status(SR_DAT_INHIBIT | SR_WRITE_AVAILABLE)) {sd_err=SD_TIMEOUT; return 0;}
    unsigned int *buf=(unsigned int *)buffer;
    if(sd_scr[0] & SCR_SUPP_CCS) {
        if(num > 1 && (sd_scr[0] & SCR_SUPP_SET_BLKCNT)) {
            sd_cmd(CMD_SET_BLOCKCNT,num);
            if(sd_err) return 0;
        }
        *EMMC_BLKSIZECNT = (num << 16) | 512;
        sd_cmd(num == 1 ? CMD_WRITE_SINGLE : CMD_WRITE_MULTI,lba);
        if(sd_err) return 0;
    } else {
        *EMMC_BLKSIZECNT = (1 << 16) | 512;
    }
    while( c < num ) {
        if(!(sd_scr[0] & SCR_SUPP_CCS)) {
            sd_cmd(CMD_WRITE_SINGLE,(lba+c)*512);
            if(sd_err) return 0;
        }
        if((r=sd_int(INT_WRITE_RDY))){uart_puts("\rERROR: Timeout waiting for ready to write\n");sd_err=r;return 0;}
        for(d=0;d<128;d++) *EMMC_DATA = buf[d];
        c++; buf+=128;
    }
    if((r=sd_int(INT_DATA_DONE))){uart_puts("\rERROR: Timeout waiting for data done\n");sd_err=r;return 0;}
    if( num > 1 && !(sd_scr[0] & SCR_SUPP_SET_BLKCNT) && (sd_scr[0] & SCR_SUPP_CCS)) sd_cmd(CMD_STOP_TRANS,0);
    return sd_err!=SD_OK || c!=num? 0 : num*512;
}

/**
 * set SD clock to frequency in Hz
 */
static int sd_clk(unsigned int f)
{
    unsigned int d,c=41666666/f,x,s=32,h=0;
    int cnt = 100000;
    while((*EMMC_STATUS & (SR_CMD_INHIBIT|SR_DAT_INHIBIT)) && cnt--) wait_msec(1);
    if(cnt<=0) {
        uart_puts("ERROR: timeout waiting for inhibit flag\n");
        return SD_ERROR;
    }

    *EMMC_CONTROL1 &= ~C1_CLK_EN; wait_msec(10);
    x=c-1; if(!x) s=0; else {
        if(!(x & 0xffff0000u)) { x <<= 16; s -= 16; }
        if(!(x & 0xff000000u)) { x <<= 8;  s -= 8; }
        if(!(x & 0xf0000000u)) { x <<= 4;  s -= 4; }
        if(!(x & 0xc0000000u)) { x <<= 2;  s -= 2; }
        if(!(x & 0x80000000u)) { x <<= 1;  s -= 1; }
        if(s>0) s--;
        if(s>7) s=7;
    }
    if(sd_hv>HOST_SPEC_V2) d=c; else d=(1<<s);
    if(d<=2) {d=2;s=0;}
    uart_puts("sd_clk divisor ");uart_hex(d);uart_puts(", shift ");uart_hex(s);uart_puts("\n");
    if(sd_hv>HOST_SPEC_V2) h=(d&0x300)>>2;
    d=(((d&0x0ff)<<8)|h);
    *EMMC_CONTROL1=(*EMMC_CONTROL1&0xffff003f)|d; wait_msec(10);
    *EMMC_CONTROL1 |= C1_CLK_EN; wait_msec(10);
    cnt=10000; while(!(*EMMC_CONTROL1 & C1_CLK_STABLE) && cnt--) wait_msec(10);
    if(cnt<=0) {
        uart_puts("ERROR: failed to get stable clock\n");
        return SD_ERROR;
    }
    return SD_OK;
}

//-----------------------  partition support -------------------------------//
// for MBR, cf: https://wiki.osdev.org/MBR_(x86)
//      illustration: https://cpl.li/posts/2019-03-12-mbrfat/
//          ("MBR, LBA, FAT32" by Alexandru-Paul Copil)
#define PACKED		__attribute__ ((packed))
typedef struct TCHSAddress {
    unsigned char Head;
    unsigned char Sector : 6,
        CylinderHigh : 2;
    unsigned char CylinderLow;
} PACKED TCHSAddress;    // CHS: cylinder-head-sector addressing. mandatory

// "Partition table entry format"
typedef struct TPartitionEntry {
    unsigned char Status;       // Drive attributes (bit 7 set = active or bootable)
    TCHSAddress FirstSector;
    unsigned char Type;         // https://en.wikipedia.org/wiki/Partition_type
    TCHSAddress LastSector;
    unsigned LBAFirstSector;  // LBA: Logical Block Allocation (linear, simple addressing)
    unsigned NumberOfSectors;
} PACKED TPartitionEntry;

typedef struct TMasterBootRecord {
    unsigned char BootCode[0x1BE];
    TPartitionEntry Partition[4];
    unsigned short BootSignature;
#define BOOT_SIGNATURE 0xAA55
} PACKED TMasterBootRecord;

static TMasterBootRecord the_mbr; 
static char is_part_valid[4]={0};  // 1=valid. for quick access
static struct spinlock sd_lock = {.locked=0, .cpu=0, .name="sd_lock"};

// return 0 on success
static int parse_mbr(TMasterBootRecord *pmbr, char isvalid[4]) {
    unsigned char *buf = kalloc();
    BUG_ON(!buf);
    TMasterBootRecord *mbr = (TMasterBootRecord *)buf;
    int ret = -1;
    // read from block 0 and parse MBR
    if (sd_readblock(0, buf, 1)) {
        if (mbr->BootSignature != BOOT_SIGNATURE) {
            W("Boot signature not found");
            goto out;
        }
        // NB: partiion 1 could start from sector32 (in fact, anywhere artibrary)
        // cf: https://unix.stackexchange.com/questions/81556/area-on-disk-after-the-mbr-and-before-the-partition-start-point
        // "The old 32KiB gap between MBR and first sector of file system is
        //  called DOS compatibility region or MBR gap, because DOS required
        //  that the partitions started at cylinder boundaries".
        I("# Status Type  1stSector    Sectors");
        for (unsigned n = 0; n < 4; n++) {
            I("%u %02X     %02X   %10u %10u",
              n + 1,
              (unsigned)mbr->Partition[n].Status,
              (unsigned)mbr->Partition[n].Type,
              mbr->Partition[n].LBAFirstSector,
              mbr->Partition[n].NumberOfSectors);
            if (mbr->Partition[n].NumberOfSectors)
                isvalid[n] = 1;
            else
                isvalid[n] = 0;
        }
        memmove(pmbr, buf, sizeof(TMasterBootRecord));
        ret = 0; 
    }
out:
    kfree(buf);
    return ret;
}

// cf: rt_thread drv_sdio.c dump_registers() 
// bcm2837 man pp66, "EMMC Address Map"
static void sd_dump_regs() 
{
#if K2_ACTUAL_DEBUG_LEVEL <= 20     // "V"    
    printf("EMMC regs\n");
    volatile unsigned int * i;
    for (i = EMMC_ARG2; i <= EMMC_CONTROL2; i++) {
        printf("\t%lx:%x\n", (unsigned long)i, *i);
    }
    
    i=(volatile unsigned int *)(MMIO_BASE+0x00300050); printf("\t%lx:%x\n", (unsigned long)i, *i);
    i=(volatile unsigned int *)(MMIO_BASE+0x00300070); printf("\t%lx:%x\n", (unsigned long)i, *i);
    i=(volatile unsigned int *)(MMIO_BASE+0x00300074); printf("\t%lx:%x\n", (unsigned long)i, *i);
    i=(volatile unsigned int *)(MMIO_BASE+0x00300080); printf("\t%lx:%x\n", (unsigned long)i, *i);
    i=(volatile unsigned int *)(MMIO_BASE+0x00300084); printf("\t%lx:%x\n", (unsigned long)i, *i);
    i=(volatile unsigned int *)(MMIO_BASE+0x00300088); printf("\t%lx:%x\n", (unsigned long)i, *i);
    i=(volatile unsigned int *)(MMIO_BASE+0x0030008c); printf("\t%lx:%x\n", (unsigned long)i, *i);
    i=(volatile unsigned int *)(MMIO_BASE+0x00300090); printf("\t%lx:%x\n", (unsigned long)i, *i);
    i=(volatile unsigned int *)(MMIO_BASE+0x003000f0); printf("\t%lx:%x\n", (unsigned long)i, *i);
    i=(volatile unsigned int *)(MMIO_BASE+0x003000fC); printf("\t%lx:%x\n", (unsigned long)i, *i);
#endif
}

/**
 * initialize EMMC to read SDHC card
 * return 0 on OK
 * 
 * also cf: Circle addon/SDCard/emmc.cpp CEMMCDevice::CardInit()
 */
int sd_init()
{    
    long r,cnt,ccs=0;

    /*  
        Below configure GPIO pins for SDIO. 
        Pin 48--53 is configured for function3, and pull up/down modes are set. 
        NB: BCM2837 manual 6.2 "alternative function assignment", which however
        does NOT list SDIO functions for pin 48--53. Cf Circle emmc.cpp.

        A common protocol per soc manual: (cf gpio.c)
        first set the desired values in SEL, then "clock" the value to gpio module 
    */
    // GPIO_CD      Pin47: card detection?
    r=*GPFSEL4; r&=~(7<<(7*3)); *GPFSEL4=r;
    *GPPUD=2; wait_cycles(150); *GPPUDCLK1=(1<<15); wait_cycles(150); *GPPUD=0; *GPPUDCLK1=0;
    r=*GPHEN1; r|=1<<15; *GPHEN1=r;

    // GPIO_CLK, GPIO_CMD   Pin 48,49
    r=*GPFSEL4; r|=(7<<(8*3))|(7<<(9*3)); *GPFSEL4=r;
    *GPPUD=2; wait_cycles(150); *GPPUDCLK1=(1<<16)|(1<<17); wait_cycles(150); *GPPUD=0; *GPPUDCLK1=0;

    // GPIO_DAT0, GPIO_DAT1, GPIO_DAT2, GPIO_DAT3; Pin 50--53
    r=*GPFSEL5; r|=(7<<(0*3)) | (7<<(1*3)) | (7<<(2*3)) | (7<<(3*3)); *GPFSEL5=r;
    *GPPUD=2; wait_cycles(150);
    *GPPUDCLK1=(1<<18) | (1<<19) | (1<<20) | (1<<21);
    wait_cycles(150); *GPPUD=0; *GPPUDCLK1=0;
    sd_hv = (*EMMC_SLOTISR_VER & HOST_SPEC_NUM) >> HOST_SPEC_NUM_SHIFT;
    I("EMMC: GPIO set up. host_spec %lu", sd_hv);

    // Reset the card.
    *EMMC_CONTROL0 = 0; *EMMC_CONTROL1 |= C1_SRST_HC;
    cnt=10000; do{wait_msec(10);} while( (*EMMC_CONTROL1 & C1_SRST_HC) && cnt-- );
    if(cnt<=0) {
        uart_puts("ERROR: failed to reset EMMC\n");
        return SD_ERROR;
    }
    I("EMMC: reset OK");
    *EMMC_CONTROL1 |= C1_CLK_INTLEN | C1_TOUNIT_MAX;
    wait_msec(10);
    // Set clock to setup frequency.
    if((r=sd_clk(400000))) return r;
    *EMMC_INT_EN   = 0xffffffff;
    *EMMC_INT_MASK = 0xffffffff;
    sd_scr[0]=sd_scr[1]=sd_rca=sd_err=0;
    sd_cmd(CMD_GO_IDLE,0);
    if(sd_err) return sd_err;

    sd_dump_regs(); 

    sd_cmd(CMD_SEND_IF_COND,0x000001AA);
    if(sd_err) return sd_err;
    cnt=6; r=0; while(!(r&ACMD41_CMD_COMPLETE) && cnt--) {
        wait_cycles(400);
        r=sd_cmd(CMD_SEND_OP_COND,ACMD41_ARG_HC);
        uart_puts("EMMC: CMD_SEND_OP_COND returned ");
        if(r&ACMD41_CMD_COMPLETE)
            uart_puts("COMPLETE ");
        if(r&ACMD41_VOLTAGE)
            uart_puts("VOLTAGE ");
        if(r&ACMD41_CMD_CCS)
            uart_puts("CCS ");
        uart_hex(r>>32);
        uart_hex(r);
        uart_puts("\n");
        if(sd_err!=SD_TIMEOUT && sd_err!=SD_OK ) {
            uart_puts("ERROR: EMMC ACMD41 returned error\n");
            return sd_err;
        }
    }
    if(!(r&ACMD41_CMD_COMPLETE) || !cnt ) return SD_TIMEOUT;
    if(!(r&ACMD41_VOLTAGE)) return SD_ERROR;
    if(r&ACMD41_CMD_CCS) ccs=SCR_SUPP_CCS;

    sd_cmd(CMD_ALL_SEND_CID,0);

    sd_rca = sd_cmd(CMD_SEND_REL_ADDR,0);
    uart_puts("EMMC: CMD_SEND_REL_ADDR returned ");
    uart_hex(sd_rca>>32);
    uart_hex(sd_rca);
    uart_puts("\n");
    if(sd_err) return sd_err;

    if((r=sd_clk(25000000))) return r;

    sd_cmd(CMD_CARD_SELECT,sd_rca);
    if(sd_err) return sd_err;

    if(sd_status(SR_DAT_INHIBIT)) return SD_TIMEOUT;
    *EMMC_BLKSIZECNT = (1<<16) | 8;
    sd_cmd(CMD_SEND_SCR,0);
    if(sd_err) return sd_err;
    if(sd_int(INT_READ_RDY)) return SD_TIMEOUT;

    r=0; cnt=100000; while(r<2 && cnt) {
        if( *EMMC_STATUS & SR_READ_AVAILABLE )
            sd_scr[r++] = *EMMC_DATA;
        else
            wait_msec(1);
    }
    if(r!=2) return SD_TIMEOUT;
    if(sd_scr[0] & SCR_SD_BUS_WIDTH_4) {
        sd_cmd(CMD_SET_BUS_WIDTH,sd_rca|2);
        if(sd_err) return sd_err;
        *EMMC_CONTROL0 |= C0_HCTL_DWITDH;
    }
    // add software flag
    uart_puts("EMMC: supports ");
    if(sd_scr[0] & SCR_SUPP_SET_BLKCNT)
        uart_puts("SET_BLKCNT ");
    if(ccs)
        uart_puts("CCS ");
    uart_puts("\n");
    sd_scr[0]&=~SCR_SUPP_CCS;
    sd_scr[0]|=ccs;

    if (parse_mbr(&the_mbr, is_part_valid) == 0)
        return SD_OK;
    else 
        return SD_ERROR; 
}

// dev as defined in param.h, e.g. DEV_SD0
int sd_dev_to_part(int dev) {
    BUG_ON(!is_sddev(dev)); return (dev-DEV_SD0); 
}


//quest: FAT
unsigned long sd_dev_get_sect_count(int dev) {
    int part = sd_dev_to_part(dev); 
    if (!is_part_valid[part]) return 0;
    return the_mbr.Partition[part].NumberOfSectors;
}

unsigned long sd_get_sect_size() {
    return 0; /* STUDENT_TODO: replace this */
}

static int sd_part_writeblock(int part, 
    unsigned char *buffer, unsigned int lba, unsigned int num) {

    if (!is_part_valid[part]) return 0;

    // check boundary 
    if (lba+num > the_mbr.Partition[part].NumberOfSectors)
        return 0;

    return sd_writeblock(buffer,
        0, /* STUDENT_TODO: replace this */
        num); 
}

static int sd_part_readblock(int part,
    unsigned int lba, unsigned char *buffer, unsigned int num) {
    if (!is_part_valid[part]) return 0;

    // check boundary 
    if (lba+num > the_mbr.Partition[part].NumberOfSectors)
        return 0;

    return sd_readblock(
        0, /* STUDENT_TODO: replace this */
        buffer, num);
}

#include "fs.h"
#include "buf.h"
// needed by bio.c
// only r/w 1 block at a time (per the bio interface)
// part: partition number
void sd_part_rw(int part, struct buf *b, int write) {
    uint sec_no = b->blockno; 
    int ret; 

    // optimize: right now sd write/read busy waits (1 block r/w takes ~1ms), 
    // change to irq driven and take sleeplock() instead
    acquire(&sd_lock); 
    if (write)
        ret = sd_part_writeblock(0,0,0,1); /* STUDENT_TODO: replace this */
    else 
        ret = sd_part_readblock(0,0,0,1); /* STUDENT_TODO: replace this */
    release(&sd_lock); 
    if (!ret) {
        E("sd_part_rw failed sec_no %u write %d", sec_no, write); BUG(); 
    }
}