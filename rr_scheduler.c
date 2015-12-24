#include "kmem_cache.h"
#include "threads.h"
#include "time.h"
#include "list.h"

#define RR_SCHED_SLICE 20
#define RR_MS          1000

struct rr_thread {
	struct thread thread;
	struct list_head link;
};


static struct rr_thread *RR_THREAD(struct thread *thread)
{ return (struct rr_thread *)thread; }

static struct thread *THREAD(struct rr_thread *thread)
{ return &thread->thread; }


static struct kmem_cache *rr_thread_cache;
static struct list_head rr_active_list;
static struct list_head rr_blocked_list;


static struct thread *rr_alloc_thread(void)
{
	struct rr_thread *thread = kmem_cache_alloc(rr_thread_cache);

	if (!thread)
		return 0;

	list_init(&thread->link);
	return THREAD(thread);
}

static void rr_free_thread(struct thread *thread)
{
	struct rr_thread *rr_thread = RR_THREAD(thread);

	kmem_cache_free(rr_thread_cache, rr_thread);
}

static bool rr_need_preempt(struct thread *thread)
{ return (jiffies() - thread->time) * RR_MS > RR_SCHED_SLICE * HZ; }

static struct thread *rr_next_thread(void)
{
	if (list_empty(&rr_active_list))
		return 0;

	struct list_head *link = list_first(&rr_active_list);
	struct rr_thread *thread = LIST_ENTRY(link, struct rr_thread, link);

	return THREAD(thread);
}

static void rr_activate_thread(struct thread *thread)
{ list_add_tail(&RR_THREAD(thread)->link, &rr_active_list); }

static void rr_place_thread(struct thread *thread)
{ list_del(&RR_THREAD(thread)->link); }

static void rr_preempt_thread(struct thread *thread)
{
	struct rr_thread *rr_thread = RR_THREAD(thread);

	switch (thread->state) {
	case THREAD_ACTIVE:
		list_add_tail(&rr_thread->link, &rr_active_list);
		break;
	case THREAD_BLOCKED:
		list_add_tail(&rr_thread->link, &rr_blocked_list);
		break;
	default:
		break;
	}
}


struct scheduler round_robin = {
	.alloc = rr_alloc_thread,
	.free = rr_free_thread,
	.activate = rr_activate_thread,
	.need_preempt = rr_need_preempt,
	.next = rr_next_thread,
	.place = rr_place_thread,
	.preempt = rr_preempt_thread
};

void setup_round_robin(void)
{
	rr_thread_cache = KMEM_CACHE(struct rr_thread);

	list_init(&rr_active_list);
	list_init(&rr_blocked_list);
}
