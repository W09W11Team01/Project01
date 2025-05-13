#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* 스레드 생명 주기에서의 상태들. */
enum thread_status {
	THREAD_RUNNING,     /* 실행 중인 스레드. */
	THREAD_READY,       /* 실행 중은 아니지만 실행 준비가 된 스레드. */
	THREAD_BLOCKED,     /* 이벤트 발생을 기다리는 스레드. */
	THREAD_DYING        /* 파괴될 예정인 스레드. */
};

/* 스레드 식별자 타입.
   원하는 어떤 타입으로든 재정의할 수 있습니다. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* tid_t 타입의 오류 값. */

/* 스레드 우선순위. */
#define PRI_MIN 0                       /* 가장 낮은 우선순위. */
#define PRI_DEFAULT 31                  /* 기본 우선순위. */
#define PRI_MAX 63       
               /* 가장 높은 우선순위. */
extern int64_t MIN_alarm_time; 

/* 커널 스레드 또는 사용자 프로세스.
 *
 * 각 스레드 구조체는 자신만의 4 kB 페이지에 저장됩니다.
 * 스레드 구조체 자체는 페이지의 맨 아래(오프셋 0)에 위치합니다.
 * 페이지의 나머지 부분은 스레드의 커널 스택을 위해 예약되어 있으며,
 * 이 스택은 페이지의 맨 위(오프셋 4 kB)에서 아래쪽으로 자랍니다.
 * 아래는 그 그림입니다:
 *
 *      4 kB +---------------------------------+
 *           |          커널 스택 (kernel stack) |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         아래쪽으로 자람         |
 *           |         (grows downward)      |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              | // 스택 오버플로우 감지용 매직 넘버
 *           |            intr_frame           | // 인터럽트 발생 시 레지스터 저장 공간
 *           |                :                |
 *           |                :                |
 *           |               name              | // 스레드 이름
 *           |              status             | // 스레드 상태
 *      0 kB +---------------------------------+
 *
 * 이것의 결과는 두 가지입니다:
 *
 *    1. 첫째, `struct thread`가 너무 커지지 않도록 해야 합니다.
 *       만약 너무 커지면 커널 스택을 위한 공간이 충분하지 않게 됩니다.
 *       기본 `struct thread`는 몇 바이트 크기밖에 안 됩니다.
 *       아마도 1 kB 미만으로 유지해야 할 것입니다.
 *
 *    2. 둘째, 커널 스택이 너무 커지지 않도록 해야 합니다.
 *       만약 스택이 오버플로우되면 스레드 상태를 손상시킬 것입니다.
 *       따라서 커널 함수는 큰 구조체나 배열을 비정적(non-static)
 *       지역 변수로 할당해서는 안 됩니다. 대신 malloc()이나
 *       palloc_get_page()를 이용한 동적 할당을 사용하세요.
 *
 * 이러한 문제들의 첫 번째 증상은 아마도 thread_current()에서의
 * 어설션(assertion) 실패일 것입니다. thread_current()는 실행 중인
 * 스레드의 `struct thread`의 `magic` 멤버가 THREAD_MAGIC으로
 * 설정되어 있는지 확인합니다. 스택 오버플로우는 보통 이 값을
 * 변경하여 어설션을 트리거합니다. */
/* `elem` 멤버는 두 가지 목적을 가집니다. 실행 큐(thread.c)의
 * 요소일 수도 있고, 세마포어 대기 목록(synch.c)의 요소일 수도 있습니다.
 * 이 두 가지 방식으로 사용될 수 있는 이유는 상호 배타적이기 때문입니다:
 * 준비(ready) 상태의 스레드만 실행 큐에 있고, 블록(blocked) 상태의
 * 스레드만 세마포어 대기 목록에 있습니다. */
struct thread {
	/* thread.c가 소유함. */
	tid_t tid;                          /* 스레드 식별자. */
	enum thread_status status;          /* 스레드 상태. */
	char name[16];                      /* 이름 (디버깅 목적). */
	int priority;                       /* 우선순위. */
	int64_t time_to_wakeup; /* alarm clock */
	/* thread.c와 synch.c 간에 공유됨. */
	struct list_elem elem;              /* 리스트 요소. */

#ifdef USERPROG
	/* userprog/process.c가 소유함. */
	uint64_t *pml4;                     /* 페이지 맵 레벨 4 (페이지 테이블 포인터) */
#endif
#ifdef VM
	/* 스레드가 소유한 전체 가상 메모리를 위한 테이블. */
	struct supplemental_page_table spt; // 보조 페이지 테이블
#endif

	/* thread.c가 소유함. */
	struct intr_frame tf;               /* 컨텍스트 스위칭을 위한 정보 (레지스터 저장) */
	unsigned magic;                     /* 스택 오버플로우 감지용. */
};

/* false (기본값)이면 라운드 로빈 스케줄러 사용.
   true이면 다단계 피드백 큐(multi-level feedback queue) 스케줄러 사용.
   커널 명령줄 옵션 "-o mlfqs"로 제어됨. */
extern bool thread_mlfqs;

void thread_init (void);                // 스레딩 시스템 초기화
void thread_start (void);               // 스레딩 시스템 시작 (스케줄러 시작)

void thread_tick (void);                // 매 타이머 틱마다 호출됨 (스케줄링 관련 처리)
void thread_print_stats (void);         // 스레드 통계 출력

typedef void thread_func (void *aux);   // 스레드 함수의 타입 정의 (void* 인자 하나를 받음)
tid_t thread_create (const char *name, int priority, thread_func *, void *); // 새 커널 스레드 생성

void thread_block (void);               // 현재 실행 중인 스레드를 블록 상태로 전환
void thread_unblock (struct thread *);  // 지정된 스레드를 준비(ready) 상태로 전환




struct thread *thread_current (void);   // 현재 실행 중인 스레드의 포인터 반환
tid_t thread_tid (void);                // 현재 스레드의 TID 반환
const char *thread_name (void);         // 현재 스레드의 이름 반환

void thread_exit (void) NO_RETURN;      // 현재 스레드 종료 (NO_RETURN은 이 함수가 반환하지 않음을 명시)
void thread_yield (void);               // CPU 사용권 양보 (다른 스레드에게 실행 기회 제공)

int thread_get_priority (void);         // 현재 스레드의 (유효) 우선순위 반환
void thread_set_priority (int);         // 현재 스레드의 우선순위 설정

// MLFQS 스케줄러 관련 함수들
int thread_get_nice (void);             // 현재 스레드의 nice 값 반환
void thread_set_nice (int);             // 현재 스레드의 nice 값 설정
int thread_get_recent_cpu (void);       // 현재 스레드의 recent_cpu 값 반환 (최근 CPU 사용량 추정치)
int thread_get_load_avg (void);         // 시스템 전체의 load average 반환 (시스템 부하 평균)

void do_iret (struct intr_frame *tf);   // 인터럽트 프레임(tf)을 이용해 사용자 프로세스로 복귀 (iret 명령어 실행)

bool compare_thread_priority(const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);

void thread_preemption(void);



#endif /* threads/thread.h */