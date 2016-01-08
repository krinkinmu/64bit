#include "locking.h"
#include "threads.h"

void __wait_queue_wait(struct wait_queue *queue, struct wait_head *head)
{
	head->thread = current();
	list_add_tail(&head->link, &queue->threads);
	block_thread();
}

void wait_queue_wait(struct wait_queue *queue, struct wait_head *head)
{
	const unsigned long flags = spin_lock_irqsave(&queue->lock);

	__wait_queue_wait(queue, head);
	spin_unlock_irqrestore(&queue->lock, flags);
}

static void __wait_queue_notify(struct wait_queue *queue)
{
	if (!list_empty(&queue->threads)) {
		struct list_head *ptr = list_first(&queue->threads);
		struct wait_head *wh = LIST_ENTRY(ptr, struct wait_head, link);

		list_del(ptr);
		activate_thread(wh->thread);
	}
}

void wait_queue_notify(struct wait_queue *queue)
{
	const unsigned long flags = spin_lock_irqsave(&queue->lock);

	__wait_queue_notify(queue);
	spin_unlock_irqrestore(&queue->lock, flags);
}

void wait_queue_notify_all(struct wait_queue *queue)
{
	LIST_HEAD(threads);
	const unsigned long flags = spin_lock_irqsave(&queue->lock);

	list_splice(&queue->threads, &threads);
	spin_unlock_irqrestore(&queue->lock, flags);

	struct list_head *head = &threads;
	struct list_head *ptr = head->next;

	for (; ptr != head; ptr = ptr->next) {
		struct wait_head *wh = LIST_ENTRY(ptr, struct wait_head, link);

		activate_thread(wh->thread);
	}
}

void mutex_lock(struct mutex *mutex)
{
	struct wait_head wait;
	const unsigned long flags = spin_lock_irqsave(&mutex->wq.lock);

	while (mutex->state == MUTEX_STATE_LOCKED)
		__wait_queue_wait(&mutex->wq, &wait);
	mutex->state = MUTEX_STATE_LOCKED;
	spin_unlock_irqrestore(&mutex->wq.lock, flags);
}

void mutex_unlock(struct mutex *mutex)
{
	const unsigned long flags = spin_lock_irqsave(&mutex->wq.lock);

	mutex->state = MUTEX_STATE_UNLOCKED;
	__wait_queue_notify(&mutex->wq);
	spin_unlock_irqrestore(&mutex->wq.lock, flags);
}
