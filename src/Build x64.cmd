@echo off

call make clean
call mingw32-make profile-build ARCH=x86-64 COMP=mingw CXX=x86_64-w64-mingw32-g++ -j14
call strip stockfish.exe
call ren stockfish.exe stockfish-9-x86-64.exe

call make clean
call pause
