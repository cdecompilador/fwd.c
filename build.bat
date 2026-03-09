@echo off

if "%1"=="release" (
    clang %CFLAGS% -Os -Wall -Wextra -std=c11 ^
        -fno-asynchronous-unwind-tables -fno-ident ^
        -Icode ^
        -lkernel32 -luser32 ^
        -Wl,/ENTRY:main,/SUBSYSTEM:CONSOLE,/MERGE:.rdata=.text,/MERGE:.pdata=.text,/OPT:REF,/OPT:ICF ^
        code/win32_fwd.c -o fwd.exe
) else (
    clang %CFLAGS% -g -Wall -Wextra -std=c11 ^
        -Icode ^
        -lkernel32 -luser32 ^
        -Wl,/ENTRY:main,/SUBSYSTEM:CONSOLE,/DEBUG,/PDB:fwd.pdb ^
        -DDEBUG ^
        code/win32_fwd.c -o fwd.exe
)
