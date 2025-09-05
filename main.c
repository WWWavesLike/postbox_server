#include "io/io.h"
#include "util/list.h"

void free_list(struct list_head *head) {
	struct list_head *pos, *n;
	list_for_each_safe(pos, n, head) {
		letter *l = container_of(pos, letter, ptr);
		list_del(&l->ptr);
		free(l);
	}
}

int main(void) {
	LIST_HEAD(postbox);
	LIST_HEAD(sending_queue);
	int err = io_main(&postbox, &sending_queue);

	free_list(&postbox);
	free_list(&sending_queue);

	return err;
}
