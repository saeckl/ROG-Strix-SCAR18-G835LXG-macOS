#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/pwr_mgt/IOPM.h>
#include <kern/task.h>

// TAS2781 amplifier driver prototype for the ASUS G835LXG on \_SB.PC02.I2C0.
// Talks to the DesignWare controller directly (polled, interrupts masked) and does
// not depend on VoodooI2C, so it can be loaded at runtime.
//
// start(): read-only check (NACK at 0x3A, revision). 10 s later the automatic mode
// initializes the amplifiers and then follows the VoodooHDA speaker engines:
// engine running -> DAC 0x03 joins the stream of DAC 0x02 (as on Linux), DAC gain from
// the macOS volume, amplifiers powered up; engine stopped -> amplifiers shut down.
// Sleep shuts down, wake re-initializes. auto=0 / auto=1 switches the automatic mode.
// Commands via IORegistryEntrySetCFProperties {"TAS2781Command": "..."} (admin only):
//   status            read R00/R7F/R02/R03/R1A/R7C at both amplifiers
//   reset             software reset (R01 := 1), as Linux tasdevice_reset()
//   init              reset, DSP program 0, config 0, factory calibration, digital
//                     attenuation, shutdown state verified. Same writes as Linux
//                     (checked against the Linux I2C trace and the UEFI CALI_DATA).
//   powerup           RCA PRE_POWER_UP (R5C := D9, R02 := 00); auto-shutdown timer
//   shutdown          RCA PRE_SHUTDOWN (R02 := 02)
//   atten=NN          digital attenuation R1A (0.5 dB steps, decimal), both amps
//   autooff=SS        auto-shutdown delay in seconds (0 = off), default 120
// Results are published in the property "TAS2781Direct".

namespace {
unsigned long parseDec(const char *p)
{
    unsigned long v = 0;
    while (*p >= '0' && *p <= '9' && v < 100000)
        v = v * 10 + unsigned(*p++ - '0');
    return v;
}
// Parses "a.b.c" hex fields (optional 0x prefix). Returns the number of fields read.
int parseHexList(const char *p, unsigned long *out, int max)
{
    int n = 0;
    while (*p && n < max) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
            p += 2;
        unsigned long v = 0;
        int digits = 0;
        for (;; ++p, ++digits) {
            char ch = *p;
            int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 :
                    ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
            if (d < 0 || digits > 8)
                break;
            v = v * 16 + unsigned(d);
        }
        if (!digits)
            return -1;
        out[n++] = v;
        if (*p == '.')
            ++p;
        else
            break;
    }
    return *p ? -1 : n;
}
bool contains(const char *h, const char *n)
{
    for (; *h; ++h) {
        const char *a = h, *b = n;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (!*b) return true;
    }
    return false;
}
struct RegisterWrite { UInt32 offset; UInt8 channel, book, page, reg, length; };
#include "TAS2781TestTables.hpp"
constexpr UInt16 kAmps[] = {0x38, 0x39};
enum : int {
    IC_CON = 0x00, IC_TAR = 0x04, IC_DATA_CMD = 0x10, IC_INTR_MASK = 0x30,
    IC_RAW_INTR_STAT = 0x34, IC_CLR_INTR = 0x40, IC_ENABLE = 0x6c, IC_STATUS = 0x70,
    IC_TXFLR = 0x74, IC_RXFLR = 0x78, IC_TX_ABRT_SOURCE = 0x80, IC_ENABLE_STATUS = 0x9c,
    IC_COMP_TYPE = 0xfc,
};
constexpr UInt16 RD = 0x100, STOP = 0x200, RESTART = 0x400;
constexpr UInt32 RAW_TX_ABRT = 1 << 6, RAW_STOP_DET = 1 << 9, RAW_TX_EMPTY = 1 << 4;
}

class TAS2781DirectI2C : public IOService {
    OSDeclareDefaultStructors(TAS2781DirectI2C)
public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOReturn setProperties(OSObject *properties) override;

private:
    IOMemoryMap *memory = nullptr;
    volatile UInt8 *base = nullptr;
    IOSimpleLock *irqLock = nullptr;
    IOWorkLoop *loop = nullptr;
    IOCommandGate *gate = nullptr;
    IOTimerEventSource *timer = nullptr;
    int books[2] = {-1, -1}, pages[2] = {-1, -1};
    bool initialized = false, powered = false;
    UInt8 attenuation = 0;         // 0 dB; volume is set on the DACs
    UInt32 autoOffSeconds = 0;     // manual tests: autooff=SS
    OSDictionary *log = nullptr;
    char lastError[96] = {};

    UInt32 rd(int off) { return *reinterpret_cast<volatile UInt32 *>(base + off); }
    void wr(int off, UInt32 v) { *reinterpret_cast<volatile UInt32 *>(base + off) = v; }
    bool setEnabled(bool on);
    bool xfer(UInt16 addr, const UInt16 *words, int count, UInt8 *rx, int rxMax, int *got,
              UInt32 *abort);
    bool rawWrite(unsigned ch, UInt8 reg, const UInt8 *data, unsigned len);
    bool rawRead(unsigned ch, UInt8 reg, UInt8 *data, unsigned len);
    bool select(unsigned ch, UInt8 book, UInt8 page);
    bool write(unsigned ch, UInt8 book, UInt8 page, UInt8 reg, const UInt8 *data, unsigned len);
    bool read(unsigned ch, UInt8 book, UInt8 page, UInt8 reg, UInt8 *data, unsigned len);
    bool write1(unsigned ch, UInt8 reg, UInt8 v) { return write(ch, 0, 0, reg, &v, 1); }
    bool program(const RegisterWrite *cmds, size_t n, const UInt8 *data);
    bool calibrate(unsigned ch);
    bool fail(const char *msg);
    void note(const char *key, UInt64 value);
    void publishStatus(const char *state);

    IOReturn doStatus();
    IOReturn doReset();
    IOReturn doInit();
    IOReturn doPowerUp();
    IOReturn doShutdown();
    IOReturn doDump(UInt8 book, UInt8 page);
    IOReturn doWrite(unsigned which, UInt8 book, UInt8 page, UInt8 reg, UInt8 value);
    bool mapHDA();
    bool verb(UInt32 cmd, UInt32 *response);
    IOReturn doVerb(UInt32 cmd);
    IOReturn doCoefDump();
    IOReturn doCodecDump();
    IOMemoryMap *hdaMap = nullptr;
    volatile UInt8 *hdaBase = nullptr;
    static IOReturn gated(OSObject *owner, void *cmd, void *, void *, void *);
    static void expired(OSObject *owner, IOTimerEventSource *);

    // ---- automatic mode (0.4): follow the VoodooHDA speaker engines
    IOTimerEventSource *pollTimer = nullptr;
    bool autoMode = true, awake = true;
    IOService *engines[2] = {nullptr, nullptr};   // VoodooHDAEngine :0 (DAC 0x02) and :1 (DAC 0x03)
    int lastActive = -1;                           // engine index currently driving the amps
    SInt64 lastVolume = -1, lastMute = -1;
    unsigned initFailures = 0, idleTicks = 0;
    static void polled(OSObject *owner, IOTimerEventSource *);
    void pollTick();
    bool findEngines();
    void releaseEngines();
    bool engineRunning(unsigned idx);
    bool readControls(unsigned idx, SInt64 *volume, SInt64 *mute);
    void applyCodec(unsigned idx, SInt64 volume, SInt64 mute);
    void releaseCodec();
    bool ampsHealthy();
    void mirrorSlave(bool force);
    UInt32 slFmt = 0xffffffff, slStream = 0xffffffff, slL = 0xffffffff, slR = 0xffffffff;
    unsigned mirrorTicks = 0;
public:
    IOReturn setPowerState(unsigned long state, IOService *device) override;
private:
    static IOReturn powerGated(OSObject *owner, void *state, void *, void *, void *);
};

namespace {
IOPMPowerState kPowerStates[2] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMPowerOn | kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0},
};
constexpr UInt32 kPollMs = 250;
constexpr UInt32 kSubVolume = 0x766c6d65;   // 'vlme'
constexpr UInt32 kSubMute = 0x6d757465;     // 'mute'
constexpr UInt32 kUsageOutput = 0x6f757470; // 'outp'
constexpr UInt8 kDacNid[2] = {0x02, 0x03};
}

OSDefineMetaClassAndStructors(TAS2781DirectI2C, IOService)

// ---------------------------------------------------------------- controller

bool TAS2781DirectI2C::setEnabled(bool on)
{
    wr(IC_ENABLE, on ? 1 : 0);
    for (int i = 0; i < 400; ++i) {
        if ((rd(IC_ENABLE_STATUS) & 1) == (on ? 1u : 0u))
            return true;
        IODelay(25);
    }
    return false;
}

bool TAS2781DirectI2C::xfer(UInt16 addr, const UInt16 *words, int count, UInt8 *rx, int rxMax,
                            int *got, UInt32 *abort)
{
    *got = 0;
    *abort = 0;
    for (int i = 0; i < 400 && (rd(IC_STATUS) & (1 << 5)); ++i)
        IODelay(25);
    if (!setEnabled(false))
        return false;
    wr(IC_INTR_MASK, 0);
    wr(IC_TAR, addr & 0x7f);
    (void)rd(IC_CLR_INTR);
    if (!setEnabled(true))
        return false;

    // Feed the TX FIFO with CPU interrupts disabled: if the FIFO ran empty in the
    // middle of a burst the controller would issue a STOP and the next data byte
    // would be taken as a register address.
    IOInterruptState is = IOSimpleLockLockDisableInterrupt(irqLock);
    bool feedOk = true;
    for (int i = 0; i < count && feedOk; ++i) {
        int spins = 0;
        while (rd(IC_TXFLR) >= 60) {
            if (++spins > 20000) { feedOk = false; break; }
            IODelay(1);
        }
        if (rd(IC_RAW_INTR_STAT) & (RAW_TX_ABRT | RAW_STOP_DET)) { feedOk = false; break; }
        while (rd(IC_RXFLR) && *got < rxMax)
            rx[(*got)++] = rd(IC_DATA_CMD) & 0xff;
        wr(IC_DATA_CMD, words[i]);
    }
    IOSimpleLockUnlockEnableInterrupt(irqLock, is);

    UInt32 raw = 0;
    int waited = 0;
    for (; waited < 5000; ++waited) {   // ~50 ms
        while (rd(IC_RXFLR) && *got < rxMax)
            rx[(*got)++] = rd(IC_DATA_CMD) & 0xff;
        raw = rd(IC_RAW_INTR_STAT);
        if (raw & (RAW_TX_ABRT | RAW_STOP_DET))
            break;
        IODelay(10);
    }
    IODelay(20);
    while (rd(IC_RXFLR) && *got < rxMax)
        rx[(*got)++] = rd(IC_DATA_CMD) & 0xff;
    raw |= rd(IC_RAW_INTR_STAT);
    if (raw & RAW_TX_ABRT)
        *abort = rd(IC_TX_ABRT_SOURCE);
    (void)rd(IC_CLR_INTR);
    setEnabled(false);
    return feedOk && waited < 5000 && !(raw & RAW_TX_ABRT);
}

bool TAS2781DirectI2C::rawWrite(unsigned ch, UInt8 reg, const UInt8 *data, unsigned len)
{
    UInt16 words[1 + 128];
    if (!len || len > 128)
        return false;
    words[0] = reg;
    for (unsigned i = 0; i < len; ++i)
        words[1 + i] = data[i];
    words[len] |= STOP;
    int got;
    UInt32 abort;
    bool ok = xfer(kAmps[ch], words, len + 1, nullptr, 0, &got, &abort);
    if (!ok) {
        note(ch ? "LastAbort39" : "LastAbort38", abort);
        books[ch] = pages[ch] = -1;
    }
    return ok;
}

bool TAS2781DirectI2C::rawRead(unsigned ch, UInt8 reg, UInt8 *data, unsigned len)
{
    UInt16 words[1 + 8];
    if (!len || len > 8)
        return false;
    words[0] = reg;
    for (unsigned i = 0; i < len; ++i)
        words[1 + i] = RD;
    words[1] |= RESTART;
    words[len] |= STOP;
    int got;
    UInt32 abort;
    bool ok = xfer(kAmps[ch], words, len + 1, data, len, &got, &abort);
    if (!ok || got != (int)len) {
        note(ch ? "LastAbort39" : "LastAbort38", abort);
        books[ch] = pages[ch] = -1;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- TAS2781 access

bool TAS2781DirectI2C::select(unsigned ch, UInt8 book, UInt8 page)
{
    const UInt8 zero = 0;
    if (books[ch] != book) {
        books[ch] = pages[ch] = -1;
        if (!rawWrite(ch, 0x00, &zero, 1) || !rawWrite(ch, 0x7f, &book, 1))
            return false;
        books[ch] = book;
        pages[ch] = 0;
    }
    if (pages[ch] != page) {
        pages[ch] = -1;
        if (!rawWrite(ch, 0x00, &page, 1))
            return false;
        pages[ch] = page;
    }
    return true;
}

bool TAS2781DirectI2C::write(unsigned ch, UInt8 book, UInt8 page, UInt8 reg, const UInt8 *data,
                             unsigned len)
{
    return select(ch, book, page) && rawWrite(ch, reg, data, len);
}

bool TAS2781DirectI2C::read(unsigned ch, UInt8 book, UInt8 page, UInt8 reg, UInt8 *data,
                            unsigned len)
{
    return select(ch, book, page) && rawRead(ch, reg, data, len);
}

bool TAS2781DirectI2C::program(const RegisterWrite *cmds, size_t n, const UInt8 *data)
{
    for (size_t i = 0; i < n; ++i) {
        const auto &c = cmds[i];
        if (!write(c.channel, c.book, c.page, c.reg, data + c.offset, c.length)) {
            note("FailedWriteIndex", i);
            return false;
        }
    }
    return true;
}

// Linux tasdev_load_calibrated_data() with tas2781_cali_preproc().
bool TAS2781DirectI2C::calibrate(unsigned ch)
{
    const auto &impedance = factoryWrites[ch * 5];
    UInt8 original[4];
    if (!read(ch, impedance.book, impedance.page, impedance.reg, original, 4))
        return false;
    UInt32 nominal = (UInt32(original[0]) << 8) | original[1];
    const UInt8 *value = factoryData + impedance.offset;
    UInt32 calibrated = (UInt32(value[0]) << 8) | value[1];
    note(ch ? "CalNominal39" : "CalNominal38", nominal);
    if (!nominal || !calibrated)
        return false;
    if (nominal > calibrated) {
        UInt8 revision = 0, gain[4];
        if (!read(ch, 0, 0, 0x7c, &revision, 1) || revision == 0xff)
            return false;
        UInt8 page = revision == 0xa0 ? 0x1b : 0x18;
        UInt8 reg = revision == 0xa0 ? 0x34 : 0x1c;
        if (!read(ch, 0, page, reg, gain, 4))
            return false;
        UInt32 corrected = nominal * ((UInt32(gain[0]) << 8) | gain[1]) / calibrated;
        if (corrected > 0xffff)
            return false;
        gain[0] = corrected >> 8;
        gain[1] = corrected & 0xff;
        if (!write(ch, 0, page, reg, gain, 4))
            return false;
        note(ch ? "SineGainCorrected39" : "SineGainCorrected38", corrected);
    }
    for (unsigned i = ch * 5; i < ch * 5 + 5; ++i) {
        const auto &c = factoryWrites[i];
        UInt8 actual[4];
        const UInt8 *expected = factoryData + c.offset;
        if (!write(ch, c.book, c.page, c.reg, expected, 4) ||
            !read(ch, c.book, c.page, c.reg, actual, 4) || memcmp(expected, actual, 4) != 0)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------- bookkeeping

void TAS2781DirectI2C::note(const char *key, UInt64 value)
{
    if (!log)
        return;
    auto n = OSNumber::withNumber(value, 64);
    if (n) {
        log->setObject(key, n);
        n->release();
    }
}

bool TAS2781DirectI2C::fail(const char *msg)
{
    strlcpy(lastError, msg, sizeof(lastError));
    return false;
}

void TAS2781DirectI2C::publishStatus(const char *state)
{
    if (log) {
        auto s = OSString::withCString(state);
        if (s) { log->setObject("State", s); s->release(); }
        auto e = OSString::withCString(lastError);
        if (e) { log->setObject("LastError", e); e->release(); }
        note("Initialized", initialized);
        note("Powered", powered);
        note("Attenuation", attenuation);
        auto copy = OSDictionary::withDictionary(log);
        if (copy) {
            setProperty("TAS2781Direct", copy);
            copy->release();
        }
    }
}

// ---------------------------------------------------------------- commands

IOReturn TAS2781DirectI2C::doStatus()
{
    struct { const char *name; UInt8 reg; } regs[] = {
        {"R00", 0x00}, {"R7F", 0x7f}, {"R02", 0x02}, {"R03", 0x03}, {"R1A", 0x1a},
        {"R5C", 0x5c}, {"R7C", 0x7c},
    };
    char key[24];
    for (unsigned ch = 0; ch < 2; ++ch) {
        books[ch] = pages[ch] = -1;
        for (const auto &r : regs) {
            UInt8 v = 0xa5;
            bool ok = read(ch, 0, 0, r.reg, &v, 1);
            snprintf(key, sizeof(key), "%s_%s", ch ? "39" : "38", r.name);
            note(key, ok ? v : 0x100 | v);
        }
    }
    UInt8 dummy = 0;
    UInt16 nack[] = {0x02, RD | RESTART | STOP};
    int got;
    UInt32 abort;
    xfer(0x3a, nack, 2, &dummy, 1, &got, &abort);
    note("NACK3A_Abort", abort);
    return kIOReturnSuccess;
}

IOReturn TAS2781DirectI2C::doReset()
{
    const UInt8 one = 1;
    for (unsigned ch = 0; ch < 2; ++ch) {
        if (!write(ch, 0, 0, 0x01, &one, 1)) {
            fail("software reset write failed");
            return kIOReturnIOError;
        }
        books[ch] = pages[ch] = -1;
    }
    IOSleep(2);
    initialized = powered = false;
    for (unsigned ch = 0; ch < 2; ++ch) {
        UInt8 rev = 0;
        if (!read(ch, 0, 0, 0x7c, &rev, 1) || rev != 0xa0) {
            note(ch ? "RevAfterReset39" : "RevAfterReset38", rev);
            fail("revision after reset is not 0xA0");
            return kIOReturnIOError;
        }
    }
    return kIOReturnSuccess;
}

IOReturn TAS2781DirectI2C::doShutdown()
{
    timer->cancelTimeout();
    bool ok = true;
    for (unsigned ch = 0; ch < 2; ++ch) {
        UInt8 state = 0xff;
        ok = write1(ch, 0x02, 0x02) && ok;
        IOSleep(2);
        if (!read(ch, 0, 0, 0x02, &state, 1) || (state & 7) != 2) {
            ok = false;
            const UInt8 mute = 0xff;
            write(ch, 0, 0, 0x1a, &mute, 1);
        }
        note(ch ? "ShutdownR02_39" : "ShutdownR02_38", state);
    }
    powered = !ok;
    if (!ok)
        fail("shutdown not verified (digital volume muted)");
    return ok ? kIOReturnSuccess : kIOReturnIOError;
}

IOReturn TAS2781DirectI2C::doInit()
{
    lastError[0] = 0;
    if (powered)
        doShutdown();
    IOReturn r = doReset();
    if (r != kIOReturnSuccess)
        return r;
    if (!program(dsp_programWrites, sizeof(dsp_programWrites) / sizeof(RegisterWrite), dsp_programData)) {
        fail("DSP program write failed");
        doReset();
        return kIOReturnIOError;
    }
    if (!program(playback_configWrites, sizeof(playback_configWrites) / sizeof(RegisterWrite),
                 playback_configData)) {
        fail("playback configuration write failed");
        doReset();
        return kIOReturnIOError;
    }
    for (unsigned ch = 0; ch < 2; ++ch) {
        if (!calibrate(ch)) {
            fail(ch ? "factory calibration failed at 0x39" : "factory calibration failed at 0x38");
            doReset();
            return kIOReturnIOError;
        }
    }
    for (unsigned ch = 0; ch < 2; ++ch) {
        UInt8 g = 0, state = 0xff;
        if (!write1(ch, 0x1a, attenuation) || !read(ch, 0, 0, 0x1a, &g, 1) || g != attenuation ||
            !read(ch, 0, 0, 0x02, &state, 1) || (state & 7) != 2) {
            note(ch ? "InitR02_39" : "InitR02_38", state);
            fail("attenuation or shutdown readback failed after init");
            doReset();
            return kIOReturnIOError;
        }
    }
    initialized = true;
    return kIOReturnSuccess;
}

IOReturn TAS2781DirectI2C::doPowerUp()
{
    lastError[0] = 0;
    if (!initialized) {
        fail("powerup refused: run init first");
        return kIOReturnNotReady;
    }
    if (!program(pre_power_upWrites, sizeof(pre_power_upWrites) / sizeof(RegisterWrite), pre_power_upData)) {
        fail("power-up write failed");
        doShutdown();
        return kIOReturnIOError;
    }
    powered = true;
    IOSleep(20);
    for (unsigned ch = 0; ch < 2; ++ch) {
        UInt8 state = 0xff, g = 0;
        read(ch, 0, 0, 0x02, &state, 1);
        read(ch, 0, 0, 0x1a, &g, 1);
        note(ch ? "PowerR02_39" : "PowerR02_38", state);
        if (g != attenuation) {
            fail("attenuation changed at power-up");
            doShutdown();
            return kIOReturnIOError;
        }
    }
    if (autoOffSeconds)
        timer->setTimeoutMS(autoOffSeconds * 1000);
    return kIOReturnSuccess;
}

IOReturn TAS2781DirectI2C::doDump(UInt8 book, UInt8 page)
{
    char key[24];
    for (unsigned ch = 0; ch < 2; ++ch) {
        UInt8 bytes[128];
        for (unsigned reg = 0; reg < 128; ++reg) {
            UInt8 v = 0xee;
            if (reg == 0 || reg == 0x7f) { bytes[reg] = reg ? book : page; continue; }
            if (!read(ch, book, page, reg, &v, 1))
                v = 0xee;
            bytes[reg] = v;
        }
        snprintf(key, sizeof(key), "Dump%s_B%02X_P%02X", ch ? "39" : "38", book, page);
        auto d = OSData::withBytes(bytes, sizeof(bytes));
        if (d && log) { log->setObject(key, d); }
        if (d) d->release();
    }
    return kIOReturnSuccess;
}

IOReturn TAS2781DirectI2C::doWrite(unsigned which, UInt8 book, UInt8 page, UInt8 reg, UInt8 value)
{
    if (reg == 0x00 || reg == 0x7f)
        return kIOReturnBadArgument;   // selectors are managed by select()
    IOReturn r = kIOReturnSuccess;
    for (unsigned ch = 0; ch < 2; ++ch) {
        if (which != 2 && which != ch)
            continue;
        UInt8 back = 0xee;
        if (!write(ch, book, page, reg, &value, 1) || !read(ch, book, page, reg, &back, 1))
            r = kIOReturnIOError;
        note(ch ? "WriteBack39" : "WriteBack38", back);
    }
    return r;
}

// ---------------------------------------------------------------- HDA immediate commands

bool TAS2781DirectI2C::mapHDA()
{
    if (hdaBase)
        return true;
    auto matching = IOService::serviceMatching("IOPCIDevice");
    auto iter = matching ? IOService::getMatchingServices(matching) : nullptr;
    if (matching) matching->release();
    if (!iter)
        return false;
    while (auto obj = iter->getNextObject()) {
        auto dev = OSDynamicCast(IOPCIDevice, obj);
        if (dev && dev->configRead16(0) == 0x8086 && dev->configRead16(2) == 0x7f50) {
            hdaMap = dev->mapDeviceMemoryWithIndex(0);
            if (hdaMap)
                hdaBase = reinterpret_cast<volatile UInt8 *>(hdaMap->getVirtualAddress());
            break;
        }
    }
    iter->release();
    return hdaBase != nullptr;
}

// Intel HDA Immediate Command interface: ICOI 0x60, IRII 0x64, ICIS 0x68 (ICB bit0, IRV bit1).
bool TAS2781DirectI2C::verb(UInt32 cmd, UInt32 *response)
{
    if (!mapHDA())
        return false;
    auto icis = reinterpret_cast<volatile UInt16 *>(hdaBase + 0x68);
    auto icoi = reinterpret_cast<volatile UInt32 *>(hdaBase + 0x60);
    auto irii = reinterpret_cast<volatile UInt32 *>(hdaBase + 0x64);
    for (int i = 0; i < 1000 && (*icis & 1); ++i)
        IODelay(1);
    if (*icis & 1)
        return false;
    *icis = 2;                 // clear IRV
    *icoi = cmd;
    *icis = 1;                 // ICB: send
    for (int i = 0; i < 1000; ++i) {
        if ((*icis & 3) == 2) {
            *response = *irii;
            *icis = 2;
            return true;
        }
        IODelay(1);
    }
    return false;
}

IOReturn TAS2781DirectI2C::doVerb(UInt32 cmd)
{
    UInt32 resp = 0xffffffff;
    bool ok = verb(cmd, &resp);
    note("VerbCommand", cmd);
    note("VerbResponse", ok ? resp : 0x1ffffffffULL);
    return ok ? kIOReturnSuccess : kIOReturnTimeout;
}

IOReturn TAS2781DirectI2C::doCoefDump()
{
    // ALC285 vendor widget 0x20: set coefficient index (verb 0x5), get coefficient (verb 0xC).
    UInt32 saved = 0;
    if (!verb((0x20u << 20) | (0xDu << 16), &saved))        // GET_COEF_INDEX
        return kIOReturnTimeout;
    UInt8 out[0x90 * 2];
    for (unsigned idx = 0; idx < 0x90; ++idx) {
        UInt32 v = 0xffff;
        if (!verb((0x20u << 20) | (0x5u << 16) | idx, &v) ||
            !verb((0x20u << 20) | (0xCu << 16), &v))
            v = 0xdead;
        out[idx * 2] = (v >> 8) & 0xff;
        out[idx * 2 + 1] = v & 0xff;
    }
    UInt32 dummy;
    verb((0x20u << 20) | (0x5u << 16) | (saved & 0xffff), &dummy);   // restore index
    note("CoefIndexSaved", saved);
    auto d = OSData::withBytes(out, sizeof(out));
    if (d && log) log->setObject("CoefDump", d);
    if (d) d->release();
    return kIOReturnSuccess;
}

IOReturn TAS2781DirectI2C::doCodecDump()
{
    // Live state of the speaker path: DAC 0x02/0x03, pins 0x14/0x17/0x21.
    struct { UInt8 nid; UInt16 verb; UInt16 payload; const char *name; } q[] = {
        {0x02, 0xA00, 0, "02_Format"}, {0x02, 0xF06, 0, "02_StreamChan"},
        {0x02, 0xB00, 0xA000, "02_AmpOutL"}, {0x02, 0xB00, 0x8000, "02_AmpOutR"},
        {0x03, 0xA00, 0, "03_Format"}, {0x03, 0xF06, 0, "03_StreamChan"},
        {0x03, 0xB00, 0xA000, "03_AmpOutL"}, {0x03, 0xB00, 0x8000, "03_AmpOutR"},
        {0x14, 0xF07, 0, "14_PinCtl"}, {0x14, 0xF0C, 0, "14_EAPD"}, {0x14, 0xF01, 0, "14_ConnSel"},
        {0x14, 0xB00, 0xA000, "14_AmpOutL"}, {0x14, 0xB00, 0x8000, "14_AmpOutR"},
        {0x14, 0xF05, 0, "14_Power"},
        {0x17, 0xF07, 0, "17_PinCtl"}, {0x17, 0xF0C, 0, "17_EAPD"}, {0x17, 0xF01, 0, "17_ConnSel"},
        {0x17, 0xB00, 0xA000, "17_AmpOutL"}, {0x17, 0xB00, 0x8000, "17_AmpOutR"},
        {0x17, 0xF05, 0, "17_Power"},
        {0x21, 0xF07, 0, "21_PinCtl"}, {0x01, 0xF05, 0, "01_AFGPower"},
        {0x01, 0xF15, 0, "01_GPIOData"}, {0x01, 0xF16, 0, "01_GPIOMask"}, {0x01, 0xF17, 0, "01_GPIODir"},
    };
    auto d = OSDictionary::withCapacity(32);
    if (!d)
        return kIOReturnNoMemory;
    for (const auto &e : q) {
        UInt32 cmd = (UInt32(e.nid) << 20);
        if (e.verb == 0xB00)
            cmd |= (0xBu << 16) | e.payload;          // 4-bit GET_AMP verb with payload
        else
            cmd |= (UInt32(e.verb) << 8) | e.payload;
        UInt32 v = 0xffffffff;
        bool ok = verb(cmd, &v);
        auto n = OSNumber::withNumber(ok ? v : 0x1ffffffffULL, 64);
        if (n) { d->setObject(e.name, n); n->release(); }
    }
    if (log) log->setObject("Codec", d);
    d->release();
    return kIOReturnSuccess;
}

// ---------------------------------------------------------------- automatic mode

void TAS2781DirectI2C::releaseEngines()
{
    for (auto &e : engines)
        if (e) { e->release(); e = nullptr; }
}

bool TAS2781DirectI2C::findEngines()
{
    // engines[0]: VoodooHDA speaker engine (pin 0x14 / DAC 0x02), engines[1]: headphone engine.
    // Pin 0x17 is hidden from VoodooHDA (NodesToPatch) and driven here as a slave of DAC 0x02.
    if (engines[0] && !engines[0]->isInactive() && (!engines[1] || !engines[1]->isInactive()))
        return true;
    releaseEngines();
    auto matching = IOService::serviceMatching("VoodooHDAEngine");
    auto iter = matching ? IOService::getMatchingServices(matching) : nullptr;
    if (matching) matching->release();
    if (!iter)
        return false;
    while (auto obj = iter->getNextObject()) {
        auto svc = OSDynamicCast(IOService, obj);
        auto desc = svc ? OSDynamicCast(OSString, svc->getProperty("IOAudioEngineDescription")) : nullptr;
        if (!desc)
            continue;
        const char *d = desc->getCStringNoCopy();
        if (contains(d, "Speaker") && !engines[0]) { svc->retain(); engines[0] = svc; }
        else if (contains(d, "Head") && !engines[1]) { svc->retain(); engines[1] = svc; }
    }
    iter->release();
    return engines[0] != nullptr;
}

bool TAS2781DirectI2C::engineRunning(unsigned idx)
{
    auto n = engines[idx] ? OSDynamicCast(OSNumber, engines[idx]->getProperty("IOAudioEngineState")) : nullptr;
    return n && n->unsigned32BitValue() == 1;
}

// Output volume (0..100 as published by VoodooHDA) and mute of an engine.
bool TAS2781DirectI2C::readControls(unsigned idx, SInt64 *volume, SInt64 *mute)
{
    *volume = -1;
    *mute = 0;
    if (!engines[idx])
        return false;
    auto iter = engines[idx]->getChildIterator(gIOServicePlane);
    if (!iter)
        return false;
    while (auto obj = iter->getNextObject()) {
        auto entry = OSDynamicCast(IORegistryEntry, obj);
        if (!entry)
            continue;
        auto sub = OSDynamicCast(OSNumber, entry->getProperty("IOAudioControlSubType"));
        auto usage = OSDynamicCast(OSNumber, entry->getProperty("IOAudioControlUsage"));
        auto value = OSDynamicCast(OSNumber, entry->getProperty("IOAudioControlValue"));
        if (!sub || !usage || !value || usage->unsigned32BitValue() != kUsageOutput)
            continue;
        if (sub->unsigned32BitValue() == kSubVolume && *volume < 0)
            *volume = (SInt32)value->unsigned32BitValue();
        else if (sub->unsigned32BitValue() == kSubMute)
            *mute = value->unsigned32BitValue() ? 1 : 0;
    }
    iter->release();
    return *volume >= 0;
}

// Linux plays one stream on both DAC 0x02 and DAC 0x03; both speaker pins are needed.
// Copy the running engine's stream/channel to the other DAC and set both DAC gains
// from the macOS volume (1..100 -> -48 dB .. 0 dB in 0.75 dB steps, 0 or mute -> muted).
void TAS2781DirectI2C::applyCodec(unsigned idx, SInt64 volume, SInt64 mute)
{
    UInt32 stream = 0;
    if (!verb((UInt32(kDacNid[idx]) << 20) | (0xF06u << 8), &stream))
        return;
    UInt32 dummy;
    verb((UInt32(kDacNid[idx ^ 1]) << 20) | (0x706u << 8) | (stream & 0xff), &dummy);
    UInt32 gain;
    if (mute || volume <= 0)
        gain = 0x80;                                   // mute bit
    else
        gain = 0x17 + (UInt32)((0x57 - 0x17) * (volume > 100 ? 100 : volume) / 100);
    for (UInt8 nid : kDacNid)
        verb((UInt32(nid) << 20) | (0x3u << 16) | 0xB000 | gain, &dummy);
    note("AutoStream", stream & 0xff);
    note("AutoDacGain", gain);
}

// DAC 0x03 / pin 0x17 follow DAC 0x02: same stream, format and left/right gain (with mute),
// so balance and volume behave exactly as VoodooHDA sets them on DAC 0x02.
void TAS2781DirectI2C::mirrorSlave(bool force)
{
    UInt32 fmt = 0, strm = 0, gl = 0, gr = 0, dummy;
    if (!verb((0x02u << 20) | (0xA00u << 8), &fmt) || !verb((0x02u << 20) | (0xF06u << 8), &strm) ||
        !verb((0x02u << 20) | (0xBu << 16) | 0xA000, &gl) || !verb((0x02u << 20) | (0xBu << 16) | 0x8000, &gr))
        return;
    if (!force && fmt == slFmt && strm == slStream && gl == slL && gr == slR)
        return;
    verb((0x03u << 20) | (0x705u << 8), &dummy);                         // DAC 0x03 power D0
    verb((0x03u << 20) | (0x2u << 16) | (fmt & 0xffff), &dummy);         // converter format
    verb((0x03u << 20) | (0x706u << 8) | (strm & 0xff), &dummy);         // stream/channel
    verb((0x03u << 20) | (0x3u << 16) | 0xA000 | (gl & 0xff), &dummy);   // left gain + mute
    verb((0x03u << 20) | (0x3u << 16) | 0x9000 | (gr & 0xff), &dummy);   // right gain + mute
    verb((0x17u << 20) | (0x705u << 8), &dummy);                         // pin 0x17 power D0
    verb((0x17u << 20) | (0x707u << 8) | 0x40, &dummy);                  // pin out enable
    verb((0x17u << 20) | (0x701u << 8) | 0x01, &dummy);                  // connection: DAC 0x03
    verb((0x17u << 20) | (0x3u << 16) | 0xB000, &dummy);                 // pin amp unmuted
    slFmt = fmt; slStream = strm; slL = gl; slR = gr;
    note("SlaveFormat", fmt);
    note("SlaveStream", strm & 0xff);
    note("SlaveGainL", gl);
    note("SlaveGainR", gr);
}

void TAS2781DirectI2C::releaseCodec()
{
    UInt32 dummy;
    if (!engineRunning(1))   // the headphone engine may own DAC 0x03
        verb((0x03u << 20) | (0x706u << 8), &dummy);
    slFmt = slStream = slL = slR = 0xffffffff;
}

// After sleep or a power loss the amplifiers read back 0x00 until a software reset.
bool TAS2781DirectI2C::ampsHealthy()
{
    for (unsigned ch = 0; ch < 2; ++ch) {
        UInt8 rev = 0, gain = 0;
        books[ch] = pages[ch] = -1;
        if (!read(ch, 0, 0, 0x7c, &rev, 1) || rev != 0xa0 ||
            !read(ch, 0, 0, 0x03, &gain, 1) || gain != 0xe8)
            return false;
    }
    return true;
}

void TAS2781DirectI2C::polled(OSObject *owner, IOTimerEventSource *)
{
    static_cast<TAS2781DirectI2C *>(owner)->pollTick();
}

void TAS2781DirectI2C::pollTick()
{
    if (!autoMode || !awake)
        return;
    if (!initialized) {
        if (initFailures < 5 && doInit() == kIOReturnSuccess) {
            initFailures = 0;
            note("AutoInits", 1);
            publishStatus("auto: initialized");
        } else if (initFailures < 5) {
            ++initFailures;
            publishStatus("auto: init failed, retrying");
            pollTimer->setTimeoutMS(2000);
            return;
        } else {
            publishStatus("auto: init failed 5 times, amplifiers stay off");
            return;
        }
    }
    if (!findEngines()) {
        pollTimer->setTimeoutMS(1000);
        return;
    }
    bool headphones = engines[1] && engineRunning(1);
    int active = engineRunning(0) && !headphones ? 0 : -1;
    if (active >= 0) {
        // Mirror on start, then check every 500 ms (VoodooHDA changes DAC 0x02 on volume/balance).
        if (lastActive < 0 || ++mirrorTicks >= 2) {
            mirrorSlave(lastActive < 0);
            mirrorTicks = 0;
        }
        if (!powered) {
            if (!ampsHealthy()) {           // lost state (wake, power loss): full init again
                initialized = false;
                if (doInit() != kIOReturnSuccess) {
                    pollTimer->setTimeoutMS(1000);
                    return;
                }
            }
            if (doPowerUp() == kIOReturnSuccess)
                publishStatus("auto: playing");
            else
                initialized = false;            // re-init on the next tick
        }
        lastActive = active;
        idleTicks = 0;
    } else if (powered || lastActive >= 0) {
        // Small hysteresis so short gaps between sounds do not toggle the amplifiers.
        if (++idleTicks >= 4) {
            doShutdown();
            releaseCodec();
            lastActive = -1;
            lastVolume = lastMute = -1;
            idleTicks = 0;
            publishStatus("auto: idle");
        }
    }
    pollTimer->setTimeoutMS(kPollMs);
}

IOReturn TAS2781DirectI2C::powerGated(OSObject *owner, void *state, void *, void *, void *)
{
    auto self = static_cast<TAS2781DirectI2C *>(owner);
    if (!state) {
        self->awake = false;
        self->pollTimer->cancelTimeout();
        if (self->powered)
            self->doShutdown();
        self->initialized = false;            // amplifiers lose their state in sleep
        self->lastActive = -1;
        self->lastVolume = self->lastMute = -1;
        self->publishStatus("sleep");
    } else {
        if (self->awake)
            return kIOReturnSuccess;          // initial power-on call: keep the boot delay
        self->awake = true;
        self->initFailures = 0;
        self->initialized = false;
        // Give VoodooI2C/IOPCIFamily time to restore the controller after wake.
        self->pollTimer->setTimeoutMS(3000);
    }
    return kIOReturnSuccess;
}

IOReturn TAS2781DirectI2C::setPowerState(unsigned long state, IOService *)
{
    if (gate)
        gate->runAction(powerGated, state ? this : nullptr);
    return IOPMAckImplied;
}

IOReturn TAS2781DirectI2C::gated(OSObject *owner, void *cmd, void *, void *, void *)
{
    auto self = static_cast<TAS2781DirectI2C *>(owner);
    auto command = static_cast<OSString *>(cmd);
    const char *c = command->getCStringNoCopy();
    IOReturn r = kIOReturnUnsupported;
    if (!strcmp(c, "status"))
        r = self->doStatus();
    else if (!strcmp(c, "reset"))
        r = self->doReset();
    else if (!strcmp(c, "init"))
        r = self->doInit();
    else if (!strcmp(c, "powerup"))
        r = self->doPowerUp();
    else if (!strcmp(c, "shutdown"))
        r = self->doShutdown();
    else if (!strncmp(c, "dump=", 5)) {
        // dump=BB.PP  -> 128 bytes of book BB page PP from both amplifiers (hex values)
        unsigned long v[2] = {0, 0};
        if (parseHexList(c + 5, v, 2) == 2)
            r = self->doDump((UInt8)v[0], (UInt8)v[1]);
    } else if (!strncmp(c, "wr=", 3)) {
        // wr=CH.BB.PP.RR.VV  (CH 0 = 0x38, 1 = 0x39, 2 = both; hex) single byte write + readback
        unsigned long v[5];
        if (parseHexList(c + 3, v, 5) == 5 && v[0] <= 2)
            r = self->doWrite((unsigned)v[0], (UInt8)v[1], (UInt8)v[2], (UInt8)v[3], (UInt8)v[4]);
    } else if (!strncmp(c, "verb=", 5)) {
        unsigned long v[1];
        if (parseHexList(c + 5, v, 1) == 1)
            r = self->doVerb((UInt32)v[0]);
    } else if (!strcmp(c, "auto=0")) {
        self->autoMode = false;
        self->pollTimer->cancelTimeout();
        r = kIOReturnSuccess;
    } else if (!strcmp(c, "auto=1")) {
        self->autoMode = true;
        self->pollTimer->setTimeoutMS(10);
        r = kIOReturnSuccess;
    } else if (!strcmp(c, "coefdump"))
        r = self->doCoefDump();
    else if (!strcmp(c, "codecdump"))
        r = self->doCodecDump();
    else if (!strncmp(c, "atten=", 6)) {
        unsigned long v = parseDec(c + 6);
        if (v <= 0xfe) {
            self->attenuation = (UInt8)v;
            r = kIOReturnSuccess;
            if (self->initialized)
                for (unsigned ch = 0; ch < 2; ++ch)
                    if (!self->write1(ch, 0x1a, self->attenuation))
                        r = kIOReturnIOError;
        }
    } else if (!strncmp(c, "autooff=", 8)) {
        self->autoOffSeconds = (UInt32)parseDec(c + 8);
        r = kIOReturnSuccess;
        if (self->powered) {
            self->timer->cancelTimeout();
            if (self->autoOffSeconds)
                self->timer->setTimeoutMS(self->autoOffSeconds * 1000);
        }
    }
    self->note("LastResult", (UInt32)r);
    auto s = OSString::withCString(c);
    if (s && self->log) { self->log->setObject("LastCommand", s); }
    if (s) s->release();
    self->publishStatus(self->powered ? "powered" : self->initialized ? "initialized, shut down" : "idle");
    return r;
}

void TAS2781DirectI2C::expired(OSObject *owner, IOTimerEventSource *)
{
    auto self = static_cast<TAS2781DirectI2C *>(owner);
    self->doShutdown();
    self->publishStatus("auto-shutdown");
}

IOReturn TAS2781DirectI2C::setProperties(OSObject *properties)
{
    auto dict = OSDynamicCast(OSDictionary, properties);
    auto command = dict ? OSDynamicCast(OSString, dict->getObject("TAS2781Command")) : nullptr;
    if (!command)
        return kIOReturnUnsupported;
    if (IOUserClient::clientHasPrivilege(current_task(), kIOClientPrivilegeAdministrator) != kIOReturnSuccess)
        return kIOReturnNotPrivileged;
    if (!gate)
        return kIOReturnNotReady;
    return gate->runAction(gated, command);
}

// ---------------------------------------------------------------- lifecycle

bool TAS2781DirectI2C::start(IOService *provider)
{
    auto pci = OSDynamicCast(IOPCIDevice, provider);
    auto path = pci ? OSDynamicCast(OSString, pci->getProperty("acpi-path")) : nullptr;
    if (!path || !path->isEqualTo("IOACPIPlane:/_SB/PC02@0/I2C0@150000") ||
        pci->configRead16(0) != 0x8086 || pci->configRead16(2) != 0x7f4c ||
        !(pci->configRead16(0x04) & 2))
        return false;
    memory = pci->mapDeviceMemoryWithIndex(0);
    if (!memory)
        return false;
    base = reinterpret_cast<volatile UInt8 *>(memory->getVirtualAddress());
    if (!base || memory->getLength() < 0x100 || rd(IC_COMP_TYPE) != 0x44570140)
        goto fail;
    irqLock = IOSimpleLockAlloc();
    log = OSDictionary::withCapacity(48);
    loop = IOWorkLoop::workLoop();
    gate = loop ? IOCommandGate::commandGate(this) : nullptr;
    timer = loop ? IOTimerEventSource::timerEventSource(this, expired) : nullptr;
    pollTimer = loop ? IOTimerEventSource::timerEventSource(this, polled) : nullptr;
    if (!irqLock || !log || !gate || !timer || !pollTimer ||
        loop->addEventSource(gate) != kIOReturnSuccess ||
        loop->addEventSource(timer) != kIOReturnSuccess ||
        loop->addEventSource(pollTimer) != kIOReturnSuccess)
        goto fail;
    if (!IOService::start(provider))
        goto fail;
    gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &TAS2781DirectI2C::doStatus));
    PMinit();
    provider->joinPMtree(this);
    registerPowerDriver(this, kPowerStates, 2);
    publishStatus("auto: waiting for boot to settle");
    registerService();
    // Start after the boot-time scan of the old diagnostic kext and VoodooHDA setup.
    pollTimer->setTimeoutMS(10000);
    return true;

fail:
    if (pollTimer) { if (loop) loop->removeEventSource(pollTimer); pollTimer->release(); pollTimer = nullptr; }
    if (timer) { if (loop) loop->removeEventSource(timer); timer->release(); timer = nullptr; }
    if (gate) { if (loop) loop->removeEventSource(gate); gate->release(); gate = nullptr; }
    if (loop) { loop->release(); loop = nullptr; }
    if (log) { log->release(); log = nullptr; }
    if (irqLock) { IOSimpleLockFree(irqLock); irqLock = nullptr; }
    memory->release();
    memory = nullptr;
    base = nullptr;
    return false;
}

void TAS2781DirectI2C::stop(IOService *provider)
{
    PMstop();
    if (gate) {
        autoMode = false;
        pollTimer->cancelTimeout();
        if (powered)
            gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &TAS2781DirectI2C::doShutdown));
        gate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &TAS2781DirectI2C::releaseCodec));
        timer->cancelTimeout();
        loop->removeEventSource(pollTimer);
        pollTimer->release();
        pollTimer = nullptr;
        loop->removeEventSource(timer);
        loop->removeEventSource(gate);
        timer->release();
        gate->release();
        loop->release();
        timer = nullptr;
        gate = nullptr;
        loop = nullptr;
    }
    releaseEngines();
    if (log) { log->release(); log = nullptr; }
    if (irqLock) { IOSimpleLockFree(irqLock); irqLock = nullptr; }
    if (memory) { memory->release(); memory = nullptr; }
    if (hdaMap) { hdaMap->release(); hdaMap = nullptr; }
    hdaBase = nullptr;
    base = nullptr;
    IOService::stop(provider);
}
