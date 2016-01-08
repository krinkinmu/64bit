#ifndef __LOCKING_H__
#define __LOCKING_H__

#include "threads.h"
#include "list.h"

struct spinlock { void *padding; };

#define SPINLOCK_INIT(name)	{ 0 }
#define DEFINE_SPINLOCK(name) 	struct spinlock name = SPINLOCK_INIT(name)

static inline void spinlock_init(struct spinlock *lock)
{ lock->padding = 0; }

static inline void spin_lock(struct spinlock *lock)
{
	(void) lock;

	local_preempt_disable();
}

static inline void spin_unlock(struct spinlock *lock)
{
	(void) lock;

	local_preempt_enable();
}

static inline bool spin_lock_irqsave(struct spinlock *lock)
{
	(void) lock;

	return local_preempt_save();
}

static inline void spin_unlock_irqrestore(struct spinlock *lock, bool enabled)
{
	(void) lock;

	local_preempt_restore(enabled);
}


struct wait_queue {
	struct list_head threads;
	struct spinlock lock;
};

#define WAIT_QUEUE_INIT(name) {	\
	LIST_HEAD_INIT((name).threads),	\
	SPINLOCK_INIT((name).lock)	\
}
#define DEFINE_WAIT_QUEUE(name)	\
	struct wait_queue name = WAIT_QUEUE_INIT(name)

struct wait_head {
	struct list_head link;
	struct thread *thread;
};

static inline void wait_queue_init(struct wait_queue *wq)
{
	list_init(&wq->threads);
	spinlock_init(&wq->lock);
}

void __wait_queue_wait(struct wait_queue *queue, struct wait_head *head);
void wait_queue_wait(struct wait_queue *queue, struct wait_head *head);
void wait_queue_notify(struct wait_queue *queue);
void wait_queue_notify_all(struct wait_queue *queue);

#define WAIT_EVENT(wq, cond) \
	do { 								\
		struct wait_queue *__WAIT_EVENT_wq = (wq);		\
		struct wait_head __WAIT_EVEN_wh;			\
		unsigned long __WAIT_EVENT_flags;			\
									\
		__WAIT_EVENT_flags =					\
			spin_lock_irqsave(&__WAIT_EVENT_wq->lock);	\
									\
		while (!(cond))						\
			__wait_queue_wait((wq), &__WAIT_EVENT_wh);	\
									\
		spin_unlock_irqrestore(&__WAIT_EVENT_wq->lock,		\
			__WAIT_EVENT_flags);				\
	} while (0);


#define MUTEX_STATE_UNLOCKED 0
#define MUTEX_STATE_LOCKED   1

struct mutex {
	struct wait_queue wq;
	int state;
};

#define MUTEX_INIT(name) {	\
	WAIT_QUEUE_INIT(name.wq),	\
	MUTEX_STATE_UNLOCKED		\
}
#define DEFINE_MUTEX(name) struct mutex name = MUTEX_INIT(name)

static inline void mutex_init(struct mutex *mutex)
{
	wait_queue_init(&mutex->wq);
	mutex->state = MUTEX_STATE_UNLOCKED;
}

void mutex_lock(struct mutex *mutex);
void mutex_unlock(struct mutex *mutex);

#endif /*__LOCKING_H__*/
