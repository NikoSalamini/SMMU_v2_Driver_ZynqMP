#include <stdbool.h>
#include "xil_printf.h"
#include "xil_io.h"
#include "xil_cache.h"

#define CBn_offset                0x1000
#define SMMU_sCR0                 0xFD800000
#define SMMU_SCR1                 0xFD800004
#define SMMU_SMR_base             0xFD800800
#define SMMU_S2CR_base            0xFD800C00
#define SMMU_CBAR_base            0xFD801000
#define SMMU_CBn_TTBR0_base       0xFD810020
#define SMMU_CBA2Rn_base          0xFD801800
#define SMMU_CBn_PRRR_MAIRn_base  0xFD810038 // for short-descriptor is PRRR, otherwise is MAIR
#define SMMU_CBn_TCR_base         0xFD810030
#define SMMU_CBn_TCR2_base        0xFD810010
#define SMMU_SIDRn_base           0xFD800020
#define SMMU_CBn_FSYNR0_base      0xFD810068
#define SMMU_CB0_FAR_low_base     0xFD810060
#define SMMU_CBn_SCTLR_base		  0xFD810000
#define NORMAL_IO_NonCacheable    0x0000000000000044
#define SMMU_REG_ISR0             0xFD5F0010
#define SMMU_CBn_FSR_base         0xFD810058
#define SMMU_CBn_FSYNR0_base      0xFD810068
#define SMMU_SGFSR                0xFD800048
#define SMMU_SGFSYNR0             0xFD800050
#define SMMU_SGFSYNR1             0xFD800054
#define SMMU_SGFAR_low            0xFD800040
#define SMMU_SGFAR_high           0xFD800044
#define SMMU_NSGFAR_low           0xFD800440
#define SMMU_NSGFAR_high          0xFD800444
#define SMMU_STLBIALL             0xFD800060
#define SMMU_TLBIALLNSNH          0xFD800068
#define GRANULARITY 	 	      4096 // 4KB (fixed for aarch32)
#define N_ENTRIES                 512
#define N_SMRs                    48
#define N_CBs                     16

// enums
enum s2cr_type {TRANSLATION_CB = 0b00, BYPASS = 0b01, FAULT = 0b10, RESERVED = 0b11};
enum cbar_type {STAGE_2_CONTEXT = 0b00, STAGE_1_BYPASS_2 = 0b01, STAGE_1_FAULT_2 = 0b10, STAGE_1_2 = 0b11};
enum va_size {VA_32 = 0, VA_64 = 1};

// function prototypes
void setBitRange16(u16* regVal, u8 end_bit, u8 start_bit, u16 value);
void setBitRange32(u32* regVal, u8 end_bit, u8 start_bit, u32 value);
void setBitRange64(u64* regVal, u8 end_bit, u8 start_bit, u64 value);
void setBit32(u32* regVal, u8 bit_position, u8 value);
void setBit64(u64* regVal, u8 bit_position, u8 value);

void show_SMMU_SIDRn(u8 index);
void set_SMMU_CBn_SCTLR(u8 offset, u8 m_bit, u8 cfre, u8 cfie);
void set_SMMU_sCR0(u8 clientpd, u8 gfre, u8 gfie, u8 stalld, u8 usfcg);
void set_SMRn(u8 index, bool valid, u16 mask, u16 tbu_number, u16 mid);
void set_SMRn_by_StreamID(u8 index, bool valid, u16 mask, u16 stream_id);
void set_S2CRn(u8 offset, enum s2cr_type type, u8 cb_index);
void set_CBARn(u8 offset, enum cbar_type type);
void set_CBnTTBR0_32_lpae_stage1(u8 offset, u16 asid, u32 translation_table_addr, u8 t0sz);
void set_CBnTTBR0_32_lpae_stage2(u8 offset, u32 translation_table_addr, u8 t0sz);
void set_CBA2Rn_VA(u8 offset, enum va_size size);
void set_CBn_MAIR_stage1(u8 offset, u32 mair_value);
void set_CBn_TCR_lpae_32_stage1(u8 offset, u8 t0sz, u8 irgn0, u8 orgn0, u8 sh0, u8 t1sz, u8 eae);
void set_CBn_TCR_lpae_32_stage2(u8 offset, u8 t0sz, u8 sl0, u8 irgn0, u8 orgn0, u8 sh0, u8 eae);
void set_CBn_TCR2_stage1(u8 offset, u8 tbi0, u8 pa_size);
void set_Table_Entry_32_lpae(u64* table, u16 entry_index, u64 entry_value);
void check_CBn_FSYNR0(u8 offset);
void invalidate_by_STLBIALL();
void invalidate_by_TLBIALLNSNH();
void printSMMUGlobalErr();
void printCBnErrors(int index);
void clear_error_status();
void getSCR1();
void setSCR1(u32 nsnumcbo, u32 nsnumsmrgo);

