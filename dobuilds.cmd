@echo off
SET BRANCH=32bpp-persdiv
SET CONFIGURATION=RelWithDebInfo

SET MSBUILD=C:\Windows\Microsoft.NET\Framework64\v4.0.30319\MSBuild.exe
SET ZIP7="C:\Program Files\7-zip\7z.exe"


echo Building branch %BRANCH%


echo Determining git revision...
SET REVISION=
"C:\cygwin\bin\git.exe" rev-list --abbrev-commit -n 1 HEAD >revs.lst
FOR /f %%a IN (revs.lst) DO (
  IF NOT DEFINED REVISION SET REVISION=%%a
)
DEL /F /Q revs.lst
echo Git revision %REVISION%

SET ZIPBASE=%BRANCH%-%REVISION%


echo Building x64 release...

PUSHD build64\client
%MSBUILD% odamex.vcxproj /m /p:Configuration=%CONFIGURATION%
IF NOT %ERRORLEVEL% EQU 0 GOTO fail

CD %CONFIGURATION%
COPY ..\..\..\README-32bpp .
DEL /F %ZIPBASE%-x64.zip
%ZIP7% a %ZIPBASE%-x64.zip "odamex.exe" "README-32bpp"
POPD


echo Building x86 release...

PUSHD build32\client
%MSBUILD% odamex.vcxproj /m /p:Configuration=%CONFIGURATION%
IF NOT %ERRORLEVEL% EQU 0 GOTO fail

CD %CONFIGURATION%
COPY ..\..\..\README-32bpp .
DEL /F %ZIPBASE%-x86.zip
%ZIP7% a %ZIPBASE%-x86.zip "odamex.exe" "README-32bpp"
POPD

echo Copying ZIPs to server...

"C:\cygwin\bin\scp.exe" "build64/client/%CONFIGURATION%/%ZIPBASE%-x64.zip" "bit:/home/ftp/jim/software/odamex/%BRANCH%/%ZIPBASE%-x64.zip"
"C:\cygwin\bin\scp.exe" "build32/client/%CONFIGURATION%/%ZIPBASE%-x86.zip" "bit:/home/ftp/jim/software/odamex/%BRANCH%/%ZIPBASE%-x86.zip"

goto good

:fail
echo Build Failed!
goto done

:good
echo Succeeded!

:done
