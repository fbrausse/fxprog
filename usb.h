
#ifndef USB_H
#define USB_H

#include <inttypes.h>
#include <libusb.h>

#include "common.h"

#define ENV_DEV_ADDR		"USB_DEVICE"

typedef uint16_t addr_t[2];

#define ADDR_T_INIT	{ 0, 0, }

struct dev_type {
	const char *name;
	addr_t addr;
};

struct dev_spec {
	addr_t bus_addr;
	addr_t vid_pid;
	const struct dev_type *dev_type;
	unsigned have_bus_addr : 1;
	unsigned have_vid_pid  : 1;
};

#define DEV_SPEC_INIT	{ ADDR_T_INIT, ADDR_T_INIT, NULL, 0,0, }

struct usb_common {
	struct dev_spec spec;
	libusb_context *ctx;
	libusb_device_handle *hdev;
	const struct dev_type *dev_types;
	const unsigned n_dev_types;
	int iface, alt;	/* -2: disabled and don't parse args; -1: disabled */
	char iface_opt, alt_opt;
};

#define USB_COMMON_INIT(dev_types,n_dev_types,iface,alt) \
	{ DEV_SPEC_INIT, NULL, NULL, (dev_types),(n_dev_types),(iface),(alt),'i','a', }

/* USB helper functions */
int usb_common_parse_opts(struct usb_common *uc, int argc, char **argv);
int usb_common_setup(struct usb_common *uc);
void usb_common_teardown(struct usb_common *uc);

libusb_device ** usb_common_get_device_list(libusb_context *ctx);
libusb_device_handle * usb_common_find_device(
	libusb_context *ctx, struct dev_spec *spec,
	const struct dev_type *dev_types, unsigned n_dev_types
);

char * usb_common_usage(const struct usb_common *uc);
char * usb_common_help(const struct usb_common *uc);

#if 0
int main(int argc, char **argv)
{
	struct dev_spec spec;
	libusb_context *ctx = NULL;
	libusb_device_handle *hdev;
	int iface = (def_iface);
	int alt = (def_alt);
	int r;

	memset(&spec, 0, sizeof(spec));
	r = usb_common_parse_spec(&spec, argc, argv, NULL, 0);
	if (r)
		return r;

	/* init libusb */
	r = libusb_init(&ctx);
	if (r)
		FATAL(1,"error initializing libusb: %s\n",libusb_error_name(r));

	/* find specified USB device */
	hdev = usb_common_find_device(ctx, &spec, NULL, 0);
	if (!hdev) {
		r = 1;
		goto out1;
	}

	if (iface > -2)
		r = usb_common_claim_iface(hdev, &iface, argc, argv);
	if (r) {
		fprintf(stderr,"error claiming interface %d: %s\n",
			iface, libusb_error_name(r));
		goto out2;
	}

	if (alt > -2)
		r = usb_common_set_iface_alt(hdev, iface, &alt, argc, argv);
	if (r) {
		fprintf(stderr,
			"error setting alt-setting %d for interface %d: %s\n",
			alt, iface, libusb_error_name(r));
		goto out3;
	}

	if (argc - optind < (min_argc) || argc - optind > (max_argc))
		FATAL(1,"usage: %s %s %s\n",argv[0],usb_common_usage,(usage));
	r = (cb)(ctx, hdev, argc - optind, argv + optind);
	if (r == 1)
		FATAL(1,"usage: %s %s %s\n",argv[0],usb_common_usage,(usage));
	else if (r)
		fprintf(stderr, "run_usb failed with %d\n", r);

out3:
	if (iface >= 0)
		libusb_release_interface(hdev, iface);
out2:
	libusb_close(hdev);
out1:
	libusb_exit(ctx);
	return r;
}
#endif

#endif
