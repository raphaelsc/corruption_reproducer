// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 1991, NeXT Computer, Inc.  All Rights Reserverd.
 *	Author:	Avadis Tevanian, Jr.
 *
 * Copyright (c) 1998-2001 Apple Computer, Inc. All rights reserved.
 *	Conrad Minshall <conrad@mac.com>
 *	Dave Jones <davej@suse.de>
 *	Zach Brown <zab@clusterfs.com>
 *	Joe Sokol, Pat Dirks, Clark Warner, Guy Harris
 *
 * Copyright (C) 2023 SUSE LLC Andrea Cervesato <andrea.cervesato@suse.com>
 *
 * Copyright (C) 2023 ScyllaDB inc. <raphaelsc@scylladb.com>
 */

/*\
 * [Description]
 *
 * This is a complete rewrite of the old fsx-linux tool, created by
 * NeXT Computer, Inc. and Apple Computer, Inc. between 1991 and 2001,
 * then adapted for LTP. Test is actually a file system exerciser: we bring a
 * file and randomly write operations like read/write/map read/map write and
 * truncate, according with input parameters. Then we check if all of them
 * have been completed.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <xfs/xfs.h>

const char* FNAME = "/var/lib/scylla/ltp-file.bin";

#define TINFO 0
#define TBROK 0

#define tst_brk(ttype, arg_fmt, ...) printf(arg_fmt"\n", ##__VA_ARGS__)
#define tst_res(ttype, arg_fmt, ...) printf(arg_fmt"\n", ##__VA_ARGS__)

#define SAFE_WRITE_ALL 0

#define SAFE_OPEN open
#define SAFE_FTRUNCATE ftruncate
#define SAFE_CLOSE close
#define SAFE_LSEEK lseek
#define SAFE_MALLOC(...) aligned_alloc(4096, ##__VA_ARGS__)
#define SAFE_READ pread
#define SAFE_WRITE pwrite
#define SAFE_MMAP mmap
#define SAFE_MUNMAP munmap


//

enum {
	OP_READ = 0,
	OP_WRITE,
	OP_TRUNCATE,
	OP_MAPREAD,
	OP_MAPWRITE,
	/* keep counter here */
	OP_TOTAL,
};

static char *str_file_max_size;
static char *str_op_max_size;
static char *str_op_nums;
static char *str_op_write_align;
static char *str_op_read_align;
static char *str_op_trunc_align;

static int file_desc;
static long long file_max_size = 1024 * 1024 * 1024;
static long long buffer_size = 128 * 1024;
static long long op_max_size = 64 * 1024;
static long long file_size;
static int op_write_align = 1;
static int op_read_align = 1;
static int op_trunc_align = 1;
static int op_nums = 10000;
static int page_size;

static char *file_buff;
static char *temp_buff;

struct file_pos_t {
	long long offset;
	long long size;
};

static void op_align_pages(struct file_pos_t *pos)
{
	long long pg_offset;
	long long pg_size_offset;

	pg_offset = pos->offset % buffer_size;

	pos->offset -= pg_offset;
	if (pos->offset + buffer_size >= file_max_size) {
		pos->offset = file_max_size - buffer_size;
	}
	pos->size = buffer_size;
}

static void update_file_size(struct file_pos_t const *pos)
{
	if (pos->offset + pos->size > file_size) {
		file_size = pos->offset + pos->size;

		tst_res(TINFO, "File size changed: %llu", file_size);
	}
}

static int memory_compare(
	const char *a,
	const char *b,
	const long long offset,
	const long long size)
{
	int diff;

	for (long long i = 0; i < size; i++) {
		diff = a[i] - b[i];
		if (diff) {
			tst_res(TINFO,
				"File memory differs at offset=%llu ('%c' != '%c')",
				offset + i, a[i], b[i]);
			break;
		}
	}

	return diff;
}

static int op_read(const struct file_pos_t pos)
{
	if (!file_size) {
		tst_res(TINFO, "Skipping zero size read");
		return 0;
	}

	tst_res(TINFO,
		"Reading at offset=%llu, size=%llu",
		pos.offset,
		pos.size);

	memset(temp_buff, 0, file_max_size);

	//SAFE_LSEEK(file_desc, (off_t)pos.offset, SEEK_SET);
	int i = SAFE_READ(file_desc, temp_buff, pos.size, pos.offset);
#ifdef DEBUG
	tst_res(TINFO,
		"Read %lld bytes at offset=%llu and expected=%lld",
		 i, pos.offset, pos.size);
#endif
	int ret = memory_compare(
		file_buff + pos.offset,
		temp_buff,
		pos.offset,
		pos.size);

	if (ret)
		return -1;

	return 1;
}

static int op_write(struct file_pos_t* pos)
{
	if (file_size > file_max_size) {
		tst_res(TINFO, "Skipping write");
		return 0;
	}

	char data;

	//op_file_position(file_max_size, op_write_align, pos);
	op_align_pages(pos);

	for (long long i = 0; i < pos->size; i++) {
		data = random() % 10 + 'a';

		file_buff[pos->offset + i] = data;
		temp_buff[i] = data;
	}

	tst_res(TINFO,
		"Writing at offset=%llu, size=%llu",
		pos->offset,
		pos->size);

	//SAFE_LSEEK(file_desc, (off_t)pos->offset, SEEK_SET);
	int written = SAFE_WRITE(file_desc, temp_buff, pos->size, pos->offset);
	if (written == -1) {
		printf("%s\n", strerror(errno));
	}
	fdatasync(file_desc);
#ifdef DEBUG
	tst_res(TINFO,
		"Wrote %d bytes at offset=%llu and expected=%lld",
		 written, pos->offset, pos->size);
#endif
	update_file_size(pos);

	return 1;
}

static int op_truncate(struct file_pos_t pos)
{
	//op_file_position(file_max_size, op_trunc_align, &pos);
	op_align_pages(&pos);

	int speculative_size = pos.offset + pos.size;

	if (speculative_size <= file_size){
		return 1;
	}

	if (!file_size) {
		speculative_size = 1024*1024;
	} else if (speculative_size < 2 * file_size) {
		speculative_size = (2 * file_size > file_max_size) ? file_max_size : 2 * file_size;
	}

	file_size = speculative_size;

	tst_res(TINFO, "Truncating to %llu", file_size);

	SAFE_FTRUNCATE(file_desc, file_size);
	memset(file_buff + file_size, 0, file_max_size - file_size);

	return 1;
}

static void run(void)
{
	int ret;
	int counter = 0;

	file_size = 0;

	memset(file_buff, 0, file_max_size);
	memset(temp_buff, 0, file_max_size);

	SAFE_FTRUNCATE(file_desc, 0);
	
	size_t file_offset = 0;

	while (counter < op_nums) {
		struct file_pos_t pos;
		pos.offset = file_offset;
		pos.size = buffer_size;
		
		ret = op_truncate(pos);
		if (ret == -1)
			break;
		
		ret = op_write(&pos);
		if (ret == -1)
			break;

		ret = op_read(pos);
		if (ret == -1)
			break;

		file_offset += buffer_size;
		if (file_offset + buffer_size >= file_max_size) {
			break;
			file_offset = 0;
		}
		counter += ret;
	}

	if (ret == -1) {
		tst_brk(TFAIL, "Some file operations failed");
		exit(1);
	}
	else
		tst_res(TPASS, "All file operations succeed");
}

static void setup(void)
{
#if 0
	if (tst_parse_filesize(str_file_max_size, &file_max_size, 1, LLONG_MAX))
		tst_brk(TBROK, "Invalid file size '%s'", str_file_max_size);

	if (tst_parse_filesize(str_op_max_size, &op_max_size, 1, LLONG_MAX))
		tst_brk(TBROK, "Invalid maximum size for single operation '%s'", str_op_max_size);

	if (tst_parse_int(str_op_nums, &op_nums, 1, INT_MAX))
		tst_brk(TBROK, "Invalid number of operations '%s'", str_op_nums);

	if (tst_parse_int(str_op_write_align, &op_write_align, 1, INT_MAX))
		tst_brk(TBROK, "Invalid memory write alignment factor '%s'", str_op_write_align);

	if (tst_parse_int(str_op_read_align, &op_read_align, 1, INT_MAX))
		tst_brk(TBROK, "Invalid memory read alignment factor '%s'", str_op_read_align);

	if (tst_parse_int(str_op_trunc_align, &op_trunc_align, 1, INT_MAX))
		tst_brk(TBROK, "Invalid memory truncate alignment factor '%s'", str_op_trunc_align);
#endif
	page_size = (int)sysconf(_SC_PAGESIZE);

	srandom(time(NULL));

	file_desc = SAFE_OPEN(FNAME, O_RDWR | O_CREAT | O_DIRECT, 0666);
	if (file_desc == -1) {
		printf("%s\n", strerror(errno));
	}
	
	struct fsxattr attr = {};
	attr.fsx_xflags |= XFS_XFLAG_EXTSIZE;
	attr.fsx_extsize = 32 * 1024 * 1024;

	// Ignore error; may be !xfs, and just a hint anyway
	ioctl(file_desc, XFS_IOC_FSSETXATTR, &attr);

	file_buff = SAFE_MALLOC(file_max_size);
	temp_buff = SAFE_MALLOC(file_max_size);
}

static void cleanup(void)
{
	if (file_buff)
		free(file_buff);

	if (temp_buff)
		free(temp_buff);

	if (file_desc)
		SAFE_CLOSE(file_desc);
}
#if 0
static struct tst_test test = {
	.needs_tmpdir = 1,
	.setup = setup,
	.cleanup = cleanup,
	.test_all = run,
	.options = (struct tst_option[]) {
		{ "l:", &str_file_max_size, "Maximum size in MB of the test file(s) (default 262144)" },
		{ "o:", &str_op_max_size, "Maximum size for single operation (default 65536)" },
		{ "N:", &str_op_nums, "Total # operations to do (default 1000)" },
		{ "w:", &str_op_write_align, "Write memory page alignment (default 1)" },
		{ "r:", &str_op_read_align, "Read memory page alignment (default 1)" },
		{ "t:", &str_op_trunc_align, "Truncate memory page alignment (default 1)" },
		{},
	},
};
#endif

int main(int argc, char** argv) {
	if (argc == 2) {
		FNAME = argv[1];
		printf("File: %s", FNAME);
	}
	
	setup();
	run();
	cleanup();
	
	return 0;
}

