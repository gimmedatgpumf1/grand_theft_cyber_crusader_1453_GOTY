@echo off
setlocal
set "DATA=%~dp0"
if not exist "%DATA%\NM_combined.mat" (
  echo Put NM_combined.mat in "%DATA%" and run this again.
  goto end
)
if not exist "%DATA%\Faces.mat" (
  echo Put Faces.mat in "%DATA%" and run this again.
  goto end
)
where py >nul 2>nul
if errorlevel 1 (
  echo Python 3 is required. Install it from python.org and run this again.
  goto end
)
py -3 -c "import numpy, scipy, h5py" >nul 2>nul
if errorlevel 1 (
  echo Installing Python packages needed to extract the skeleton...
  py -3 -m pip install numpy scipy h5py
  if errorlevel 1 goto failed
)
py -3 "%~dp002-export-subject.py" --dir "%DATA%" --subject N197925 --out "%DATA%\male-N197925.npz"
if errorlevel 1 goto failed
echo.
echo Upload male-N197925.npz and male-N197925.json from "%DATA%".
goto end
:failed
echo Extraction failed. Please send the error shown above.
:end
pause
