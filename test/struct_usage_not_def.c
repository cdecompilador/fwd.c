#define func static

struct Arena
{
    int x;
};

func void
use_struct(struct Arena *arena)
{
    arena->x = 42;
}
