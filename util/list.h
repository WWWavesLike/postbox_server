#ifndef LIST_H
#define LIST_H
#include <stddef.h> // offsetof
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*
 static struct task *new_node(T t, U u) {
	struct node *n = (struct node *)malloc(sizeof *node);
	if (!n) { perror("malloc"); exit(EXIT_FAILURE); }
	n->foo = t;
	n->bar = u;
	INIT_LIST_HEAD(&t->node);
	return n;
}
static void to_do_something(struct list_head *head) {
	if (list_empty(head)) {
		printf("(empty)\n");
		return;
	}
	struct list_head *pos;
	list_for_each(pos, head) {
		struct node *n = container_of(pos, struct node, list_head_var_name);
		//something...
	}
}
 */

/* -------- 공용 list_head와 핵심 매크로/함수 -------- */

struct list_head {
	struct list_head *prev, *next;
};

/* 멤버 포인터 -> 상위 구조체 포인터 복원 */
#define container_of(ptr, type, member) \
	((type *)((char *)(ptr) - offsetof(type, member)))

/* 헤드 정적 선언/초기화 */
#define LIST_HEAD(name) \
	struct list_head name = {&(name), &(name)}

/* 동적 초기화 */
void init_list_head(struct list_head *list);
void list_push_front(struct list_head *node, struct list_head *head);
void list_push_back(struct list_head *node, struct list_head *head);
void list_del(struct list_head *entry);
int list_empty(const struct list_head *head);
struct list_head *list_pop_front(struct list_head *head);
void *list_pop_front_container_(struct list_head *head, size_t member_offset);
#define list_for_each(pos, head) for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)
#define list_pop_front_entry(head, type, member) \
	((type *)list_pop_front_container_((head), offsetof(type, member)))
#define list_for_each_safe(pos, n, head)          \
	for ((pos) = (head)->next, (n) = (pos)->next; \
		 (pos) != (head);                         \
		 (pos) = (n), (n) = (pos)->next)
#endif
