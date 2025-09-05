#ifndef SHM_H
#define SHM_H
#include "../postbox/postbox.h"
#include "../util/list.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SHM_NAME "/postbox_system"
#define CAP 4096 // 2의 거듭제곱 권장
#define MSG_SZ 65536
#define MAX_CONS 64
#define PERM_MODE 0644

void *shm_main(void *args);
#endif
