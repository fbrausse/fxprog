
#define _POSIX_C_SOURCE		201501L

#include <unistd.h>
#include <libusb.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "usb.h"
/*
extern const char *usage;
extern int min_argc, max_argc;
int run_usb(libusb_context *ctx, libusb_device_handle *hdev, int argc, char **argv);
*/
libusb_device ** usb_common_get_device_list(libusb_context *ctx)
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

static int usb_desc_eq_vid_pid(
	const struct libusb_device_descriptor *desc,
	const addr_t addr
) {
	return desc->idVendor == addr[0] && desc->idProduct == addr[1];
}

libusb_device_handle * usb_common_find_device(
	libusb_context *ctx, struct dev_spec *spec,
	const struct dev_type *dev_types, unsigned n_dev_types
) {
	struct libusb_device_descriptor desc;
	libusb_device_handle *hdev = NULL;
	libusb_device **j, **devs, *dev = NULL;
	addr_t addr;
	const addr_t *a;
	unsigned i;
	int r;

	devs = usb_common_get_device_list(ctx);
	if (!devs)
		return NULL;

	for (j = devs; *j; j++) {
		addr[0] = libusb_get_bus_number(*j);
		addr[1] = libusb_get_device_address(*j);
		if (spec->have_bus_addr) {
			if (memcmp(addr, spec->bus_addr, sizeof(addr_t)))
				continue;
		} else {
			r = libusb_get_device_descriptor(*j, &desc);
			if (r) {
				fprintf(stderr,
					"warning: unable to access descriptor "
					"of device on %hu.%hu: %s\n",
					addr[0], addr[1], libusb_error_name(r));
				goto out;
			}
			if (spec->have_vid_pid) {
				if (usb_desc_eq_vid_pid(&desc, spec->vid_pid))
					goto found;
			} else if (spec->dev_type) {
				a = &spec->dev_type->addr;
				if (usb_desc_eq_vid_pid(&desc, *a))
					goto found;
			} else {
				for (i=0; i<n_dev_types; i++) {
					a = &dev_types[i].addr;
					if (usb_desc_eq_vid_pid(&desc, *a))
						goto found;
				}
			}
			/* nothing matched */
			continue;
		}
found:
		/* take this one */
		if (!dev) {
			dev = *j;
			continue;
		}
		/* not unique */
		fprintf(stderr,
			"ambigious device specifier: "
			"both %hhu.%hhu and %hu.%hu match\n",
			libusb_get_bus_number(dev),
			libusb_get_device_address(dev),
			addr[0], addr[1]);
		goto out;
	}
	if (!dev) {
		fprintf(stderr, "no matching USB device found\n");
	} else {
		r = libusb_get_device_descriptor(dev, &desc);
		for (i=0; i<n_dev_types; i++)
			if (usb_desc_eq_vid_pid(&desc, dev_types[i].addr)) {
				spec->dev_type = dev_types + i;
				break;
			}
		fprintf(stderr, "using %s device %04hx:%04hx on bus.addr %hhu.%hhu\n",
			spec->dev_type ? spec->dev_type->name : "unknown",
			desc.idVendor, desc.idProduct,
			libusb_get_bus_number(dev),
			libusb_get_device_address(dev));
		r = libusb_open(dev, &hdev);
		if (r) {
			fprintf(stderr, "error opening device: %s\n",
				libusb_error_name(r));
		}
	}

out:
	libusb_free_device_list(devs, 1);
	return hdev;
}

static int parse_dev_spec(struct dev_spec *spec, const char *dev_addr)
{
	addr_t *addr;
	const char *delim = dev_addr + strcspn(dev_addr, ".:");
	const char *fmt = NULL;
	int k;
	switch (*delim) {
	case '.':
		fmt = "%hu.%hu";
		addr = &spec->bus_addr;
		spec->have_bus_addr = 1;
		break;
	case ':':
		fmt = "%04hx:%04hx";
		addr = &spec->vid_pid;
		spec->have_vid_pid = 1;
		break;
	}
	if (!fmt || sscanf(dev_addr, fmt, *addr + 0, *addr + 1) != 2) {
		fprintf(stderr,"invalid device address: '%s'\n",dev_addr);
		return 1;
	}
	return 0;
}

const char *usb_common_dev_spec_help = "\
  -c <bus>.<addr> N: bus #, M: device # (see /sys/bus/usb/devices/N-*/devnum)\n\
     <vid>:<pid>  address USB device via a Vendor / Product ID pair\n\
                  both formats override " ENV_DEV_ADDR "= in environment\n\
";
const char *usb_common_dev_type_help = "\
  -t <dev-type>   use Vendor / Product ID pair identified by shortcut <dev-type>\n\
";
const char *usb_common_iface_help = "\
  -i <iface>      use interface number <iface> (default: %d, -1 to disable)\n\
";
const char *usb_common_iface_alt_help = "\
  -a <alt-iface>  set alt-setting on interface (default: %d, -1 to disable)\n\
";

char * usb_common_usage(const struct usb_common *uc)
{
	char buf[256];
	unsigned n = 0;
	n += snprintf(buf+n,sizeof(buf)-n, "[-c {<bus>.<addr> | <vid>:<pid>}]");
	if (uc->n_dev_types)
		n += snprintf(buf+n, sizeof(buf)-n, " [-t <dev-type>]");
	if (uc->iface > -2) {
		n += snprintf(buf+n, sizeof(buf)-n, " [-i <iface>]");
		if (uc->alt > -2)
			n += snprintf(buf+n,sizeof(buf)-n, " [-a <alt-iface>]");
	}
	return strndup(buf, n);
}

char * usb_common_help(const struct usb_common *uc)
{
	unsigned n = snprintf(NULL, 0, "%s", usb_common_dev_spec_help);
	if (uc->n_dev_types) {
		n += snprintf(NULL, 0, "%s\
                  supported:", usb_common_dev_type_help);
		for (unsigned i=0; i<uc->n_dev_types; i++)
			n += 1 + strlen(uc->dev_types[i].name) + 1;
	}
	if (uc->iface >= -1) {
		n += snprintf(NULL, 0, usb_common_iface_help, uc->iface);
		if (uc->alt >= -1)
			n += snprintf(NULL,0,usb_common_iface_alt_help,uc->alt);
	}

	char *s = malloc(n + 1);
	unsigned at = snprintf(s, n+1, "%s", usb_common_dev_spec_help);
	if (uc->n_dev_types) {
		at += snprintf(s+at, n+1-at, "%s\
                  supported:", usb_common_dev_type_help);
		for (unsigned i=0; i<uc->n_dev_types; i++)
			at += snprintf(s+at, n+1-at, " %s%c",
			               uc->dev_types[i].name,
			               i+1 < uc->n_dev_types ? ',' : '\n');
	}
	if (uc->iface >= -1) {
		at += snprintf(s+at, n+1-at, usb_common_iface_help, uc->iface);
		if (uc->alt >= -1)
			at += snprintf(s+at, n+1-at, usb_common_iface_alt_help,
			               uc->alt);
	}
	return s;
}

static long opt_parse_long(int argc, char **argv, int default_val, char c)
{
	long v = default_val;
	int opt;
	char *end;
	char shortopts[] = ":x:";

	shortopts[1] = c;

	while ((opt = getopt(argc, argv, shortopts)) != -1)
		if (opt == c) {
			v = strtol(optarg, &end, 0);
			if (*end)
				FATAL(1,
				      "invalid argument for option '-%c': '%s'\n",
				      c,optarg);
		} else if (opt == ':') {
			FATAL(1,"argument expected for option '-%c'\n",optopt);
		} else if (opt == '?') {
			break;
		}

	return v;
}

int usb_common_parse_opts(struct usb_common *uc, int argc, char **argv)
{
	const char *dev_addr = getenv(ENV_DEV_ADDR);
	const char *dev_type = NULL;
	char *end;
	int opt, r;
	int iface = 0;
	int alt_iface = -1;

	while ((opt = getopt(argc, argv, ":c:t:")) != -1)
		switch (opt) {
		case 'c': dev_addr = optarg; break;
		case 't': dev_type = optarg; break;
		case ':': FATAL(1,"argument expected for option '-%c'\n",optopt);
		case '?': optind--; goto done_opt_parsing;
		}

done_opt_parsing:
	if (dev_addr && (r = parse_dev_spec(&uc->spec, dev_addr)))
		return r;
	if (dev_type) {
		const struct dev_type *t = NULL, *tt;
		for (unsigned i=0; i<uc->n_dev_types; i++) {
			tt = uc->dev_types + i;
			if (strstr(tt->name, dev_type) != tt->name)
				continue;
			if (t)
				FATAL(1,"ambiguous <dev-type> '%s'\n",dev_type);
			t = tt;
		}
		if (!t)
			FATAL(1,"unknown <dev-type> '%s'\n",dev_type);
		uc->spec.dev_type = t;
	}
	if (uc->iface > -2)
		uc->iface = opt_parse_long(argc, argv, uc->iface, 'i');
	if (uc->alt > -2)
		uc->alt = opt_parse_long(argc, argv, uc->alt, 'a');

	return 0;
}

int usb_common_setup(struct usb_common *uc)
{
	int r = 0;

	/* init libusb */
	r = libusb_init(&uc->ctx);
	if (r) {
		fprintf(stderr, "error initializing libusb: %s\n",
			libusb_error_name(r));
		r = 1;
		goto err;
	}

	/* find specified USB device */
	uc->hdev = usb_common_find_device(uc->ctx, &uc->spec,
	                                  uc->dev_types, uc->n_dev_types);
	if (!uc->hdev) {
		r = 2;
		goto err;
	}

	if (uc->iface > -1) {
		if ((r = libusb_claim_interface(uc->hdev, uc->iface))) {
			fprintf(stderr, "error claiming interface %d: %s\n",
				uc->iface, libusb_error_name(r));
			uc->iface = -1;
			r = 3;
			goto err;
		}
		if (uc->alt > -1 &&
		    (r = libusb_set_interface_alt_setting(uc->hdev, uc->iface,
		                                          uc->alt))) {
			fprintf(stderr,
				"error setting alt-setting %d on interface %d: "
				"%s\n", uc->alt, uc->iface,
				libusb_error_name(r));
			r = 4;
			goto err;
		}
	}

	return 0;

err:
	usb_common_teardown(uc);
	return r;
}

void usb_common_teardown(struct usb_common *uc)
{
	if (uc->hdev && uc->iface > -1)
		libusb_release_interface(uc->hdev, uc->iface);
	if (uc->hdev)
		libusb_close(uc->hdev);
	if (uc->ctx)
		libusb_exit(uc->ctx);
}

/*
int main(int argc, char **argv)
{
	struct dev_spec spec;
	libusb_context *ctx = NULL;
	libusb_device **devs = NULL, *dev = NULL;
	libusb_device_handle *hdev = NULL;
	int r = 0;

	memset(&spec, 0, sizeof(spec));
	progname = argv[0];

done_opt_parsing:
	if (argc - optind < min_argc || argc - optind > max_argc)
		USAGE(1);

	r = libusb_init(&ctx);
	if (r)
		FATAL(1,"error initializing libusb: %s\n", libusb_error_name(r));

	hdev = usb_find_device(ctx, &spec);
	if (!hdev)
		goto out;

	r = run_usb(ctx, hdev, argc - optind, argv + optind);
	if (r == 1)
		fprintf(stderr, "usage: %s <bus> <addr> %s\n", argv[0], usage);
	else if (r)
		fprintf(stderr, "run_usb failed with %d\n", r);

out:
	if (hdev)
		libusb_close(hdev);
	if (ctx)
		libusb_exit(ctx);

	return r;
}*/
