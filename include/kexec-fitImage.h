#ifndef __KEXEC_FITIMAGE_H__
#define __KEXEC_FITIMAGE_H__

#include <libfdt.h>
#include <sys/types.h>

struct FitImage_contents {
	const char *kernel;
	off_t kernel_size;
	const char *initrd;
	off_t initrd_size;
	const char *dtb;
	off_t dtb_size;
};

int fitImage_probe(const char *buf, off_t len, unsigned int arch);
int fitImage_load(const char *buf, off_t len, struct FitImage_contents *info);
#endif
