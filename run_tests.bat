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
echo %PASS%/%TOTAL% passed, %FAIL% failed
if %FAIL% gtr 0 exit /b 1
