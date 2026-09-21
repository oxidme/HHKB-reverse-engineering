// Dumps the running firmware over the vendor HID interface (DUMP_FIRMWARE).
//
// Read-only. The keyboard streams the image unprompted after a single request:
// each packet carries a big-endian packet counter ahead of up to 56 firmware
// bytes, and the stream ends on the first short packet.
//
// WARNING: the keyboard stops reporting key presses for the rest of the USB
// session once this command runs, and the command itself only works once per
// session -- a second attempt gets no reply at all. Unplug and replug before
// dumping again. Nothing is written and no stored data changes; only key
// scanning stops.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPORT_SIZE 64
#define HHKB_VID 0x04FE
#define USAGE_PAGE_VENDOR 0xFF00

#define GET_KEYBOARD_INFO 2
#define DUMP_FIRMWARE 208

#define MAX_FIRMWARE (1024 * 1024)
#define MAX_PACKETS 65536
#define CHUNK_MAX 56

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

static void build(uint8_t *buf, uint8_t cmd)
{
	memset(buf, 0, REPORT_SIZE);
	buf[0] = 0xAA;
	buf[1] = 0xAA;
	buf[2] = cmd;
}

int main(int argc, char **argv)
{
	const char *out_path = (argc > 1) ? argv[1] : "firmware.bin";

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

	uint8_t out[REPORT_SIZE];
	build(out, GET_KEYBOARD_INFO);
	char serial[17] = { 0 };
	if (IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, 0, out, REPORT_SIZE) ==
	    kIOReturnSuccess && wait_report(2.0) == 0)
		memcpy(serial, g_in + 30, 16);
	printf("serial: %s\n", serial);

	build(out, DUMP_FIRMWARE);
	if (IOHIDDeviceSetReport(dev, kIOHIDReportTypeOutput, 0, out, REPORT_SIZE) !=
	    kIOReturnSuccess) {
		fprintf(stderr, "SetReport failed\n");
		return 1;
	}

	uint8_t *data = calloc(MAX_FIRMWARE, 1);
	size_t size = 0;
	int packets = 0, n = 0;
	uint16_t first_seq = 0, last_seq = 0;
	int seq_is_sequential = 1;

	for (;;) {
		if (wait_report(3.0) < 0) {
			if (packets == 0)
				fprintf(stderr,
					"no reply. DUMP_FIRMWARE works once per USB session;\n"
					"unplug and replug the keyboard, then try again.\n");
			else
				fprintf(stderr, "timeout after %d packet(s)\n", packets);
			break;
		}
		if (g_in[0] != 0x55 || g_in[1] != 0x55 || g_in[2] != DUMP_FIRMWARE) {
			fprintf(stderr, "unexpected header at packet %d: %02X %02X %02X\n",
				packets, g_in[0], g_in[1], g_in[2]);
			break;
		}
		if (g_in[3] != 0) {
			fprintf(stderr, "status 0x%02X at packet %d\n", g_in[3], packets);
			break;
		}

		int len = g_in[5];
		n = len - 2;
		uint16_t seq = (uint16_t)((g_in[6] << 8) | g_in[7]);

		if (packets < 4 || n != CHUNK_MAX)
			printf("  packet %5d: hdr %02X %02X %02X %02X %02X %02X"
			       "  seq=%u  bytes=%d\n",
			       packets, g_in[0], g_in[1], g_in[2], g_in[3], g_in[4], g_in[5],
			       seq, n);

		if (n < 0 || n > CHUNK_MAX) {
			fprintf(stderr, "bad length %d at packet %d\n", n, packets);
			break;
		}
		if (size + (size_t)n > MAX_FIRMWARE) {
			fprintf(stderr, "firmware exceeds %d bytes; stopping\n", MAX_FIRMWARE);
			break;
		}

		if (packets == 0)
			first_seq = seq;
		else if (seq != (uint16_t)(last_seq + 1))
			seq_is_sequential = 0;
		last_seq = seq;

		memcpy(data + size, g_in + 8, (size_t)n);
		size += (size_t)n;
		packets++;

		if (n < CHUNK_MAX)
			break;
		if (packets >= MAX_PACKETS) {
			fprintf(stderr, "packet cap reached; stopping\n");
			break;
		}
	}

	printf("\npackets: %d, bytes: %zu (0x%zX)\n", packets, size, size);
	printf("packet counter: first=%u last=%u, %s\n", first_seq, last_seq,
	       seq_is_sequential ? "sequential" : "NOT sequential - packets may be missing");
	printf("\nNOTE: key input stays dead until the keyboard is unplugged and\n"
	       "      replugged. Nothing stored on the board was modified.\n");

	if (size > 0) {
		FILE *f = fopen(out_path, "wb");
		if (!f || fwrite(data, 1, size, f) != size) {
			fprintf(stderr, "failed to write %s\n", out_path);
			return 1;
		}
		fclose(f);
		printf("wrote %s\n", out_path);
	}

	free(data);
	IOHIDDeviceClose(dev, kIOHIDOptionsTypeNone);
	return size > 0 ? 0 : 1;
}
