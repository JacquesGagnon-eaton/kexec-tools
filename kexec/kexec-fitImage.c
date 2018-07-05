#define _GNU_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <image.h>
#include <getopt.h>
#include <arch/options.h>
#include "kexec.h"
#include "kexec-zlib.h"
#include <kexec-fitImage.h>

/*
 * fitImage parser and loader.
 *
 * The fitImage format is based on the device tree, with 2 top-level nodes:
 *   - configurations: contains a list of configurations, each config containing
 *     at least a kernel, and optionally a device tree and/or initrd
 *   - images: contains the binary data, each image being either a kernel
 *     device tree or initrd file
 * The /configurations node has a "default" property stating the subnode name
 * of the default config.
 * Each image has its own subnode under /images containing at least the
 * following properties:
 *   - data: the binary data blob
 *   - comp: the compression type used (for now, only "gzip" is implemented here)
 * Other image nodes properties (architecture, OS type, has/signature...)
 * are not processed here (at least for now) as they are not mandatory for
 * extracting the image files.
 */

static const char *fitImage_find_default_conf(const char *fit)
{
	int offset;

	/* Find default configuration */
	offset = fdt_subnode_offset(fit, 0, "configurations");
	if (offset > 0)
		return fdt_getprop(fit, offset, "default", NULL);
	else
		fprintf(stderr, "kexec: unable to find configurations\n");

	return NULL;
}

static const char *fitImage_get_config_property(const char *fit,
	const char *config, const char *name)
{
	int offset;

	/* Find default configuration */
	offset = fdt_subnode_offset(fit, 0, "configurations");
	if (offset > 0) {
		offset = fdt_subnode_offset(fit, offset, config);
		if (offset > 0)
			return fdt_getprop(fit, offset, name, NULL);
		else {
			fprintf(stderr, "kexec: unable to find configuration '%s'\n",
				config);
		}
	}
	else
		fprintf(stderr, "kexec: unable to find configurations\n");

	return NULL;
}

static const struct fdt_property *fitImage_get_image_property(const char *fit,
	const char *image, const char *property)
{
	int offset;

	/* Find images node */
	offset = fdt_subnode_offset(fit, 0, "images");
	if (offset > 0) {
		offset = fdt_subnode_offset(fit, offset, image);
		if (offset > 0)
			return fdt_get_property(fit, offset, property, NULL);
		else {
			fprintf(stderr, "kexec: unable to find image '%s'\n",
			 	image);
		}
	}
	else
		fprintf(stderr, "kexec: unable to find images\n");

	return NULL;
}

static const char *fitImage_get_image_data(const char *fit, const char *image,
	uint32_t *size)
{
	const struct fdt_property *image_prop;

	image_prop = fitImage_get_image_property(fit, image, "data");
	if (image_prop) {
		*size = be32_to_cpu(image_prop->len);
		return image_prop->data;
	}
	else
		fprintf(stderr, "kexec: unable to get image '%s' data\n", image);

	return NULL;
}

static int fitImage_get_image_comp(const char *fit, const char *image)
{
	const struct fdt_property *image_prop;

	image_prop = fitImage_get_image_property(fit, image, "compression");
	if (image_prop && image_prop->len > 0) {
		if (strcmp(image_prop->data, "gzip") == 0)
			return IH_COMP_GZIP;
	}
	else {
		fprintf(stderr, "kexec: unable to get image '%s' compression\n",
			image);
	}

	return IH_COMP_NONE;
}

static const char *fitImage_get_image(const char *fit, const char *name,
	off_t *size)
{
	uint32_t image_size;
	int comp, fd;
	const char *data;
	const char *image;

	data = fitImage_get_image_data(fit, name, &image_size);
	comp = fitImage_get_image_comp(fit, name);
	if (data) {
		switch(comp) {
		case IH_COMP_NONE:
			image = data;
			*size = image_size;
			break;
		case IH_COMP_GZIP:
			image = zlib_decompress_buffer(data, image_size, size);
			break;
		default:
			image = NULL;
			*size = 0;
			break;
		}
	}

	return image;
}

/*
 * Returns 0 if the image could be succesfully parsed as a fitImage.
 *
 * Returns -1 if this is not a fitImage or a corrupted image
 */
int fitImage_probe(const char *buf, off_t len, unsigned int arch)
{
	uint32_t kernel_size;
	const char *kernel_data;
	const char *kernel_name;
	const char *default_config;

	default_config = fitImage_find_default_conf(buf);
	if (!default_config)
		return -1;

	kernel_name = fitImage_get_config_property(buf, default_config, "kernel");
	if (!kernel_name)
		return -1;

	kernel_data = fitImage_get_image_data(buf, kernel_name, &kernel_size);
	if (!kernel_data)
		return -1;

	return 0;
}

int fitImage_load(const char *buf, off_t len, struct FitImage_contents *image)
{
	uint32_t image_size;
	int image_comp, fd;
	const char *image_data;
	const char *image_name;
	const char *default_config;

	default_config = fitImage_find_default_conf(buf);
	if (!default_config) {
		fprintf(stderr, "kexec: no default configuration in fitImage\n");
		return -1;
	}

	/* Load kernel (mandatory) */
	image_name = fitImage_get_config_property(buf, default_config, "kernel");
	if (image_name) {
		image->kernel = fitImage_get_image(buf,
			image_name, &image->kernel_size);
	}

	if (!image->kernel) {
		fprintf(stderr, "kexec: unable to load kernel from fitImage\n");
		return -1;
	}

	/* Load ramdisk if present */
	image_name = fitImage_get_config_property(buf, default_config, "ramdisk");
	if (image_name) {
		image->initrd = fitImage_get_image(buf,
			image_name, &image->initrd_size);
	}

	/* Load device tree if present */
	image_name = fitImage_get_config_property(buf, default_config, "fdt");
	if (image_name) {
		image->dtb = fitImage_get_image(buf,
			image_name, &image->dtb_size);
	}

	return 0;
}
