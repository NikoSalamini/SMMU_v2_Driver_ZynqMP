/******************************************************************************
*
* Copyright (C) 2009 - 2014 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
******************************************************************************/

/*
 * helloworld.c: simple test application
 *
 * This application configures UART 16550 to baud rate 9600.
 * PS7 UART (Zynq) is not initialized by this application, since
 * bootrom/bsp configures it to baud rate 115200
 *
 * ------------------------------------------------
 * | UART TYPE   BAUD RATE                        |
 * ------------------------------------------------
 *   uartns550   9600
 *   uartlite    Configurable only in HW design
 *   ps7_uart    115200 (configured by bootrom/bsp)
 */

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <xil_exception.h>
#include <xscugic.h>
#include "platform.h"
#include "smmu_driver.h"
#include "xzdma.h"
#include "xaxicdma.h"

// NOTE: the pool_size has been set to 6

// MID bits [9:0]
// HPC MID: 1000, AXI ID [5:0] from PL (set according to the design)
#define CDMA0_MID 0b1000000000
#define CDMA1_MID 0b1000000110
#define DAP_APB_control_MID 0x62

// TBU NUMBER
#define HPC0_TBU 0x0
#define DAP_APB_control_TBU 0x2

/* https://support.xilinx.com/s/question/0D52E00006hpmCxSAI/smmu-on-zcu102?language=en_US */
/* https://docs.xilinx.com/r/en-US/ug1085-zynq-ultrascale-trm/Master-IDs-List */
/* https://docs.xilinx.com/r/en-US/ug1087-zynq-ultrascale-registers */
/* https://developer.arm.com/documentation/102416/0100/Single-level-table-at-EL3 */
/* https://users.ece.utexas.edu/~mcdermot/arch/articles/Zynq/ug1085-zynq-ultrascale-trm%20copy.pdf */

/* -- GDMA Buffers -- */

// those address do not overlay with table ones
#define DMA_BUF_SIZE 8
volatile static u8 SrcBuf[DMA_BUF_SIZE] __attribute__ ((aligned (64)));
volatile static u8 DstBuf[DMA_BUF_SIZE] __attribute__ ((aligned (64)));

/* it's not common practice to initiate a pointer in this way, but i know that the program's section
 * will be outside this and so I can write it using the CDMA1 unit.
 * CDMA1 use those address as source and dest.
 */

volatile static u8* SrcBuf_virt = (u8*)(SrcBuf + 0x10000000);
volatile static u8* DstBuf_virt = (u8*)(DstBuf + 0x10000000);

// CDMA config instance
XAxiCdma_Config *CDmaConfig;

/* -- GDMA Buffers -- */

// I have put the translation tables here because in the main function setting it to 0 was blocking the program
// Maybe it goes over the stack size limitation

// translation table locations
// TTBR addresses must be aligned with the granule (4KB in aarch32 lpae)
#define GRANULE 4096
u64 cb0_tt_l1_base_64[4] __attribute__((aligned(GRANULE)));
u64 cb1_tt_l1_base_64[4] __attribute__((aligned(GRANULE)));

// output addresses for the translation of 1GB
// Note: those are the 10 bits [39:30] for the output address
// DDR_LOW is DDR Low is 2 GB 0x0000_0000-0x7FFF_FFFF
u16 output_address_0 = 0x0; // flat [39:30] = 0b00000000
u16 output_address_1 = 0x1; // not flat [39:30] = 0b00000001

/* -- NOTES -- */

/*
 * Aarch32 LPAE:
 * -Uses 64-bit descriptors in its translation tables
 * -Is added to the ARMv7 architecture by the Large Physical Address Extension (LPAE)
 * -Supports the translation of VAs of up to 32 bits
 * -Supports output addresses of up to 40 bits, that can be IPAs or VAs.
 */

/* The configuration is a an entry block for the first stage of translation:
 * The bits [32:30] are used to select one of the 4 entries of the translation table. Then, the entry will contain
 * an output address of 39:30 set that will be attached to the remaining bits.
 * In a flat configuration, those bits are set to 0x0.
 * Supposing to have the output bits set to 0x400 (0b10000000000), the output will be that bits concatenated to the
 * remaining ones [29:0].
 */

/* -- NOTES -- */

// configure CDMA1-0 (PL)
#define HPC_CHANNEL	1
#define AXI_ATTR(prot, cache)	((prot << 4) | cache)
#define NORM_WB_OUT_CACHE	0x605UL
#define PROT_UP_S_D	0x0	/* Unprivileged, Secure, Data */
#define AXI_PROT	PROT_UP_S_D
#define	CACHE_OA_M	0xB /* Write-through No-allocate */

XAxiCdma FpdCDma0;
XAxiCdma FpdCDma1;
XAxiCdma_Config *CDmaConfig0;
XAxiCdma_Config *CDmaConfig1;
static void FpdCdmaConfiguration(void)
{
	CDmaConfig0 = XAxiCdma_LookupConfig(XPAR_AXICDMA_0_DEVICE_ID);
	if (!CDmaConfig0) {
		xil_printf("Cannot find config structure \r\n");
		return XST_FAILURE;
	}
	CDmaConfig1 = XAxiCdma_LookupConfig(XPAR_AXICDMA_1_DEVICE_ID);
	if (!CDmaConfig1) {
		xil_printf("Cannot find config structure \r\n");
		return XST_FAILURE;
	}
	XAxiCdma_CfgInitialize(&FpdCDma0, CDmaConfig0, CDmaConfig0->BaseAddress);
	XAxiCdma_CfgInitialize(&FpdCDma1, CDmaConfig1, CDmaConfig1->BaseAddress);
}

// Interrupt handler
void SMMU_InterruptHandler(void *CallbackRef) {

	xil_printf("-- SMMU INTERRUPT HANDLER -- \n\r");
    // print errors
    printSMMUGlobalErr();

    // print CB errors
    printCBnErrors(0);
    printCBnErrors(1);

    // clear the interrupt status registers
    clear_error_status();
}

// function to setup the interrupt system
#define INTC_DEVICE_ID	XPAR_SCUGIC_SINGLE_DEVICE_ID
#define SMMU_INTR_ID  XPAR_XSMMU_FPD_INTR
static XScuGic xInterruptController;

static int SetupInterruptSystem() {
    XScuGic_Config *IntcConfig;
    int Status;

    // Initialize the interrupt controller driver with the configuration available in the configuration table
    IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
    if (NULL == IntcConfig) {
        return XST_FAILURE;
    }

    // Initialize the interrupt controller driver
    Status = XScuGic_CfgInitialize(&xInterruptController, IntcConfig, IntcConfig->CpuBaseAddress);
    if (Status != XST_SUCCESS){
        return XST_FAILURE;
    }

    // Initialize the exception table and register the interrupt controller handler
    // This is the driver for the Gic and it is used to dispatch the interrupt to the proper active handler
    // This function assumes that an interrupt vector table has been previously initialized.
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT, (Xil_ExceptionHandler)XScuGic_InterruptHandler, &xInterruptController);

    // Enable interrupts in the Processor.
    Xil_ExceptionEnable();

    // Connect the interrupt ID with the handler function
    // Connect the device driver handler that will be called when an interrupt for the device occurs
    Status = XScuGic_Connect(&xInterruptController, SMMU_INTR_ID, (Xil_ExceptionHandler)SMMU_InterruptHandler, NULL);
    if (Status != XST_SUCCESS) {
        return Status;
    }

    // The SMMU has the interrupt enabled. See psu_init_gpl SMMU_REG Interrrupt Enable
    // Enabling the SMMU interrupts in the gic
    XScuGic_Enable(&xInterruptController, SMMU_INTR_ID);

    // Enable interrupts in the processor
    Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);


    return XST_SUCCESS;
}

// #define VA_64_Config 1
int main()
{
    init_platform();

    // disable data and instruction cache
    Xil_DCacheDisable();
    Xil_ICacheDisable();
    /*Xil_DCacheInvalidate();
    Xil_ICacheInvalidate();*/

    /* -- Init GIC -- */
    int Status;

    // Initialize the interrupt controller
    Status = SetupInterruptSystem();
	if (Status != XST_SUCCESS) {
		xil_printf("Error setting up interrupt system\n");
		return XST_FAILURE;
	}
	/* -- Init GIC -- */

	/* -- Invalidate all secure tlb unlocked entries */
	invalidate_by_STLBIALL();
	invalidate_by_TLBIALLNSNH();
	/* -- Invalidate all secure tlb unlocked entries */

	clear_error_status();

	// configure the cdmas
	FpdCdmaConfiguration();

	// set the HP0 as a secure port

#ifndef VA_64_Config

/* -------------- 32 bit lpae config -------------- */

	/* -- SET SCR1 --*/
	// set SCR1 so that all the SMRs and CBs are available to the NS world
	u32 nsnumcbo = 0x0;
	u32 nsnumsmrgo = 0x0;
	setSCR1(nsnumcbo, nsnumsmrgo);
	/* -- SET SCR1 --*/

    /* -- configure the translation tables -- */

	// set the entries to 0
	memset(cb0_tt_l1_base_64, 0x0, sizeof(u64)*4);
	memset(cb1_tt_l1_base_64, 0x0, sizeof(u64)*4);

	// generate the translation tables for CB0
	// NOTE: The VA size is 39 bits, so there is no L0 ([47:39])
	// we decided to point directly to blocks and not lower level tables, so we only have L1 tables
	// entry format https://developer.arm.com/documentation/den0024/a/The-Memory-Management-Unit/Translation-tables-in-ARMv8-A/AArch64-descriptor-format

	// the bits [31:30] bits will select the L1 entry, the entry will contain the address of the block (1 GiB) 2^30
	// the remaining bits of the VA [29:0] are used to offset in the block range
	// I set the first entry, with index 0, of both tables and it points to a 1GiB block at the address 0x00000000
	// The translating address is at the beginning of the entry
	// The entries are not cacheable by the TLBs
	// bitwise or example: 0x00000000000000401 | 0x00000000040000000 = 0x00000000040000401 --> PA: 0x000040000

	// NOTE: in the example it sets a different output address 0x40000000 but in the wrong way
	// Indeed, the ORR is done without shifting the value to the 12-th bit (output address is [47:12] for 4KB granule
	// https://armv8-ref.codingbelief.com/en/chapter_d4/d43_2_armv8_translation_table_level_3_descriptor_formats.html#
	u16 entry_index = 0x0;

	// following this: https://developer.arm.com/documentation/ddi0406/c/System-Level-Architecture/Virtual-Memory-System-Architecture--VMSA-/Long-descriptor-translation-table-format/Memory-attributes-in-the-Long-descriptor-translation-table-format-descriptors?lang=en

	// valid [0]: 1 (valid)
	u64 entry_value = ((u64)0x1); // valid entry

	// descriptor type [1]: 0 (block)
	setBit64(&entry_value, 1, 0x0);

	// AttrIndex [2:0] bits [4:2]: 0b000) (index 0 of MAIR)
	setBitRange64(&entry_value, 4, 2, 0x0);

	// NS [5]: 0 (in secure state, only secure addresses are accessed)
	setBit64(&entry_value, 5, 0x0); // ignored for non-secure accesses

	// AP [2:1] [7:6]: 0b01 (read/write at any privilege level)
	setBitRange64(&entry_value, 7, 6, 0x1);

	// note: inner shareable limits the shareability of the entry to the inner one
	// SH [9:8]: 0b10 (outer shareable: can be seen by both inner and outer devices)
	setBitRange64(&entry_value, 9, 8, 0x2);

	// AF [10]: 1 (indicate that the page has not been accessed --> value of 1 --> No Access Flag Fault is generated on access.)
	setBit64(&entry_value, 10, 0x1);

	// nG [11]: 0 (translation is global, can be seen by all processes)
	setBit64(&entry_value, 11, 0x0);

	// 29 to 12 SBZ
	setBitRange64(&entry_value, 29, 12, 0x0);

	// contiguos hint [52]: 0 (the entry is not part of a contiguos block)
	setBit64(&entry_value, 52, 0x0);

	// PXN [53] (privileged execute-never bit): 0 (the region is executable at PL1)
	setBit64(&entry_value, 53, 0x0);

	// XN [54] (Execute-never bit): 0 (the region is executable)
	setBit64(&entry_value, 54, 0x0);

	u64 entry_value_0 = entry_value;
	u64 entry_value_1 = entry_value;

	// 0 entry PA: 0x0000,0000 - 0x3FFF,FFFF (FLAT)
	// the output address is set to
	setBitRange64(&entry_value_0, 39, 30, output_address_0);

	// 0 entry PA: 0x2000,0000 - idk (NOT FLAT)
	// the output address is set to
	setBitRange64(&entry_value_1, 39, 30, output_address_1);

	/* Note: the TTBRs will point on those tables. Then, when the CPU generates a memory access, the
	 * first 2 bits of the 32 bit address ([31:30]) are used to access one of the four entries. In this
	 * case we defined a single entry.
	 * For a first-level Block descriptor, bits[39:30] are bits[39:30] of the output address that
	 * specifies a 1GB block of memory, the bits [29:0] are used to access the memory offset.
	 * In this case we have an output address of 0x0 and 0x1.
	*/

	set_Table_Entry_32_lpae(&cb0_tt_l1_base_64, entry_index, entry_value_0);
	set_Table_Entry_32_lpae(&cb1_tt_l1_base_64, entry_index, entry_value_1);

	/* -- configure the translation tables -- */

    /* -- Init sCR0 -- */

	/*
		Accessing the NS copy of sCR0:
		CLIENTPD [bit 0]:   1 (bypass transaction)
		GFRE [bit 1]:       1 (raise global fault)
		GFIE [bit 2]:       1 (raise a global interrupt in case of fault)
		EXIDENABLE [bit 3]: 0 use the SMMUv1 format for SMR (reserved in zynq ultrascale+, so in the function is not set)
		STALLD [bit 8]: 	1 Disable per-context stalling on context faults
		USFCG [bit 10]:     1 to generate a Unidentified stream fault instead of apply SMMU_sCR0 attributes
		GCFGFRE, GCFGFIE are read-only and are for global configuration faults but they are read-only.
		NOTE: in NSCR0 the bits from [31:28] are not implemented
	*/
	u8 clientpd = 0x1;
	u8 gfre = 0x1;
	u8 gfie = 0x1;
	u8 stalld = 0x1;
	u8 usfcfg = 0x1;
	set_SMMU_sCR0(clientpd, gfre, gfie, stalld, usfcfg);

	/* -- Init sCR0 -- */

	/* The POOL size is 6, the AXI ID is 0 for both. So, the only possible way the mapper can work, is by
	 * perform the remapping of axi id into the first value of the corresponding pool for that master.
	 * STREAM_ID from HP0: 00000 (TBU_0) | 1000 (MID [9:6]) | AXI_ID [5:0]
	 * STREAM_ID1 = 000001000000000
	 * STREAM_ID2 = 000001000000110
	*/

    // SMR indexes
    u8 smr_index_0 = 0; // SMR0
    u8 smr_index_1 = 1; // SMR1
    u8 smr_index_2 = 2; // SMR2

	// CB indexes
    u8 cb_index_0  = 0; // CB0
    u8 cb_index_1  = 1; // CB1
    u8 cb_index_2  = 2; // CB2

	/* -- Init SMR -- */

	// Note: the stream id is: TBU number [14:10], master id [9:0] (AXI ID included)

    // reset all the SMRs
    bool valid = false;
    for(int i=0; i < N_SMRs; i++){
    	set_SMRn(i, valid, 0x0, 0x0, 0x0);
    }

    // setting the SMRs

    // masks: the values to 0 indicates the ones to consider
	u16 stream_id_mask = 0x0000; // the mask match the entire stream id
	valid = true;

	// CDMA0 --> SMR_0
	set_SMRn(smr_index_0, valid, stream_id_mask, HPC0_TBU, CDMA0_MID);

	// CDMA1 --> SMR_1
	set_SMRn(smr_index_1, valid, stream_id_mask, HPC0_TBU, CDMA1_MID);

	// DAP_APB_control --> SMR_2
	set_SMRn(smr_index_2, valid, stream_id_mask, DAP_APB_control_TBU, DAP_APB_control_MID);

	/* -- Init SMR -- */

	/* -- Init S2CR -- */

	set_S2CRn(cb_index_0, TRANSLATION_CB, cb_index_0);
	set_S2CRn(cb_index_1, TRANSLATION_CB, cb_index_1);

	// BYPASS FOR DAP APB CONTROL, in this case the index is just the S2CR index
	set_S2CRn(cb_index_2, BYPASS, cb_index_2);

	/* -- Init S2CR -- */

	/* -- Disable all the CBs -- */

	for (int i = 0; i < N_CBs; i++){
		set_SMMU_CBn_SCTLR(i, 0x0, 0x0, 0x0);
	}

	/* -- Disable all the CBs -- */

	/* -- Init CBA2R -- */

	// set the context bank of index 0-1 to AArch32 translation scheme
	set_CBA2Rn_VA(cb_index_0, VA_32);
	set_CBA2Rn_VA(cb_index_1, VA_32);

	/* -- Init CBA2R -- */

	/* -- Init CBAR -- */

	// in SMMUv2 each context bank has its own pin, and this register can be configured to raise an interrupt in
	// the event of context fault
	// RESET STATE: the SMMU_CBARn registers are not initialized. They must be initialized before use.
	set_CBARn(cb_index_0, STAGE_1_BYPASS_2);
	set_CBARn(cb_index_1, STAGE_1_BYPASS_2);

	/* -- Init CBAR -- */

	/* -- Init MAIR -- */

	/*
	 * The CBn_MAIR registers are used when either the AArch32 Long-descriptor or the AArch64
	 * translation scheme is selected. The SMMU_CBn_PRRR and SMMU_CBn_NMRR
	 * registers are used when the AArch32 Short-descriptor translation scheme is selected.
	 * See Memory attribute indirection on page 16-291. */
	// set Attribute 0 Normal, Inner/Outer Non-Cacheable (the only available)
	// the field AttrIndex determines the attribute that must be applied for the page table
	// MAIR defines
	// type: normal or device memory
	// if normal it specifies the properties for inner and outer cacheability: WT, WB
	// if WT/WB, the read allocates and write allocates policy
	// in this case entry 0 is Normal memory, Inner/Outer Non-Cacheable

	set_CBn_MAIR_stage1(cb_index_0, NORMAL_IO_NonCacheable);
	set_CBn_MAIR_stage1(cb_index_1, NORMAL_IO_NonCacheable);

	/* -- Init MAIR -- */

	/* -- Init TCR and TCR2 -- */
	// TCR has different formats depending on the value of SMMU_CBA2Rn.VA64 and on the CB stage (1 or 2).
	// Note: the SMMU_CBn_TCR determines which TTBR must be used for translation. By default is set to TTBR0.
	// In aarch32 granule is fixed to 4KB (there is no TG0)

	// set TCR for context bank 0 and context bank 1
	u8 eae   = 0x1; // EAE exists only if aarch32 is selected, EAE = 1 select the LPAE mode
	u8 t0sz  = 0x0; // T0SZ = 0x0 VA space is 32 bits (32 - 0x0 = 32 bit), TTBR1 disabled (x = 5)
	u8 t1sz  = 0x0; // T1SZ = 0x0 TTBR1 disabled (pp.31-32)
	u8 irgn0 = 0x1; // IGRN0=0b01  Walks to TTBR0 are Inner WB/WA
	u8 orgn0 = 0x1; // OGRN0=0b01  Walks to TTBR0 are Outer WB/WA
	u8 sh0   = 0x1; // SH0=0b11   Inner Shareable (Shareable attributes for the memory associated with the translation table walks using SMMU_CBn_TTBR0)

	// WARNING tg0: in aarch32 the granule is fixed at 4KB
	set_CBn_TCR_lpae_32_stage1(cb_index_0, t0sz, irgn0, orgn0, sh0, t1sz, eae);
	set_CBn_TCR_lpae_32_stage1(cb_index_1, t0sz, irgn0, orgn0, sh0, t1sz, eae);

	// set TCR2 for context bank 0 and context bank 1
	// Note: This register does not exist for stage 2 CBs
	u8 tbi0    = 0b0;  // Top byte not ignored. It is used in the address calculation.
	u8 pa_size = 0b00; // IPS: pa_size = 0 --> 32 bit PA space
	set_CBn_TCR2_stage1(cb_index_0, tbi0, pa_size);
	set_CBn_TCR2_stage1(cb_index_1, tbi0, pa_size);

	/* -- Init TCR and TCR2 -- */

	/* -- Init CBn_TTBR */

	// pp.341 format for aarch32 lpae
	// first 32 bits of the variables will be taken
	xil_printf("The value of cb0_tt_l1_base_64 address is 0x%016llX\n\r", cb0_tt_l1_base_64);
	xil_printf("The value of cb1_tt_l1_base_64 address is 0x%016llX\n\r", cb1_tt_l1_base_64);
	set_CBnTTBR0_32_lpae_stage1(cb_index_0, 0x0, (u32)(&cb0_tt_l1_base_64), t0sz);
	set_CBnTTBR0_32_lpae_stage1(cb_index_1, 0x0, (u32)(&cb1_tt_l1_base_64), t0sz);

	/* -- Init CBn_TTBR */

	/* -- set SMMU_CBn_SCTLR -- */

	u8 m_bit = 0x1; // enable bit
	u8 cfre = 0x1;  // return an abort when a context fault occurs for the cb
	u8 cfie = 0x1;  // raise an interrupt when a context fault occurs for the cb

	set_SMMU_CBn_SCTLR(cb_index_0, m_bit, cfre, cfie);
	set_SMMU_CBn_SCTLR(cb_index_1, m_bit, cfre, cfie);

	/* -- set SMMU_CBn_SCTLR -- */

	/* -- set ClientPD to 0 --*/

	clientpd = 0x0;
	gfre = 0x1;
	gfie = 0x1;
	stalld = 0x1;
	usfcfg = 0x1;
	set_SMMU_sCR0(clientpd, gfre, gfie, stalld, usfcfg);

	/* -- set ClientPD to 0 --*/

/* -------------- 32 bit lpae config -------------- */
#else

/* -------------- 64 bit config (TODO) -------------- */

	/* -- SET SCR1 --*/
	// set SCR1 so that all the SMRs and CBs are available to the NS world
	u32 nsnumcbo = 0x0;
	u32 nsnumsmrgo = 0x0;
	setSCR1(nsnumcbo, nsnumsmrgo);
	/* -- SET SCR1 --*/

	/* -- configure the translation tables -- */

	// set the entries to 0
	memset(cb0_tt_l1_base_64, 0x0, sizeof(u64)*4);
	memset(cb1_tt_l1_base_64, 0x0, sizeof(u64)*4);


	/* -- configure the translation tables -- */

	// generate the translation tables for CB0
	// NOTE: The VA size is 39 bits, so there is no L0 ([47:39])
	// we decided to point directly to blocks and not lower level tables, so we only have L1 tables
	// entry format https://developer.arm.com/documentation/den0024/a/The-Memory-Management-Unit/Translation-tables-in-ARMv8-A/AArch64-descriptor-format

	// the bits [38:30] bits will select the L1 entry, the entry will contain the address of the block (1 GiB) 2^30
	// the remaining bits of the VA [29:0] are used to offset in the block range
	// I set the first entry, with index 0, of both tables and it points to a 1GiB block at the address 0x00000000
	// The translating address is at the beginning of the entry
	// The entries are not cacheable by the TLBs
	// bitwise or example: 0x00000000000000401 | 0x00000000040000000 = 0x00000000040000401 --> PA: 0x000040000

	// NOTE: in the example it sets a different output address 0x40000000 but in the wrong way
	// Indeed, the ORR is done without shifting the value to the 12-th bit (output address is [47:12] for 4KB granule
	// https://armv8-ref.codingbelief.com/en/chapter_d4/d43_2_armv8_translation_table_level_3_descriptor_formats.html#
	u16 entry_index = 0x0;

	// following this: https://developer.arm.com/documentation/ddi0406/c/System-Level-Architecture/Virtual-Memory-System-Architecture--VMSA-/Long-descriptor-translation-table-format/Memory-attributes-in-the-Long-descriptor-translation-table-format-descriptors?lang=en

	// valid [0]: 1 (valid)
	u64 entry_value = ((u64)0x1); // valid entry

	// descriptor type [1]: 0 (block)
	setBit64(&entry_value, 1, 0x0);

	// AttrIndex [2:0] bits [4:2]: 0b000) (index 0 of MAIR)
	setBitRange64(&entry_value, 4, 2, 0x0);

	// NS [5]: 0 (in secure state, only secure addresses are accessed)
	setBit64(&entry_value, 5, 0x0); // ignored for non-secure accesses

	// AP [2:1] [7:6]: 0b01 (read/write at any privilege level)
	setBitRange64(&entry_value, 7, 6, 0x1);

	// note: inner shareable limits the shareability of the entry to the inner one
	// SH [9:8]: 0b10 (outer shareable: can be seen by both inner and outer devices)
	setBitRange64(&entry_value, 9, 8, 0x2);

	// AF [10]: 1 (indicate that the page has not been accessed --> value of 1 --> No Access Flag Fault is generated on access.)
	setBit64(&entry_value, 10, 0x1);

	// nG [11]: 0 (translation is global, can be seen by all processes)
	setBit64(&entry_value, 11, 0x0);

	// contiguos hint [52]: 0 (the entry is not part of a contiguos block)
	setBit64(&entry_value, 52, 0x0);

	// PXN [53] (privileged execute-never bit): 0 (the region is executable at PL1)
	setBit64(&entry_value, 53, 0x0);

	// XN [54] (Execute-never bit): 0 (the regions is executable)
	setBit64(&entry_value, 54, 0x0);

	u64 entry_value_0 = entry_value;
	u64 entry_value_1 = entry_value;

	// 0 entry PA: 0x0000,0000 - 0x3FFF,FFFF (FLAT)
	setBitRange64(&entry_value_0, 47, 30, output_address_0);

	// 0 entry PA: 0x4000,0000 - 0x7FFF,FFFF (NOT FLAT)
	setBitRange64(&entry_value_1, 47, 30, output_address_1);

	set_Table_Entry_64(&cb0_tt_l1_base_64, entry_index, entry_value_0);
	set_Table_Entry_64(&cb1_tt_l1_base_64, entry_index, entry_value_1);

	/* -- configure the translation tables -- */

	/* -- Init sCR0 -- */

	/*
		Accessing the secure copy of sCR0:
		CLIENTPD [bit 0]:   1 (bypass transaction)
		GFRE [bit 1]:       1 (raise global fault)
		GFIE [bit 2]:       1 (raise a global interrupt in case of fault)
		EXIDENABLE [bit 3]: 0 use the SMMUv1 format for SMR (reserved in zynq ultrascale+, so in the function is not set)
		STALLD [bit 8]: 	1 Disable per-context stalling on context faults
		USFCG [bit 10]:     1 to generate a Unidentified stream fault instead of apply SMMU_sCR0 attributes
		GCFGFRE, GCFGFIE are read-only and are for global configuration faults but they are read-only.
		NOTE: in NSCR0 the bits from [31:28] are not implemented
	*/
	u8 clientpd = 0x1;
	u8 gfre = 0x1;
	u8 gfie = 0x1;
	u8 stalld = 0x1;
	u8 usfcfg = 0x1;
	set_SMMU_sCR0(clientpd, gfre, gfie, stalld, usfcfg);

	/* -- Init sCR0 -- */

	/* -- Init SMR -- */
    // SMR indexes
    u8 smr_index_0 = 0; // SMR0
    u8 smr_index_1 = 1; // SMR1
    u8 smr_index_2 = 2; // SMR2

	// CB indexes
    u8 cb_index_0  = 0; // CB0
    u8 cb_index_1  = 1; // CB1
    u8 cb_index_2  = 2; // CB2

	// Note: the stream id is: TBU number [14:10], master id [9:0] (AXI ID included)

	// reset all the SMRs
	bool valid = false;
	for(int i=0; i < N_SMRs; i++){
		set_SMRn(i, valid, 0x0, 0x0, 0x0);
	}

	// setting the SMRs

    // masks: the values to 0 indicates the ones to consider
	u16 stream_id_mask = 0x0000; // the mask match the entire stream id
	valid = true;

	// FPD_DMA0 --> SMR_0
	set_SMRn(smr_index_0, valid, stream_id_mask, GDMA_TBU, GDMA_0_MID);

	// FPD_DMA1 --> SMR_1
	set_SMRn(smr_index_1, valid, stream_id_mask, GDMA_TBU, GDMA_1_MID);

	// DAP_APB_control --> SMR_2
	set_SMRn(smr_index_2, valid, stream_id_mask, DAP_APB_control_TBU, DAP_APB_control_MID);

	/* -- Init SMR -- */

	/* -- Init S2CR -- */

	set_S2CRn(cb_index_0, TRANSLATION_CB, cb_index_0);
	set_S2CRn(cb_index_1, TRANSLATION_CB, cb_index_1);

	// BYPASS FOR DAP APB CONTROL, in this case the index is just the S2CR index
	set_S2CRn(cb_index_2, BYPASS, cb_index_2);

	/* -- Init S2CR -- */

	/* -- Disable all the CBs -- */

	for (int i = 0; i < N_CBs; i++){
		set_SMMU_CBn_SCTLR(i, 0x0, 0x0, 0x0);
	}

	/* -- Disable all the CBs -- */

	/* -- Init CBA2R -- */

	// set the context bank of index 0-1 to AArch32 translation scheme (RPU processors are 32 bit)
	set_CBA2Rn_VA(cb_index_0, VA_64);
	set_CBA2Rn_VA(cb_index_1, VA_64);

	/* -- Init CBA2R -- */

	/* -- Init CBAR -- */

	// in SMMUv2 each context bank has its own pin, and this register can be configured to raise an interrupt in
	// the event of context fault
	// RESET STATE: the SMMU_CBARn registers are not initialized. They must be initialized before use.
	set_CBARn(cb_index_0, STAGE_1_BYPASS_2);
	set_CBARn(cb_index_1, STAGE_1_BYPASS_2);

	/* -- Init CBAR -- */

	/* -- Init MAIR -- */

	/*
	 * The CBn_MAIR registers are used when either the AArch32 Long-descriptor or the AArch64
	 * translation scheme is selected. The SMMU_CBn_PRRR and SMMU_CBn_NMRR
	 * registers are used when the AArch32 Short-descriptor translation scheme is selected.
	 * See Memory attribute indirection on page 16-291. */
	// set Attribute 0 Normal, Inner/Outer Non-Cacheable (the only available)
	// the field AttrIndex determines the attribute that must be applied for the page table
	// MAIR defines
	// type: normal or device memory
	// if normal it specifies the properties for inner and outer cacheability: WT, WB
	// if WT/WB, the read allocates and write allocates policy
	// in this case entry 0 is Normal memory, Inner/Outer Non-Cacheable

	set_CBn_MAIR_stage1(cb_index_0, NORMAL_IO_NonCacheable);
	set_CBn_MAIR_stage1(cb_index_1, NORMAL_IO_NonCacheable);

	/* -- Init MAIR -- */

	/* -- Init TCR and TCR2 -- */
	// TCR has different formats depending on the value of SMMU_CBA2Rn.VA64 and on the CB stage (1 or 2).
	// Note: the SMMU_CBn_TCR determines which TTBR must be used for translation. By default is set to TTBR0.
	// In aarch32 granule is fixed to 4KB (there is no TG0)

	// set TCR for context bank 0 and context bank 1
	// t0sz select the lookup level for TTBRm
	// ARRIVATP QUA
	u8 t0sz  = 0x19; // T0SZ = 0x19, it limits the VA space to 39 bits (64 - 0x19 = 39 bits)
	u8 t1sz  = 0x0;  // T1SZ
	u8 irgn0 = 0x1;  // IGRN0=0b01, Walks to TTBR0 are Inner WB/WA
	u8 orgn0 = 0x1;  // OGRN0=0b01, Walks to TTBR0 are Outer WB/WA
	u8 sh0   = 0x1;  // SH0=0b11, Inner Shareable (Shareable attributes for the memory associated with the translation table walks using SMMU_CBn_TTBR0)
	u8 tg0   = 0x0;  // TG0=0b00, 4KB granule 8only for stage2)
	set_CBn_TCR_64_stage1(cb_index_rpu0, t0sz, t1sz, irgn0, orgn0, sh0, tg0);

	// set TCR2 for context bank 0 and context bank 1
	// Note: This register does not exist for stage 2 CBs
	u8 tbi0    = 0b0;  // Top byte not ignored. It is used in the address calculation.
	u8 pa_size = 0b00; // IPS: pa_size = 0 --> 32 bit PA space
	set_CBn_TCR2_stage1(cb_index_0, tbi0, pa_size);
	set_CBn_TCR2_stage1(cb_index_1, tbi0, pa_size)

	/* -- Init TCR and TCR2 -- */

	/* -- Init CBn_TTBR */

	// pp.341 aarch64 format
	set_CBnTTBR0_64_stage1(cb_index_0, 0x0, &cb0_tt_l1_base_64, t0sz);
	set_CBnTTBR0_64_stage1(cb_index_1, 0x0, &cb1_tt_l1_base_64, t0sz);

	/* -- Init CBn_TTBR */

	/* -- enable the CBs -- */
	set_SMMU_CBn_SCTLR_M(cb_index_rpu0, 0x1);
	// set_SMMU_CBn_SCTLR_M(cb_index_rpu1, 0x1);
	// set_SMMU_CBn_SCTLR_M(cb_index_DAP, 0x1);
	/* -- enable the CBs -- */

	/* -- set ClientPD to 0 --*/

	clientpd = 0x0;
	gfre = 0x1;
	gfie = 0x1;
	stalld = 0x1;
	usfcfg = 0x1;
	set_SMMU_sCR0(clientpd, gfre, gfie, stalld, usfcfg);

	/* -- set ClientPD to 0 --*/

/* -------------- 64 bit config -------------- */
#endif

	xil_printf("# APU0: All is set \n\r");

	/* Note: the SrcAddr (SrcBuf) and the DstAddr (DstBuf) are both written in the CDMA register, for this
	 * reason the virtualization is both on the Src and Dst.
	 */

	// wait some time
	usleep(1000000*2);

	xil_printf("# SrcBuf address is 0x%08X\n\r", SrcBuf);
	xil_printf("# DstBuf address is 0x%08X\n\r", DstBuf);

	xil_printf("# ------------- APU0: CDMA0 test ------------- \n\r");
	// set SrcBuf
	for (int i=0; i<DMA_BUF_SIZE; i++){
		SrcBuf[i] = 0xFF;
	}

	// clear DstBuffer
	for (int i=0; i<DMA_BUF_SIZE; i++){
		DstBuf[i] = 0x00;
	}

	xil_printf("# APU0: Destination buffer read: 0x%08X\r\n", DstBuf[0]);


	xil_printf("# APU0: Starting the DMA transfer using CDMA0...\n\r");

	// CDMA0 transfer
	/*XAxiCdma_SimpleTransfer(&FpdCDma0, (UINTPTR)SrcBuf, (UINTPTR)DstBuf, DMA_BUF_SIZE, NULL, NULL);
	while (XAxiCdma_IsBusy(&FpdCDma0));

	// print
	xil_printf("# APU0: Completed\n\r");
	xil_printf("# APU0: Destination buffer readback: 0x%08X\r\n", DstBuf[0]);*/

	xil_printf("# ------------- APU0: CDMA1 test ------------- \n\r");
	xil_printf("# APU0: SrcBuf_virt address is: 0x%08X\r\n", SrcBuf_virt);
	xil_printf("# APU0: DstBuf_virt address is: 0x%08X\r\n", DstBuf_virt);

	// Clear the DstBuf_Virt
	for (int i=0; i<DMA_BUF_SIZE; i++){
		DstBuf_virt[i] = 0x00;
	}

	// Set the SrcBuf_Virt
	for (int i=0; i<DMA_BUF_SIZE; i++){
		SrcBuf_virt[i] = 0xFF;
	}

	xil_printf("# APU0: Repeating the experiment with virtualized buffer initialized...\n\r");
	xil_printf("# APU0: Starting the DMA transfer using CDMA1...\n\r");

	// CDMA1 transfer
	XAxiCdma_SimpleTransfer(&FpdCDma1, (UINTPTR)SrcBuf, (UINTPTR)DstBuf, DMA_BUF_SIZE, NULL, NULL);
	while (XAxiCdma_IsBusy(&FpdCDma1));

	// wait for 5 seconds for the transfer
	/*sleep(5);
	if(XAxiCdma_IsBusy(&FpdCDma1)){
		printCBnErrors(1);
		printSMMUGlobalErr();
	}*/
	//while (XAxiCdma_IsBusy(&FpdCDma1));

	// print
	xil_printf("# APU0: Completed\n\r");
	xil_printf("# APU0: Virtualized Destination buffer DstBuf_Virt: 0x%08X\r\n", DstBuf_virt[0]);

	// cleanup
    cleanup_platform();
    return 0;
}
