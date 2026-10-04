@echo off
rem Builds and runs all C++ tests on the PC (MSVC Build Tools + DevEco SDK cmake/ninja):
rem our tests (sns_tests) and the teammate's engine tests (ambient_test_*), plus wav_cli end-to-end checks.
rem Set DEVECO_NATIVE to the SDK's openharmony\native folder if it is not the default below.
setlocal
if "%DEVECO_NATIVE%"=="" set "DEVECO_NATIVE=F:\huwaei\DevEco Studio\sdk\default\openharmony\native"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set "PATH=%DEVECO_NATIVE%\build-tools\cmake\bin;%PATH%"
cd /d "%~dp0.."
if not exist build-host mkdir build-host
cmake -S entry/src/main/cpp/tests -B build-host/tests -G Ninja -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release >build-host\configure.log 2>&1 || (type build-host\configure.log & exit /b 1)
cmake --build build-host/tests || exit /b 1
set RC=0
build-host\tests\sns_tests.exe || set RC=1
for %%T in (test_core test_custom test_safety test_sounds speech_test) do (
  build-host\tests\ambient_%%T.exe >nul 2>&1 && echo [PASS] ambient_%%T || (echo [FAIL] ambient_%%T & set RC=1)
)
set S=entry\src\main\cpp\ambient\tests\data
build-host\tests\wav_cli.exe --expect alarm=1,knock=1,loud_sound=0 %S%\sample_alarm_knock.wav >nul 2>&1 && echo [PASS] wav_alarm_knock || (echo [FAIL] wav_alarm_knock & set RC=1)
build-host\tests\wav_cli.exe --expect alarm=0,knock=0,loud_sound=1 %S%\sample_loud.wav >nul 2>&1 && echo [PASS] wav_loud || (echo [FAIL] wav_loud & set RC=1)
build-host\tests\wav_cli.exe --learn fridge=%S%\sample_fridge_learn.wav --expect custom:fridge=2,knock=0,loud_sound=0 %S%\sample_fridge_stream.wav >nul 2>&1 && echo [PASS] wav_custom_fridge || (echo [FAIL] wav_custom_fridge & set RC=1)
build-host\tests\wav_cli.exe --expect alarm=0,knock=0,loud_sound=0 %S%\sample_quiet_noise.wav >nul 2>&1 && echo [PASS] wav_quiet_noise || (echo [FAIL] wav_quiet_noise & set RC=1)
exit /b %RC%
