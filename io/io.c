#include "io.h"

/*
 sq에 노드 집어넣고 동시에 uring 루프 깨우기.
 안하면 계속 대기상태로 머뭄.
	list_push_back(&item->ptr, sq);
	uint64_t one = 1;
	write(g_kickfd, &one, sizeof(one));  // uring 루프 깨우기
*/
/*
static void print_list(struct list_head *head) {
	printf("== 리스트 현황 ==\n");
	if (list_empty(head)) {
		printf("(empty)\n");
		return;
	}
	struct list_head *pos;
	int n = 1;
	list_for_each(pos, head) {
letter *l = container_of(pos, letter, ptr);
printf("%d : fd=%d, \n", n++, l->cfd);
}
}
*/
pthread_mutex_t g_pb_mu = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t g_sq_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_kickfd = -1;
static conn_t *g_conn_by_fd[CONN_FD_MAX] = {0};
static letter *g_tx_cur[CONN_FD_MAX] = {0};
static uint32_t g_tx_off[CONN_FD_MAX] = {0};
static bool g_tx_wait_wr[CONN_FD_MAX] = {0};
extern volatile sig_atomic_t g_stop;
static struct list_head g_inflight;

static int set_nonblock(int fd) {
	int f = fcntl(fd, F_GETFL, 0);
	if (f < 0)
		return -1;
	return fcntl(fd, F_SETFL, f | O_NONBLOCK);
}

static int create_listen_socket(uint16_t port) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return -1;
	int on = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#ifdef SO_REUSEPORT
	setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(fd);
		return -1;
	}
	if (listen(fd, SOMAXCONN) < 0) {
		perror("listen");
		close(fd);
		return -1;
	}
	if (set_nonblock(fd) < 0) {
		perror("set_nonblock(listen)");
		close(fd);
		return -1;
	}
	return fd;
}

static int submit_tag(struct io_uring *ring, struct io_uring_sqe *sqe, ev_t *e) {
	if (!sqe) {
		if (io_uring_submit(ring) < 0)
			return -1;
		sqe = io_uring_get_sqe(ring);
		if (!sqe)
			return -1;
	}
	io_uring_sqe_set_data(sqe, e);
	int r = io_uring_submit(ring);
	return (r < 0) ? r : 0;
}

static int post_accept(struct io_uring *ring, int lfd) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		if (io_uring_submit(ring) < 0)
			return -1;
		sqe = io_uring_get_sqe(ring);
		if (!sqe)
			return -1;
	}
	ev_t *e = malloc(sizeof(*e));
	if (!e)
		return -1;
	e->type = EV_ACCEPT;
	e->c = NULL;
	list_push_back(&e->lnk, &g_inflight);
	io_uring_prep_accept(sqe, lfd, NULL, NULL, 0);
	int rc = submit_tag(ring, sqe, e);
	if (rc < 0) { // ★ 실패 시 반드시 회수
		list_del(&e->lnk);
		free(e);
	}
	return rc;
}
static int post_kick_read(struct io_uring *ring) {
	static uint64_t kick_buf; // 읽기 버퍼(8바이트)
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		if (io_uring_submit(ring) < 0)
			return -1;
		sqe = io_uring_get_sqe(ring);
		if (!sqe)
			return -1;
	}
	ev_t *e = malloc(sizeof(*e));
	if (!e)
		return -1;
	e->type = EV_KICK;
	e->c = NULL;
	e->fd = g_kickfd;
	list_push_back(&e->lnk, &g_inflight);
	io_uring_prep_read(sqe, g_kickfd, &kick_buf, sizeof(kick_buf), 0);
	int rc = submit_tag(ring, sqe, e);
	if (rc < 0) { // ★ 실패 시 반드시 회수
		list_del(&e->lnk);
		free(e);
	}
	return rc;
}
static int post_recv(struct io_uring *ring, conn_t *c) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		if (io_uring_submit(ring) < 0)
			return -1;
		sqe = io_uring_get_sqe(ring);
		if (!sqe)
			return -1;
	}
	ev_t *e = malloc(sizeof(*e));
	if (!e)
		return -1;
	e->type = EV_RECV;
	e->c = c;
	list_push_back(&e->lnk, &g_inflight);
	io_uring_prep_recv(sqe, c->fd, c->scratch, sizeof(c->scratch), 0);
	int rc = submit_tag(ring, sqe, e);
	if (rc < 0) { // ★ 실패 시 반드시 회수
		list_del(&e->lnk);
		free(e);
	}
	return rc;
}
static int post_pollout(struct io_uring *ring, conn_t *c) {
	struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
	if (!sqe) {
		if (io_uring_submit(ring) < 0)
			return -1;
		sqe = io_uring_get_sqe(ring);
		if (!sqe)
			return -1;
	}
	ev_t *e = malloc(sizeof(*e));
	if (!e)
		return -1;
	e->type = EV_POLLWR; // io.h의 ev_type에 EV_POLLWR 추가되어 있어야 함
	e->c = c;
	e->fd = c->fd;
	list_push_back(&e->lnk, &g_inflight);
	io_uring_prep_poll_add(sqe, c->fd, POLLOUT | POLLERR | POLLHUP);
	int rc = submit_tag(ring, sqe, e);
	if (rc < 0) { // ★ 실패 시 반드시 회수
		list_del(&e->lnk);
		free(e);
	}
	return rc;
}

static void free_conn(conn_t *c) {
	if (!c)
		return;
	if (c->fd >= 0) {
		if (c->fd < CONN_FD_MAX) {
			if (g_tx_cur[c->fd]) {
				free(g_tx_cur[c->fd]);
				g_tx_cur[c->fd] = NULL;
			}
			g_tx_off[c->fd] = 0;
			g_tx_wait_wr[c->fd] = false;
			g_conn_by_fd[c->fd] = NULL;
		}
		close(c->fd);
	}
	free(c->rbuf);
	// 주의: c->wbuf는 애플리케이션 큐 소유라고 가정 → 여기서 free 하지 않음
	free(c);
}

static int ensure_cap(uint8_t **buf, size_t *cap, size_t need) {
	if (*cap >= need)
		return 0;
	size_t ncap = (*cap == 0) ? 4096 : *cap;
	while (ncap < need)
		ncap *= 2;
	uint8_t *p = realloc(*buf, ncap);
	if (!p)
		return -1;
	*buf = p;
	*cap = ncap;
	return 0;
}

// 디버그용: payload 전체를 16바이트 단위 헥사로 출력
static void print_hex_dump(const uint8_t *p, size_t n) {
	for (size_t i = 0; i < n; ++i) {
		if ((i % 16) == 0)
			printf("  %08zx: ", i);
		printf("%02X ", p[i]);
		if ((i % 16) == 15 || i + 1 == n)
			printf("\n");
	}
}

// 프레임 처리: printf로 길이/내용 출력
static int on_frame(conn_t *c, const uint8_t *payload, uint32_t len, struct list_head *pb) {
	printf("[fd=%d] frame %u bytes\n", c->fd, len);
	//	print_hex_dump(payload, len);
	fflush(stdout);
	if (len < 4) {
		return -1;
	}
	letter *n = create_letter(c->fd, len, payload);
	if (!n) {
		fprintf(stderr, "[fd=%d] drop frame : ENOMEM (len=%u)\n", c->fd, len);
		return 1;
	}
	pthread_mutex_lock(&g_pb_mu);
	list_push_back(&n->ptr, pb);
	pthread_mutex_unlock(&g_pb_mu);
	return 0;
}

// rbuf에서 가능한 만큼 프레임 파싱하여 on_frame 호출
static int drain_frames(conn_t *c, struct list_head *pb) {
	for (;;) {
		if (c->rsize < 4)
			break; // 헤더 부족
		uint32_t be_len;
		memcpy(&be_len, c->rbuf, 4);
		uint32_t len = ntohl(be_len);
		if (len > MAX_FRAME_SIZE)
			return -1; // 보호
		if (len < 4) {
			fprintf(stderr, "[fd=%d] protocol error: len=%u (<4)\n", c->fd, len);
			return -1;
		}
		if (c->rsize < 4u + len)
			break; // payload 부족

		// 프레임 완성 → payload 포인터
		const uint8_t *payload = c->rbuf + 4u;
		int retval = on_frame(c, payload, len, pb);

		// 소비한 만큼 앞으로 당김
		size_t remain = c->rsize - (4u + len);
		if (remain)
			memmove(c->rbuf, c->rbuf + 4u + len, remain);
		c->rsize = remain;

		if (retval < 0) {
			// -1이므로 버퍼 사이즈가 비정상적임.
			// 성공 0, 메모리부족 실패 -1
			return -1;
		}
	}
	return 0;
}

// 특정 fd의 진행 중 레터를 가능한 만큼 전송 (POLLWR 이벤트 등에서 호출)
static void tx_resume_fd(struct io_uring *ring, int fd) {
	if (fd < 0 || fd >= CONN_FD_MAX)
		return;
	conn_t *c = g_conn_by_fd[fd];
	letter *node = g_tx_cur[fd];
	if (!c || !node)
		return;

	uint32_t off = g_tx_off[fd];
	if (off > node->bufsz) {
		free(node);
		g_tx_cur[fd] = NULL;
		g_tx_off[fd] = 0;
		return;
	}

	const uint8_t *p = node->buf + off;
	size_t remain = (size_t)node->bufsz - (size_t)off;
	size_t sent_total = 0;
	int iters = 0;
	for (;;) {
		ssize_t n = send(c->fd, p, remain, MSG_NOSIGNAL);
		if (n == (ssize_t)remain) {
			// 완료
			free(node);
			g_tx_cur[fd] = NULL;
			g_tx_off[fd] = 0;
			g_tx_wait_wr[fd] = false;
			return;
		}
		if (n > 0) {
			off += (uint32_t)n;
			p += n;
			remain -= (size_t)n;
			sent_total += (size_t)n;
			if (++iters >= TX_BURST_ITERS || sent_total >= TX_BURST_BYTES) {
				g_tx_off[fd] = off; // 진행 저장 후 한 텍 돌아가도록 종료
				return;
			}
			continue;
		}
		if (n < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				// 다시 POLLOUT 대기
				if (!g_tx_wait_wr[fd]) {
					post_pollout(ring, c);
					g_tx_wait_wr[fd] = true;
				}
				g_tx_off[fd] = off;
				return;
			}
			// 기타 에러 → 연결 종료
			// 추후 개발 : postbox에 에러로그 저장기능:
			free(node);
			g_tx_cur[fd] = NULL;
			g_tx_off[fd] = 0;
			g_tx_wait_wr[fd] = false;
			free_conn(c);
			return;
		}
		// n == 0 → peer closed
		free(node);
		g_tx_cur[fd] = NULL;
		g_tx_off[fd] = 0;
		g_tx_wait_wr[fd] = false;
		free_conn(c);
		return;
	}
}

// 전역 큐에서 여러 항목을 당겨 보내기 (이벤트 처리 후에 호출)
static void tx_drain_queue_once(struct io_uring *ring,
								struct list_head *sq) {
	int iters = 0;
	while (iters++ < TX_BURST_ITERS) {

		pthread_mutex_lock(&g_sq_mu);
		if (list_empty(sq)) {
			pthread_mutex_unlock(&g_sq_mu);
			return;
		}

		letter *node = list_pop_front_entry(sq, letter, ptr);
		pthread_mutex_unlock(&g_sq_mu);

		int fd = node->cfd;
		if (fd < 0 || fd >= CONN_FD_MAX || !g_conn_by_fd[fd]) {
			free(node);
			continue;
		}

		// 이 fd에 진행 중 레터가 남아있다면(=EAGAIN 상태) 큐 앞에 되돌리고 종료
		if (g_tx_cur[fd]) {
			pthread_mutex_lock(&g_sq_mu);
			list_push_front(&node->ptr, sq);
			pthread_mutex_unlock(&g_sq_mu);
			return;
		}

		// 새로 전송 시작
		g_tx_cur[fd] = node;
		g_tx_off[fd] = 0;

		// 가능한 만큼 보냄
		tx_resume_fd(ring, fd);

		// EAGAIN이면 진행상태가 유지되고, 완료면 g_tx_cur가 NULL이 됨
	}
}
/* ============================================================= */

static inline void free_evt(ev_t *e) {
	list_del(&e->lnk);
	free(e);
}

void *io_main(void *args) {
	struct list_head *pb = ((listargs *)args)->pb;
	struct list_head *sq = ((listargs *)args)->sq;

	g_kickfd = eventfd(0, EFD_CLOEXEC);
	if (g_kickfd < 0) {
		perror("eventfd"); /* 필요 시 종료 */
	}
	init_list_head(&g_inflight);
	uint16_t port = 12345;

	int lfd = create_listen_socket(port);
	if (lfd < 0) {
		fprintf(stderr, "리스닝 소켓 생성 실패\n");
		return NULL;
	}
	printf("io_uring RX server (length-framed) listening on 0.0.0.0:%u\n", port);

	struct io_uring ring;
	int r = io_uring_queue_init(RING_ENTRIES, &ring, 0);
	if (r < 0) {
		fprintf(stderr, "io_uring_queue_init 실패: %s\n", strerror(-r));
		close(lfd);
		return NULL;
	}

	for (int i = 0; i < MAX_OUTSTANDING_ACCEPT; i++) {
		if (post_accept(&ring, lfd) < 0) {
			fprintf(stderr, "초기 ACCEPT 등록 실패\n");
			io_uring_queue_exit(&ring);
			close(lfd);
			return NULL;
		}
	}
	if (post_kick_read(&ring) < 0) {
		fprintf(stderr, "post_kick_read 초기 등록 실패\n");
		io_uring_queue_exit(&ring);
		close(lfd);
		if (g_kickfd >= 0)
			close(g_kickfd);
		return NULL;
	}
	while (!g_stop) {
		struct io_uring_cqe *cqe = NULL;
		int ret = io_uring_wait_cqe(&ring, &cqe);
		if (ret < 0) {
			if (ret == -EINTR && g_stop)
				break;
			fprintf(stderr, "io_uring_wait_cqe 실패: %s\n", strerror(-ret));
			break;
		}
		ev_t *e = (ev_t *)io_uring_cqe_get_data(cqe);
		int res = cqe->res;
		io_uring_cqe_seen(&ring, cqe);
		if (!e)
			continue;
		switch (e->type) {
		case EV_ACCEPT: {
			if (post_accept(&ring, lfd) < 0)
				fprintf(stderr, "ACCEPT 재등록 실패\n");

			if (res < 0) {
				free_evt(e);
				break;
			}
			int cfd = res;
			if (set_nonblock(cfd) < 0) {
				close(cfd);
				free_evt(e);
				break;
			}

			conn_t *c = calloc(1, sizeof(*c));
			if (!c) {
				close(cfd);
				free_evt(e);
				break;
			}
			c->fd = cfd;

			if (cfd >= 0 && cfd < CONN_FD_MAX)
				g_conn_by_fd[cfd] = c;

			if (post_recv(&ring, c) < 0) {
				free_conn(c);
			}
			free_evt(e);
			break;
		}
		case EV_RECV: {
			conn_t *c = e->c;
			if (res <= 0) {
				free_evt(e);
				free_conn(c);
				break;
			}

			size_t need = c->rsize + (size_t)res;
			if (ensure_cap(&c->rbuf, &c->rcap, need) < 0) {
				free_evt(e);
				free_conn(c);
				break;
			}
			memcpy(c->rbuf + c->rsize, c->scratch, (size_t)res);
			c->rsize += (size_t)res;

			if (drain_frames(c, pb) < 0) {
				free_evt(e);
				free_conn(c);
				break;
			}

			if (post_recv(&ring, c) < 0) {
				free_evt(e);
				free_conn(c);
				break;
			}

			free_evt(e);
			break;
		}
		case EV_POLLWR: { // 쓰기 가능 알림
			int fd = e->fd;
			if (fd >= 0 && fd < CONN_FD_MAX && g_conn_by_fd[fd]) {
				if (res < 0 || (res & (POLLERR | POLLHUP))) {
					// 에러/종료: 진행 중 레터 정리 후 연결 종료
					if (g_tx_cur[fd]) {
						free(g_tx_cur[fd]);
						g_tx_cur[fd] = NULL;
					}
					g_tx_off[fd] = 0;
					g_tx_wait_wr[fd] = false;
					free_conn(g_conn_by_fd[fd]);

				} else if (res & POLLOUT) {
					g_tx_wait_wr[fd] = false;
					tx_resume_fd(&ring, fd);
				}
			}
			free_evt(e);
			break;
		}
		case EV_KICK: {
			// 다음 킥 다시 대기
			post_kick_read(&ring);
			// 전송 큐 드레인
			tx_drain_queue_once(&ring, sq);
			free_evt(e);
			break;
		}
		default:
			free_evt(e);
			break;
		}

		// 이벤트 처리 후 전역 송신 큐를 한 번 드레인
		tx_drain_queue_once(&ring, sq);
	}
	// 1) 리스닝 소켓 닫아서 pending ACCEPT를 깨움(에러로 CQE 발생)
	close(lfd);

	// 2) 열려있는 모든 연결 종료(각종 RECV/POLL도 에러로 CQE 발생)
	for (int fd = 0; fd < CONN_FD_MAX; ++fd) {
		if (g_conn_by_fd[fd]) {
			free_conn(g_conn_by_fd[fd]); // 내부에서 g_tx_cur[]도 정리
		}
	}

	// 3) 짧게 CQE 드레인해서 남은 e를 회수 (중복 안전; in-flight 제거+free)
	struct __kernel_timespec ts = {.tv_sec = 0, .tv_nsec = 100 * 1000 * 1000}; // 100ms
	for (;;) {
		struct io_uring_cqe *cqe = NULL;
		int rc = io_uring_wait_cqe_timeout(&ring, &cqe, &ts);
		if (rc == -ETIME)
			break;
		if (rc < 0)
			break;
		ev_t *e = (ev_t *)io_uring_cqe_get_data(cqe);
		if (e) {
			list_del(&e->lnk); // ★ 혹시 남아있으면 제거
			free(e);
		}
		io_uring_cqe_seen(&ring, cqe);
	}

	struct list_head *p, *tmp;
	list_for_each_safe(p, tmp, &g_inflight) {
		ev_t *e = container_of(p, ev_t, lnk);
		list_del(p);
		free(e);
	}
	io_uring_queue_exit(&ring);
	if (g_kickfd >= 0)
		close(g_kickfd);
	printf("서버 종료\n");
	return NULL;
}
