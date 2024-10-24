/*-SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2024 ARJANEN Loïc Jean David
 * All rights reserved.
 */

#include "test.h"
#ifdef HAVE_LZMA_H
#include <lzma.h>

/* File data */
static const char file_name[] = "file";
/* We have liblzma's lzma_crc32() so no need to rely on a handmade
 * CRC-32...but it requires a uint8_t* for its data, hence the change
 * compared to the usual */
static const uint8_t file_data1[] = {'.', ';', ':', '!', '?', ',', '"', '\'', ')', '(', '*'};
static const uint8_t file_data2[] = {'-', '/', '>', '$', '\\', '#', '@', '+', '=', '{', ']', '[', '}', '&', '<', '%'};
static const int file_perm = 00644;
static const short file_uid = 10;
static const short file_gid = 20;

/* Folder data */
static const char folder_name[] = "folder/";
static const int folder_perm = 00755;
static const short folder_uid = 30;
static const short folder_gid = 40;

static time_t now;

static void verify_write_lzma(struct archive *a)
{
	struct archive_entry *entry;

	/* Write entries. */

	/* Regular file */
	assert((entry = archive_entry_new()) != NULL);
	archive_entry_set_pathname(entry, file_name);
	archive_entry_set_mode(entry, S_IFREG | 0644);
	archive_entry_set_size(entry, sizeof(file_data1) + sizeof(file_data2));
	archive_entry_set_uid(entry, file_uid);
	archive_entry_set_gid(entry, file_gid);
	archive_entry_set_mtime(entry, now, 0);
	archive_entry_set_atime(entry, now + 3, 0);
	assertEqualIntA(a, 0, archive_write_header(a, entry));
	assertEqualIntA(a, sizeof(file_data1), archive_write_data(a, file_data1, sizeof(file_data1)));
	assertEqualIntA(a, sizeof(file_data2), archive_write_data(a, file_data2, sizeof(file_data2)));
	archive_entry_free(entry);

	/* Folder */
	assert((entry = archive_entry_new()) != NULL);
	archive_entry_set_pathname(entry, folder_name);
	archive_entry_set_mode(entry, S_IFDIR | folder_perm);
	archive_entry_set_size(entry, 0);
	archive_entry_set_uid(entry, folder_uid);
	archive_entry_set_gid(entry, folder_gid);
	archive_entry_set_mtime(entry, now, 0);
	archive_entry_set_ctime(entry, now + 5, 0);
	assertEqualIntA(a, 0, archive_write_header(a, entry));
	archive_entry_free(entry);
}

/* Quick and dirty: Read 2-byte and 4-byte integers from Zip file. */
static unsigned int
i2(const void *p_)
{
	const unsigned char *p = p_;
	return (p[0] | (p[1] << 8));
}

static unsigned int
i4(const void *p_)
{
	const unsigned char *p = p_;
	return (i2(p) | (i2(p + 2) << 16));
}

static void verify_xz_lzma(const char *buff, size_t used, uint16_t id,
	uint16_t flags)
{
	const char *buffend;
	struct archive* zip_archive;
	struct archive_entry *ae;
	char filedata[sizeof(file_data1) + sizeof(file_data2)];
	/* Misc variables */
	unsigned long crc;
	struct tm *tm;
#if defined(HAVE_LOCALTIME_R) || defined(HAVE_LOCALTIME_S)
	struct tm tmbuf;
#endif
	/* p is the pointer to walk over the central directory,
	 * q walks over the local headers, the data and the data descriptors. */
	const char *p, *q, *local_header, *extra_start;

#if defined(HAVE_LOCALTIME_S)
	tm = localtime_s(&tmbuf, &now) ? NULL : &tmbuf;
#elif defined(HAVE_LOCALTIME_R)
	tm = localtime_r(&now, &tmbuf);
#else
	tm = localtime(&now);
#endif

	/* Open archive from memory, we'll need it for checking the file
	 * value */
	assert((zip_archive = archive_read_new()) != NULL);
	assertEqualIntA(zip_archive, ARCHIVE_OK, archive_read_support_format_all(zip_archive));
	assertEqualIntA(zip_archive, ARCHIVE_OK, archive_read_support_filter_all(zip_archive));
	assertEqualIntA(zip_archive, ARCHIVE_OK, archive_read_open_memory(zip_archive, buff, used));

	/* Remember the end of the archive in memory. */
	buffend = buff + used;

	/* Verify "End of Central Directory" record. */
	/* Get address of end-of-central-directory record. */
	p = buffend - 22; /* Assumes there is no zip comment field. */
	failure("End-of-central-directory begins with PK\\005\\006 signature");
	assertEqualMem(p, "PK\005\006", 4);
	failure("This must be disk 0");
	assertEqualInt(i2(p + 4), 0);
	failure("Central dir must start on disk 0");
	assertEqualInt(i2(p + 6), 0);
	failure("All central dir entries are on this disk");
	assertEqualInt(i2(p + 8), i2(p + 10));
	failure("CD start (%d) + CD length (%d) should == archive size - 22",
	    i4(p + 12), i4(p + 16));
	assertEqualInt(i4(p + 12) + i4(p + 16), used - 22);
	failure("no zip comment");
	assertEqualInt(i2(p + 20), 0);

	/* Get address of first entry in central directory. */
	p = buff + i4(buffend - 6);
	failure("Central file record at offset %d should begin with"
	    " PK\\001\\002 signature",
	    i4(buffend - 10));

	/* Verify file entry in central directory, except compressed size (offset 20). */
	assertEqualMem(p, "PK\001\002", 4); /* Signature */
	assertEqualInt(i2(p + 4), 3 * 256 + 63); /* Version made by */
	assertEqualInt(i2(p + 6), 63); /* Version needed to extract */
	assertEqualInt(i2(p + 8), flags); /* Flags */
	assertEqualInt(i2(p + 10), id); /* Compression method */
	assertEqualInt(i2(p + 12), (tm->tm_hour * 2048) + (tm->tm_min * 32) + (tm->tm_sec / 2)); /* File time */
	assertEqualInt(i2(p + 14), ((tm->tm_year - 80) * 512) + ((tm->tm_mon + 1) * 32) + tm->tm_mday); /* File date */
	crc = lzma_crc32(file_data1, sizeof(file_data1), 0);
	crc = lzma_crc32(file_data2, sizeof(file_data2), crc);
	assertEqualInt(i4(p + 16), crc); /* CRC-32 */
	assertEqualInt(i4(p + 24), sizeof(file_data1) + sizeof(file_data2)); /* Uncompressed size */
	assertEqualInt(i2(p + 28), strlen(file_name)); /* Pathname length */
	assertEqualInt(i2(p + 30), 24); /* Extra field length */
	assertEqualInt(i2(p + 32), 0); /* File comment length */
	assertEqualInt(i2(p + 34), 0); /* Disk number start */
	assertEqualInt(i2(p + 36), 0); /* Internal file attrs */
	assertEqualInt(i4(p + 38) >> 16 & 01777, file_perm); /* External file attrs */
	assertEqualInt(i4(p + 42), 0); /* Offset of local header */
	assertEqualMem(p + 46, file_name, strlen(file_name)); /* Pathname */
	p = p + 46 + strlen(file_name);

	assertEqualInt(i2(p), 0x7875); /* 'ux' extension header */
	assertEqualInt(i2(p + 2), 11); /* 'ux' size */
/* TODO */
	p = p + 4 + i2(p + 2);

	assertEqualInt(i2(p), 0x5455); /* 'UT' extension header */
	assertEqualInt(i2(p + 2), 5); /* 'UT' size */
	assertEqualInt(p[4], 1); /* 'UT' flags */
	assertEqualInt(i4(p + 5), now); /* 'UT' mtime */
	p = p + 4 + i2(p + 2);

	/* Verify local header of file entry. */
	local_header = q = buff;
	assertEqualMem(q, "PK\003\004", 4); /* Signature */
	assertEqualInt(i2(q + 4), 63); /* Version needed to extract */
	assertEqualInt(i2(q + 6), flags); /* Flags: bit 3 = length-at-end (required because CRC32 is unknown) and bit 1 = EOPM (because we always write it) */
	assertEqualInt(i2(q + 8), id); /* Compression method */
	assertEqualInt(i2(q + 10), (tm->tm_hour * 2048) + (tm->tm_min * 32) + (tm->tm_sec / 2)); /* File time */
	assertEqualInt(i2(q + 12), ((tm->tm_year - 80) * 512) + ((tm->tm_mon + 1) * 32) + tm->tm_mday); /* File date */
	assertEqualInt(i4(q + 14), 0); /* CRC-32 */
	assertEqualInt(i4(q + 18), 0); /* Compressed size, must be zero because of length-at-end */
	assertEqualInt(i4(q + 22), 0); /* Uncompressed size, must be zero because of length-at-end */
	assertEqualInt(i2(q + 26), strlen(file_name)); /* Pathname length */
	assertEqualInt(i2(q + 28), 41); /* Extra field length */
	assertEqualMem(q + 30, file_name, strlen(file_name)); /* Pathname */
	extra_start = q = q + 30 + strlen(file_name);

	assertEqualInt(i2(q), 0x7875); /* 'ux' extension header */
	assertEqualInt(i2(q + 2), 11); /* 'ux' size */
	assertEqualInt(q[4], 1); /* 'ux' version */
	assertEqualInt(q[5], 4); /* 'ux' uid size */
	assertEqualInt(i4(q + 6), file_uid); /* 'Ux' UID */
	assertEqualInt(q[10], 4); /* 'ux' gid size */
	assertEqualInt(i4(q + 11), file_gid); /* 'Ux' GID */
	q = q + 4 + i2(q + 2);

	assertEqualInt(i2(q), 0x5455); /* 'UT' extension header */
	assertEqualInt(i2(q + 2), 9); /* 'UT' size */
	assertEqualInt(q[4], 3); /* 'UT' flags */
	assertEqualInt(i4(q + 5), now); /* 'UT' mtime */
	assertEqualInt(i4(q + 9), now + 3); /* 'UT' atime */
	q = q + 4 + i2(q + 2);

	assertEqualInt(i2(q), 0x6c78); /* 'xl' experimental extension header */
	assertEqualInt(i2(q + 2), 9); /* size */
	assertEqualInt(q[4], 7); /* Bitmap of fields included. */
	assertEqualInt(i2(q + 5) >> 8, 3); /* system & version made by */
	assertEqualInt(i2(q + 7), 0); /* internal file attributes */
	assertEqualInt(i4(q + 9) >> 16 & 01777, file_perm); /* external file attributes */
	q = q + 4 + i2(q + 2);

	assert(q == extra_start + i2(local_header + 28));
	q = extra_start + i2(local_header + 28);

	/* Verify data of file entry, using our own zip reader to test. */
	assertEqualIntA(zip_archive, ARCHIVE_OK, archive_read_next_header(zip_archive, &ae));
	assertEqualString("file", archive_entry_pathname(ae));
	assertEqualIntA(zip_archive, sizeof(filedata), archive_read_data(zip_archive, filedata, sizeof(filedata)));
	assertEqualMem(filedata, file_data1, sizeof(file_data1));
	assertEqualMem(filedata + sizeof(file_data1), file_data2,
		sizeof(file_data2));

	/* Skip data of file entry in q */
	while (q < buffend - 3) {
		if (memcmp(q, "PK\007\010", 4) == 0) {
			break;
		}
		q++;
	}

	/* Verify data descriptor of file entry, except compressed size (offset 8). */
	assertEqualMem(q, "PK\007\010", 4); /* Signature */
	assertEqualInt(i4(q + 4), crc); /* CRC-32 */
	assertEqualInt(i4(q + 12), sizeof(file_data1) + sizeof(file_data2)); /* Uncompressed size */
	q = q + 16;

	/* Verify folder entry in central directory. */
	assertEqualMem(p, "PK\001\002", 4); /* Signature */
	assertEqualInt(i2(p + 4), 3 * 256 + 20); /* Version made by */
	assertEqualInt(i2(p + 6), 20); /* Version needed to extract */
	assertEqualInt(i2(p + 8), 0); /* Flags */
	assertEqualInt(i2(p + 10), 0); /* Compression method */
	assertEqualInt(i2(p + 12), (tm->tm_hour * 2048) + (tm->tm_min * 32) + (tm->tm_sec / 2)); /* File time */
	assertEqualInt(i2(p + 14), ((tm->tm_year - 80) * 512) + ((tm->tm_mon + 1) * 32) + tm->tm_mday); /* File date */
	crc = 0;
	assertEqualInt(i4(p + 16), crc); /* CRC-32 */
	assertEqualInt(i4(p + 20), 0); /* Compressed size */
	assertEqualInt(i4(p + 24), 0); /* Uncompressed size */
	assertEqualInt(i2(p + 28), strlen(folder_name)); /* Pathname length */
	assertEqualInt(i2(p + 30), 24); /* Extra field length */
	assertEqualInt(i2(p + 32), 0); /* File comment length */
	assertEqualInt(i2(p + 34), 0); /* Disk number start */
	assertEqualInt(i2(p + 36), 0); /* Internal file attrs */
	assertEqualInt(i4(p + 38) >> 16 & 01777, folder_perm); /* External file attrs */
	assertEqualInt(i4(p + 42), q - buff); /* Offset of local header */
	assertEqualMem(p + 46, folder_name, strlen(folder_name)); /* Pathname */
	p = p + 46 + strlen(folder_name);

	assertEqualInt(i2(p), 0x7875); /* 'ux' extension header */
	assertEqualInt(i2(p + 2), 11); /* 'ux' size */
	assertEqualInt(p[4], 1); /* 'ux' version */
	assertEqualInt(p[5], 4); /* 'ux' uid size */
	assertEqualInt(i4(p + 6), folder_uid); /* 'ux' UID */
	assertEqualInt(p[10], 4); /* 'ux' gid size */
	assertEqualInt(i4(p + 11), folder_gid); /* 'ux' GID */
	p = p + 4 + i2(p + 2);

	assertEqualInt(i2(p), 0x5455); /* 'UT' extension header */
	assertEqualInt(i2(p + 2), 5); /* 'UT' size */
	assertEqualInt(p[4], 1); /* 'UT' flags */
	assertEqualInt(i4(p + 5), now); /* 'UT' mtime */
	p = p + 4 + i2(p + 2);

	/* Verify local header of folder entry. */
	local_header = q;
	assertEqualMem(q, "PK\003\004", 4); /* Signature */
	assertEqualInt(i2(q + 4), 20); /* Version needed to extract */
	assertEqualInt(i2(q + 6), 0); /* Flags */
	assertEqualInt(i2(q + 8), 0); /* Compression method */
	assertEqualInt(i2(q + 10), (tm->tm_hour * 2048) + (tm->tm_min * 32) + (tm->tm_sec / 2)); /* File time */
	assertEqualInt(i2(q + 12), ((tm->tm_year - 80) * 512) + ((tm->tm_mon + 1) * 32) + tm->tm_mday); /* File date */
	assertEqualInt(i4(q + 14), 0); /* CRC-32 */
	assertEqualInt(i4(q + 18), 0); /* Compressed size */
	assertEqualInt(i4(q + 22), 0); /* Uncompressed size */
	assertEqualInt(i2(q + 26), strlen(folder_name)); /* Pathname length */
	assertEqualInt(i2(q + 28), 41); /* Extra field length */
	assertEqualMem(q + 30, folder_name, strlen(folder_name)); /* Pathname */
	extra_start = q = q + 30 + strlen(folder_name);

	assertEqualInt(i2(q), 0x7875); /* 'ux' extension header */
	assertEqualInt(i2(q + 2), 11); /* 'ux' size */
	assertEqualInt(q[4], 1); /* 'ux' version */
	assertEqualInt(q[5], 4); /* 'ux' uid size */
	assertEqualInt(i4(q + 6), folder_uid); /* 'ux' UID */
	assertEqualInt(q[10], 4); /* 'ux' gid size */
	assertEqualInt(i4(q + 11), folder_gid); /* 'ux' GID */
	q = q + 4 + i2(q + 2);

	assertEqualInt(i2(q), 0x5455); /* 'UT' extension header */
	assertEqualInt(i2(q + 2), 9); /* 'UT' size */
	assertEqualInt(q[4], 5); /* 'UT' flags */
	assertEqualInt(i4(q + 5), now); /* 'UT' mtime */
	assertEqualInt(i4(q + 9), now + 5); /* 'UT' atime */
	q = q + 4 + i2(q + 2);

	assertEqualInt(i2(q), 0x6c78); /* 'xl' experimental extension header */
	assertEqualInt(i2(q + 2), 9); /* size */
	assertEqualInt(q[4], 7); /* bitmap of fields */
	assertEqualInt(i2(q + 5) >> 8, 3); /* system & version made by */
	assertEqualInt(i2(q + 7), 0); /* internal file attributes */
	assertEqualInt(i4(q + 9) >> 16 & 01777, folder_perm); /* external file attributes */
	q = q + 4 + i2(q + 2);

	assert(q == extra_start + i2(local_header + 28));
	q = extra_start + i2(local_header + 28);

	/* There should not be any data in the folder entry,
	 * so the first central directory entry should be next: */
	assertEqualMem(q, "PK\001\002", 4); /* Signature */

	/* Close archive, in case. */
	archive_read_free(zip_archive);
}

static void verify_xz_contents(const char *buff, size_t used)
{
	verify_xz_lzma(buff, used, 95, 0x8);
}

static void verify_lzma_contents(const char *buff, size_t used)
{
	verify_xz_lzma(buff, used, 14, 0xA);
}
#endif
DEFINE_TEST(test_write_format_zip_compression_lzmaxz)
{
#ifndef HAVE_LZMA_H
	skipping("This build does not include LZMA");
}
#else
	/* Buffer data */
	struct archive *a;
	char buff[100000];
	size_t used;

	/* Time data */
	now = time(NULL);

	/* Create new ZIP archive in memory without padding. */
	/* Use the setter function to use LZMA Alone compression. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_format_zip(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_zip_set_compression_lzma(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_options(a, "zip:experimental"));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_add_filter_none(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_bytes_per_block(a, 1));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_bytes_in_last_block(a, 1));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_open_memory(a, buff, sizeof(buff), &used));

	verify_write_lzma(a);

	/* Close the archive . */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
	dumpfile("constructed.zip", buff, used);

	verify_lzma_contents(buff, used);

	/* Create new ZIP archive in memory without padding. */
	/* Use compression-level=9 to check that compression
	 * levels are somewhat supported. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_format_zip(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_options(a, "zip:compression=lzma"));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_options(a, "zip:compression-level=9"));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_options(a, "zip:experimental"));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_add_filter_none(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_bytes_per_block(a, 1));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_bytes_in_last_block(a, 1));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_open_memory(a, buff, sizeof(buff), &used));

	verify_write_lzma(a);

	/* Close the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
	dumpfile("constructed.zip", buff, used);

	verify_lzma_contents(buff, used);

	/* Same song and dance, but for XZ */

	/* Create new ZIP archive in memory without padding. */
	/* Use the setter function to use XZ compression. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_format_zip(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_zip_set_compression_xz(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_options(a, "zip:experimental"));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_add_filter_none(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_bytes_per_block(a, 1));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_bytes_in_last_block(a, 1));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_open_memory(a, buff, sizeof(buff), &used));

	verify_write_lzma(a);

	/* Close the archive . */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
	dumpfile("constructed.zip", buff, used);

	verify_xz_contents(buff, used);

	/* Create new ZIP archive in memory without padding. */
	/* Use compression-level=9 to check that compression levels are
	 * somewhat supported as well as threads=2 to check the multi-threaded
	 * encoder, if available. */
	assert((a = archive_write_new()) != NULL);
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_format_zip(a));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_options(a, "zip:compression=xz"));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_options(a, "zip:compression-level=9"));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_options(a, "zip:threads=2"));
	assertEqualIntA(a, ARCHIVE_OK,
	    archive_write_set_options(a, "zip:experimental"));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_add_filter_none(a));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_bytes_per_block(a, 1));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_set_bytes_in_last_block(a, 1));
	assertEqualIntA(a, ARCHIVE_OK, archive_write_open_memory(a, buff, sizeof(buff), &used));

	verify_write_lzma(a);

	/* Close the archive. */
	assertEqualIntA(a, ARCHIVE_OK, archive_write_close(a));
	assertEqualInt(ARCHIVE_OK, archive_write_free(a));
	dumpfile("constructed.zip", buff, used);

	verify_xz_contents(buff, used);
}
#endif
