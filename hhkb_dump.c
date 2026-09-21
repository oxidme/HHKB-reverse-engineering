// Dumps every stored keymap (4 keyboard modes x 2 layers) from the HHKB
// vendor HID interface and writes them out for safekeeping.
//
// Read-only: only GET_KEYBOARD_INFO / GET_DIP_STATE / GET_KEYBOARD_MODE /
// GET_KEYMAP are issued. GET_KEYMAP carries the mode as a parameter, so this
// probes modes other than the active one; the active mode is re-read at the
// end to confirm nothing was switched as a side effect.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define REPORT_SIZE 64
#define KEYMAP_SIZE 128
#define HHKB_VID 0x04FE
#define USAGE_PAGE_VENDOR 0xFF00

#define GET_KEYBOARD_INFO 2
#define GET_DIP_STATE 5
#define GET_KEYBOARD_MODE 6
#define GET_KEYMAP 135

static const char *MODE_NAMES[4] = { "hhk", "mac", "win", "secret" };

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

static int send(IOHIDDeviceRef dev, const uint8_t *out)
{
	IOReturn r = IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, 0, out, REPORT_SIZE);
	if (r != kIOReturnSuccess) {
		fprintf(stderr, "SetReport failed: 0x%08X\n", r);
		return -1;
	}
	return 0;
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
	if (send(dev, out) < 0 || wait_report(2.0) < 0)
		return -1;
	return (g_in[0] == 0x55 && g_in[1] == 0x55 && g_in[2] == cmd && g_in[3] == 0) ? 0 : -1;
}

// Chunk markers are (n << 6) | n for n = 1..3; reject anything else so a
// desynchronised stream cannot be silently written out as a valid dump.
static int expect_chunk(uint8_t n, uint8_t want_len)
{
	uint8_t marker = (uint8_t)((n << 6) | n);
	if (g_in[0] != 0x55 || g_in[1] != 0x55 || g_in[2] != GET_KEYMAP) {
		fprintf(stderr, "  chunk %u: bad header %02X %02X %02X\n",
			n, g_in[0], g_in[1], g_in[2]);
		return -1;
	}
	if (g_in[3] != 0) {
		fprintf(stderr, "  chunk %u: status 0x%02X\n", n, g_in[3]);
		return -1;
	}
	if (g_in[4] != marker || g_in[5] != want_len) {
		fprintf(stderr, "  chunk %u: expected marker %02X len %u, got %02X %u\n",
			n, marker, want_len, g_in[4], g_in[5]);
		return -1;
	}
	return 0;
}

static int get_keymap(IOHIDDeviceRef dev, uint8_t mode, uint8_t fn, uint8_t *layout)
{
	static const uint8_t lens[3] = { 58, 58, 12 };
	static const uint8_t offs[3] = { 0, 58, 116 };

	uint8_t out[REPORT_SIZE];
	build(out, GET_KEYMAP);
	out[4] = 2;     // payload length: mode + fn
	out[5] = mode;
	out[6] = fn;

	memset(layout, 0, KEYMAP_SIZE);
	if (send(dev, out) < 0)
		return -1;

	// Only the first chunk is requested; the keyboard pushes the other two.
	for (int i = 0; i < 3; i++) {
		if (wait_report(2.0) < 0) {
			fprintf(stderr, "  chunk %d: timeout\n", i + 1);
			return -1;
		}
		if (expect_chunk((uint8_t)(i + 1), lens[i]) < 0)
			return -1;
		memcpy(layout + offs[i], g_in + 6, lens[i]);
	}
	return 0;
}

static void write_bin(const char *dir, uint8_t mode, uint8_t fn, const uint8_t *layout)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/keymap_%d_%s_fn%d.bin", dir, mode, MODE_NAMES[mode], fn);
	FILE *f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "cannot write %s\n", path);
		return;
	}
	fwrite(layout, 1, KEYMAP_SIZE, f);
	fclose(f);
	printf("  wrote %s\n", path);
}

int main(int argc, char **argv)
{
	const char *dir = (argc > 1) ? argv[1] : "dumps";
	mkdir(dir, 0755);

	IOHIDDeviceRef dev = find_device();
	if (!dev) {
		fprintf(stderr, "HHKB vendor HID interface (04FE / usage page FF00) not found\n");
		return 1;
	}
	if (IOHIDDeviceOpen(dev, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
		fprintf(stderr, "IOHIDDeviceOpen failed\n");
		return 1;
	}

	uint8_t inbuf[REPORT_SIZE];
	IOHIDDeviceRegisterInputReportCallback(dev, inbuf, REPORT_SIZE, input_cb, NULL);
	IOHIDDeviceScheduleWithRunLoop(dev, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);

	// Provenance: which board this dump came from, and its state at dump time.
	char type_number[21] = { 0 }, revision[5] = { 0 }, serial[17] = { 0 };
	uint8_t appfirm[4] = { 0 }, bootfirm[4] = { 0 }, running = 0;
	if (simple_cmd(dev, GET_KEYBOARD_INFO) == 0) {
		memcpy(type_number, g_in + 6, 20);
		memcpy(revision, g_in + 26, 4);
		memcpy(serial, g_in + 30, 16);
		memcpy(appfirm, g_in + 46, 4);
		memcpy(bootfirm, g_in + 54, 4);
		running = g_in[62];
	}

	uint8_t dip[6] = { 0 };
	if (simple_cmd(dev, GET_DIP_STATE) == 0)
		memcpy(dip, g_in + 6, 6);

	int mode_before = -1;
	if (simple_cmd(dev, GET_KEYBOARD_MODE) == 0)
		mode_before = g_in[6];

	printf("Device : %s rev %s serial %s\n", type_number, revision, serial);
	printf("Firmware: App %X%d.%d%d / Boot %X%d.%d%d (running %s)\n",
	       appfirm[0], appfirm[1], appfirm[2], appfirm[3],
	       bootfirm[0], bootfirm[1], bootfirm[2], bootfirm[3],
	       running ? "Boot" : "App");
	printf("DIP    : %d%d%d%d%d%d\n", dip[0], dip[1], dip[2], dip[3], dip[4], dip[5]);
	printf("Mode   : %d (%s)\n\n", mode_before,
	       (mode_before >= 0 && mode_before < 4) ? MODE_NAMES[mode_before] : "?");

	uint8_t maps[4][2][KEYMAP_SIZE];
	int ok[4][2];

	for (int mode = 0; mode < 4; mode++) {
		for (int fn = 0; fn < 2; fn++) {
			printf("GET_KEYMAP mode=%d (%s) fn=%d\n", mode, MODE_NAMES[mode], fn);
			ok[mode][fn] = (get_keymap(dev, (uint8_t)mode, (uint8_t)fn,
						   maps[mode][fn]) == 0);
			if (ok[mode][fn])
				write_bin(dir, (uint8_t)mode, (uint8_t)fn, maps[mode][fn]);
			else
				printf("  FAILED - not saved\n");
		}
	}

	// A read command should not change state; verify the board is still in the
	// mode it started in before trusting this dump.
	int mode_after = -1;
	if (simple_cmd(dev, GET_KEYBOARD_MODE) == 0)
		mode_after = g_in[6];
	printf("\nMode after dump: %d (%s)\n", mode_after,
	       (mode_after >= 0 && mode_after < 4) ? MODE_NAMES[mode_after] : "?");
	if (mode_after != mode_before)
		fprintf(stderr, "WARNING: keyboard mode changed during dump (%d -> %d)\n",
			mode_before, mode_after);

	// If the mode parameter were ignored, every mode would return the same
	// bytes; say so explicitly rather than leaving four identical files.
	printf("\nDistinctness check (vs mode 0):\n");
	for (int mode = 1; mode < 4; mode++) {
		for (int fn = 0; fn < 2; fn++) {
			if (!ok[mode][fn] || !ok[0][fn]) {
				printf("  mode %d fn %d: n/a\n", mode, fn);
				continue;
			}
			int diff = 0;
			for (int i = 0; i < KEYMAP_SIZE; i++)
				if (maps[mode][fn][i] != maps[0][fn][i])
					diff++;
			printf("  mode %d fn %d: %d byte(s) differ\n", mode, fn, diff);
		}
	}

	// Manifest, so the raw .bin files stay interpretable on their own.
	char path[512];
	snprintf(path, sizeof(path), "%s/manifest.json", dir);
	FILE *f = fopen(path, "w");
	if (f) {
		time_t now = time(NULL);
		char ts[64];
		strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S%z", localtime(&now));

		fprintf(f, "{\n");
		fprintf(f, "  \"dumped_at\": \"%s\",\n", ts);
		fprintf(f, "  \"type_number\": \"%s\",\n", type_number);
		fprintf(f, "  \"revision\": \"%s\",\n", revision);
		fprintf(f, "  \"serial\": \"%s\",\n", serial);
		fprintf(f, "  \"app_firmware\": \"%X%d.%d%d\",\n",
			appfirm[0], appfirm[1], appfirm[2], appfirm[3]);
		fprintf(f, "  \"boot_firmware\": \"%X%d.%d%d\",\n",
			bootfirm[0], bootfirm[1], bootfirm[2], bootfirm[3]);
		fprintf(f, "  \"running_firmware\": \"%s\",\n", running ? "boot" : "app");
		fprintf(f, "  \"dip\": [%d,%d,%d,%d,%d,%d],\n",
			dip[0], dip[1], dip[2], dip[3], dip[4], dip[5]);
		fprintf(f, "  \"active_mode\": %d,\n", mode_before);
		fprintf(f, "  \"keymaps\": [\n");

		int first = 1;
		for (int mode = 0; mode < 4; mode++) {
			for (int fn = 0; fn < 2; fn++) {
				if (!ok[mode][fn])
					continue;
				if (!first)
					fprintf(f, ",\n");
				first = 0;
				fprintf(f, "    { \"mode\": %d, \"mode_name\": \"%s\", \"fn\": %d, \"hex\": \"",
					mode, MODE_NAMES[mode], fn);
				for (int i = 0; i < KEYMAP_SIZE; i++)
					fprintf(f, "%02X", maps[mode][fn][i]);
				fprintf(f, "\" }");
			}
		}
		fprintf(f, "\n  ]\n}\n");
		fclose(f);
		printf("\nwrote %s\n", path);
	}

	IOHIDDeviceClose(dev, kIOHIDOptionsTypeNone);
	return 0;
}
