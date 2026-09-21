// Runs RESET_FACTORY_DEFAULTS and reports exactly what it changed.
//
// Destructive, so it requires --yes. Every stored keymap is read before and
// after the reset and the differences are printed per key, which is the only
// way to see what the command actually covers -- the reply carries a status
// byte and nothing else.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define REPORT_SIZE 64
#define KEYMAP_SIZE 128
#define HHKB_VID 0x04FE
#define USAGE_PAGE_VENDOR 0xFF00

#define RESET_FACTORY_DEFAULTS 3
#define GET_DIP_STATE 5
#define GET_KEYBOARD_MODE 6
#define GET_KEYMAP 135

#define N_MODES 3
#define WATCHDOG_SECONDS 60

static const char *MODE_NAMES[N_MODES] = { "hhk", "mac", "win" };

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

static void on_watchdog(int sig)
{
	(void)sig;
	static const char msg[] = "\nwatchdog: no progress; replug the keyboard and retry\n";
	write(STDERR_FILENO, msg, sizeof(msg) - 1);
	_exit(3);
}

static int32_t int_prop(IOHIDDeviceRef d, CFStringRef key)
{
	int32_t v = 0;
	CFTypeRef r = IOHIDDeviceGetProperty(d, key);
	if (r && CFGetTypeID(r) == CFNumberGetTypeID())
		CFNumberGetValue((CFNumberRef)r, kCFNumberSInt32Type, &v);
	return v;
}

static IOHIDDeviceRef find_device(void)
{
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

static int wait_report(double timeout)
{
	g_got = 0;
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

static int simple_cmd(IOHIDDeviceRef dev, uint8_t cmd)
{
	uint8_t out[REPORT_SIZE];
	build(out, cmd);
	if (IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, 0, out, REPORT_SIZE) !=
	    kIOReturnSuccess)
		return -1;
	if (wait_report(3.0) < 0)
		return -1;
	if (g_in[0] != 0x55 || g_in[1] != 0x55 || g_in[2] != cmd)
		return -1;
	return g_in[3] == 0 ? 0 : -(int)g_in[3];
}

static int get_keymap(IOHIDDeviceRef dev, uint8_t mode, uint8_t fn, uint8_t *layout)
{
	static const uint8_t lens[3] = { 58, 58, 12 };
	static const uint8_t offs[3] = { 0, 58, 116 };

	uint8_t out[REPORT_SIZE];
	build(out, GET_KEYMAP);
	out[4] = 2;
	out[5] = mode;
	out[6] = fn;

	memset(layout, 0, KEYMAP_SIZE);
	if (IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, 0, out, REPORT_SIZE) !=
	    kIOReturnSuccess)
		return -1;

	for (int i = 0; i < 3; i++) {
		if (wait_report(2.0) < 0)
			return -1;
		uint8_t marker = (uint8_t)(((i + 1) << 6) | (i + 1));
		if (g_in[0] != 0x55 || g_in[2] != GET_KEYMAP || g_in[3] != 0 ||
		    g_in[4] != marker || g_in[5] != lens[i])
			return -1;
		memcpy(layout + offs[i], g_in + 6, lens[i]);
	}
	return 0;
}

static int read_all(IOHIDDeviceRef dev, uint8_t maps[N_MODES][2][KEYMAP_SIZE])
{
	for (int m = 0; m < N_MODES; m++)
		for (int fn = 0; fn < 2; fn++)
			if (get_keymap(dev, (uint8_t)m, (uint8_t)fn, maps[m][fn]) < 0) {
				fprintf(stderr, "failed to read mode %d fn %d\n", m, fn);
				return -1;
			}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2 || strcmp(argv[1], "--yes") != 0) {
		fprintf(stderr, "usage: %s --yes\n", argv[0]);
		fprintf(stderr, "\nRuns RESET_FACTORY_DEFAULTS. Every stored keymap is\n");
		fprintf(stderr, "overwritten. Dump your keymaps first (hhkb_dump).\n");
		return 2;
	}

	signal(SIGALRM, on_watchdog);
	alarm(WATCHDOG_SECONDS);

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

	static uint8_t before[N_MODES][2][KEYMAP_SIZE];
	static uint8_t after[N_MODES][2][KEYMAP_SIZE];
	uint8_t dip_before[6] = { 0 }, dip_after[6] = { 0 };
	int mode_before = -1, mode_after = -1;

	if (read_all(dev, before) < 0)
		return 1;
	if (simple_cmd(dev, GET_DIP_STATE) == 0)
		memcpy(dip_before, g_in + 6, 6);
	if (simple_cmd(dev, GET_KEYBOARD_MODE) == 0)
		mode_before = g_in[6];

	printf("running RESET_FACTORY_DEFAULTS...\n");
	int rc = simple_cmd(dev, RESET_FACTORY_DEFAULTS);
	printf("  reply: %02X %02X %02X %02X %02X %02X\n",
	       g_in[0], g_in[1], g_in[2], g_in[3], g_in[4], g_in[5]);
	if (rc < 0) {
		fprintf(stderr, "rejected (status 0x%02X)\n", -rc);
		return 1;
	}

	if (read_all(dev, after) < 0)
		return 1;
	if (simple_cmd(dev, GET_DIP_STATE) == 0)
		memcpy(dip_after, g_in + 6, 6);
	if (simple_cmd(dev, GET_KEYBOARD_MODE) == 0)
		mode_after = g_in[6];

	alarm(0);

	printf("\nkeymap changes:\n");
	int total = 0;
	for (int m = 0; m < N_MODES; m++) {
		for (int fn = 0; fn < 2; fn++) {
			int changed = 0;
			for (int k = 0; k < KEYMAP_SIZE; k++) {
				if (before[m][fn][k] == after[m][fn][k])
					continue;
				if (!changed)
					printf("  mode %d (%s) %s layer:\n", m, MODE_NAMES[m],
					       fn ? "Fn" : "base");
				printf("      key %3d: %02X -> %02X\n", k,
				       before[m][fn][k], after[m][fn][k]);
				changed = 1;
				total++;
			}
		}
	}
	if (total == 0)
		printf("  none (the board was already at factory defaults)\n");

	printf("\nDIP  : %d%d%d%d%d%d -> %d%d%d%d%d%d\n",
	       dip_before[0], dip_before[1], dip_before[2], dip_before[3], dip_before[4],
	       dip_before[5], dip_after[0], dip_after[1], dip_after[2], dip_after[3],
	       dip_after[4], dip_after[5]);
	printf("mode : %d -> %d\n", mode_before, mode_after);

	IOHIDDeviceClose(dev, kIOHIDOptionsTypeNone);
	return 0;
}
