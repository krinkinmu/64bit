#include "memory.h"
#include "string.h"
#include "error.h"
#include "stdio.h"
#include "ide.h"

static int write_page(const void *data, unsigned long sector)
{
	struct page *page = alloc_pages(0, NT_LOW);

	if (!page)
		return -ENOMEM;

	struct bio *bio = bio_alloc();

	if (!bio) {
		free_pages(page, 0);
		return -ENOMEM;
	}

	const pfn_t pfn = page2pfn(page);
	void *buf = kernel_virt(pfn << PAGE_BITS);

	memcpy(buf, data, PAGE_SIZE);
	bio->dir = BIO_WRITE;
	bio->map.page = page;
	bio->map.sector = sector;
	bio->map.offset = 0;
	bio->map.length = PAGE_SIZE;

	bio_submit(bio);
	bio_wait(bio);

	const int rc = (bio->status == BIO_FINISHED ? 0 : -EIO);

	bio_free(bio);
	free_pages(page, 0);

	return rc;
}

static int read_page(void *data, unsigned long sector)
{
	struct page *page = alloc_pages(0, NT_LOW);

	if (!page)
		return -ENOMEM;

	struct bio *bio = bio_alloc();

	if (!bio) {
		free_pages(page, 0);
		return -ENOMEM;
	}

	bio->dir = BIO_READ;
	bio->map.page = page;
	bio->map.sector = sector;
	bio->map.offset = 0;
	bio->map.length = PAGE_SIZE;

	bio_submit(bio);
	bio_wait(bio);

	if (bio->status == BIO_FINISHED) {
		const pfn_t pfn = page2pfn(page);
		void *buf = kernel_virt(pfn << PAGE_BITS);

		memcpy(data, buf, PAGE_SIZE);
	}

	const int rc = (bio->status == BIO_FINISHED ? 0 : -EIO);

	bio_free(bio);
	free_pages(page, 0);

	return rc;	
}

static void generate_random_page(char *data)
{
	static int seed = 17 * 42 + 23;

	for (size_t i = 0; i != PAGE_SIZE; ++i, seed = seed * 17 + 23)
		*data++ = seed;
}

static int match_pages(const void *page0, const void *page1)
{
	const char *p0 = page0;
	const char *p1 = page1;

	for (size_t i = 0; i != PAGE_SIZE; ++i)
		if (*p0++ != *p1++)
			return 0;
	return 1;
}

void ide_test(void)
{
	const unsigned long rsecs[] = { 0, 13, 42, 56, 1024 };
	static char src[PAGE_SIZE], dst[PAGE_SIZE];

	for (size_t i = 0; i != sizeof(rsecs)/sizeof(rsecs[0]); ++i) {
		const unsigned long sector = rsecs[i];
		int rc;

		generate_random_page(src);

		rc = write_page(src, sector);
		if (rc)
			DBG_ERR("write_page failed with error %s", errstr(rc));

		read_page(dst, sector);
		if (rc)
			DBG_ERR("read_page failed with error %s", errstr(rc));

		if (!match_pages(src, dst))
			DBG_ERR("Read page doesn't match written (sector: %lu)",
						sector);
		else
			DBG_INFO("Read page matches written page (sector: %lu)",
						sector);
	}
}
