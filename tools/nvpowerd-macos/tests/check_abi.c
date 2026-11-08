/* SPDX-License-Identifier: MIT
 * Optional compile-time check against NVIDIA's public 595.99.02 headers. */
#include "nvpowerd.h"
#include "nvos.h"
#include "nv-ioctl.h"
#include "class/cl0080.h"
#include "class/cl2080.h"
#include "class/cl0005.h"
#include "class/cl2080_notification.h"
#include "ctrl/ctrl2080/ctrl2080event.h"
#include "ctrl/ctrl0000/ctrl0000system.h"
#include "ctrl/ctrl0000/ctrl0000gpu.h"
#define SAME(a,fa,b,fb) _Static_assert(offsetof(a,fa)==offsetof(b,fb),"wire field offset")
_Static_assert(sizeof(NvAlloc)==sizeof(NVOS64_PARAMETERS),"NVOS64 size");
SAME(NvAlloc,root,NVOS64_PARAMETERS,hRoot);
SAME(NvAlloc,parent,NVOS64_PARAMETERS,hObjectParent);
SAME(NvAlloc,object,NVOS64_PARAMETERS,hObjectNew);
SAME(NvAlloc,cls,NVOS64_PARAMETERS,hClass);
SAME(NvAlloc,params,NVOS64_PARAMETERS,pAllocParms);
SAME(NvAlloc,rights,NVOS64_PARAMETERS,pRightsRequested);
SAME(NvAlloc,size,NVOS64_PARAMETERS,paramsSize);
SAME(NvAlloc,flags,NVOS64_PARAMETERS,flags);
SAME(NvAlloc,status,NVOS64_PARAMETERS,status);
_Static_assert(sizeof(NvControl)==sizeof(NVOS54_PARAMETERS),"NVOS54 size");
SAME(NvControl,params,NVOS54_PARAMETERS,params);
SAME(NvControl,status,NVOS54_PARAMETERS,status);
_Static_assert(sizeof(NvFree)==sizeof(NVOS00_PARAMETERS),"NVOS00 size");
_Static_assert(sizeof(NV0080_ALLOC_PARAMETERS)==56,"device params");
_Static_assert(sizeof(NV2080_ALLOC_PARAMETERS)==4,"subdevice params");
_Static_assert(sizeof(NV0000_CTRL_SYSTEM_NVPCF_GET_POWER_MODE_INFO_PARAMS)==NVPCF_SIZE,"NVPCF size");
_Static_assert(offsetof(NV0000_CTRL_SYSTEM_NVPCF_GET_POWER_MODE_INFO_PARAMS,ctgpOffsetmW)==20,"NVPCF ctgp");
_Static_assert(offsetof(NV0000_CTRL_SYSTEM_NVPCF_GET_POWER_MODE_INFO_PARAMS,maxOutputOffsetmW)==28,"NVPCF boost");
_Static_assert(offsetof(NV0000_CTRL_SYSTEM_NVPCF_GET_POWER_MODE_INFO_PARAMS,bEnableForAC)==72,"NVPCF AC");
_Static_assert(offsetof(NV0000_CTRL_SYSTEM_NVPCF_GET_POWER_MODE_INFO_PARAMS,targetTppOffsetmW)==24,"NVPCF total budget offset");
_Static_assert(offsetof(NV0000_CTRL_SYSTEM_NVPCF_GET_POWER_MODE_INFO_PARAMS,minOutputOffsetmW)==32,"NVPCF minimum offset");
_Static_assert(offsetof(NV0000_CTRL_SYSTEM_NVPCF_GET_POWER_MODE_INFO_PARAMS,filterType)==80,"NVPCF filter");
_Static_assert(offsetof(NV0000_CTRL_SYSTEM_NVPCF_GET_POWER_MODE_INFO_PARAMS,filterParam)==81,"NVPCF weight");
_Static_assert(offsetof(NV0000_CTRL_SYSTEM_NVPCF_GET_POWER_MODE_INFO_PARAMS,bIsBoostController)==84,"NVPCF boost controller");
_Static_assert(offsetof(NV0000_CTRL_SYSTEM_NVPCF_GET_POWER_MODE_INFO_PARAMS,incRatio)==86,"NVPCF increase gain");
_Static_assert(offsetof(NV0000_CTRL_SYSTEM_NVPCF_GET_POWER_MODE_INFO_PARAMS,decRatio)==88,"NVPCF decrease gain");
_Static_assert(NV0000_CTRL_CMD_SYSTEM_NVPCF_GET_POWER_MODE_INFO==C_NVPCF,"NVPCF command");
_Static_assert(NV0000_CTRL_GPU_DETACH_ALL_ATTACHED_IDS==0xffff,"detach all");
_Static_assert(sizeof(NvEventParams)==sizeof(NV0005_ALLOC_PARAMETERS),"event params size");
SAME(NvEventParams,root,NV0005_ALLOC_PARAMETERS,hParentClient);
SAME(NvEventParams,source,NV0005_ALLOC_PARAMETERS,hSrcResource);
SAME(NvEventParams,cls,NV0005_ALLOC_PARAMETERS,hClass);
SAME(NvEventParams,index,NV0005_ALLOC_PARAMETERS,notifyIndex);
SAME(NvEventParams,data,NV0005_ALLOC_PARAMETERS,data);
_Static_assert(sizeof(NvOsEvent)==sizeof(nv_ioctl_alloc_os_event_t),"OS event alloc size");
_Static_assert(sizeof(NvOsEvent)==sizeof(nv_ioctl_free_os_event_t),"OS event free size");
SAME(NvOsEvent,root,nv_ioctl_alloc_os_event_t,hClient);
SAME(NvOsEvent,device,nv_ioctl_alloc_os_event_t,hDevice);
SAME(NvOsEvent,fd,nv_ioctl_alloc_os_event_t,fd);
SAME(NvOsEvent,status,nv_ioctl_alloc_os_event_t,Status);
_Static_assert(sizeof(NV2080_CTRL_EVENT_SET_NOTIFICATION_PARAMS)==20,"notify params size");
_Static_assert(NV2080_NOTIFIERS_POWER_EVENT==118 && NV2080_NOTIFIERS_AUX_POWER_EVENT==181 && NV2080_NOTIFIERS_NVPCF_EVENTS==177 && NV2080_NOTIFIERS_PSTATE_CHANGE==33,"notifier IDs");
int main(void){return 0;}
