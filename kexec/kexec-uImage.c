#define _GNU_SOURCE
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <image.h>
#include <getopt.h>
#include <arch/options.h>
#include "kexec.h"
#include "kexec-zlib.h"
#include <kexec-uImage.h>

#ifdef HAVE_LIBZ
#include <zlib.h>
#endif
/*
 * Basic uImage loader. Not rocket science.
 */

/*
 * Returns the image type if everything goes well. This would
 * allow the user to decide if the image is of their interest.
 *
 * Returns -1 on a corrupted image
 *
 * Returns 0 if this is not a uImage
 */
int uImage_probe(const char *buf, off_t len, unsigned int arch)
{
	struct image_header header;
#ifdef HAVE_LIBZ
	unsigned int crc;
	unsigned int hcrc;
#endif

	if ((uintmax_t)len < (uintmax_t)sizeof(header))
		return -1;

	memcpy(&header, buf, sizeof(header));
	if (be32_to_cpu(header.ih_magic) != IH_MAGIC)
		return 0;
#ifdef HAVE_LIBZ
	hcrc = be32_to_cpu(header.ih_hcrc);
	header.ih_hcrc = 0;
	crc = crc32(0, (void *)&header, sizeof(header));
	if (crc != hcrc) {
		printf("Header checksum of the uImage does not match\n");
		return -1;
	}
#endif
	switch (header.ih_type) {
	case IH_TYPE_KERNEL:
	case IH_TYPE_KERNEL_NOLOAD:
		break;
	case IH_TYPE_RAMDISK:
		break;
	default:
		printf("uImage type %d unsupported\n", header.ih_type);
		return -1;
	}

	if (header.ih_os != IH_OS_LINUX) {
		printf("uImage os %d unsupported\n", header.ih_os);
		return -1;
	}

	if (header.ih_arch != arch) {
		printf("uImage arch %d unsupported\n", header.ih_arch);
		return -1;
	}

	switch (header.ih_comp) {
	case IH_COMP_NONE:
#ifdef HAVE_LIBZ
	case IH_COMP_GZIP:
#endif
		break;
	default:
		printf("uImage uses unsupported compression method\n");
		return -1;
	}

	if (be32_to_cpu(header.ih_size) > len - sizeof(header)) {
		printf("uImage header claims that image has %d bytes\n",
				be32_to_cpu(header.ih_size));
		printf("we read only %lld bytes.\n",
		       (long long)len - sizeof(header));
		return -1;
	}
#ifdef HAVE_LIBZ
	crc = crc32(0, (void *)buf + sizeof(header), be32_to_cpu(header.ih_size));
	if (crc != be32_to_cpu(header.ih_dcrc)) {
		printf("uImage: The data CRC does not match. Computed: %08x "
				"expected %08x\n", crc,
				be32_to_cpu(header.ih_dcrc));
		return -1;
	}
#endif
	return (int)header.ih_type;
}

/* 
 * To conform to the 'probe' routine in file_type struct,
 * we return :
 *  0		- If the image is valid 'type' image.
 *
 *  Now, we have to pass on the 'errors' in the image. So,
 *
 * -1		- If the image is corrupted.
 *  1		- If the image is not a uImage.
 */

int uImage_probe_kernel(const char *buf, off_t len, unsigned int arch)
{
	int type = uImage_probe(buf, len, arch);
	if (type < 0)
		return -1;

	return !(type == IH_TYPE_KERNEL || type == IH_TYPE_KERNEL_NOLOAD);
}

int uImage_probe_ramdisk(const char *buf, off_t len, unsigned int arch)
{
	int type = uImage_probe(buf, len, arch);

	if (type < 0)
		return -1;
	return !(type == IH_TYPE_RAMDISK);
}

static int uImage_gz_load(const char *buf, off_t len,
		struct Image_info *image)
{
	image->buf = zlib_decompress_buffer(buf, len, &image->len);
	if (!image->buf)
		return -1;

	return 0;
}

int uImage_load(const char *buf, off_t len, struct Image_info *image)
{
	const struct image_header *header = (const struct image_header *)buf;
	const char *img_buf = buf + sizeof(struct image_header);
	off_t img_len = be32_to_cpu(header->ih_size);

	/*
	 * Prevent loading a modified image.
	 * CRC check is perfomed only when zlib is compiled
	 * in. This check will help us to detect
	 * size related vulnerabilities. 	
	 */
 	if (img_len != (len - sizeof(struct image_header))) {
		printf("Image size doesn't match the header\n");
		return -1;
	}

	image->base = cpu_to_be32(header->ih_load);
	image->ep = cpu_to_be32(header->ih_ep);
	switch (header->ih_comp) {
	case IH_COMP_NONE:
		image->buf = img_buf;
		image->len = img_len;
		return 0;
		break;

	case IH_COMP_GZIP:
		/*
		 * uboot doesn't decompress the RAMDISK images.
		 * Comply to the uboot behaviour.
		 */
		if (header->ih_type == IH_TYPE_RAMDISK) {
			image->buf = img_buf;
			image->len = img_len;
			return 0;
		} else
			return uImage_gz_load(img_buf, img_len, image);
		break;

	default:
		return -1;
	}
}
