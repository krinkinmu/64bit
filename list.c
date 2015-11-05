#include "list.h"

void list_init(struct list_head *head)
{ head->next = head->prev = head; }

static void list_insert(struct list_head *new, struct list_head *prev,
			struct list_head *next)
{
	new->prev = prev;
	new->next = next;
	prev->next = new;
	next->prev = new;
}

void list_add(struct list_head *new, struct list_head *head)
{ list_insert(new, head, head->next); }

void list_add_tail(struct list_head *new, struct list_head *head)
{ list_insert(new, head->prev, head); }

static void __list_del(struct list_head *prev, struct list_head *next)
{
	prev->next = next;
	next->prev = prev;
}

void list_del(struct list_head *entry)
{ __list_del(entry->prev, entry->next); }

int list_empty(const struct list_head *head)
{ return head->next == head; }
