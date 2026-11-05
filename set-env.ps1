$ENV:Path += ";D:\Windows\Git"
$ENV:Path += ";D:\Windows\Git\bin"
$ENV:Path += ";D:\Windows\mingw64\bin"

Set-Alias -Name make -Value mingw32-make.exe

git config --global user.email "ggponti74@hotmail.com"
git config --global user.name "ggponti74"
