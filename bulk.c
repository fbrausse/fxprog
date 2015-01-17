
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <libusb.h>
#include <stdio.h>

const char *usage = "<ep> <wLength> [<timeout_ms>]";

int run_usb(libusb_context *ctx, libusb_device_handle *hdev, int argc, char **argv)
{
	int r;
	if (argc < 2 || argc > 3)
		return 1;

	uint8_t ep = strtol(argv[0], NULL, 0);
	unsigned len = strtol(argv[1], NULL, 0);
	unsigned timeout = argc > 2 ? strtol(argv[2], NULL, 0) : 500;

	uint8_t *buf = calloc(1, len);
	if (~ep & 0x80) {
		/* host to device transfer */
		if (!fread(buf, len, 1, stdin)) {
			fprintf(stderr, "error reading %u bytes from stdin: %s\n",
				len, strerror(errno));
			return 2;
		}
	}

	r = libusb_claim_interface(hdev, 0);
	if (r) {
		fprintf(stderr, "error claiming interface 0: %s\n",
			libusb_error_name(r));
		return 3;
	}
	r = libusb_set_interface_alt_setting(hdev, 0, 1);
	if (r) {
		libusb_release_interface(hdev, 0);
		fprintf(stderr, "error setting alt-setting 0 for interface 0: %s\n",
			libusb_error_name(r));
		return 4;
	}

	int tferd = 0;
	r = libusb_bulk_transfer(hdev, ep, buf, len, &tferd, timeout);
	libusb_release_interface(hdev, 0);
	if (r) {
		fprintf(stderr, "error during bulk transfer: %s\n",
			libusb_error_name(r));
		return 5;
	}

	if (ep & 0x80) {
		/* device to host transfer */
		r = fwrite(buf, tferd, 1, stdout);
	}

	return 0;
}
