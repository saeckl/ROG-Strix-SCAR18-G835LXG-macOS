//
// NVPCFBridge - Lilu plugin for NullMoth's NVIDIA driver on laptops.
//
// NullMoth's nv_acpi_dsm_method() (kexts/NVRM/os-xnu2.cpp) answers only the NBCI _DSM and returns
// NV_ERR_NOT_SUPPORTED for NVPCF. Without NVPCF, NVIDIA's RM never gets the platform's power tables
// (base TGP, configurable TGP, Dynamic Boost), so a laptop GPU stays near its base clock in every
// performance profile. This plugin routes nv_acpi_dsm_method: calls with acpiNvpcfDsmFunction set go to
// \_SB.NPCF._DSM, exactly as kernel-open/nvidia/nv-acpi.c does on Linux; everything else goes to NullMoth.
//
// Boot-args: -nvpcfoff (disable), -nvpcfdbg (debug log), -nvpcfbeta (unsupported macOS).
//
#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>
#include <IOKit/IOLib.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>

typedef uint32_t NV_STATUS;
static constexpr NV_STATUS NV_OK = 0x00, NV_ERR_BUFFER_TOO_SMALL = 0x02, NV_ERR_INVALID_ARGUMENT = 0x1F,
                           NV_ERR_NOT_SUPPORTED = 0x56, NV_ERR_OPERATING_SYSTEM = 0x59;
static constexpr uint16_t kMaxDsmParam = 1024;   // NV_MAX_ACPI_DSM_PARAM_SIZE

using dsm_t = NV_STATUS (*)(void *nv, uint8_t *guid, uint32_t rev, uint8_t nvpcf, uint32_t sub,
                            void *in, uint16_t inSize, uint32_t *outStatus, void *out, uint16_t *pSize);
static dsm_t orgDsm;
static IOACPIPlatformDevice *npcf;
static uint32_t calls, ok, fail;

static const char *nvrmPath[] { "/Library/Extensions/NVRM.kext/Contents/MacOS/NVRM" };
static KernelPatcher::KextInfo kextNVRM { "com.nullmoth.NVRM", nvrmPath, arrsize(nvrmPath), {true}, {},
                                          KernelPatcher::KextInfo::Unloaded };

// nv_acpi_extract_object(): integers, buffers and packages of them, flattened into one buffer.
static NV_STATUS extract(OSObject *o, uint8_t *buf, uint32_t cap, uint32_t *size)
{
    *size = 0;
    if (!o) return NV_OK;
    if (auto n = OSDynamicCast(OSNumber, o)) {
        uint64_t v = n->unsigned64BitValue(); uint32_t len = (v >> 32) ? 8 : 4;
        *size = len; if (cap < len) return NV_ERR_BUFFER_TOO_SMALL;
        memcpy(buf, &v, len); return NV_OK;
    }
    if (auto d = OSDynamicCast(OSData, o)) {
        *size = d->getLength(); if (cap < d->getLength()) return NV_ERR_BUFFER_TOO_SMALL;
        if (d->getLength()) memcpy(buf, d->getBytesNoCopy(), d->getLength());
        return NV_OK;
    }
    if (auto a = OSDynamicCast(OSArray, o)) {
        uint32_t used = 0;
        for (unsigned i = 0; i < a->getCount(); i++) {
            uint32_t len = 0; NV_STATUS st = extract(a->getObject(i), buf + used, cap - used, &len);
            if (st != NV_OK) { *size = used; return st; }
            used += len;
        }
        *size = used; return NV_OK;
    }
    return NV_ERR_NOT_SUPPORTED;
}

static IOACPIPlatformDevice *findNpcf()
{
    if (npcf) return npcf;
    auto e = IORegistryEntry::fromPath("IOACPIPlane:/_SB/NPCF");
    auto d = OSDynamicCast(IOACPIPlatformDevice, e);
    if (!d) { OSSafeReleaseNULL(e); return nullptr; }
    if (!OSCompareAndSwapPtr(nullptr, d, (void * volatile *)&npcf)) d->release();
    return npcf;
}

static NV_STATUS wrapDsm(void *nv, uint8_t *guid, uint32_t rev, uint8_t nvpcf, uint32_t sub,
                         void *in, uint16_t inSize, uint32_t *outStatus, void *out, uint16_t *pSize)
{
    if (!nvpcf)
        return orgDsm(nv, guid, rev, nvpcf, sub, in, inSize, outStatus, out, pSize);

    if (!guid || !in || inSize > kMaxDsmParam || !out || !pSize) return NV_ERR_INVALID_ARGUMENT;
    auto dev = findNpcf();
    if (!dev) { SYSLOG("nvpcf", "\\_SB.NPCF not found - NVPCF stays unsupported"); return NV_ERR_NOT_SUPPORTED; }

    OSObject *args[4] = { OSData::withBytes(guid, 16), OSNumber::withNumber(rev, 32), OSNumber::withNumber(sub, 32),
                          inSize ? OSData::withBytes(in, inSize) : OSData::withCapacity(1) };
    OSObject *res = nullptr;
    IOReturn r = (args[0] && args[1] && args[2] && args[3]) ? dev->evaluateObject("_DSM", &res, args, 4) : kIOReturnNoMemory;
    for (auto a : args) OSSafeReleaseNULL(a);

    NV_STATUS st = NV_ERR_OPERATING_SYSTEM;
    uint32_t len = 0;
    if (r == kIOReturnSuccess) {
        st = extract(res, static_cast<uint8_t *>(out), *pSize, &len);
        *pSize = static_cast<uint16_t>(len);
    }
    OSSafeReleaseNULL(res);
    uint32_t n = ++calls;
    if (st == NV_OK) ok++; else fail++;
    if (n <= 40 || (n & (n - 1)) == 0)
        SYSLOG("nvpcf", "NVPCF _DSM #%u rev 0x%x sub 0x%x in %u -> ioreturn 0x%x status 0x%x out %u bytes (ok %u fail %u)",
               n, rev, sub, inSize, r, st, len, ok, fail);
    return st;
}

static void processKext(void *, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size)
{
    if (index != kextNVRM.loadIndex) return;
    KernelPatcher::RouteRequest req("_nv_acpi_dsm_method", wrapDsm, orgDsm);
    if (patcher.routeMultiple(index, &req, 1, address, size))
        SYSLOG("nvpcf", "routed nv_acpi_dsm_method in com.nullmoth.NVRM: NVPCF goes to \\_SB.NPCF._DSM");
    else
        SYSLOG("nvpcf", "could not route nv_acpi_dsm_method (%d) - nothing changed", patcher.getError());
    patcher.clearError();
}

static void pluginStart()
{
    SYSLOG("nvpcf", "NVPCFBridge starting");
    lilu.onKextLoadForce(&kextNVRM, 1, processKext);
}

static const char *bootargOff[] { "-nvpcfoff" };
static const char *bootargDebug[] { "-nvpcfdbg" };
static const char *bootargBeta[] { "-nvpcfbeta" };

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal,
    bootargOff, arrsize(bootargOff),
    bootargDebug, arrsize(bootargDebug),
    bootargBeta, arrsize(bootargBeta),
    KernelVersion::Sequoia,
    KernelVersion::Tahoe,
    pluginStart
};
