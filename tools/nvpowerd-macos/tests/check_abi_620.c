/* SPDX-License-Identifier: MIT
 * Optional compile-time check of the 620 NVPCF converter against NVIDIA's public
 * 620.70.06 headers: xcrun clang -fsyntax-only -Isrc -I<620 sdk inc> tests/check_abi_620.c */
#include "nvpowerd.h"
#include "nvtypes.h"
#include "ctrl/ctrl0000/ctrl0000system.h"
typedef NV0000_CTRL_SYSTEM_NVPCF_GET_POWER_MODE_INFO_PARAMS P;
#define AT(f,o) _Static_assert(offsetof(P,f)==(o),#f)
_Static_assert(sizeof(P)==NVPCF_SIZE_620,"620 size");
AT(gpuId,4);AT(ratedTgp,12);AT(nvpcfFunc,16);AT(subFunc,20);AT(ctgpOffsetmW,24);AT(dcRosReserveOverridemW,56);
AT(dcTspLongTimescaleLimitOverridemA,60);AT(dcTspShortTimescaleLimitOverridemA,64);AT(bEnableForAC,68);
AT(sysPwrIndex,92);AT(sysPwrGetInfo,96);AT(cpuTdpControlType,876);AT(inOutData,880);AT(inOutDataSize,888);
