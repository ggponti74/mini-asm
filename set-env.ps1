winget install --id Microsoft.PowerShell --source winget

$ENV:Path += ";C:\Users\Puser.CFL2024-P-03\OneDrive\DevEnv\Git"
$ENV:Path += ";C:\Users\Puser.CFL2024-P-03\OneDrive\DevEnv\Git\bin"
$ENV:Path += ";C:\Users\Puser.CFL2024-P-03\OneDrive\DevEnv\mingw64\bin"

Set-Alias -Name make -Value mingw32-make.exe

git config --global user.email "ggponti74@hotmail.com"
git config --global user.name "ggponti74"