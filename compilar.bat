@echo off
rem Compila o projeto com o gcc (MinGW). Execute a partir da pasta do projeto.
rem main.c inclui os outros .c, entao so ele e compilado.
gcc -std=c99 -Wall -Wextra -O2 -o ecommerce.exe src\main.c
if %errorlevel% neq 0 exit /b %errorlevel%
echo Compilado: ecommerce.exe
