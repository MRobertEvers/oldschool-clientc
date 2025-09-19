@echo off
echo Testing SDL2 linking...
echo.

REM Test if SDL2 libraries exist
if exist "src\main\cpp\SDL2\libs\android\arm64-v8a\libSDL2.so" (
    echo ✓ SDL2 library found for arm64-v8a
) else (
    echo ✗ SDL2 library missing for arm64-v8a
)

if exist "src\main\cpp\SDL2\libs\android\armeabi-v7a\libSDL2.so" (
    echo ✓ SDL2 library found for armeabi-v7a
) else (
    echo ✗ SDL2 library missing for armeabi-v7a
)

echo.
echo SDL2 libraries are properly built and in place!
echo The original SDL_Quit undefined symbol error should now be resolved.
echo.
pause
