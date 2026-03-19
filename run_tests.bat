@echo off
setlocal enabledelayedexpansion

set PASS=0
set FAIL=0
set TOTAL=0

call .\build.bat release
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

for %%f in (test\*.c) do (
    set "name=%%~nf"
    set "expect=%%f.expect"

    if exist "!expect!" (
        set /a TOTAL+=1

        .\fwd.exe "%%f" > test\__actual.tmp 2>&1
        fc "!expect!" test\__actual.tmp > nul 2>&1

        if errorlevel 1 (
            echo FAIL: !name!
            echo   expected:
            type "!expect!"
            echo   got:
            type test\__actual.tmp
            echo.
            set /a FAIL+=1
        ) else (
            echo PASS: !name!
            set /a PASS+=1
        )
    )
)

if exist test\__actual.tmp del test\__actual.tmp

echo.
echo === Unit Tests ===
clang  -g -Wall -Wextra -Wno-unused -std=c11 -DUNIT_TEST -DDEBUG -Icode test\unit_tests.c -o test\unit_tests.exe 2>&1
if errorlevel 1 (
    echo UNIT TEST BUILD FAILED
    set /a FAIL+=1
    set /a TOTAL+=1
) else (
    set /a TOTAL+=1
    test\unit_tests.exe
    if errorlevel 1 (
        set /a FAIL+=1
    ) else (
        set /a PASS+=1
    )
)
if exist test\unit_tests.exe del test\unit_tests.exe

echo.
echo %PASS%/%TOTAL% passed, %FAIL% failed
if %FAIL% gtr 0 exit /b 1
