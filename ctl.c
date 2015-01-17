
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <libusb.h>
#include <stdio.h>

const char *usage = "<bmRequestType> <bRequest> <wValue> <wIndex> <wLength> [<timeout_ms>]";

int run_usb(libusb_context *ctx, libusb_device_handle *hdev, int argc, char **argv)
{
	if (argc < 5 || argc > 6)
		return 1;

	uint8_t  bmRequestType = strtol(argv[0], NULL, 0);
	uint8_t  bRequest      = strtol(argv[1], NULL, 0);
	uint16_t wValue        = strtol(argv[2], NULL, 0);
	uint16_t wIndex        = strtol(argv[3], NULL, 0);
	uint16_t wLength       = strtol(argv[4], NULL, 0);
	unsigned timeout = argc > 5 ? strtol(argv[5], NULL, 0) : 500;

	uint8_t *buf = calloc(1, wLength);

	if (~bmRequestType & 0x80) {
		/* host to device transfer */
		if (!fread(buf, wLength, 1, stdin)) {
			fprintf(stderr, "error reading %hu bytes from stdin: %s\n",
				wLength, strerror(errno));
			return 2;
		}
	}

	int r = libusb_control_transfer(hdev, bmRequestType, bRequest, wValue, wIndex, buf, wLength, timeout);
	if (r < 0) {
		fprintf(stderr, "error during control transfer: %s\n",
			libusb_error_name(r));
		return 3;
	}

	if (bmRequestType & 0x80) {
		/* device to host transfer */
		r = fwrite(buf, r, 1, stdout);
	}

	return 0;
}
