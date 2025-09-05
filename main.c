#include "io/io.h"
#include "shm/shm.h"
#include "util/list.h"
#include <pthread.h>

extern int g_kickfd;
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
	uint64_t one = 1;
	if (g_kickfd >= 0) {
		(void)write(g_kickfd, &one, sizeof(one));
	}
}

static void install_signals(void) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_sigint; // SA_RESTART 미설정 → EINTR 유도
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

int main(void) {
	LIST_HEAD(postbox);
	LIST_HEAD(sendingqueue);
	//	signal(SIGINT, on_sigint);
	//	signal(SIGTERM, on_sigint);
	install_signals();
	listargs *las = malloc(sizeof(*las));
	las->pb = &postbox;
	las->sq = &sendingqueue;

	pthread_t io_thread, shm_thread;
	if (pthread_create(&io_thread, NULL, io_main, las) != 0) {
		perror("io_thread 실패");
	}
	if (pthread_create(&shm_thread, NULL, shm_main, las) != 0) {
		perror("shm_thread 실패");
	}

	pthread_join(io_thread, NULL);
	pthread_join(shm_thread, NULL);

	free(las);
	free_list(&postbox);
	free_list(&sendingqueue);
	return 0;
}
