/*
 * Copyright (c) 2022-2024 3mdeb Sp. z o.o. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This entry point is entered from xen/arch/x86/boot/head.S with Xen base at
 * 0x4(%esp). A pointer to MBI is returned in %eax.
 */
asm (
    "    .text                         \n"
    "    .globl _start                 \n"
    "_start:                           \n"
    "    jmp  slaunch_early_tests      \n"
    );

#include "defs.h"
#include "../include/asm/intel_txt.h"
#include "../include/asm/slaunch.h"
#include "../include/asm/x86-vendors.h"

/*
 * The AMD-defined structure layout for the SLB. The last two fields are
 * SL-specific.
 */
struct skinit_sl_header
{
    uint16_t skl_entry_point;
    uint16_t length;
    uint8_t reserved[62];
    uint16_t skl_info_offset;
    uint16_t bootloader_data_offset;
} __packed;

struct early_tests_results
{
    uint32_t mbi_pa;
    uint32_t slrt_pa;
} __packed;

static bool is_intel_cpu(void)
{
    /* No boot_cpu_data in early code. */
    uint32_t eax, ebx, ecx, edx;
    cpuid(0x00000000, &eax, &ebx, &ecx, &edx);
    return ebx == X86_VENDOR_INTEL_EBX
        && ecx == X86_VENDOR_INTEL_ECX
        && edx == X86_VENDOR_INTEL_EDX;
}

void __stdcall slaunch_early_tests(uint32_t load_base_addr,
                                   uint32_t tgt_base_addr,
                                   uint32_t tgt_end_addr,
                                   uint32_t slaunch_param,
                                   struct early_tests_results *result)
{
    void *txt_heap;
    struct txt_os_mle_data *os_mle;
    struct slr_table *slrt;
    struct txt_os_sinit_data *os_sinit;
    struct slr_entry_intel_info *intel_info;
    uint32_t size = tgt_end_addr - tgt_base_addr;

    if ( !is_intel_cpu() )
    {
        /*
         * Not an Intel CPU. Currently the only other option is AMD with SKINIT
         * and secure-kernel-loader (SKL).
         */
        struct slr_entry_amd_info *amd_info;
        const struct skinit_sl_header *sl_header = (void *)slaunch_param;

        /*
         * slaunch_param holds a physical address of SLB.
         * Bootloader's data is SLRT.
         */
        result->slrt_pa = slaunch_param + sl_header->bootloader_data_offset;
        result->mbi_pa = 0;

        slrt = (struct slr_table *)result->slrt_pa;

        amd_info = (struct slr_entry_amd_info *)
                   slr_next_entry_by_tag (slrt, NULL, SLR_ENTRY_AMD_INFO);
        /* Basic checks only, SKL checked and consumed the rest. */
        if ( amd_info == NULL || amd_info->hdr.size != sizeof(*amd_info) )
            return;

        result->mbi_pa = amd_info->boot_params_base;
        return;
    }

    txt_heap = txt_init();
    os_mle = txt_os_mle_data_start(txt_heap);
    os_sinit = txt_os_sinit_data_start(txt_heap);

    result->slrt_pa = os_mle->slrt;
    result->mbi_pa = 0;

    slrt = (struct slr_table *)result->slrt_pa;

    intel_info = (struct slr_entry_intel_info *)
               slr_next_entry_by_tag (slrt, NULL, SLR_ENTRY_INTEL_INFO);
    if ( intel_info == NULL || intel_info->hdr.size != sizeof(*intel_info) )
        return;

    result->mbi_pa = intel_info->boot_params_base;

    txt_verify_dma_protection(os_mle, os_sinit, intel_info,
                              load_base_addr, tgt_base_addr, size);
}
