# fwd.c (forward declarator for C language)

Generates C forward declarations **REAL FAST** from source files. Scans for `func`-annotated
functions and `struct`, `enum`, `union` definitions, outputting a header with `static`
prototypes and `typedef struct` declarations. Its intended to be used in *UNITY BUILDS*.

This project is inspired by [Vjekoslav Krajačić](https://youtu.be/bUOOaXf9qIM?si=_tB5_LObO3hDnLYN&t=1337) as
an attempt to replicate his internal tool using as much heavy **multithreading** as I could and using
his code style.

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

Running `fwd.exe example.c` outputs into stdout:

```c
typedef struct Vec2 Vec2;
static float vec2_length(struct Vec2 *v);
```

## Building

Requires `clang` on PATH, you can pass build options via `CFLAGS`.

```bat
build.bat           &:: debug build
build.bat release   &:: optimized, small binary
```

## Tests
All basic funcionality coverage and some tricky unit testing for the multithreading edge cases.
```bat
run_tests.bat
```

## Roadmap
[x] - Initial prototype
[x] - Refactor using multithreading
[x] - Unit testing multithreading
[ ] - Benchmark (against alternatives?)
[ ] - Fuzzing **(1.0.0)**
[ ] - Support for auto-exploration of files (basically implementing a basic C preprocessor and follow
`#include` directives to discover the files to analyze) **(1.1.0)**
[ ] - Support for non-unity build projects, for example to emit not only internal linkage functions,
right now for these you are intended to directly forward declare them yourself in a header (which you
would then distribute), this feature would imply generating two headers **(1.2.0)**
[ ] - Support for C++?
