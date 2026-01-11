#define func static

struct Foo
{
    int a;
};

struct Bar
{
    float b;
};

func int
foo_get(struct Foo *f)
{
    return f->a;
}

func float
bar_get(struct Bar *b)
{
    return b->b;
}
