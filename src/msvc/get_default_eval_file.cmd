:: This batch file will parse "evaluate.h" in the parent directory and download the default evaluation file specified in it.
:: If called with a parameter, it will also copy the downloaded *.nnue file to the directory provided in the parameter.
:: This could be the directory where stockfish.exe is located, so the default *.nnue is always loaded when stockfish is started.

@echo off

cd /D "%~dp0"
set nnuefile=""

for /f "tokens=1-3 delims= " %%a in (..\evaluate.h) do (
    if "%%a"=="#define" (
        if "%%b"=="EvalFileDefaultName" set nnuefile=%%c
    )
)

:: Remove double quotes, if any.
set nnuefile=%nnuefile:"=%

if "%nnuefile%"=="" (
    echo #define EvalFileDefaultName was not found in evaluate.h!
) else (
    if exist %nnuefile% (
        echo Download of %nnuefile% skipped because the file already exists.
    ) else (
        echo Downloading https://tests.stockfishchess.org/api/nn/%nnuefile%...
        powershell -Command "(New-Object Net.WebClient).DownloadFile('https://tests.stockfishchess.org/api/nn/%nnuefile%', '%nnuefile%')"
    )
    if not "%~1"=="" (
        echo Copying %nnuefile% to stockfish.exe directory...
        copy /B /Y %nnuefile% %1
    )
)
