#include "smmu_driver.h"

void setBitRange16(u16* regVal, u8 end_bit, u8 start_bit, u16 value){
    u8 numBits = end_bit - start_bit + 1;
    u16 mask = ((1U << numBits) - 1) << start_bit;

    // Clear the specified range of bits in regVal
    *regVal &= ~mask;

    // Set the bits in the specified range to the value
    *regVal |= (value << start_bit) & mask;
}


void setBitRange32(u32* regVal, u8 end_bit, u8 start_bit, u32 value){
	u32 numBits = end_bit - start_bit + 1;
	u32 mask = ((1U << numBits) - 1) << start_bit;

	// Clear the specified range of bits in regVal
	*regVal &= ~mask;

	// Set the bits in the specified range to the value
	*regVal |= (value << start_bit) & mask;
}

// Function to set a range of bits in a 64-bit value
void setBitRange64(u64* regVal, u8 end_bit, u8 start_bit, u64 value) {
    u8 numBits = end_bit - start_bit + 1;
    u64 mask = ((1ULL << numBits) - 1) << start_bit;

    // Clear the specified range of bits in regVal
    *regVal &= ~mask;

    // Set the bits in the specified range to the value
    *regVal |= (value << start_bit) & mask;
}

// Function to set a specific bit in a 32-bit value
void setBit32(u32* regVal, u8 bit_position, u8 value) {
    u32 mask = 1U << bit_position;

    // Clear the specific bit in regVal
    *regVal &= ~mask;

    // Set the specific bit to the specified value (0 or 1)
    *regVal |= (value << bit_position) & mask;
}

// Function to set a specific bit in a 64-bit value
void setBit64(u64* regVal, u8 bit_position, u8 value) {
    u64 mask = 1ULL << bit_position;

    // Clear the specific bit in regVal
    *regVal &= ~mask;

    // Set the specific bit to the specified value (0 or 1)
    *regVal |= ((u64)value << bit_position) & mask;
}

void show_SMMU_SIDRn(u8 index){
	u32 regVal = 0x0;
	u32 targetReg = SMMU_SIDRn_base + index*4;

	regVal = Xil_In32(targetReg);

	xil_printf("SMMU_SIDR%d is: 0x%08X\n\r", index, regVal);
}

void set_SMMU_CBn_SCTLR(u8 offset, u8 m_bit, u8 cfre, u8 cfie){
	u32 targetReg = SMMU_CBn_SCTLR_base + offset*CBn_offset;
	u32 regVal = 0x0;

	// set the M bit (enable)
	setBit32(&regVal, 0, m_bit);

	// set the cfre bit
	setBit32(&regVal, 5, cfre);

	// set the cfie bit
	setBit32(&regVal, 6, cfie);

	// update
	Xil_Out32(targetReg, regVal);

	// print
	regVal = Xil_In32(targetReg);
	xil_printf("SMMU_CB%d_SCTLR(0x%08X) has been set to: 0x%08X\n\r", offset, targetReg, regVal);
}

// this will access the corresponding banked copy of SCR depending if secure or non-secure
void set_SMMU_sCR0(u8 clientpd, u8 gfre, u8 gfie, u8 stalld, u8 usfcg){
	u32 regVal = 0x0;
	u32 targetReg = SMMU_sCR0;

	// set:
	// CLIENTPD [bit 0]: 0 (do not bypass transaction)
	setBit32(&regVal, 0, clientpd);

	// GFRE [bit 1]: 1 (raise a global fault)
	setBit32(&regVal, 1, gfre);

	// GFIE [bit 2]: 1 (raise a global interrupt in case of fault)
	setBit32(&regVal, 2, gfie);

	// STALLD [bit 8]
	setBit32(&regVal, 8, stalld);

	// USFCG [bit 10]: 1 to generate a Unidentified stream fault on any transaction that does not
	// match any Stream mapping table entries.
	setBit32(&regVal, 10, usfcg);

	// GCFGFRE, GCFGFIE are read-only and are for global configuration faults but they are read-only.
	// NOTE: in NSCR0 the bits from [31:28] are not implemented
	// update register
	Xil_Out32(targetReg, regVal);

	// print
	regVal = Xil_In32(targetReg);
	xil_printf("SMMU_SCR0(0x%08X) has been set to: 0x%08X\n\r", targetReg, regVal);
}

void set_SMRn(u8 index, bool valid, u16 mask, u16 tbu_number, u16 mid){
	u32 targetReg = SMMU_SMR_base + index*4;
	u32 regVal = 0x0;

	// set VALID
	if ( valid ){
		// Set the first bit to 1 (VALID)
		setBit32(&regVal, 31, 0x1);
	}

	// Set bits 30 to 16 to the MASK
	setBitRange32(&regVal, 30, 16, mask);

	// Set bits 14 to 10 to the tbu_number
	setBitRange32(&regVal, 14, 10, tbu_number);

	// Set bits 9 to 0 to the mid
	setBitRange32(&regVal, 9, 0, mid);

	// Write the value to the target SMR register
	Xil_Out32(targetReg, regVal);

	// Print
	regVal = Xil_In32(targetReg);
	xil_printf("SMR%d(0x%08X) has been set to: 0x%08X\n\r", index, targetReg, regVal);
}


void set_S2CRn (u8 offset, enum s2cr_type type, u8 cb_index){
	u32 regVal = 0x0;
	u32 targetReg = SMMU_S2CR_base + offset*4;

	if(type == TRANSLATION_CB){
		// read the default values
		//regVal = Xil_In32(targetReg);

		// set the type bits [17:16]
		setBitRange32(&regVal, 17, 16, type);

		// set context bank index CBNDX[7:0]
		// NOTE: the context banks are less than the SMRn and S2CRn. We can have more SMRs matching the same context bank!
		setBitRange32(&regVal, 7, 0, cb_index);

		// update the register
		Xil_Out32(targetReg, regVal);

		// print
		regVal = Xil_In32(targetReg);
		xil_printf("S2CR%d(0x%08X) has been set to: 0x%08X\n\r", offset, targetReg, regVal);
	}
	else{
		// Note: set the register value to all 0s (default)
		// set the type bits [17:16]
		setBitRange32(&regVal, 17, 16, type);

		// update the register
		Xil_Out32(targetReg, regVal);

		// print
		regVal = Xil_In32(targetReg);

		xil_printf("S2CR%d(0x%08X) has been set to: 0x%08X\n\r", offset, targetReg, regVal);
	}
}

void set_CBARn(u8 offset, enum cbar_type type){
	u32 regVal = 0x0;
	u32 targetReg = SMMU_CBAR_base + offset*4;

	// set the type bits [17:16]
	setBitRange32(&regVal, 17, 16, type);

	// update the register
	Xil_Out32(targetReg, regVal);

	// print
	regVal = Xil_In32(targetReg);
	xil_printf("CBAR%d(0x%08X) has been set to: 0x%08X\n\r", offset, targetReg, regVal);
}

// PP.341 OF THE MANUAL
// TTBR is ASID[63:48], [47:x] base address, [x-1:0] reserved (SBZ)
// x = 28 - TOSZ   by pp.79 (VMSAv8-64 using 4kb granule)

// aarch32 lpae version
// TTBR0 is RESERVED[63:56], ASID[55:48], base address [47:x], SBZ [x-1:0]
// pp.71 and pp.31
// Note that LPAE uses a 4KB granule by default
void set_CBnTTBR0_32_lpae_stage1(u8 offset, u16 asid, u32 translation_table_addr, u8 t0sz){
	u32 targetReg = SMMU_CBn_TTBR0_base + CBn_offset*offset;
	u64 regVal = 0x0;
	u8 x = 5-t0sz; // unused

	// set the table address
	// the output address is said to be [39:x] but actually the address must start from 0 and the first
	// 3 bit are 0b000 (4KB aligned)
	// Remember that the granularity is fixed at 4Kb for aarch32 lpae
	xil_printf("Writing on TTBR0 the translation table address: 0x%08X\n\r", translation_table_addr);
	setBitRange64(&regVal, 31, 0, translation_table_addr);

	// set the rest of the base address to 0
	// AArch32 state does not support addresses larger than 40 bits, therefore bits[47:40] are always RES0.
	setBitRange64(&regVal, 47, 40, 0x0);

	// set the ASID [55:48]
	setBitRange64(&regVal, 55, 48, asid);

	// update register
	Xil_Out64(targetReg, regVal);

	// print
	regVal = Xil_In64(targetReg);
	xil_printf("The CB%d_TTBR0(0x%08X) register has been set to: 0x%016llX\n\r", offset, targetReg, (u64)regVal);
}

void set_CBnTTBR0_64_stage1(u8 offset, u8 asid, u32 translation_table_addr, u8 t0sz, u8 first_lookup_level){
	u32 targetReg = SMMU_CBn_TTBR0_base + offset*8;
	u64 regVal = 0x0;
	u8 x = 5-t0sz;

	// check if the translation table addr is aligned to 0b000 (16)
	/*u32 mask = 0x7;
	if ((translation_table_addr & mask) != 0){
		xil_printf("Error, the translation table addr must be aligned to 0b000\n\r");
		return;
	}

	if (x != 5){
		xil_printf("Error, t0sz must be 0\n\r");
		return;
	}*/

	// set the table address
	xil_printf("Writing on TTBR0 the translation table address: 0x%016llX\n\r", translation_table_addr);
	setBitRange64(&regVal, 47, x, translation_table_addr);

	// set the ASID [55:48]
	setBitRange64(&regVal, 63, 48, asid);

	// update register
	Xil_Out64(targetReg, regVal);

	// print
	regVal = Xil_In64(targetReg);
	xil_printf("The CB%d_TTBR0(0x%08X) register has been set to: 0x%016llX\n\r", offset, targetReg, (u64)regVal);
}

void set_CBnTTBR0_32_lpae_stage2(u8 offset, u32 translation_table_addr, u8 t0sz){
	u32 targetReg = SMMU_CBn_TTBR0_base + offset*CBn_offset;
	u64 regVal = 0x0;
	u8 x = 5-t0sz;

	xil_printf("The translation table address to write is: 0x%016llX\n\r", translation_table_addr);

	// read default values
	regVal = Xil_In64(targetReg);
	xil_printf("The CB%d_TTBR0 register is:0x%016llX\n\r", offset, (u64)regVal);

	// set the table address
	// if x = 5, the output address is in [39:5]
	xil_printf("Writing on TTBR0...\n\r");
	setBitRange64(&regVal, 39, x, translation_table_addr);

	// update register
	Xil_Out64(targetReg, regVal);

	// print
	regVal = Xil_In64(targetReg);
	xil_printf("The CB%d_TTBR0 register has been set to: 0x%016llX\n\r", offset, (u64)regVal);
}

void set_CBA2Rn_VA(u8 offset, enum va_size size){
	u32 regVal = 0x0;
	u32 targetReg = SMMU_CBA2Rn_base + offset*4;

	// read the default values
	regVal = Xil_In32(targetReg);

	// set the VA64 bit
	setBit32(&regVal, 0, size);

	// update the register
	Xil_Out32(targetReg, regVal);

	// print
	regVal = Xil_In32(targetReg);
	xil_printf("CBA2R%d(0x%08X) has been set to: 0x%08X\n\r", offset, targetReg, regVal);
}

// Note: The register to be used is PRRR for aarch32 and MAIR for aarch32 LPAE or aarch64
// this register does not exist in stage 2
// Along with MAIR1, provides the memory attribute encodings corresponding to the possible AttrIndx values
// in a Long-descriptor format translation table entry for stage 1 translations.
// when When AttrIndx[2] is 0 in the entry, MAIR0 is used. AttrIndx[2] is 1, MAIR1 is used.
void set_CBn_MAIR_stage1(u8 offset, u32 mair_value){
	u32 targetReg = SMMU_CBn_PRRR_MAIRn_base + offset*CBn_offset;

	// update the register
	Xil_Out32(targetReg, mair_value);

	// print
	u32 regVal = Xil_In32(targetReg);
	xil_printf("CB%d_MAIR(0x%08X) has been set to: 0x%08X\n\r", offset, targetReg, regVal);
}

/* SL0 == 0 if the initial lookup is level 2, SL0 == 1 if the initial lookup is level 1,
 * and SL0 == 2 if the initial lookup level is level 0
 */
// following pp. 358 of the doc
// Note that TCR properties applies for stage 2 translations
void set_CBn_TCR_lpae_32_stage1(u8 offset, u8 t0sz, u8 irgn0, u8 orgn0, u8 sh0, u8 t1sz, u8 eae){
	u32 regVal = 0x0;
	u32 targetReg = SMMU_CBn_TCR_base + offset*CBn_offset;

	// read the default values
	// regVal = Xil_In32(targetReg);

	// set the fields
	// T0SZ[2:0]
	// this field determine the size of the address in TTBR according to the algorithm pp.79
	setBitRange32(&regVal, 2, 0, t0sz);

	// IRGN0: Inner cacheability attributes for the memory associated with the translation table walks using SMMU_CBn_TTBR0.
	setBitRange32(&regVal, 9, 8, irgn0);

	// ORGN0: Outer cacheability attributes for the memory associated with the translation table walks using SMMU_CBn_TTBR0.
	setBitRange32(&regVal, 11, 10, orgn0);

	// SH0: Shareability attributes for the memory associated with the translation table walks using SMMU_CBn_TTBR0.
	setBitRange32(&regVal, 13, 12, sh0);

	// T1SZ [18:16]: The size offset of the SMMU_CBn_TTBR1 addressed region.
	setBitRange32(&regVal, 18, 16, t1sz);

	// EAE: A value of 1 means that the translation system defined in the LPAE is used.
	setBit32(&regVal, 31, eae);

	// update the register
	xil_printf("Writing to CB%d_TCR_lpae(0x%08X) the value of: 0x%08X\n\r", offset, targetReg, regVal);
	Xil_Out32(targetReg, regVal);

	// print
	regVal = Xil_In32(targetReg);
	xil_printf("CB%d_TCR_lpae(0x%08X) has been set to: 0x%08X\n\r", offset, targetReg, regVal);
}

void set_CBn_TCR_lpae_32_stage2(u8 offset, u8 t0sz, u8 sl0, u8 irgn0, u8 orgn0, u8 sh0, u8 eae){
	u32 regVal = 0x0;
	u32 targetReg = SMMU_CBn_TCR_base + offset*CBn_offset;

	// read the default values
	regVal = Xil_In32(targetReg);

	// set the fields
	// T0SZ [2:0]
	// this field determine the size of the address in TTBR according to the algorithm pp.79
	setBitRange32(&regVal, 3, 0, t0sz);

	// SL0 [7:6]
	// starting lookup level for the SMMU_CBn_TTBR0 addressed region (for stage 2): 0 for level 2, 1 for level 1
	setBitRange32(&regVal, 7, 6, sl0);

	// IRGN0: Inner cacheability attributes for the memory associated with the translation table walks using SMMU_CBn_TTBR0.
	setBitRange32(&regVal, 9, 8, irgn0);

	// ORGN0: Outer cacheability attributes for the memory associated with the translation table walks using SMMU_CBn_TTBR0.
	setBitRange32(&regVal, 11, 10, orgn0);

	// SH0: Shareability attributes for the memory associated with the translation table walks using SMMU_CBn_TTBR0.
	setBitRange32(&regVal, 13, 12, sh0);

	// EAE: A value of 1 means that the translation system defined in the LPAE is used.
	setBit32(&regVal, 31, eae);

	// update the register
	Xil_Out32(targetReg, regVal);

	// print
	regVal = Xil_In32(targetReg);
	xil_printf("CB%d_TCR_lpae has been set to: 0x%08X\n\r", offset, regVal);
}

void set_CBn_TCR_64_stage1(u8 offset, u8 t0sz, u8 t1sz, u8 irgn0, u8 orgn0, u8 sh0, u8 tg0){
	u32 regVal = 0x0;
	u32 targetReg = SMMU_CBn_TCR_base + offset*CBn_offset;

	// read the default values
	regVal = Xil_In32(targetReg);

	// set the fields
	// T0SZ [2:0]
	// this field determine the size of the address in TTBR according to the algorithm pp.79
	setBitRange32(&regVal, 5, 0, t0sz);

	// IRGN0: Inner cacheability attributes for the memory associated with the translation table walks using SMMU_CBn_TTBR0.
	setBitRange32(&regVal, 9, 8, irgn0);

	// ORGN0: Outer cacheability attributes for the memory associated with the translation table walks using SMMU_CBn_TTBR0.
	setBitRange32(&regVal, 11, 10, orgn0);

	// SH0: Shareability attributes for the memory associated with the translation table walks using SMMU_CBn_TTBR0.
	setBitRange32(&regVal, 13, 12, sh0);

	// TG0: granularity
	setBitRange32(&regVal, 15, 14, tg0);

	// T1SZ
	setBitRange32(&regVal, 21, 16, t1sz);

	// update the register
	Xil_Out32(targetReg, regVal);

	// print
	regVal = Xil_In32(targetReg);
	xil_printf("CB%d_TCR_lpae has been set to: 0x%08X\n\r", offset, regVal);
}

// TCR2 does not exists in stage 2 CBs
void set_CBn_TCR2_stage1(u8 offset, u8 tbi0, u8 pa_size){
	u32 regVal = 0x0;
	u32 targetReg = SMMU_CBn_TCR2_base + offset*CBn_offset;

	// tbi0 [5]: Top Byte Ignored
	setBit32(&regVal, 5, tbi0);

	// pa_size [2:0]
	setBitRange32(&regVal, 2, 0, pa_size);

	// update the register
	Xil_Out32(targetReg, regVal);

	// print
	regVal = Xil_In32(targetReg);
	xil_printf("CB%d_TCR2(0x%08X) has been set to: 0x%08X\n\r", offset, targetReg, regVal);
}

void set_Table_Entry_32_lpae(u64* table, u16 entry_index, u64 entry_value){
	// set the entry value
	if (entry_index < GRANULARITY){
		// set
		table[entry_index] = entry_value;

		//print
		xil_printf("The entry %d has been set to 0x%016llX\n\r", entry_index, (u64)table[entry_index]);
	}
	else {
		xil_printf("Error in table entry index \r\n");
	}
}

void printSMMUGlobalErr(){

	u32 regVal = 0x0;
	u32 targetReg;

	// print global fault status
	regVal = Xil_In32(SMMU_SGFSR);
	xil_printf("The value of SMMU_SGFSR is: 0x%08X\n\r", regVal);

	regVal = Xil_In32(SMMU_SGFSYNR0);
	xil_printf("The value of SMMU_SGFSYNR0 is: 0x%08X\n\r", regVal);

	regVal = Xil_In32(SMMU_SGFSYNR1);
	xil_printf("The value of SMMU_SGFSYNR1 is: 0x%08X\n\r", regVal);

	// read SGFAR
	regVal = Xil_In32(SMMU_SGFAR_low);
	xil_printf("The value of SMMU_SGFAR_low is: 0x%u\n\r", regVal);

	regVal = Xil_In32(SMMU_SGFAR_high);
	xil_printf("The value of SMMU_SGFAR_high is: 0x%u\n\r", regVal);

	// read SMMU_NSGFAR_low
	regVal = Xil_In32(SMMU_NSGFAR_low);
	xil_printf("The value of SMMU_NSGFAR_low is: 0x%u\n\r", regVal);

	// read SMMU_NSGFAR_low
	regVal = Xil_In32(SMMU_NSGFAR_high);
	xil_printf("The value of SMMU_NSGFAR_high is: 0x%lX\n\r", regVal);
}

void printCBnErrors(int index){
	u32 targetReg;
	u32 regVal;

	// FSR
	targetReg = SMMU_CBn_FSR_base + CBn_offset*index;
	regVal = Xil_In32(targetReg);
	xil_printf("The value of SMMU_CB%d_FSR is: 0x%08X\n\r", index, regVal);

	// FAR
	targetReg = SMMU_CB0_FAR_low_base + CBn_offset*index;
	regVal = Xil_In32(targetReg);
	xil_printf("The value of SMMU_CB%d_FAR_low is: 0x%08X\n\r", index, regVal);

	targetReg = targetReg + 4;
	regVal = Xil_In32(targetReg);
	xil_printf("The value of SMMU_CB%d_FAR_high is: 0x%08X\n\r", index, regVal);

	// FSYNR0
	targetReg = SMMU_CBn_FSYNR0_base + CBn_offset*index;
	regVal = Xil_In32(targetReg);
	xil_printf("The value of SMMU_CB%d_FSYNR0 is: 0x%08X\n\r", index, regVal);
}

void clear_error_status(){
	// wtc: write 1 to clear

	// clear ISR0
	Xil_Out32(SMMU_REG_ISR0, 0xFFFFFFFF);

	// clear global fault status
	Xil_Out32(SMMU_SGFSYNR0, 0x0);
	Xil_Out32(SMMU_SGFSYNR1, 0x0);
	Xil_Out32(SMMU_SGFAR_low, 0x0);
	Xil_Out32(SMMU_SGFAR_high, 0x0);
	Xil_Out32(SMMU_NSGFAR_low, 0x0);
	Xil_Out32(SMMU_NSGFAR_high, 0x0);

	// Clear CBn_FSR, CBn_FSYNR, CBn_FAR
	u32 targetReg;
	for(int i=0; i<N_CBs; i++){

		// FSR
		targetReg = SMMU_CBn_FSR_base + i*CBn_offset;
		Xil_Out32(targetReg, 0xFFFFFFFF);

		// FSYNR
		targetReg = SMMU_CBn_FSYNR0_base + CBn_offset*i;
		Xil_Out32(targetReg, 0x00000000);

		// FAR
		targetReg = SMMU_CB0_FAR_low_base + CBn_offset*i;
		Xil_Out32(targetReg, 0x00000000);
		targetReg = targetReg + 4;
		Xil_Out32(targetReg, 0x00000000);
	}

}

// Invalidates all unlocked Secure entries in the TLB.
void invalidate_by_STLBIALL(){
	Xil_Out32(SMMU_STLBIALL, 0xFFFFFFFF);
}

// validates all Non-secure non-Hyp tagged entries in the TLB.
void invalidate_by_TLBIALLNSNH(){
	Xil_Out32(SMMU_TLBIALLNSNH, 0xFFFFFFFF);
}

void getSCR1(){
	u32 regVal = Xil_In32(SMMU_SCR1);

	xil_printf("The value of SMMU_SCR1(0x%08X) is: 0x%08X\n\r", SMMU_SCR1, regVal);
}

void setSCR1(u32 nsnumcbo, u32 nsnumsmrgo){
	u32 regVal = Xil_In32(SMMU_SCR1);

	setBitRange32(&regVal, 4, 0, nsnumcbo);

	setBitRange32(&regVal, 13, 8, nsnumsmrgo);

	// update value
	Xil_Out32(SMMU_SCR1, regVal);
	regVal = Xil_In32(SMMU_SCR1);

	// print
	xil_printf("The value of SMMU_SCR1(0x%08X) has been set to: 0x%08X\n\r", SMMU_SCR1, regVal);
}
