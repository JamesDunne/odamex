@echo off
SET BRANCH=32bpp

SET MSBUILD=C:\Windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe
SET ZIP7="C:\Program Files\7-zip\7z.exe"

PUSHD \git\odamex\

echo Checking out branch %BRANCH%
"C:\cygwin\bin\git.exe" checkout -f %BRANCH%

echo Determining git revision...
SET REVISION=
"C:\cygwin\bin\git.exe" rev-list --abbrev-commit -n 1 HEAD >revs.lst
FOR /f %%a IN (revs.lst) DO (
  IF NOT DEFINED REVISION SET REVISION=%%a
)
DEL /F /Q revs.lst
echo Git revision %REVISION%

SET ZIPBASE=%BRANCH%-%REVISION%

echo Building 64-bit release...

PUSHD \git\odamex\build64\client
%MSBUILD% odamex.vcxproj /m /p:Configuration=RelWithDebInfo
CD RelWithDebInfo
COPY ..\..\..\README-32bpp .
DEL /F %ZIPBASE%-x64.zip
%ZIP7% a %ZIPBASE%-x64.zip "odamex.exe" "README-32bpp"
"C:\cygwin\bin\scp.exe" "%ZIPBASE%-x64.zip" bit:/home/ftp/jim/software/odamex/%BRANCH%/
REM START .
POPD

echo Building 32-bit release...

PUSHD \git\odamex\build32\client
%MSBUILD% odamex.vcxproj /m /p:Configuration=RelWithDebInfo
CD RelWithDebInfo
COPY ..\..\..\README-32bpp .
DEL /F %ZIPBASE%-x86.zip
%ZIP7% a %ZIPBASE%-x86.zip "odamex.exe" "README-32bpp"
"C:\cygwin\bin\scp.exe" "%ZIPBASE%-x86.zip" bit:/home/ftp/jim/software/odamex/%BRANCH%/
REM START .
POPD

POPD

echo Done