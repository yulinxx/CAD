@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo vcvars failed
  exit /b 1
)
cmake --build "C:\Users\xx\Documents\Cpp\CAD\build" --target ViewportRefreshTests -j 8
exit /b %errorlevel%
