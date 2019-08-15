set PATH="c:\cygwin64\bin";%PATH%

cd ../src

uname -a

set PARAMS=OUT=so ARCH=armv7 COMP=mingw

make.exe -f Makefile objclean
make.exe -f Makefile stockfish %PARAMS% -j6
make.exe -f Makefile strip %PARAMS%

pause
