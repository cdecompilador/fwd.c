@echo off

if "%1"=="release" (
    clang %CFLAGS% -Os -Wall -Wextra ^
        -fno-asynchronous-unwind-tables -fno-ident ^
        -Icode ^
        -lkernel32 -luser32 ^
        -Wl,/ENTRY:main,/SUBSYSTEM:CONSOLE,/MERGE:.rdata=.text,/MERGE:.pdata=.text,/OPT:REF,/OPT:ICF ^
        code/fwd.c -o fwd.exe
) else (
    clang %CFLAGS% -g -Wall -Wextra ^
        -Icode ^
        -lkernel32 -luser32 ^
        -Wl,/ENTRY:main,/SUBSYSTEM:CONSOLE,/DEBUG,/PDB:fwd.pdb ^
        -DFWD_DEBUG ^
        code/fwd.c -o fwd.exe
)
