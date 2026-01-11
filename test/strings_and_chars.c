#define func static

func int
tricky(int a, int b)
{
    char *s = "a string with { braces } and ( parens )";
    char c = '{';
    char d = '}';
    /* comment with struct and func keywords */
    // another comment with { braces }
    return a + b;
}
