#include "kmem_cache.h"
#include "threads.h"
#include "paging.h"
#include "ioport.h"
#include "string.h"
#include "ide.h"


#define IDE_CBREG_BASE   0x1f0
#define IDE_DATA_REG     (IDE_CBREG_BASE + 0)
#define IDE_ERR_REG      (IDE_CBREG_BASE + 1)
#define IDE_FET_REG      (IDE_CBREG_BASE + 1)
#define IDE_SC_REG       (IDE_CBREG_BASE + 2)
#define IDE_LBA0_REG     (IDE_CBREG_BASE + 3)
#define IDE_LBA1_REG     (IDE_CBREG_BASE + 4)
#define IDE_LBA2_REG     (IDE_CBREG_BASE + 5)
#define IDE_LBA3_REG     (IDE_CBREG_BASE + 6)
#define IDE_DEV_REG      (IDE_CBREG_BASE + 6)
#define IDE_STATUS_REG   (IDE_CBREG_BASE + 7)
#define IDE_CMD_REG      (IDE_CBREG_BASE + 7)

#define IDE_DEVCTL_REG   0x3f6
#define IDE_ASTATUS_REG  0x3f6

/* status register bits */
#define IDE_ERR          (1 << 0)
#define IDE_IDX          (1 << 1)
#define IDE_CORR         (1 << 2)
#define IDE_DRQ          (1 << 3)
#define IDE_DSC          (1 << 4)
#define IDE_DF           (1 << 5)
#define IDE_DRDY         (1 << 6)
#define IDE_BSY          (1 << 7)

/* error register bits */
#define IDE_AMNF         (1 << 0)
#define IDE_TK0NF        (1 << 1)
#define IDE_MCR          (1 << 2)
#define IDE_ABRT         (1 << 3)
#define IDE_IDNF         (1 << 4)
#define IDE_MC           (1 << 5)
#define IDE_UNC          (1 << 6)
#define IDE_BBK          (1 << 7)

/* device control register bits */
#define IDE_NIEN         (1 << 1)
#define IDE_SRST         (1 << 2)

/* drive address register */
#define IDE_NDS0         (1 << 0)
#define IDE_NDS1         (1 << 1)
#define IDE_NHS0         (1 << 2)
#define IDE_NHS1         (1 << 3)
#define IDE_NHS2         (1 << 4)
#define IDE_NHS3         (1 << 5)
#define IDE_NWTG         (1 << 6)
#define IDE_HIZ          (1 << 7)

/* lba3 register bits */
#define IDE_DRV          (1 << 4)
#define IDE_LBA          (1 << 6)
#define IDE_RSRV         ((1 << 5) | (1 << 7))

#define IDE_CMD_WRITE_LBA48    0x34
#define IDE_CMD_READ_LBA48     0x24

#define IDE_SECTOR_SIZE        512


static struct kmem_cache *ide_bio_cache;

static DEFINE_MUTEX(ide_bio_queue_mutex);
static DEFINE_CONDITION(ide_bio_queue_condition);
static LIST_HEAD(ide_bio_queue);
static volatile int done; // Well... We are never going to use it.


static struct bio *dequeue_bio(void)
{
	struct bio *bio = 0;

	mutex_lock(&ide_bio_queue_mutex);
	while (!done && list_empty(&ide_bio_queue))
		condition_wait(&ide_bio_queue_mutex, &ide_bio_queue_condition);

	if (!list_empty(&ide_bio_queue)) {
		struct list_head *head = list_first(&ide_bio_queue);

		list_del(head);
		bio = LIST_ENTRY(head, struct bio, link);
	}
	mutex_unlock(&ide_bio_queue_mutex);

	return bio;
}

static void finish_bio(struct bio *bio, enum bio_status status)
{
	mutex_lock(&bio->mutex);
	bio->status = status;
	condition_notify_all(&bio->cond);
	mutex_unlock(&bio->mutex);
}

static int ide_wait_ready(void)
{
	int status;

	do {
		status = in8(IDE_STATUS_REG);
	} while ((status & IDE_BSY) != 0);

	return (status & (IDE_ERR | IDE_DF)) != 0;
}

static int __read_sectors_lba48(void *data, unsigned long long sector,
			size_t count)
{
	const unsigned long slo = sector & 0xfffffffful;
	const unsigned long shi = sector >> 32;

	const unsigned clo = count & 0xffu;
	const unsigned chi = count >> 8;

	ide_wait_ready();

	out8(IDE_DEVCTL_REG, IDE_NIEN);

	out8(IDE_SC_REG, chi);
	out8(IDE_LBA0_REG, shi & 0xff);
	out8(IDE_LBA1_REG, (shi >> 8) & 0xff);
	out8(IDE_LBA2_REG, (shi >> 16) & 0xff);

	out8(IDE_SC_REG, clo);
	out8(IDE_LBA0_REG, slo & 0xff);
	out8(IDE_LBA1_REG, (slo >> 8) & 0xff);
	out8(IDE_LBA2_REG, (slo >> 16) & 0xff);

	out8(IDE_DEV_REG, IDE_LBA);
	out8(IDE_CMD_REG, IDE_CMD_READ_LBA48);

	ide_wait_ready();

	uint16_t *wptr = (uint16_t *)data;
	const size_t words = IDE_SECTOR_SIZE / sizeof(*wptr);

	for (size_t i = 0; i != count; ++i) {
		for (size_t j = 0; j != words; ++j)
			*wptr++ = in16(IDE_DATA_REG);
		in8(IDE_ASTATUS_REG); // wait one PIO transfer cycle

		if (ide_wait_ready() != 0)
			return -1;
	}

	return 0;
}

static int __write_sectors_lba48(const void *data, unsigned long long sector,
			size_t count)
{
	const unsigned long slo = sector & 0xfffffffful;
	const unsigned long shi = sector >> 32;

	const unsigned clo = count & 0xffu;
	const unsigned chi = count >> 8;

	out8(IDE_DEVCTL_REG, IDE_NIEN);

	out8(IDE_SC_REG, chi);
	out8(IDE_LBA0_REG, shi & 0xff);
	out8(IDE_LBA1_REG, (shi >> 8) & 0xff);
	out8(IDE_LBA2_REG, (shi >> 16) & 0xff);

	out8(IDE_SC_REG, clo);
	out8(IDE_LBA0_REG, slo & 0xff);
	out8(IDE_LBA1_REG, (slo >> 8) & 0xff);
	out8(IDE_LBA2_REG, (slo >> 16) & 0xff);

	out8(IDE_DEV_REG, IDE_LBA);
	out8(IDE_CMD_REG, IDE_CMD_WRITE_LBA48);

	ide_wait_ready();

	const uint16_t *wptr = (uint16_t *)data;
	const size_t words = IDE_SECTOR_SIZE / sizeof(*wptr);

	for (size_t i = 0; i != count; ++i) {
		for (size_t j = 0; j != words; ++j)
			out16(IDE_DATA_REG, *wptr++);
		in8(IDE_ASTATUS_REG); // wait one PIO transfer cycle
		if (ide_wait_ready() != 0)
			return -1;
	}

	return 0;
}

static int write_sectors(const char *data, unsigned long long sector,
			size_t count)
{
	return __write_sectors_lba48(data, sector, count);
}

static int read_sectors(char *data, unsigned long long sector, size_t count)
{
	return __read_sectors_lba48(data, sector, count);
}

static void handle_bio(struct bio *bio)
{
	if (bio->map.length % IDE_SECTOR_SIZE) {
		finish_bio(bio, BIO_FAILED);
		return;
	}

	char *page = kmap(bio->map.page, 1);

	if (!page) {
		finish_bio(bio, BIO_FAILED);
		return;
	}

	const unsigned long long lba = bio->map.sector;
	const size_t count = bio->map.length / IDE_SECTOR_SIZE;
	void *data = page + bio->map.offset;
	const int rc = bio->dir == BIO_READ
				? read_sectors(data, lba, count)
				: write_sectors(data, lba, count);

	kunmap(page);

	finish_bio(bio, rc == 0 ? BIO_FINISHED : BIO_FAILED);
}

static void process_bio_queue(void *data)
{
	(void) data;

	while (!done) {
		struct bio *bio = dequeue_bio();

		if (!bio)
			continue;

		handle_bio(bio);
	}
}

struct bio *bio_alloc(void)
{
	struct bio *bio = kmem_cache_alloc(ide_bio_cache);

	if (!bio)
		return 0;

	memset(bio, 0, sizeof(*bio));
	condition_init(&bio->cond);
	mutex_init(&bio->mutex);
	list_init(&bio->link);
	bio->status = BIO_NONE;
	return bio;
}

void bio_free(struct bio *bio)
{
	kmem_cache_free(ide_bio_cache, bio);
}

void bio_submit(struct bio *bio)
{
	mutex_lock(&ide_bio_queue_mutex);
	list_add_tail(&bio->link, &ide_bio_queue);
	condition_notify(&ide_bio_queue_condition);
	mutex_unlock(&ide_bio_queue_mutex);
}

void bio_wait(struct bio *bio)
{
	mutex_lock(&bio->mutex);
	while (bio->status == BIO_NONE)
		condition_wait(&bio->mutex, &bio->cond);
	mutex_unlock(&bio->mutex);
}

void setup_ide(void)
{
	ide_bio_cache = KMEM_CACHE(struct bio);

	static unsigned long stack[512];
	struct thread *thread = create_thread(&process_bio_queue,
				0, stack, sizeof(stack));
	activate_thread(thread);

#ifdef CONFIG_IDE_TEST
	void ide_test(void);

	ide_test();
#endif /* CONFIG_IDE_TEST */
}
