#define func static

/* "func" in a comment should not match */

static int
helper(int x)
{
    return x * 2;
}

int functional(int x)
{
    return x;
}

int restructure(int x)
{
    return x;
}
