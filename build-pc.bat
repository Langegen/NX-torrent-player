@echo off
setlocal
cd /d "%~dp0"

set "BASH_EXE=C:\devkitPro\msys2\usr\bin\bash.exe"
set "MINGW_BIN=C:\devkitPro\msys2\mingw64\bin"

if not exist "%BASH_EXE%" (
    echo [ERROR] devkitPro MSYS2 bash not found at %BASH_EXE%!
    pause
    exit /b 1
)

echo ========================================================
echo  NX-torrent-player: Building PC Desktop Version
echo ========================================================

if not exist build-pc mkdir build-pc

echo.
echo [1/3] Configuring CMake...
"%BASH_EXE%" -lc "export PATH=/usr/bin:/mingw64/bin:\$PATH && export PKG_CONFIG_PATH=/d/NX-torrent-player/pc_deps/mpv:/mingw64/lib/pkgconfig && cd /d/NX-torrent-player && cmake -B build-pc -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPLATFORM_DESKTOP=ON -DCMAKE_C_COMPILER=/mingw64/bin/x86_64-w64-mingw32-gcc.exe -DCMAKE_CXX_COMPILER=/mingw64/bin/x86_64-w64-mingw32-g++.exe -DCMAKE_SYSTEM_NAME=Windows -DPKG_CONFIG_EXECUTABLE=/usr/bin/pkg-config.exe"

if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    pause
    exit /b %errorlevel%
)

echo.
echo [2/3] Compiling NX-torrent-player.exe with Ninja...
"%BASH_EXE%" -lc "export PATH=/usr/bin:/mingw64/bin:\$PATH && ninja -C /d/NX-torrent-player/build-pc"

if errorlevel 1 (
    echo [ERROR] Compilation failed.
    pause
    exit /b %errorlevel%
)

echo.
echo [3/3] Syncing runtime DLLs and assets...
for %%F in (
    libbrotlicommon.dll
    libbrotlidec.dll
    libcrypto-3-x64.dll
    libcurl-4.dll
    libgcc_s_seh-1.dll
    libiconv-2.dll
    libidn2-0.dll
    libintl-8.dll
    libmbedcrypto-16.dll
    libnghttp2-14.dll
    libnghttp3-9.dll
    libngtcp2-16.dll
    libngtcp2_crypto_ossl-0.dll
    libpsl-5.dll
    libsharpyuv-0.dll
    libssh2-1.dll
    libssl-3-x64.dll
    libstdc++-6.dll
    libunistring-5.dll
    libwebp-7.dll
    libwinpthread-1.dll
    libzstd.dll
    zlib1.dll
) do (
    if exist "%MINGW_BIN%\%%F" (
        copy /y "%MINGW_BIN%\%%F" build-pc\ >nul
    )
)

if exist pc_deps\mpv\libmpv-2.dll (
    copy /y pc_deps\mpv\libmpv-2.dll build-pc\ >nul
)

if exist resources (
    xcopy /e /i /y resources build-pc\resources >nul
)

echo.
echo ========================================================
echo [SUCCESS] PC version built successfully!
echo Executable: build-pc\NX-torrent-player.exe
echo Run "run-pc.bat" to start testing on PC.
echo ========================================================
