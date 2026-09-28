make clean && make
./mini-asm -t X86 -o test.exe test.asm
file test.exe
test.exe
echo %ERRORLEVEL%