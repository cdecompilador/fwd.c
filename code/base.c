#define ARENA_DEFAULT_RESERVE (16 * 1024 * 1024) /* 16 MB */
#define ARENA_COMMIT_ALIGN   4096

func void
arena_init(struct Arena *arena, void *memory, usize size)
{
    arena->base_address = (u8 *)memory;
    arena->reserved     = size;
    arena->committed    = size;
    arena->used         = 0;
}

func void *
push_size(struct Arena *arena, usize size)
{
    /* Arena inicializada a {0}: reservar espacio virtual en el primer push */
    if (arena->base_address == 0)
    {
        usize reserve = ARENA_DEFAULT_RESERVE;
        arena->base_address = (u8 *)os_mem_reserve(reserve);
        ASSERT(arena->base_address);
        arena->reserved  = reserve;
        arena->committed = 0;
        arena->used      = 0;
    }

    ASSERT(arena->used + size <= arena->reserved);

    /* Commitear más páginas si hace falta */
    if (arena->used + size > arena->committed)
    {
        usize needed  = (arena->used + size) - arena->committed;
        usize aligned = (needed + ARENA_COMMIT_ALIGN - 1) & ~(ARENA_COMMIT_ALIGN - 1);
        b32 ok = os_mem_commit(arena->base_address + arena->committed, aligned);
        ASSERT(ok);
        arena->committed += aligned;
    }

    void *result  = arena->base_address + arena->used;
    arena->used  += size;
    return result;
}
