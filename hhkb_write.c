// Writes a keymap back to the HHKB over the vendor HID interface.
//
// WRITE_KEYMAP is the one command here with lasting effect, so every write is
// bracketed by a read: the current map is fetched, modified, written, and read
// back again. A write the firmware silently drops looks identical to a
// successful one at the status-byte level, so the read-back is what decides.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPORT_SIZE 64
#define KEYMAP_SIZE 128
#define HHKB_VID 0x04FE
#define USAGE_PAGE_VENDOR 0xFF00

#define NOTIFY_APPLICATION_STATE 1
#define GET_KEYBOARD_MODE 6
#define CONFIRM_KEYMAP 4
#define WRITE_KEYMAP 134
#define GET_KEYMAP 135

static uint8_t g_in[REPORT_SIZE];
static volatile int g_got;

static void input_cb(void *ctx, IOReturn res, void *sender, IOHIDReportType type, uint32_t id,
                     uint8_t *report, CFIndex len) {
    (void)ctx;
    (void)res;
    (void)sender;
    (void)type;
    (void)id;
    memset(g_in, 0, sizeof(g_in));
    memcpy(g_in, report, len < REPORT_SIZE ? len : REPORT_SIZE);
    g_got = 1;
    CFRunLoopStop(CFRunLoopGetCurrent());
}

static int32_t int_prop(IOHIDDeviceRef d, CFStringRef key) {
    int32_t v = 0;
    CFTypeRef r = IOHIDDeviceGetProperty(d, key);
    if (r && CFGetTypeID(r) == CFNumberGetTypeID())
        CFNumberGetValue((CFNumberRef)r, kCFNumberSInt32Type, &v);
    return v;
}

static IOHIDDeviceRef find_device(void) {
    IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDManagerSetDeviceMatching(mgr, NULL);
    IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);

    CFSetRef set = IOHIDManagerCopyDevices(mgr);
    if (!set)
        return NULL;

    CFIndex n = CFSetGetCount(set);
    IOHIDDeviceRef *devs = calloc(n, sizeof(IOHIDDeviceRef));
    CFSetGetValues(set, (const void **)devs);

    IOHIDDeviceRef found = NULL;
    for (CFIndex i = 0; i < n; i++) {
        if (int_prop(devs[i], CFSTR(kIOHIDVendorIDKey)) != HHKB_VID)
            continue;
        if (int_prop(devs[i], CFSTR(kIOHIDPrimaryUsagePageKey)) != USAGE_PAGE_VENDOR)
            continue;
        found = devs[i];
        CFRetain(found);
        break;
    }
    free(devs);
    CFRelease(set);
    return found;
}

static int wait_report(double timeout) {
    g_got = 0;
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, timeout, true);
    return g_got ? 0 : -1;
}

static int send(IOHIDDeviceRef dev, const uint8_t *out) {
    IOReturn r = IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, 0, out, REPORT_SIZE);
    if (r != kIOReturnSuccess) {
        fprintf(stderr, "SetReport failed: 0x%08X\n", r);
        return -1;
    }
    return 0;
}

static void build(uint8_t *buf, uint8_t cmd) {
    memset(buf, 0, REPORT_SIZE);
    buf[0] = 0xAA;
    buf[1] = 0xAA;
    buf[2] = cmd;
}

static void dump_resp(const char *label) {
    printf("    %s <- ", label);
    for (int i = 0; i < 8; i++)
        printf("%02X ", g_in[i]);
    printf("\n");
}

static int txn(IOHIDDeviceRef dev, const uint8_t *out, uint8_t cmd, const char *label) {
    if (send(dev, out) < 0)
        return -1;
    if (wait_report(2.0) < 0) {
        printf("    %s <- (timeout)\n", label);
        return -1;
    }
    dump_resp(label);
    if (g_in[0] != 0x55 || g_in[1] != 0x55 || g_in[2] != cmd) {
        printf("    unexpected header\n");
        return -1;
    }
    if (g_in[3] != 0) {
        printf("    status 0x%02X (rejected)\n", g_in[3]);
        if (g_in[3] == 0x01)
            printf("    status 0x01 means the board is read-only, which is what\n"
                   "    DUMP_FIRMWARE leaves behind. Reads in that state return\n"
                   "    stale data too, so do not trust a read-back until the\n"
                   "    keyboard has been unplugged and replugged.\n");
        return -1;
    }
    return 0;
}

static int get_keymap(IOHIDDeviceRef dev, uint8_t mode, uint8_t fn, uint8_t *layout) {
    static const uint8_t lens[3] = {58, 58, 12};
    static const uint8_t offs[3] = {0, 58, 116};

    uint8_t out[REPORT_SIZE];
    build(out, GET_KEYMAP);
    out[4] = 2;
    out[5] = mode;
    out[6] = fn;

    memset(layout, 0, KEYMAP_SIZE);
    if (send(dev, out) < 0)
        return -1;

    for (int i = 0; i < 3; i++) {
        if (wait_report(2.0) < 0)
            return -1;
        uint8_t marker = (uint8_t)(((i + 1) << 6) | (i + 1));
        if (g_in[0] != 0x55 || g_in[2] != GET_KEYMAP || g_in[3] != 0 || g_in[4] != marker ||
            g_in[5] != lens[i])
            return -1;
        memcpy(layout + offs[i], g_in + 6, lens[i]);
    }
    return 0;
}

// 128 bytes split 57 / 59 / 12; the first chunk's payload is prefixed with the
// mode and layer, which is why it carries fewer keymap bytes than the second.
static int write_keymap(IOHIDDeviceRef dev, uint8_t mode, uint8_t fn, const uint8_t *layout) {
    uint8_t out[REPORT_SIZE];

    build(out, WRITE_KEYMAP);
    out[3] = 0x41;
    out[4] = 59;
    out[5] = mode;
    out[6] = fn;
    memcpy(out + 7, layout, 57);
    if (txn(dev, out, WRITE_KEYMAP, "WRITE_KEYMAP(1)") < 0)
        return -1;

    build(out, WRITE_KEYMAP);
    out[3] = 0x82;
    out[4] = 59;
    memcpy(out + 5, layout + 57, 59);
    if (txn(dev, out, WRITE_KEYMAP, "WRITE_KEYMAP(2)") < 0)
        return -1;

    build(out, WRITE_KEYMAP);
    out[3] = 0xC3;
    out[4] = 12;
    memcpy(out + 5, layout + 116, 12);
    if (txn(dev, out, WRITE_KEYMAP, "WRITE_KEYMAP(3)") < 0)
        return -1;

    build(out, CONFIRM_KEYMAP);
    return txn(dev, out, CONFIRM_KEYMAP, "CONFIRM_KEYMAP");
}

static void notify(IOHIDDeviceRef dev, uint8_t state) {
    uint8_t out[REPORT_SIZE];
    build(out, NOTIFY_APPLICATION_STATE);
    out[3] = 0;
    out[4] = 1;
    out[5] = state;
    txn(dev, out, NOTIFY_APPLICATION_STATE, state ? "NOTIFY(close)" : "NOTIFY(open)");
}

// Returns 0 when the board now reports exactly `want`.
static int apply_and_verify(IOHIDDeviceRef dev, uint8_t mode, uint8_t fn, const uint8_t *want,
                            const char *what) {
    printf("  %s\n", what);
    if (write_keymap(dev, mode, fn, want) < 0) {
        printf("  -> write rejected\n");
        return -1;
    }

    uint8_t after[KEYMAP_SIZE];
    if (get_keymap(dev, mode, fn, after) < 0) {
        printf("  -> read-back failed\n");
        return -1;
    }
    if (memcmp(after, want, KEYMAP_SIZE) != 0) {
        printf("  -> read-back does NOT match what was written:\n");
        for (int i = 0; i < KEYMAP_SIZE; i++)
            if (after[i] != want[i])
                printf("       key %d: wanted %02X, got %02X\n", i, want[i], after[i]);
        return -1;
    }
    printf("  -> read-back matches\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <mode> <fn> <key> <code> [--test] [--notify]\n", argv[0]);
        fprintf(stderr, "  default  apply the change and leave it in place\n");
        fprintf(stderr, "  --test   apply, verify, then put the old value back\n");
        return 2;
    }
    uint8_t mode = (uint8_t)strtol(argv[1], NULL, 0);
    uint8_t fn = (uint8_t)strtol(argv[2], NULL, 0);
    int key = (int)strtol(argv[3], NULL, 0);
    uint8_t code = (uint8_t)strtol(argv[4], NULL, 0);
    int use_notify = 0, test_only = 0;
    for (int i = 5; i < argc; i++) {
        if (strcmp(argv[i], "--notify") == 0)
            use_notify = 1;
        else if (strcmp(argv[i], "--test") == 0)
            test_only = 1;
    }

    if (key < 1 || key > 60) {
        fprintf(stderr, "key must be 1..60\n");
        return 2;
    }

    IOHIDDeviceRef dev = find_device();
    if (!dev) {
        fprintf(stderr, "HHKB vendor HID interface not found\n");
        return 1;
    }
    if (IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
        fprintf(stderr, "IOHIDDeviceOpen failed\n");
        return 1;
    }

    uint8_t inbuf[REPORT_SIZE];
    IOHIDDeviceRegisterInputReportCallback(dev, inbuf, REPORT_SIZE, input_cb, NULL);
    IOHIDDeviceScheduleWithRunLoop(dev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

    if (use_notify)
        notify(dev, 0);

    uint8_t original[KEYMAP_SIZE];
    if (get_keymap(dev, mode, fn, original) < 0) {
        fprintf(stderr, "cannot read current keymap for mode %d fn %d\n", mode, fn);
        return 1;
    }
    printf("original key %d = %02X\n", key, original[key]);
    if (original[key] == code) {
        printf("already set to %02X; nothing to do\n", code);
        return 0;
    }

    uint8_t modified[KEYMAP_SIZE];
    memcpy(modified, original, KEYMAP_SIZE);
    modified[key] = code;

    char msg[128];
    snprintf(msg, sizeof(msg), "writing key %d: %02X -> %02X", key, original[key], code);
    int wrote = apply_and_verify(dev, mode, fn, modified, msg);

    // A failed write can leave the board in a mixed state, so roll back on
    // failure even when the caller wanted the change kept.
    int restored = 0;
    int roll_back = test_only || wrote != 0;
    if (roll_back) {
        printf("\n");
        snprintf(msg, sizeof(msg), "restoring key %d to %02X", key, original[key]);
        restored = apply_and_verify(dev, mode, fn, original, msg);
    } else {
        printf("\nkey %d left as %02X\n", key, code);
        printf("undo with: %s %d %d %d 0x%02X\n", argv[0], mode, fn, key, original[key]);
    }

    if (use_notify)
        notify(dev, 1);

    printf("\n=== result ===\n");
    printf("write  : %s\n", wrote == 0 ? "ACCEPTED" : "FAILED");
    if (roll_back)
        printf("restore: %s\n", restored == 0 ? "OK" : "FAILED - keyboard may be modified");
    else
        printf("restore: not requested\n");

    IOHIDDeviceClose(dev, kIOHIDOptionsTypeNone);
    return (wrote == 0 && restored == 0) ? 0 : 1;
}
