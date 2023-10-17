/*
 * fitImage support for ARM64
 */

#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <image.h>
#include <kexec-fitImage.h>
#include "../../kexec.h"
#include "kexec-arm64.h"

int fitImage_arm64_probe(const char *buf, off_t len)
{
	return fitImage_probe(buf, len, IH_ARCH_ARM64);
}

int fitImage_arm64_load(int argc, char **argv, const char *buf, off_t len,
	struct kexec_info *info)
{
	struct FitImage_contents img;
	int ret;

	ret = fitImage_load(buf, len, &img);
	if (ret)
		return ret;

	info->initrd = img.initrd;
	info->initrd_size = img.initrd_size;
	info->dtb = img.dtb;
	info->dtb_size = img.dtb_size;

	return image_arm64_load(argc, argv, img.kernel, img.kernel_size, info);
}

void fitImage_arm64_usage(void)
{
	printf(
"     An ARM64 fitImage file, compressed or not, big or little endian.\n\n");
}
