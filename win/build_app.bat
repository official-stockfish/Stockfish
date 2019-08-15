set PATH="c:\cygwin64\bin";%PATH%

cd ../src

uname -a

make.exe -f Makefile objclean
make.exe -f Makefile stockfish ARCH=x86-64 COMP=mingw -j6
make.exe -f Makefile strip COMP=mingw

pause
