#include "list.h"

void init_list_head(struct list_head *list) {
	list->next = list;
	list->prev = list;
}

/* 연결/해제 기본 연산 */
static inline void __list_add(struct list_head *node,
							  struct list_head *prev,
							  struct list_head *next) {
	next->prev = node;
	node->next = next;
	node->prev = prev;
	prev->next = node;
}

void list_push_front(struct list_head *node, struct list_head *head) {
	/* head 바로 뒤에 삽입 (스택/큐에서 push-front 느낌) */
	__list_add(node, head, head->next);
}
struct list_head *list_pop_front(struct list_head *head) {
	if (list_empty(head))
		return NULL;
	struct list_head *first = head->next;
	list_del(first); // 연결 해제 및 next/prev = NULL
	return first;	 // caller가 container_of로 복원
}
void *list_pop_front_container_(struct list_head *head, size_t member_offset) {
	if (list_empty(head))
		return NULL;
	struct list_head *first = head->next;
	list_del(first);
	return (void *)((char *)first - member_offset);
}
void list_push_back(struct list_head *node, struct list_head *head) {
	/* 꼬리에 삽입 (큐에서 push-back 느낌) */
	__list_add(node, head->prev, head);
}

void __list_del(struct list_head *prev, struct list_head *next) {
	next->prev = prev;
	prev->next = next;
}

void list_del(struct list_head *entry) {
	__list_del(entry->prev, entry->next);
	entry->next = entry->prev = NULL; // use-after-free 탐지에 유리
}

int list_empty(const struct list_head *head) {
	return head->next == head;
}
