#define func static

func int
nested(int x)
{
    if (x > 0)
    {
        if (x > 10)
        {
            return 1;
        }
    }
    return 0;
}
