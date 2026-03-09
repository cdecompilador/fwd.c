#define big_arena_default_reserve mb(16)

#ifdef DEBUG
void *g_big_arena_base_address = (void*)0xFFFF00;
#else
void *g_big_arena_base_address = (void*)0; /* The os interprets this as random */
#endif

func void
mem_clear(void *src, usize size)
{
	assert(src != 0 && size != 0);
	for (u8 *ptr = (u8 *)src; ptr < (u8*)src + size; ptr++)
	{
#ifdef DEBUG
		*ptr = magic_debug_byte;
#else
		*ptr = 0;
#endif
	}
}

func void
mem_zero(void *src, usize size)
{
	assert(src != 0 && size != 0);
	for (u8 *ptr = (u8 *)src; ptr < (u8*)src + size; ptr++)
		*ptr = 0;
}

func b32
mem_match(void *_a, void *_b, usize len)
{
	u8 *a = (u8 *)_a;
	u8 *b = (u8 *)_b;
	for (usize i = 0; i < len; i++)
	{
		if (a[i] != b[i])
			return 0;
	}

	return 1;
}

func usize
align_forward_usize(usize value, usize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

func void * 
align_forward_ptr(void *raw_ptr, usize alignment)
{
	return (void *)align_forward_usize((usize)raw_ptr, alignment);
}

func struct Arena
init_big_arena()
{
	struct Arena result = {0};

	/* NOTE(cdecompilador): Reserve the big chunk of memory and commit the first page */
	u8 *base = (u8 *)os_mem_reserve(g_big_arena_base_address, big_arena_default_reserve);

	os_mem_commit(base, g_page_size);
	result.base_address = base;
	result.reserved		= big_arena_default_reserve;
	result.committed    = g_page_size;

	/* NOTE(cdecompilador): If in debug mode and we have a fixed base address we increment it
	 * such that the next large arena doen't try to use it */
#ifdef DEBUG
	mem_clear(result.base_address, g_page_size);
	if (g_big_arena_base_address != 0) g_big_arena_base_address += big_arena_default_reserve;
#endif

	return result;
}

func void *
_arena_push(struct Arena *arena, usize size, usize alignment, b32 clear_to_zero)
{
	b32 is_big_arena = arena->reserved != arena->committed;

    usize aligned_used = align_forward_usize(arena->used, alignment);
    assert(aligned_used + size <= arena->reserved);

	if (is_big_arena)
	{
		/* Commitear más páginas si hace falta */
    	if (aligned_used + size > arena->committed)
    	{
    	    usize needed  = (aligned_used + size) - arena->committed;
    	    usize aligned_commit = align_forward_usize(needed, g_page_size);
    	    os_mem_commit(arena->base_address + arena->committed, aligned_commit);
#ifdef DEBUG
			mem_clear(arena->base_address + arena->committed, aligned_commit);
#endif
    	    arena->committed += aligned_commit;
    	}
	}
	else
	{
		if (aligned_used + size > arena->committed)
		{
			/* TODO(cdecompilador): incluir más información */
			os_con_write("Arena overflow\n");
			os_exit(1);
		}
	}

    void *result  = arena->base_address + aligned_used;
    arena->used   = aligned_used + size;

    if (clear_to_zero)
    {
        mem_zero(result, size);
    }

    return result;
}

func void
arena_clear(struct Arena *arena)
{
    arena->used = 0;
}

func struct Temp_Arena
temp_arena_get(struct Arena *arena)
{
    struct Temp_Arena ta;
    ta.arena      = arena;
    ta.saved_used = arena->used;
    return ta;
}

func void
temp_arena_release(struct Temp_Arena ta)
{
#ifdef DEBUG
	mem_clear(ta.arena->base_address + ta.saved_used, ta.arena->used - ta.saved_used);
#endif
    ta.arena->used = ta.saved_used;
}

func char *
push_string(struct Arena *arena, char *str)
{
	usize str_len = 0;
	char *ptr = str;
	while (*ptr != '\0')
	{
		str_len++;
		ptr++;
	}

	char *target_buf = push_array(arena, char, str_len);

	memcpy(target_buf, str, str_len);

	return target_buf;
}

/* Thread-local scratch arena system */
#define scratch_arena_count 4
#define scratch_arena_size mb(4)
#define max_broadcast_size kb(16)

local_variable struct Arena _l_scratch_arenas[scratch_arena_count];
local_variable b32 _l_in_use_arenas[scratch_arena_count];
local_variable u32 l_lane_index = !0;
global_variable void *g_broadcast_memory;
global_variable OS_Barrier g_barrier;
global_variable u32 g_lane_count;

#define get_scratch()			 temp_arena_get(tc_get_scratch_arena())
#define release_scratch(scratch) do { temp_arena_release(scratch); tc_release_scratch_arena(scratch.arena); } while (0)

#define lane_index() l_lane_index
#define lane_sync() tc_lane_barrier_wait(0, 0, 0)
#define lane_count() g_lane_count
#define lane_sync_u64(ptr, src_lane_index) tc_lane_barrier_wait((ptr), sizeof(*(ptr)), (src_lane_index))

/* NOTE(cdecompilador): useful function to distribute splitable loads of work between lanes */
func void lane_range(usize work_values_count, usize *range_start, usize *range_end)
{
	usize values_per_lane = work_values_count / lane_count();
	usize leftover_values_count = work_values_count % lane_count();
	b32 lane_has_leftover = lane_index() < leftover_values_count;
	usize leftovers_before_this_lane_index = (lane_has_leftover ? lane_index() : leftover_values_count);
	usize lane_first_value_index = (values_per_lane * lane_index() + leftovers_before_this_lane_index);
	usize lane_last_value_index = lane_first_value_index + values_per_lane + !!lane_has_leftover;

	*range_start = lane_first_value_index;
	*range_end = lane_last_value_index;
}

func void
tc_initialize(u32 lane_index)
{
    /* NOTE(cdecompilador): Allocate first arena and broadcast memory on it, the rest
	 * will be lazy initialized */
    struct Arena bootstrap = {0};
	bootstrap.committed = scratch_arena_size;
	bootstrap.reserved  = scratch_arena_size;
	bootstrap.base_address = os_mem_reserve(0, scratch_arena_size);
	os_mem_commit(bootstrap.base_address, scratch_arena_size);
    _l_scratch_arenas[0] = bootstrap;

	l_lane_index = lane_index;
}

func struct Arena *
tc_get_scratch_arena()
{
	for (int arena_index = 0; arena_index < scratch_arena_count; arena_index++)
	{
		if (!_l_in_use_arenas[arena_index]) {
			if (_l_scratch_arenas[arena_index].base_address == 0)
			{
				struct Arena arena = {0};
				arena.committed = scratch_arena_size;
				arena.reserved  = scratch_arena_size;
				arena.base_address = os_mem_reserve(0, scratch_arena_size);
				os_mem_commit(arena.base_address, scratch_arena_size);
				_l_scratch_arenas[arena_index] = arena;
			}

			_l_in_use_arenas[arena_index] = 1;
			return &_l_scratch_arenas[arena_index];
		}
	}

#ifdef DEBUG
	/* TODO(cdecompilador): Include the thread name and more information */
	os_con_write("Limit of scratch arenas reached\n");
	os_exit(1);
#endif

	return &_l_scratch_arenas[0];
}

func void
tc_release_scratch_arena(struct Arena *scratch)
{
	b32 found = 0;
	for (int arena_index = 0; arena_index < scratch_arena_count; arena_index++)
	{
		if (&_l_scratch_arenas[arena_index] == scratch) {
			_l_in_use_arenas[arena_index] = 0;
			found = 1;
			break;
		}
	}

#ifdef DEBUG
	if (!found) {
		/* TODO(cdecompilador): Include the thread name and more information */
		os_con_write("Tried to release an already released scratch\n");
		os_exit(1);
	}
#else
	(void)found;
#endif
}

func void
tc_lane_barrier_wait(void *broadcast_ptr, u64 broadcast_size, u64 broadcast_src_lane_index)
{
	assert(broadcast_size <= max_broadcast_size);

	/* NOTE(cdecompilador): If THIS THREAD is doing the broadcast we copy -> g_broadcast */
	if (broadcast_ptr != 0 && l_lane_index == broadcast_src_lane_index)
	{
		memcpy(g_broadcast_memory, broadcast_ptr, broadcast_size);
	}

	os_barrier_wait(g_barrier);

	/* NOTE(cdecompilador): If other thread is goind the broadcast we cpy -> broadcast_ptr */
	if (broadcast_ptr != 0 && l_lane_index != broadcast_src_lane_index)
	{
		memcpy(broadcast_ptr, g_broadcast_memory, broadcast_size);
	}

	os_barrier_wait(g_barrier);
}
