#ifndef __IDE_H__
#define __IDE_H__

#include "locking.h"
#include "memory.h"
#include "list.h"

enum bio_dir {
	BIO_READ,
	BIO_WRITE
};

enum bio_status {
	BIO_NONE,
	BIO_FINISHED,
	BIO_FAILED,
};

struct bio_map {
	struct page *page;
	unsigned long long sector;
	int offset;
	int length;
};

struct bio {
	struct list_head link;
	struct condition cond;
	struct mutex mutex;
	struct bio_map map;
	enum bio_dir dir;
	enum bio_status status;
};

struct bio *bio_alloc(void);
void bio_free(struct bio *bio);
void bio_submit(struct bio *bio);
void bio_wait(struct bio *bio);

void setup_ide(void);

#endif /*__IDE_H__*/
