#ifndef IO_H
#define IO_H
#include "../postbox/postbox.h"
#include "../util/list.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef RING_ENTRIES
#define RING_ENTRIES 1024
#endif
#ifndef MAX_OUTSTANDING_ACCEPT
#define MAX_OUTSTANDING_ACCEPT 64
#endif
#ifndef RX_SCRATCH
#define RX_SCRATCH 8192 // 커널이 채워줄 임시 수신 버퍼 크기
#endif
#ifndef MAX_FRAME_SIZE
#define MAX_FRAME_SIZE (1u << 20) // 단일 프레임 최대 1MiB (프로토콜 보호용)
#endif
#ifndef TX_BURST_BYTES
#define TX_BURST_BYTES (256 * 1024)
#endif
#ifndef TX_BURST_ITERS
#define TX_BURST_ITERS 64 // 한 번 호출에서 최대 64개까지 처리
#endif
#ifndef CONN_FD_MAX
#define CONN_FD_MAX 65536
#endif
typedef enum { EV_ACCEPT = 1,
			   EV_RECV = 2,
			   EV_POLLWR = 3,
			   EV_KICK = 4
} ev_type;

typedef struct conn {
	int fd;

	// 누적 수신 버퍼(프레임 파싱용)
	uint8_t *rbuf;
	size_t rcap;
	size_t rsize;

	// ===== 송신(선택 사항): "애플리케이션 송신 큐"에서 꺼내 전송한다고 가정 =====
	// 아래 4개 필드는, 큐에서 pop한 버퍼를 바로 보낼 때 사용합니다.
	uint8_t *wbuf; // 전송할 버퍼(앱이 관리; 완료 후 반환/해제는 앱 정책)
	size_t wsize;  // 전송 총 길이
	size_t woff;   // 전송 진행 오프셋
	bool sending;  // 현재 SEND 진행중 여부

	// 커널이 직접 채우는 scratch 수신 버퍼(복사 후 rbuf로 누적)
	uint8_t scratch[RX_SCRATCH];
} conn_t;

typedef struct ev {
	ev_type type;
	conn_t *c; // EV_ACCEPT는 NULL
	int fd;
	struct list_head lnk;
} ev_t;

void *io_main(void *args);

#endif
