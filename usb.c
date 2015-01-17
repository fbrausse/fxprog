
#include <libusb.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

const char *usage;
int run_usb(libusb_context *ctx, libusb_device_handle *hdev, int argc, char **argv);

static libusb_device ** usb_get_device_list(libusb_context *ctx)
{
	libusb_device **devs = NULL;
	ssize_t ndevs = libusb_get_device_list(ctx, &devs);
	if (ndevs < 0) {
		fprintf(stderr, "error enumerating USB devices: %s\n",
			libusb_error_name(ndevs));
	} else if (!ndevs) {
		fprintf(stderr, "error: no USB devices found\n");
	}
	return devs;
}

static int init_usb(uint16_t bus, uint16_t addr, libusb_context **ctx, libusb_device ***devs, libusb_device **dev, libusb_device_handle **hdev)
{
	int r = libusb_init(ctx);
	if (r) {
		fprintf(stderr, "error initializing libusb: %s\n",
			libusb_error_name(r));
		return 1;
	}

	*devs = usb_get_device_list(*ctx);
	if (!*devs)
		return 2;

	libusb_device **j;
	for (j = *devs; *j; j++) {
		uint16_t b = libusb_get_bus_number(*j);
		uint16_t a = libusb_get_device_address(*j);
		if (b == bus && a == addr)
			break;
	}
	if (!*j) {
		fprintf(stderr, "no device with bus:addr %hu:%hu found\n",
			bus, addr);
		return 2;
	}

	*dev = *j;

	r = libusb_open(*j, hdev);
	if (r) {
		fprintf(stderr, "error opening device: %s\n",
			libusb_error_name(r));
		return 2;
	}

	return 0;
}

int main(int argc, char **argv)
{
	libusb_context *ctx = NULL;
	libusb_device **devs = NULL, *dev = NULL;
	libusb_device_handle *hdev = NULL;
	int r = 0;

	if (argc < 3) {
		fprintf(stderr, "usage: %s <bus> <addr> %s\n", argv[0], usage);
		return 1;
	}

	r = init_usb(atoi(argv[1]), atoi(argv[2]), &ctx, &devs, &dev, &hdev);
	if (r)
		goto out;

	r = run_usb(ctx, hdev, argc - 3, argv + 3);
	if (r == 1)
		fprintf(stderr, "usage: %s <bus> <addr> %s\n", argv[0], usage);
	else if (r)
		fprintf(stderr, "run_usb failed with %d\n", r);

out:
	if (dev)
		libusb_close(hdev);
	if (devs)
		libusb_free_device_list(devs, 1);
	if (ctx)
		libusb_exit(ctx);

	return r;
}
