@echo on

set ANDROID_NDK_ROOT=c:\Dev\android_ndk
call %ANDROID_NDK_ROOT%\ndk-build NDK_PROJECT_PATH=. NDK_APPLICATION_MK=Application.mk all
copy libs\armeabi-v7a\libstockfish.so .\

pause

rmdir /S /Q libs
rmdir /S /Q obj

pause
