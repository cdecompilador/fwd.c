# fwd.c

Generates C forward declarations from source files. Scans for `func`-annotated
functions and `struct` definitions, outputting a self-header with `static`
prototypes and `typedef struct` declarations.

## Usage

```
fwd.exe <file.c> [file.c ...]
```

Given a file like:

```c
#define func static

struct Vec2
{
    float x;
    float y;
};

func float
vec2_length(struct Vec2 *v)
{
    return sqrt(v->x * v->x + v->y * v->y);
}
```

Running `fwd.exe example.c` outputs:

```c
typedef struct Vec2 Vec2;
static float vec2_length(struct Vec2 *v);
```

## Building

Requires `clang` on PATH.

```bat
build.bat           &:: debug build
build.bat release   &:: optimized, small binary
```

For 32-bit builds, set `CFLAGS` before building:

```bat
set CFLAGS=-m32 -target i686-pc-windows-msvc
build.bat release
```

## Tests

```bat
run_tests.bat
```
