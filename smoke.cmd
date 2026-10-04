make clean && make || exit /b 1
if exist test.exe del test.exe
./mini-asm -t X86 -o test.exe test.asm
if errorlevel 1 exit /b 1
file test.exe
test.exe
echo %ERRORLEVEL%