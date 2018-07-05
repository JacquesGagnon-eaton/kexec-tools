#include "kexec-zlib.h"
#include "kexec.h"

#ifdef HAVE_LIBZ
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <zlib.h>

/* gzip flag byte */
#define ASCII_FLAG	0x01 /* bit 0 set: file probably ascii text */
#define HEAD_CRC	0x02 /* bit 1 set: header CRC present */
#define EXTRA_FIELD	0x04 /* bit 2 set: extra field present */
#define ORIG_NAME	0x08 /* bit 3 set: original file name present */
#define COMMENT		0x10 /* bit 4 set: file comment present */
#define RESERVED	0xE0 /* bits 5..7: reserved */

static void _gzerror(gzFile fp, int *errnum, const char **errmsg)
{
	*errmsg = gzerror(fp, errnum);
	if (*errnum == Z_ERRNO) {
		*errmsg = strerror(*errnum);
	}
}

char *zlib_decompress_file(const char *filename, off_t *r_size)
{
	gzFile fp;
	int errnum;
	const char *msg;
	char *buf;
	off_t size = 0, allocated;
	ssize_t result;

	dbgprintf("Try gzip decompression.\n");

	*r_size = 0;
	if (!filename) {
		return NULL;
	}
	fp = gzopen(filename, "rb");
	if (fp == 0) {
		_gzerror(fp, &errnum, &msg);
		dbgprintf("Cannot open `%s': %s\n", filename, msg);
		return NULL;
	}
	if (gzdirect(fp)) {
		/* It's not in gzip format */
		return NULL;
	}
	allocated = 65536;
	buf = xmalloc(allocated);
	do {
		if (size == allocated) {
			allocated <<= 1;
			buf = xrealloc(buf, allocated);
		}
		result = gzread(fp, buf + size, allocated - size);
		if (result < 0) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			_gzerror(fp, &errnum, &msg);
			dbgprintf("Read on %s of %d bytes failed: %s\n",
				  filename, (int)(allocated - size), msg);
			size = 0;
			goto fail;
		}
		size += result;
	} while(result > 0);

fail:
	result = gzclose(fp);
	if (result != Z_OK) {
		_gzerror(fp, &errnum, &msg);
		dbgprintf(" Close of %s failed: %s\n", filename, msg);
	}

	if (size > 0) {
		*r_size = size;
	} else {
		free(buf);
		buf = NULL;
	}
	return buf;
}

char *zlib_decompress_buffer(const char *buffer, off_t len, off_t *r_size)
{
	int ret;
	z_stream strm;
	unsigned int skip;
	unsigned int flags;
	unsigned char *uncomp_buf;
	unsigned int mem_alloc;

	mem_alloc = 10 * 1024 * 1024;
	uncomp_buf = malloc(mem_alloc);
	if (!uncomp_buf)
		return NULL;

	memset(&strm, 0, sizeof(strm));

	/* Skip magic, method, time, flags, os code ... */
	skip = 10;

	/* check GZ magic */
	if (buffer[0] != 0x1f || buffer[1] != 0x8b)
		return NULL;

	flags = buffer[3];
	if (buffer[2] != Z_DEFLATED || (flags & RESERVED) != 0) {
		puts ("Error: Bad gzipped data\n");
		return NULL;
	}

	if (flags & EXTRA_FIELD) {
		skip += 2;
		skip += buffer[10];
		skip += buffer[11] << 8;
	}
	if (flags & ORIG_NAME) {
		while (buffer[skip++])
			;
	}
	if (flags & COMMENT) {
		while (buffer[skip++])
			;
	}
	if (flags & HEAD_CRC)
		skip += 2;

	strm.avail_in = len - skip;
	strm.next_in = (void *)buffer + skip;

	/* - activates parsing gz headers */
	ret = inflateInit2(&strm, -MAX_WBITS);
	if (ret != Z_OK)
		return NULL;

	strm.next_out = uncomp_buf;
	strm.avail_out = mem_alloc;

	do {
		ret = inflate(&strm, Z_FINISH);
		if (ret == Z_STREAM_END)
			break;

		if (ret == Z_OK || ret == Z_BUF_ERROR) {
			void *new_buf;
			int inc_buf = 5 * 1024 * 1024;

			mem_alloc += inc_buf;
			new_buf = realloc(uncomp_buf, mem_alloc);
			if (!new_buf) {
				inflateEnd(&strm);
				free(uncomp_buf);
				return NULL;
			}

			uncomp_buf = new_buf;
			strm.next_out = uncomp_buf + mem_alloc - inc_buf;
			strm.avail_out = inc_buf;
		} else {
			printf("Error during decompression %d\n", ret);
			return NULL;
		}
	} while (1);

	inflateEnd(&strm);
	*r_size = mem_alloc - strm.avail_out;
	return (char *)uncomp_buf;
}
#else
char *zlib_decompress_file(const char *UNUSED(filename), off_t *UNUSED(r_size))
{
	return NULL;
}

char *zlib_decompress_buffer(const char *UNUSED(buffer), off_t UNUSED(len), off_t *UNUSED(r_size))
{
	return NULL;
}
#endif /* HAVE_ZLIB */
