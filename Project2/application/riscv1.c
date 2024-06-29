/*
 *
 * Copyright (c) 2005-2021 Imperas Software Ltd., www.imperas.com
 *
 * The contents of this file are provided under the Software License
 * Agreement that you accepted before downloading this file.
 *
 * This source forms part of the Software and can be used for educational,
 * training, and demonstration purposes but cannot be used for derivative
 * works except in cases where the derivative works require OVP technology
 * to run.
 *
 * For open source models released under licenses that you can use for
 * derivative works, please visit www.OVPworld.org or www.imperas.com
 * for the location of the open source models.
 *
 */

typedef unsigned int  Uns32;
typedef unsigned char Uns8;
//typedef bool Uns1;

#define DMA_BASE                  0x100000
#define DMA_SOURCE_ADDR1          0x00
#define DMA_TARGET_ADDR1          0x04
#define DMA_SIZE_ADDR1            0x08
#define DMA_CLEAR_ADDR1           0x0c
#define DMA_SOURCE_ADDR2          0x10
#define DMA_TARGET_ADDR2          0x14
#define DMA_SIZE_ADDR2            0x18
#define DMA_CLEAR_ADDR2           0x1c

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "riscvInterrupts.h"

void int_init(void (*handler)()) {

	// Set MTVEC register to point to handler function in direct mode
	int handler_int = (int) handler & ~0x1;
	write_csr(mtvec, handler_int);

	// Enable Machine mode external interrupts
    set_csr(mie, MIE_MEIE);
}

void int_enable() {
    set_csr(mstatus, MSTATUS_MIE);
}
// r/w func-----------
static inline void writeReg32(Uns32 address, Uns32 offset, Uns32 value)
{
    *(volatile Uns32*) (address + offset) = value;
}
static inline Uns32 readReg32(Uns32 address, Uns32 offset)
{
    return *(volatile Uns32*) (address + offset);
}
static inline void writeReg8(Uns32 address, Uns32 offset, Uns8 value)
{
    *(volatile Uns8*) (address + offset) = value;
}
static inline Uns8 readReg8(Uns32 address, Uns32 offset)
{
    return *(volatile Uns8*) (address + offset);
}
//static inline void writeReg(Uns32 address, Uns32 offset, bool value)
//{
//    *(volatile Uns1*) (address + offset) = value;
//}
//dma-----------
static void dmaBurst1(void *from, void *to, Uns32 bytes)
{
    printf("dmaBurst 1, bytes:%d\n", bytes);
    //printf("SRC: %x\n", (Uns32)from);
    //writeReg32(DMA_BASE, DMA_SOURCE_ADDR1, (Uns32)from);
    writeReg32(DMA_BASE, DMA_SOURCE_ADDR1, (Uns32)from);
    writeReg32(DMA_BASE, DMA_TARGET_ADDR1, (Uns32)to);
    writeReg32(DMA_BASE, DMA_SIZE_ADDR1, bytes);
    writeReg32(DMA_BASE, DMA_CLEAR_ADDR1, 0x1);
}

// interrupt----
volatile static Uns32 interruptCount = 0;

void interruptHandler(void)
{
    if (readReg8(DMA_BASE, DMA_CLEAR_ADDR1))  //interrupt1
    {
        printf("Interrupt 1 0x%x (0x%02x)\n", readReg8(DMA_BASE, DMA_CLEAR_ADDR1), 0x1);
        writeReg32(DMA_BASE, DMA_CLEAR_ADDR1, 0x0); //Pull down START/CLEAR
    }
}
//--------------------------------------
int main () {

    printf("[CPU1]Start\n");

    //static unsigned int* src_adr1 = (int*)0x200400;  //SOURCE address
    static unsigned int* adr_a = (int*)0x200400;
    static unsigned int* adr_b = (int*)0x300400;
    static unsigned int* adr_c = (int*)0x300000;
    static unsigned int* adr_d = (int*)0x200000;
    unsigned int offset, data;

    //RISCV-1 writes 1KB data into 0x200400 to 0x2007FF.
    printf("[CPU1]Writing data to 0x200400 ~ 0x2007FF...\n");
    offset = 0x0;
    data = 0x0;
    for(unsigned int i=0; i<1024; i+=1)
    {
        //Transfer every bytes(8 bits, two data)
        printf("Write %x at %x\n", (data*16 + data+1), adr_a+offset);
        writeReg8((Uns32)adr_a, offset, (data*16 + data+1));
        offset += 1;
        data = (data == 0xe)? 0x0: data+2;
    }
    
    int_init(trap_entry);
    int_enable();
    
    for(int k=0; k<3; k+=1)
    {
        //Monitor data in 0x200800.
        printf("[CPU1]Wait for data in 0x200800 to be 0x0\n");
        while(readReg8(0x200800, 0) != (Uns32)0x0)
            /*printf("[CPU1]\tWaiting...\n")*/;
        
        //RISCV-1 programs DMA’s 1st set of control registers to copy a 1KB data starting from 0x200400 (on RAM3) to 0x300000 (on RAM4).
        for(unsigned int i=0; i<256; i+=1)
        {
            dmaBurst1(adr_a+i, adr_c+i, 4);
            wfi();
        }
        writeReg8(0x200800, 0, 0x1);    //RISCV-1 writes a flag 0x1 to address 0x200800 to indicate the end of data transmission.
    }

    printf("[CPU1]Finish\n");
}
