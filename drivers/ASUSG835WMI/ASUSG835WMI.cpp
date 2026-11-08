#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <kern/task.h>

// ASUS-WMI access for the ROG Strix SCAR 18 G835LXG (\_SB.ATKD, PNP0C14, _UID "ATK").
// Calls WMNB(0, method, {dev_id, value}) like Linux asus-wmi. Unknown device IDs are
// forwarded by the firmware (WCHK -> SMI), which is how the thermal policy works.
//
// Commands via IORegistryEntrySetCFProperties {"ASUSCommand": "..."}:
//   profile=quiet|balanced|performance   any user (only this one device ID)
//   profile?                             any user: read current profile
//   dsts=ID / devs=ID.VAL (hex)          root only
// Results: property "ASUSWMI" (dictionary).
class ASUSG835WMI : public IOService {
    OSDeclareDefaultStructors(ASUSG835WMI)
public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    IOReturn setProperties(OSObject *properties) override;
private:
    IOACPIPlatformDevice *acpi = nullptr;
    IOLock *lock = nullptr;
    int current = -1;   // DSTS of 0x00120075 always reads 0x00010003; track the last set value
    bool call(UInt32 method, UInt32 devId, UInt32 value, UInt32 *result);
    void publish(const char *key, UInt64 value);
    void publishProfile();
};

OSDefineMetaClassAndStructors(ASUSG835WMI, IOService)

namespace {
constexpr UInt32 kDSTS = 0x53545344, kDEVS = 0x53564544;
constexpr UInt32 kThermalPolicy = 0x00120075;
// Firmware values as in Linux asus-wmi: 0 balanced, 1 performance, 2 quiet.
const char *profileName(UInt32 v) { return v == 0 ? "balanced" : v == 1 ? "performance" : v == 2 ? "quiet" : "unknown"; }
int parseHex(const char *p, unsigned long *out, int max)
{
    int n = 0;
    while (*p && n < max) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
        unsigned long v = 0; int digits = 0;
        for (;; ++p, ++digits) {
            char c = *p;
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0 || digits > 8) break;
            v = v * 16 + unsigned(d);
        }
        if (!digits) return -1;
        out[n++] = v;
        if (*p == '.') ++p; else break;
    }
    return *p ? -1 : n;
}
}

IOService *ASUSG835WMI::probe(IOService *provider, SInt32 *score)
{
    auto dev = OSDynamicCast(IOACPIPlatformDevice, provider);
    auto uid = dev ? OSDynamicCast(OSString, dev->getProperty("_UID")) : nullptr;
    if (!uid || !uid->isEqualTo("ATK") || dev->validateObject("WMNB") != kIOReturnSuccess)
        return nullptr;
    return IOService::probe(provider, score);
}

bool ASUSG835WMI::start(IOService *provider)
{
    acpi = OSDynamicCast(IOACPIPlatformDevice, provider);
    lock = IOLockAlloc();
    if (!acpi || !lock || !IOService::start(provider))
        return false;
    // Start in "quiet" (Leise), like Armoury Crate's silent default on this machine.
    // The NVRAM variable asus-start-profile (quiet|balanced|performance|keep) overrides it:
    //   sudo nvram asus-start-profile=performance     ("keep" leaves the firmware's profile alone)
    UInt32 start = 2;
    if (auto opts = IORegistryEntry::fromPath("/options", gIODTPlane)) {
        char want[16] = {};
        OSObject *v = opts->copyProperty("asus-start-profile");
        if (auto d = OSDynamicCast(OSData, v))
            memcpy(want, d->getBytesNoCopy(), d->getLength() < sizeof(want) - 1 ? d->getLength() : sizeof(want) - 1);
        else if (auto s = OSDynamicCast(OSString, v))
            strlcpy(want, s->getCStringNoCopy(), sizeof(want));
        OSSafeReleaseNULL(v);
        opts->release();
        if (!strcmp(want, "balanced")) start = 0;
        else if (!strcmp(want, "performance")) start = 1;
        else if (!strcmp(want, "keep")) start = 0xff;
        if (want[0]) IOLog("ASUSG835WMI: asus-start-profile=%s\n", want);
    }
    UInt32 res = 0;
    if (start != 0xff && call(kDEVS, kThermalPolicy, start, &res) && res == 1)
        current = (int)start;
    publishProfile();
    registerService();
    return true;
}

bool ASUSG835WMI::call(UInt32 method, UInt32 devId, UInt32 value, UInt32 *result)
{
    // Linux struct bios_args: six u32. WMNB creates DWord fields up to offset 0x10, so a
    // shorter buffer aborts the method with a buffer-limit error.
    UInt32 args[6] = {devId, value, 0, 0, 0, 0};
    OSObject *params[3] = {
        OSNumber::withNumber(0ULL, 32), OSNumber::withNumber(method, 32), OSData::withBytes(args, sizeof(args)),
    };
    OSObject *ret = nullptr;
    IOLockLock(lock);
    IOReturn r = (params[0] && params[1] && params[2]) ? acpi->evaluateObject("WMNB", &ret, params, 3) : kIOReturnNoMemory;
    IOLockUnlock(lock);
    for (auto p : params) if (p) p->release();
    auto n = OSDynamicCast(OSNumber, ret);
    auto d = OSDynamicCast(OSData, ret);
    bool ok = false;
    if (n) { *result = n->unsigned32BitValue(); ok = true; }
    else if (d && d->getLength() >= 1) {
        UInt32 v = 0;
        memcpy(&v, d->getBytesNoCopy(), d->getLength() < 4 ? d->getLength() : 4);
        *result = v; ok = true;
    }
    publish("LastEvalStatus", (UInt32)r);
    publish("LastEvalType", !ret ? 0 : n ? 1 : d ? 2 : 3);
    if (ret) ret->release();
    return r == kIOReturnSuccess && ok;
}

void ASUSG835WMI::publish(const char *key, UInt64 value)
{
    auto cur = OSDynamicCast(OSDictionary, getProperty("ASUSWMI"));
    auto d = cur ? OSDictionary::withDictionary(cur) : OSDictionary::withCapacity(8);
    auto n = OSNumber::withNumber(value, 64);
    if (d && n) { d->setObject(key, n); setProperty("ASUSWMI", d); }
    if (n) n->release();
    if (d) d->release();
}

void ASUSG835WMI::publishProfile()
{
    UInt32 v = 0xffffffff;
    bool ok = call(kDSTS, kThermalPolicy, 0, &v);
    publish("ThermalPolicyRaw", ok ? v : 0x1ffffffffULL);
    if (!ok || !(v & 0x00010000))
        setProperty("Profile", "unsupported");
    else
        setProperty("Profile", current >= 0 ? profileName((UInt32)current) : "unknown");
}

IOReturn ASUSG835WMI::setProperties(OSObject *properties)
{
    auto dict = OSDynamicCast(OSDictionary, properties);
    auto cmd = dict ? OSDynamicCast(OSString, dict->getObject("ASUSCommand")) : nullptr;
    if (!cmd)
        return kIOReturnUnsupported;
    const char *c = cmd->getCStringNoCopy();
    UInt32 res = 0;
    if (!strcmp(c, "profile?")) {
        publishProfile();
        return kIOReturnSuccess;
    }
    if (!strncmp(c, "profile=", 8)) {
        const char *p = c + 8;
        UInt32 v = !strcmp(p, "balanced") ? 0 : !strcmp(p, "performance") ? 1 : !strcmp(p, "quiet") ? 2 : 0xff;
        if (v == 0xff)
            return kIOReturnBadArgument;
        bool ok = call(kDEVS, kThermalPolicy, v, &res);
        publish("LastDEVSResult", ok ? res : 0x1ffffffffULL);
        if (ok && res == 1)
            current = (int)v;
        publishProfile();
        return ok && res == 1 ? kIOReturnSuccess : kIOReturnIOError;
    }
    if (IOUserClient::clientHasPrivilege(current_task(), kIOClientPrivilegeAdministrator) != kIOReturnSuccess)
        return kIOReturnNotPrivileged;
    unsigned long v[2] = {0, 0};
    if (!strncmp(c, "dsts=", 5) && parseHex(c + 5, v, 1) == 1) {
        bool ok = call(kDSTS, (UInt32)v[0], 0, &res);
        publish("LastDSTSId", v[0]);
        publish("LastDSTSResult", ok ? res : 0x1ffffffffULL);
        return ok ? kIOReturnSuccess : kIOReturnIOError;
    }
    if (!strncmp(c, "devs=", 5) && parseHex(c + 5, v, 2) == 2) {
        bool ok = call(kDEVS, (UInt32)v[0], (UInt32)v[1], &res);
        publish("LastDEVSId", v[0]);
        publish("LastDEVSResult", ok ? res : 0x1ffffffffULL);
        return ok ? kIOReturnSuccess : kIOReturnIOError;
    }
    return kIOReturnUnsupported;
}
