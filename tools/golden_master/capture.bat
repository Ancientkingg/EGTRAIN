@echo off
REM EGTRAIN Golden-Master Baseline Capture Script (Windows batch)
REM
REM Runs all 4 case studies headless with a fixed seed, then copies
REM outputs to golden/<case>/ for later comparison.
REM
REM Usage: capture.bat [exe_path] [horizon]
REM   exe_path   path to QEGTRAIN_no_GUI.exe (default: ..\..\QEGTRAIN)
REM   horizon    simulation horizon in seconds (default: 2000 for fast CI)


setlocal enabledelayedexpansion

set EXE_DIR=%~1
if "%EXE_DIR%"=="" set EXE_DIR=..\..\QEGTRAIN
set EXE=%EXE_DIR%\QEGTRAIN_no_GUI.exe

set HORIZON=%~2
if "%HORIZON%"=="" set HORIZON=2000

set TOOLS_DIR=%~dp0
set GOLDEN_DIR=%TOOLS_DIR%golden
set FIXED_SEED=%TOOLS_DIR%fixed_seed.seed

if not exist "%EXE%" (
    echo ERROR: %EXE% not found
    echo Usage: capture.bat [exe_path] [horizon]
    echo   exe_path: path to directory containing QEGTRAIN_no_GUI.exe
    echo   horizon: simulation horizon in seconds (default: 2000)
    exit /b 1
)

if not exist "%FIXED_SEED%" (
    echo WARNING: fixed_seed.seed not found at %FIXED_SEED%
    echo Will not restore seed before each run - outputs may not be reproducible!
)

echo === EGTRAIN Golden-Master Baseline Capture ===
echo EXE: %EXE%
echo Horizon: %HORIZON% s
echo Golden dir: %GOLDEN_DIR%
echo.

for %%C in (1 2 3 4) do (
    echo --- Case study %%C ---

    REM Restore fixed seed before each run
    if exist "%FIXED_SEED%" copy /Y "%FIXED_SEED%" "%EXE_DIR%\rand1.seed" > nul

    REM Run simulation headless
    REM All mandatory args supplied to avoid interactive stdin prompts
    pushd "%EXE_DIR%"
    "%EXE%" -n %%C -h %HORIZON% -g 0 -TSM 0 -RC 0 > stdout_case%%C.txt 2>&1
    popd

    REM Determine output directory for this case study
    if %%C==1 set OUTDIR=%EXE_DIR%\Output\Output_EGTRAIN_Netherlands
    if %%C==2 set OUTDIR=%EXE_DIR%\Output\Output_EGTRAIN_Paimpol
    if %%C==3 set OUTDIR=%EXE_DIR%\Output\Output_EGTRAIN_Banedanmark
    if %%C==4 set OUTDIR=%EXE_DIR%\Output\Output_EGTRAIN_Milano_Brescia

    REM Create golden directory for this case
    set CASE_GOLDEN=%GOLDEN_DIR%\case%%C
    if not exist "!CASE_GOLDEN!" mkdir "!CASE_GOLDEN!"

    REM Copy output files
    if exist "!OUTDIR!" (
        xcopy /E /I /Y "!OUTDIR!" "!CASE_GOLDEN!" > nul
        echo   Copied output from !OUTDIR!
    ) else (
        echo   WARNING: Output directory not found: !OUTDIR!
    )

    REM Copy captured stdout
    if exist "%EXE_DIR%\stdout_case%%C.txt" (
        copy /Y "%EXE_DIR%\stdout_case%%C.txt" "!CASE_GOLDEN!\stdout.txt" > nul
        del "%EXE_DIR%\stdout_case%%C.txt"
        echo   Captured stdout
    )

    echo.
)

echo === Done. Baselines in %GOLDEN_DIR%/case{1,2,3,4} ===
echo To compare: python compare.py golden
