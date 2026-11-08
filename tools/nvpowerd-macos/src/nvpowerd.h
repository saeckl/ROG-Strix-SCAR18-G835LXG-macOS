/* SPDX-License-Identifier: MIT */
#ifndef NVPOWERD_H
#define NVPOWERD_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define NVPCF_SIZE 884u   /* canonical layout = 595.99.02 wire ABI */
#define NVPCF_SIZE_620 896u /* 620.70.06 wire ABI (nvpcfFunc, inOutData added) */
#define POLICY_SIZE 1160u
#define POLICY_INFO_SIZE 8632u
#define SET_SIZE 392u
/* GSP 615/620 control-table sizes (read from gsp_ga10x.bin 620.70.06): the PMGR
   board-object masks grew E32 -> E255 (4 -> 32 bytes) and the arrays 32 -> 64. */
#define POLICY_SIZE_620 2340u
#define POLICY_INFO_SIZE_620 20512u
#define SET_SIZE_620 808u
#define C_SET_GET 0x2080a632u /* read-only twin of C_SET (API_LOCK_READONLY) */
#define BASE_MW 95000u
#define C_IDS 0x201u
#define C_ID_INFO 0x202u
#define C_ATTACH 0x215u
#define C_DETACH 0x216u
#define C_PCIE 0x20801801u
#define C_NVPCF 0x13bu
#define C_POLICY 0x2080a630u
#define C_POLICY_INFO 0x2080a618u
#define C_SET 0x2080e634u
#define C_BIOS 0x20800808u
#define C_NOTIFY 0x20800301u
#define EVENT_COUNT 4u
#define C_UPDATE 0x20803d08u
#define NV_INVALID 0xffffffffu
/* Public NVOS layouts, independently declared for the 64-bit wire ABI. */
typedef struct { uint32_t root,parent,object,cls; uint64_t params,rights; uint32_t size,flags,status,pad; } NvAlloc;
typedef struct { uint32_t root,object,cmd,flags; uint64_t params; uint32_t size,status; } NvControl;
typedef struct { uint32_t root,parent,object,status; } NvFree;
typedef struct { uint32_t command,reply; char version[64]; } NvVersion;
_Static_assert(sizeof(NvAlloc)==48 && offsetof(NvAlloc,status)==40,"NVOS64 ABI");
_Static_assert(sizeof(NvControl)==32 && offsetof(NvControl,params)==16,"NVOS54 ABI");
_Static_assert(sizeof(NvFree)==16 && sizeof(NvVersion)==72,"NVOS00/version ABI");
typedef struct {uint32_t root,device,fd,status;} NvOsEvent;
typedef struct {uint32_t root,source,cls,index;uint64_t data;} NvEventParams;
_Static_assert(sizeof(NvOsEvent)==16 && sizeof(NvEventParams)==24,"OS/event ABI");
typedef struct {uint32_t index,hardware_min_mw,base_mw,max_mw,reference_mw;} NvPolicyInfo;
typedef struct {bool valid;uint32_t mw,age_ms,interval_ms;uint64_t seq;} NvCpuSample;
typedef struct {uint32_t base_mw,total_mw,min_mw,max_mw;bool boost;} NvPowerProfile;
enum {NV_FIXED,NV_CPU_MISSING,NV_WARMUP,NV_FEEDBACK,NV_BUDGET_EXHAUSTED};
typedef struct {bool primed;uint32_t filtered_mw,profile_cap;uint64_t seq;int64_t delta_sum;uint32_t missing,hold_mw;} NvRegulator;
#define CPU_GRACE_TICKS 10u /* 100 ms ticks a primed controller may hold its last target without a CPU sample */
typedef struct {uint32_t target_mw,filtered_mw;unsigned mode;} NvDecision;
int nv_profile(const uint8_t *,size_t,NvPowerProfile *,char *,size_t);
int nv_regulate(NvRegulator *,const NvPowerProfile *,const NvCpuSample *,uint32_t,uint32_t,bool,NvDecision *);
typedef struct {
    void *opaque;
    int (*open)(void *, bool ctl); /* zero success; one ctl and one device connection */
    int (*escape)(void *, uint32_t nr, void *data, size_t size);
    int (*close)(void *); /* zero success; cleanup errors propagate */
    int (*device_escape)(void *,uint32_t,void *,size_t);
    int (*event_fd)(void *,uint32_t *);
    int (*event_generation)(void *,uint32_t *); /* nonblocking NullMoth selector 6 */
    int (*cpu_power)(void *,NvCpuSample *); /* optional read-only package power */
} NvTransport;
typedef struct {
    NvTransport io;
    uint32_t root,device,subdevice,gpu;
    uint32_t current_mw;
    NvPolicyInfo policy;
    uint32_t events[EVENT_COUNT], event_fd, event_seq;
    bool event_os, notification_attempted[EVENT_COUNT];
    bool opened,attached,changed,initialized;
    unsigned nvpcf_abi; /* 595 or 620, chosen from the strict RM version */
    uint8_t static_conf[NVPCF_SIZE]; /* last verified NVPCF static reply, seeds sub 3 */
    bool static_valid;
    NvRegulator regulator;
    unsigned regulator_mode;
    char error[256];
} NvSession;
uint32_t nv_get32(const void *, size_t);
void nv_put32(void *, size_t, uint32_t);
int nv_policy_check(const uint8_t *, size_t, char *, size_t);
int nv_policy_info(const uint8_t *,size_t,NvPolicyInfo *,char *,size_t);
/* 620 variants; policy is converted to the canonical 595 layout first. */
int nv_policy_from620(const uint8_t in[POLICY_SIZE_620],uint8_t out[POLICY_SIZE],char *,size_t);
int nv_policy_check_tag(const uint8_t *,size_t,uint32_t tag,char *,size_t);
int nv_policy_info_620(const uint8_t *,size_t,NvPolicyInfo *,char *,size_t);
void nv_build_set_620(uint8_t out[SET_SIZE_620],uint32_t mw);
void nv_nvpcf_to620(const uint8_t in[NVPCF_SIZE],uint8_t out[NVPCF_SIZE_620]);
int nv_budget(const uint8_t *, size_t, uint32_t requested_mw, uint32_t *target, char *, size_t);
void nv_build_set(uint8_t out[SET_SIZE], uint32_t mw);
int nv_begin(NvSession *, NvTransport, const char *expected_version);
/* Diagnostics only: open/attach/alloc and identity checks, nothing else. */
int nv_begin_open_only(NvSession *, NvTransport, const char *expected_version);
int nv_read_nvpcf(NvSession *,unsigned sub,uint8_t out[NVPCF_SIZE]);
int nv_snapshot(NvSession *, uint8_t policy[POLICY_SIZE], uint8_t dynamic[NVPCF_SIZE]);
int nv_tick(NvSession *, uint32_t requested_mw, bool mains);
int nv_end(NvSession *); /* best-effort reset, free and detach; errors remain visible */
int nv_control(NvSession *, uint32_t object,uint32_t cmd,void *,size_t);
/* 620 reply -> canonical 595 layout; removed TSP limit fields read as 0. */
void nv_nvpcf_from620(const uint8_t in[NVPCF_SIZE_620],uint8_t out[NVPCF_SIZE]);
#endif
