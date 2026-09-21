// Read-only probe for the HHKB vendor-defined HID interface (usage page 0xFF00).
// Uses IOHIDManager directly instead of hidapi so that no third-party
// dependency is needed and the binary stays native arm64.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <string.h>

#define REPORT_SIZE 64
#define HHKB_VID 0x04FE
#define USAGE_PAGE_VENDOR 0xFF00

enum {
	NOTIFY_APPLICATION_STATE = 1,
	GET_KEYBOARD_INFO = 2,
	GET_DIP_STATE = 5,
	GET_KEYBOARD_MODE = 6,
	GET_KEYMAP = 135,
};

static uint8_t g_in[REPORT_SIZE];
static volatile int g_got;

static void input_cb(void *ctx, IOReturn res, void *sender, IOHIDReportType type,
		     uint32_t id, uint8_t *report, CFIndex len)
{
	(void)ctx; (void)res; (void)sender; (void)type; (void)id;
	memset(g_in, 0, sizeof(g_in));
	memcpy(g_in, report, len < REPORT_SIZE ? len : REPORT_SIZE);
	g_got = 1;
	CFRunLoopStop(CFRunLoopGetCurrent());
}

static int32_t int_prop(IOHIDDeviceRef d, CFStringRef key)
{
	int32_t v = 0;
	CFTypeRef r = IOHIDDeviceGetProperty(d, key);
	if (r && CFGetTypeID(r) == CFNumberGetTypeID())
		CFNumberGetValue((CFNumberRef)r, kCFNumberSInt32Type, &v);
	return v;
}

static IOHIDDeviceRef find_device(IOHIDManagerRef mgr)
{
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

// Sends a 64-byte report and waits for the reply. The reply arrives on the
// interrupt IN endpoint, so the run loop has to be pumped rather than polled.
static int transact(IOHIDDeviceRef dev, const uint8_t *out, double timeout)
{
	g_got = 0;
	IOReturn r = IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, 0, out, REPORT_SIZE);
	if (r != kIOReturnSuccess) {
		fprintf(stderr, "SetReport failed: 0x%08X\n", r);
		return -1;
	}
	CFRunLoopRunInMode(kCFRunLoopDefaultMode, timeout, true);
	return g_got ? 0 : -1;
}

static void build(uint8_t *buf, uint8_t cmd)
{
	memset(buf, 0, REPORT_SIZE);
	buf[0] = 0xAA;
	buf[1] = 0xAA;
	buf[2] = cmd;
}

static void hexdump(const char *label, const uint8_t *b, int len)
{
	printf("%-22s", label);
	for (int i = 0; i < len; i++)
		printf("%02X%s", b[i], (i % 16 == 15 && i + 1 < len) ? "\n                      " : " ");
	printf("\n");
}

static void print_ascii(const char *label, const uint8_t *b, int len)
{
	printf("  %-18s\"", label);
	for (int i = 0; i < len && b[i]; i++)
		printf("%c", (b[i] >= 32 && b[i] < 127) ? b[i] : '.');
	printf("\"\n");
}

int main(void)
{
	IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
	IOHIDDeviceRef dev = find_device(mgr);
	if (!dev) {
		fprintf(stderr, "HHKB vendor HID interface (04FE / usage page FF00) not found\n");
		return 1;
	}

	IOReturn r = IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone);
	if (r != kIOReturnSuccess) {
		fprintf(stderr, "IOHIDDeviceOpen failed: 0x%08X\n", r);
		return 1;
	}

	uint8_t inbuf[REPORT_SIZE];
	IOHIDDeviceRegisterInputReportCallback(dev, inbuf, REPORT_SIZE, input_cb, NULL);
	IOHIDDeviceScheduleWithRunLoop(dev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

	uint8_t out[REPORT_SIZE];

	printf("=== GET_KEYBOARD_INFO (0x02) ===\n");
	build(out, GET_KEYBOARD_INFO);
	if (transact(dev, out, 2.0) == 0) {
		hexdump("response:", g_in, REPORT_SIZE);
		print_ascii("TypeNumber", g_in + 6, 20);
		print_ascii("Revision", g_in + 26, 4);
		print_ascii("Serial", g_in + 30, 16);
		printf("  %-18s%02X %02X %02X %02X\n", "AppFirmVersion",
		       g_in[46], g_in[47], g_in[48], g_in[49]);
		printf("  %-18s%02X %02X %02X %02X\n", "BootFirmVersion",
		       g_in[54], g_in[55], g_in[56], g_in[57]);
		printf("  %-18s%d\n", "RunningFirmware", g_in[62]);
	} else {
		printf("  (no response)\n");
	}

	printf("\n=== GET_KEYBOARD_MODE (0x06) ===\n");
	uint8_t mode = 0xFF;
	build(out, GET_KEYBOARD_MODE);
	if (transact(dev, out, 2.0) == 0) {
		hexdump("response:", g_in, 16);
		mode = g_in[6];
		const char *names[] = { "HHK", "Mac", "Lite", "Secret" };
		printf("  mode = %d (%s)\n", mode, mode < 4 ? names[mode] : "?");
	}

	printf("\n=== GET_DIP_STATE (0x05) ===\n");
	build(out, GET_DIP_STATE);
	if (transact(dev, out, 2.0) == 0) {
		hexdump("response:", g_in, 16);
		for (int i = 0; i < 6; i++)
			printf("  SW%d = %s\n", i + 1, g_in[6 + i] ? "ON" : "OFF");
	}

	for (int fn = 0; fn <= 1; fn++) {
		printf("\n=== GET_KEYMAP (0x87) fn=%d ===\n", fn);
		uint8_t layout[128];
		memset(layout, 0, sizeof(layout));
		build(out, GET_KEYMAP);
		out[4] = 2;
		out[5] = (mode == 0xFF) ? 0 : mode;
		out[6] = (uint8_t)fn;

		if (transact(dev, out, 2.0) != 0) {
			printf("  (no response)\n");
			continue;
		}
		hexdump("chunk1:", g_in, REPORT_SIZE);
		memcpy(layout, g_in + 6, 58);

		// Chunks 2 and 3 are pushed by the keyboard without a new request.
		for (int c = 2; c <= 3; c++) {
			g_got = 0;
			CFRunLoopRunInMode(kCFRunLoopDefaultMode, 2.0, true);
			if (!g_got) {
				printf("  (chunk %d missing)\n", c);
				break;
			}
			hexdump(c == 2 ? "chunk2:" : "chunk3:", g_in, REPORT_SIZE);
			if (c == 2)
				memcpy(layout + 58, g_in + 6, 58);
			else
				memcpy(layout + 116, g_in + 6, 12);
		}

		printf("  keymap[128]:\n");
		for (int i = 0; i < 128; i += 16) {
			printf("    %3d: ", i);
			for (int j = 0; j < 16; j++)
				printf("%02X ", layout[i + j]);
			printf("\n");
		}
	}

	IOHIDDeviceClose(dev, kIOHIDOptionsTypeNone);
	return 0;
}
