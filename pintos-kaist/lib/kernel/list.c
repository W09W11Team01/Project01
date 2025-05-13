#include "list.h"
#include "../debug.h"

/* 양방향 리스트 구현
   head는 리스트 앞에, tail은 리스트 뒤에 존재하며 실제 데이터는 그 사이에 위치함 */

static bool is_sorted (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) UNUSED; // 정렬 여부 확인 함수

/* ELEM이 head이면 true 반환 */
static inline bool is_head (struct list_elem *elem) {
	return elem != NULL && elem->prev == NULL && elem->next != NULL;
}

/* ELEM이 내부 요소면 true 반환 */
static inline bool is_interior (struct list_elem *elem) {
	return elem != NULL && elem->prev != NULL && elem->next != NULL;
}

/* ELEM이 tail이면 true 반환 */
static inline bool is_tail (struct list_elem *elem) {
	return elem != NULL && elem->prev != NULL && elem->next == NULL;
}

/* 리스트 초기화 (빈 리스트 구성) */
void list_init (struct list *list) {
	ASSERT (list != NULL);
	list->head.prev = NULL;
	list->head.next = &list->tail;
	list->tail.prev = &list->head;
	list->tail.next = NULL;
}

/* 리스트 시작 위치 반환 (실제 데이터 첫 요소) */
struct list_elem *list_begin (struct list *list) {
	ASSERT (list != NULL);
	return list->head.next;
}

/* 현재 요소 다음 반환 (끝이면 tail 반환) */
struct list_elem *list_next (struct list_elem *elem) {
	ASSERT (is_head (elem) || is_interior (elem));
	return elem->next;
}

/* 리스트의 끝 반환 (tail 반환) */
struct list_elem *list_end (struct list *list) {
	ASSERT (list != NULL);
	return &list->tail;
}

/* 역방향 순회를 위한 시작 지점 반환 (마지막 요소) */
struct list_elem *list_rbegin (struct list *list) {
	ASSERT (list != NULL);
	return list->tail.prev;
}

/* 현재 요소 이전 반환 */
struct list_elem *list_prev (struct list_elem *elem) {
	ASSERT (is_interior (elem) || is_tail (elem));
	return elem->prev;
}

/* 역방향 순회를 위한 끝 지점 반환 (head) */
struct list_elem *list_rend (struct list *list) {
	ASSERT (list != NULL);
	return &list->head;
}

/* 리스트 head 반환 */
struct list_elem *list_head (struct list *list) {
	ASSERT (list != NULL);
	return &list->head;
}

/* 리스트 tail 반환 */
struct list_elem *list_tail (struct list *list) {
	ASSERT (list != NULL);
	return &list->tail;
}

/* BEFORE 앞에 ELEM 삽입 */
void list_insert (struct list_elem *before, struct list_elem *elem) {
	
	ASSERT (is_interior (before) || is_tail (before));
	ASSERT (elem != NULL);

	elem->prev = before->prev;
	elem->next = before;
	before->prev->next = elem;
	before->prev = elem;
}

/* FIRST부터 LAST 전까지 잘라내서 BEFORE 앞에 삽입 */
void list_splice (struct list_elem *before,
			struct list_elem *first, struct list_elem *last) {
	ASSERT (is_interior (before) || is_tail (before));
	if (first == last) return;
	last = list_prev (last);

	ASSERT (is_interior (first));
	ASSERT (is_interior (last));

	first->prev->next = last->next;
	last->next->prev = first->prev;

	first->prev = before->prev;
	last->next = before;
	before->prev->next = first;
	before->prev = last;
}

/* 리스트 앞에 ELEM 삽입 */
void list_push_front (struct list *list, struct list_elem *elem) {
	list_insert (list_begin (list), elem);
}

/* 리스트 뒤에 ELEM 삽입 */
void list_push_back (struct list *list, struct list_elem *elem) {
	list_insert (list_end (list), elem);
}

/* ELEM 제거 후 다음 요소 반환 */
struct list_elem *list_remove (struct list_elem *elem) {
	ASSERT (is_interior (elem));
	elem->prev->next = elem->next;
	elem->next->prev = elem->prev;
	return elem->next;
}

/* 리스트 앞 요소 제거 후 반환 */
struct list_elem *list_pop_front (struct list *list) {
	struct list_elem *front = list_front (list);
	list_remove (front);
	return front;
}

/* 리스트 뒤 요소 제거 후 반환 */
struct list_elem *list_pop_back (struct list *list) {
	struct list_elem *back = list_back (list);
	list_remove (back);
	return back;
}

/* 리스트 첫 요소 반환 */
struct list_elem *list_front (struct list *list) {
	ASSERT (!list_empty (list));
	return list->head.next;
}

/* 리스트 마지막 요소 반환 */
struct list_elem *list_back (struct list *list) {
	ASSERT (!list_empty (list));
	return list->tail.prev;
}

/* 리스트 크기 반환 */
size_t list_size (struct list *list) {
	struct list_elem *e;
	size_t cnt = 0;
	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		cnt++;
	return cnt;
}

/* 리스트가 비었는지 여부 반환 */
bool list_empty (struct list *list) {
	return list_begin (list) == list_end (list);
}

/* 두 요소 포인터를 교환 */
static void swap (struct list_elem **a, struct list_elem **b) {
	struct list_elem *t = *a;
	*a = *b;
	*b = t;
}

/* 리스트 순서 뒤집기 */
void list_reverse (struct list *list) {
	if (!list_empty (list)) {
		struct list_elem *e;
		for (e = list_begin (list); e != list_end (list); e = e->prev)
			swap (&e->prev, &e->next);
		swap (&list->head.next, &list->tail.prev);
		swap (&list->head.next->prev, &list->tail.prev->next);
	}
}

/* 정렬되어 있는지 확인 */
static bool is_sorted (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) {
	if (a != b)
		while ((a = list_next (a)) != b)
			if (less (a, list_prev (a), aux)) return false;
	return true;
}

/* 정렬 구간의 끝 찾기 */
static struct list_elem *find_end_of_run (struct list_elem *a, struct list_elem *b,
		list_less_func *less, void *aux) {
	ASSERT (a != NULL && b != NULL && less != NULL && a != b);
	do {
		a = list_next (a);
	} while (a != b && !less (a, list_prev (a), aux));
	return a;
}

/* 두 개의 정렬 구간을 병합 */
static void inplace_merge (struct list_elem *a0, struct list_elem *a1b0,
		struct list_elem *b1,
		list_less_func *less, void *aux) {
	ASSERT (a0 != NULL && a1b0 != NULL && b1 != NULL && less != NULL);
	ASSERT (is_sorted (a0, a1b0, less, aux));
	ASSERT (is_sorted (a1b0, b1, less, aux));

	while (a0 != a1b0 && a1b0 != b1)
		if (!less (a1b0, a0, aux))
			a0 = list_next (a0);
		else {
			a1b0 = list_next (a1b0);
			list_splice (a0, list_prev (a1b0), a1b0);
		}
}

/* 리스트 정렬 수행 (병합 정렬) */
void list_sort (struct list *list, list_less_func *less, void *aux) {
	size_t output_run_cnt;
	ASSERT (list != NULL && less != NULL);
	
	do {
		struct list_elem *a0, *a1b0, *b1;
		output_run_cnt = 0;
		for (a0 = list_begin (list); a0 != list_end (list); a0 = b1) {
			output_run_cnt++;
			a1b0 = find_end_of_run (a0, list_end (list), less, aux);
			if (a1b0 == list_end (list)) break;
			b1 = find_end_of_run (a1b0, list_end (list), less, aux);
			inplace_merge (a0, a1b0, b1, less, aux);
		}
	} while (output_run_cnt > 1);
	ASSERT (is_sorted (list_begin (list), list_end (list), less, aux));
}

/* 정렬된 리스트에 elem을 적절한 위치에 삽입 */
/*list: 삽입 대상 리스트

elem: 새로 넣을 요소 (struct list_elem *)

less: 비교 함수 포인터 → 두 요소 중 누가 앞에 와야 하는지 판단

aux: 비교 함수에 넘겨줄 보조 인자 (보통 NULL을 넘김)*/
void list_insert_ordered (struct list *list, struct list_elem *elem, list_less_func *less, void *aux) 
	{
	struct list_elem *e;
	ASSERT (list != NULL && elem != NULL && less != NULL);


	for (e = list_begin (list); e != list_end (list); e = list_next (e))
		if (less (elem, e, aux)) 
			break; // less 가 어떤 기준으로 less 인지 판단을 할 수 가없음 
	return list_insert (e, elem);
}



/* 중복 요소 제거 (인접한 동일 요소 제거) */
void list_unique (struct list *list, struct list *duplicates,
		list_less_func *less, void *aux) {
	struct list_elem *elem, *next;
	ASSERT (list != NULL && less != NULL);
	if (list_empty (list)) return;
	elem = list_begin (list);
	while ((next = list_next (elem)) != list_end (list))
		if (!less (elem, next, aux) && !less (next, elem, aux)) {
			list_remove (next);
			if (duplicates != NULL)
				list_push_back (duplicates, next);
		} else elem = next;
}

/* 리스트 내 가장 큰 요소 반환 */
struct list_elem *list_max (struct list *list, list_less_func *less, void *aux) {
	struct list_elem *max = list_begin (list);
	if (max != list_end (list)) {
		struct list_elem *e;
		for (e = list_next (max); e != list_end (list); e = list_next (e))
			if (less (max, e, aux)) max = e;
	}
	return max;
}

/* 리스트 내 가장 작은 요소 반환 */
struct list_elem *list_min (struct list *list, list_less_func *less, void *aux) {
	struct list_elem *min = list_begin (list);
	if (min != list_end (list)) {
		struct list_elem *e;
		for (e = list_next (min); e != list_end (list); e = list_next (e))
			if (less (e, min, aux)) min = e;
	}
	return min;
}
