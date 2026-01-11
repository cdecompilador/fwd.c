#ifndef BASE_H
#define BASE_H

#include <stdint.h>

#define func static
#define internal static
#define global_variable static

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

#define ASSERT(x) if(!(x)) { *(int volatile*)0 = 0; }

/* Minimal memset — needed because we have no CRT and compilers emit
   memset calls for struct zero-initialization like `= {0}`. */
#pragma function(memset)
void *memset(void *dest, int value, usize count)
{
    u8 *d = (u8 *)dest;
    while (count--) *d++ = (u8)value;
    return dest;
}

struct Arena
{
    u8    *base_address;
    usize  reserved;
    usize  committed;
    usize  used;
};

func void   arena_init(struct Arena *arena, void *memory, usize size);
func void  *push_size(struct Arena *arena, usize size);

#define push_array(arena, type, count) (type *)push_size(arena, sizeof(type) * (count))

#endif /* BASE_H */
