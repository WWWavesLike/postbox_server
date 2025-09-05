#include "io/io.h"
#include "shm/shm.h"
#include "util/list.h"
#include <pthread.h>

volatile sig_atomic_t g_stop = 0;
void free_list(struct list_head *head) {
	struct list_head *pos, *n;
	list_for_each_safe(pos, n, head) {
		letter *l = container_of(pos, letter, ptr);
		list_del(&l->ptr);
		free(l);
	}
}
void on_sigint(int s) {
	(void)s;
	g_stop = 1;
}
int main(void) {
	LIST_HEAD(postbox);
	LIST_HEAD(sendingqueue);
	signal(SIGINT, on_sigint);
	signal(SIGTERM, on_sigint);

	listargs *las = malloc(sizeof(*las));
	las->pb = &postbox;
	las->sq = &sendingqueue;

	pthread_t io_thread;
	if (pthread_create(&io_thread, NULL, io_main, las) != 0) {
		perror("io_thread 실패");
	}

	pthread_t shm_thread;
	if (pthread_create(&shm_thread, NULL, shm_main, las) != 0) {
		perror("shm_thread 실패");
	}

	pthread_join(io_thread, NULL);
	pthread_join(shm_thread, NULL);

	while (!g_stop) {
	}
	free(las);
	free_list(&postbox);
	free_list(&sendingqueue);
	return 0;
}
