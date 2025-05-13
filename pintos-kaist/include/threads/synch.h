#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* 카운팅 세마포어 (Counting Semaphore). */
struct semaphore {
	unsigned value;             /* 현재 값. */
	struct list waiters;        /* 대기 중인 스레드들의 리스트. */
};

void sema_init (struct semaphore *, unsigned value); // 세마포어를 주어진 값(value)으로 초기화합니다.
void sema_down (struct semaphore *); // 세마포어 값을 감소시킵니다 (P 연산). 값이 0이면 대기합니다.
bool sema_try_down (struct semaphore *); // 세마포어 down을 시도하지만, 기다리지 않습니다. 성공 시 true 반환.
void sema_up (struct semaphore *); // 세마포어 값을 증가시킵니다 (V 연산). 대기 중인 스레드가 있으면 깨웁니다.
void sema_self_test (void); // 세마포어 자체 테스트 함수.

/* 락 (Lock). */
struct lock {
	struct thread *holder;      /* 락을 보유하고 있는 스레드 (디버깅용). */
	struct semaphore semaphore; /* 접근을 제어하는 이진(binary) 세마포어. */
	                            /* (내부적으로 세마포어를 사용하여 락을 구현) */
};

void lock_init (struct lock *); // 락을 초기화합니다.
void lock_acquire (struct lock *); // 락을 획득합니다. 이미 사용 중이면 대기합니다.
bool lock_try_acquire (struct lock *); // 락 획득을 시도하지만, 기다리지 않습니다. 성공 시 true 반환.
void lock_release (struct lock *); // 락을 해제합니다.
bool lock_held_by_current_thread (const struct lock *); // 현재 스레드가 해당 락을 보유하고 있는지 확인합니다.

/* 조건 변수 (Condition Variable). */
struct condition {
	struct list waiters;        /* 특정 조건을 기다리는 스레드들의 리스트. */
};

void cond_init (struct condition *); // 조건 변수를 초기화합니다.
void cond_wait (struct condition *, struct lock *); // 연관된 락(lock)을 해제하고 조건(condition)을 기다립니다. 신호(signal)를 받으면 락을 다시 획득합니다.
void cond_signal (struct condition *, struct lock *); // 조건(condition)을 기다리는 스레드 중 하나를 깨웁니다. 락(lock)을 보유한 상태에서 호출해야 합니다.
void cond_broadcast (struct condition *, struct lock *); // 조건(condition)을 기다리는 모든 스레드를 깨웁니다. 락(lock)을 보유한 상태에서 호출해야 합니다.

/* 최적화 장벽 (Optimization Barrier).
 *
 * 컴파일러는 최적화 장벽을 가로질러 연산 순서를 재배치하지 않습니다.
 * 자세한 정보는 참조 가이드의 "최적화 장벽(Optimization Barriers)" 부분을 참조하세요.
 */
#define barrier() asm volatile ("" : : : "memory") // 메모리 연산 순서를 보장하기 위한 어셈블리 구문.

#endif /* threads/synch.h */