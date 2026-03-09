#include <stdint.h>
#include <intrin.h>

#define func static
#define internal static
#define global_variable static
#define local_variable _Thread_local static

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;
typedef size_t   usize;
typedef u32      b32;

#ifdef DEBUG
	#define assert(x) if(!(x)) { *(int volatile*)0 = 0; }
#else
	#define assert(x)
#endif

#define kb(value) ((value) * 1024)
#define mb(value) (kb(value) * 1024)
#define gb(value) (mb(value) * 1024)

/* NOTE(cdecompilador): I pick this number since its unlikely to be used by anything and 
 * easy to see in the debugger */
#define magic_debug_byte 0xCD

/* Minimal memset — needed because we have no CRT and compilers emit memset calls for struct 
 * zero-initialization like `= {0}`. */
#pragma function(memset)
void *memset(void *dest, int value, usize count)
{
    u8 *d = (u8 *)dest;
    while (count--) *d++ = (u8)value;
    return dest;
}

#pragma function(memcpy)
void *memcpy(void *dest, const void *src, usize count)
{
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;
    while (count--) *d++ = *s++;
    return dest;
}

func void mem_zero(void *src, usize size);
func void mem_clear(void *src, usize size);
func b32 mem_match(void *_a, void *_b, usize len);

#define carray_size(array) (sizeof(array) / sizeof((array)[0]))
/* NOTE(cdecompilador): Usually standard libraries define them even though its not standard */
#ifndef min
	#define min(a, b) ((a) < (b) ? (a) : (b)) 
#endif
#ifndef max
	#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

/* NOTE(cdecompilador): For arenas that can hold very heterogeneous data its a good pick since
 * it allows for SSE optimizations */
#define default_alignment 16

func usize align_forward_usize(usize value, usize alignment);
func void * align_forward_ptr(void *raw_ptr, usize alignment);

struct Arena
{
    usize  reserved;
    usize  committed;
    usize  used;
    u8    *base_address;
};

func void *_arena_push(struct Arena *arena, usize size, usize alignment, b32 clear_to_zero);

#define push_struct(arena, type)       (type *)_arena_push(arena, sizeof(type), default_alignment, 1)
#define push_array(arena, type, count) (type *)_arena_push(arena, (count) * sizeof(type), _Alignof(type), 1)

func void arena_clear(struct Arena *arena);

struct Temp_Arena
{
    struct Arena *arena;
    usize saved_used;
};

/* Thread-local scratch arena system */
#define scratch_arena_count 4
#define scratch_arena_size mb(4)
#define max_broadcast_size kb(16)

typedef usize OS_Barrier;

func struct Temp_Arena temp_arena_get(struct Arena *arena);
func void              temp_arena_release(struct Temp_Arena ta);
func void              tc_initialize(u32 lane_index);
func struct Arena     *tc_get_scratch_arena(void);
func void			   tc_release_scratch_arena(struct Arena *);
func void              tc_lane_barrier_wait(void *broadcast_ptr, u64 broadcast_size, u64 broadcast_src_lane_index);

#define atomic_add_i64(ptr, val) _InterlockedExchangeAdd64((volatile i64*)(ptr), (val))

func void lane_range(u64 work_values_count, u64 *range_start, u64 *range_end);

#define stack_push_n(first, node, next) \
    ((node)->next = (first), (first) = (node))
#define stack_pop_n(first, next) \
    ((first) = (first)->next)
#define stack_push(first, node) stack_push_n(first, node, next)
#define stack_pop(first)         stack_pop_n(first, next)
#define queue_push_n(first, last, node, next) ((first) == 0 ? \
    ((first) = (last) = (node), (node)->next = 0) :           \
    ((last)->next = (node), (last) = (node), (node)->next = 0))

#define queue_pop_n(first, last, next) ((first) == (last) ? \
    (first) = (last) = 0 :    \
    ((first) = (first)->next))

#define queue_push(first, last, node) queue_push_n(first, last, node, next)
#define queue_pop(first, last)          queue_pop_n(first, last, next)
