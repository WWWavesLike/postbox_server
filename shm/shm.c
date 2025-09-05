#include "shm.h"

#define SHM_NAME "/postbox_system"
#define CAP 4096 // 2의 거듭제곱 권장
#define MSG_SZ 65536
#define MAX_CONS 64
#define PERM_MODE 0644

_Static_assert((CAP & (CAP - 1)) == 0, "CAP must be a power of two");

extern pthread_mutex_t g_pb_mu;
extern volatile int g_stop;

#define DIE(fmt, ...)                                                                              \
	do {                                                                                           \
		fprintf(stderr, "[ERROR] " fmt " (errno=%d:%s)\n", ##__VA_ARGS__, errno, strerror(errno)); \
		exit(EXIT_FAILURE);                                                                        \
	} while (0)

static void
sem_wait_intr(sem_t *s) {
	for (;;) {
		if (sem_wait(s) == 0)
			return;
		if (errno == EINTR)
			continue;
		DIE("sem_wait failed");
	}
}

// ----- 슬롯 상태 머신 -----
typedef enum { S_EMPTY = 0,
			   S_READY = 1,
			   S_CLAIMED = 2,
			   S_DONE = 3 } slot_state_t;

// ----- 슬롯(로그 레코드) & 소비자 상태 -----
typedef struct {
	_Atomic uint64_t seq; // 준비된 순번 = write index + 1
	_Atomic int state;	  // S_READY -> S_CLAIMED -> S_DONE (재사용 시 S_READY로 재설정)
	uint32_t id;		  // 라우팅 키
	uint32_t len;		  // <= MSG_SZ
	unsigned char data[MSG_SZ];
} slot_t;

typedef struct {
	_Atomic int active;		 // 1: 사용 중
	_Atomic uint32_t sub_id; // 구독 id(단일)
	_Atomic uint64_t tail;	 // 개인 커서(다음 읽을 seq)
	sem_t items;			 // 이 소비자 전용 알림
} consumer_t;

typedef struct {
	_Atomic uint64_t head; // 다음 쓸 seq
	slot_t slots[CAP];
	consumer_t cons[MAX_CONS];
	_Atomic int initialized;
} shm_bus_t;

// ----- 공유메모리 열기/매핑/초기화 -----
static int open_or_create_shm(int *out_creator) {
	int fd = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, PERM_MODE);
	if (fd >= 0) {
		*out_creator = 1;
		if (ftruncate(fd, (off_t)sizeof(shm_bus_t)) != 0) {
			int e = errno;
			close(fd);
			shm_unlink(SHM_NAME);
			errno = e;
			DIE("ftruncate");
		}
		return fd;
	}
	if (errno != EEXIST)
		DIE("shm_open(O_EXCL)");
	*out_creator = 0;
	fd = shm_open(SHM_NAME, O_RDWR | O_CLOEXEC, PERM_MODE);
	if (fd < 0)
		DIE("shm_open");
	return fd;
}

static shm_bus_t *map_bus(int fd) {
	void *p = mmap(NULL, sizeof(shm_bus_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		DIE("mmap");
	return (shm_bus_t *)p;
}

static void init_if_needed(shm_bus_t *bus, int creator) {
	if (creator) {
		memset(bus, 0, sizeof(*bus));
		atomic_store_explicit(&bus->head, 0, memory_order_relaxed);
		for (int i = 0; i < MAX_CONS; ++i) {
			if (sem_init(&bus->cons[i].items, 1, 0) != 0)
				DIE("sem_init(items)");
			atomic_store_explicit(&bus->cons[i].active, 0, memory_order_relaxed);
			atomic_store_explicit(&bus->cons[i].sub_id, 0, memory_order_relaxed);
			atomic_store_explicit(&bus->cons[i].tail, 0, memory_order_relaxed);
		}
		for (size_t i = 0; i < CAP; ++i) {
			atomic_store_explicit(&bus->slots[i].seq, 0, memory_order_relaxed);
			atomic_store_explicit(&bus->slots[i].state, S_EMPTY, memory_order_relaxed);
		}
		atomic_thread_fence(memory_order_release);
		atomic_store_explicit(&bus->initialized, 1, memory_order_release);
		fprintf(stderr, "[creator] bus initialized\n");
		return;
	}
	while (atomic_load_explicit(&bus->initialized, memory_order_acquire) == 0) {
		struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000};
		nanosleep(&ts, NULL);
	}
}

// ----- 생산자: 게시 -----
static int bus_publish(shm_bus_t *bus, uint32_t id, const void *data, uint32_t len) {
	if (len > MSG_SZ)
		len = MSG_SZ;

	for (;;) {
		uint64_t h = atomic_load_explicit(&bus->head, memory_order_relaxed);

		// min_tail 계산(간단 구현: O(N))
		uint64_t min_tail = h;
		for (int i = 0; i < MAX_CONS; ++i) {
			if (atomic_load_explicit(&bus->cons[i].active, memory_order_acquire)) {
				uint64_t t = atomic_load_explicit(&bus->cons[i].tail, memory_order_acquire);
				if (t < min_tail)
					min_tail = t;
			}
		}
		if ((h - min_tail) >= CAP) {
			// 꽉 참 — 잠깐 대기 후 재시도
			struct timespec ts = {.tv_sec = 0, .tv_nsec = 2000000};
			nanosleep(&ts, NULL);
			continue;
		}

		size_t idx = (size_t)(h & (CAP - 1));
		slot_t *s = &bus->slots[idx];

		// payload 먼저 쓰기
		s->id = id;
		s->len = len;
		memcpy(s->data, data, len);

		// 재사용 시 상태를 READY로 갱신 → 이후 seq 공개
		//		atomic_store_explicit(&s->state, S_READY, memory_order_release);
		atomic_store_explicit(&s->seq, h + 1, memory_order_release);

		// head 증가
		atomic_store_explicit(&bus->head, h + 1, memory_order_relaxed);

		// 해당 id 구독자 깨우기(스레드/프로세스 여러 개면 모두 깨어남 → 첫 번째가 claim)
		for (int i = 0; i < MAX_CONS; ++i) {
			if (!atomic_load_explicit(&bus->cons[i].active, memory_order_acquire))
				continue;
			if (atomic_load_explicit(&bus->cons[i].sub_id, memory_order_acquire) == id) {
				if (sem_post(&bus->cons[i].items) != 0)
					DIE("sem_post(items)");
			}
		}
		return 0;
	}
}

// ----- 생산자 실행 -----
static void run_producer(struct list_head *pb, struct list_head *sq) {
	int creator = 0;
	int fd = open_or_create_shm(&creator);
	shm_bus_t *bus = map_bus(fd);
	init_if_needed(bus, creator);

	while (1) {
		pthread_mutex_lock(&g_pb_mu);
		struct list_head *nodeptr = list_pop_front(pb);
		pthread_mutex_unlock(&g_pb_mu);

		while (!g_stop) {
			letter *l = NULL;

			pthread_mutex_lock(&g_pb_mu);
			if (!list_empty(pb)) {
				l = list_pop_front_entry(pb, letter, ptr);
			}
			pthread_mutex_unlock(&g_pb_mu);

			if (!l) {
				// 비어있으면 잠깐 쉼(또는 eventfd/cond로 대기)
				struct timespec ts = {.tv_sec = 0, .tv_nsec = 500000}; // 0.5ms
				nanosleep(&ts, NULL);
				continue;
			}

			uint32_t id = l->key;
			const void *payload = l->buf;
			uint32_t len = l->bufsz;
			if (len > MSG_SZ)
				len = MSG_SZ; // 방어

			if (bus_publish(bus, id, payload, len) != 0) {
				fprintf(stderr, "publish failed (queue full)\n");
			}

			free(l); // ★ 퍼블리시 후 반드시 해제
		}
	}

	munmap(bus, sizeof(*bus));
	close(fd);
	shm_unlink(SHM_NAME);
}

void *shm_main(void *args) {
	struct list_head *pb = ((listargs *)args)->pb;
	struct list_head *sq = ((listargs *)args)->sq;

	run_producer(pb, sq);
	return NULL;
}
