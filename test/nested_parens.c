#define func static

func void
callback_user(void (*callback)(int, int))
{
    callback(1, 2);
}
