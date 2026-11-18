@echo off
rem Raw frame -> 0x3C/0x3E wrapped txt tool (Windows launcher)
rem Requires Python 3 with tkinter (bundled with the python.org installer).
rem NOTE: keep this file ASCII-only and CRLF; cmd.exe breaks on LF and on UTF-8 CJK text.
setlocal
cd /d "%~dp0"

py -3 frame_wrap_gui.py %*
if not errorlevel 1 goto done

python frame_wrap_gui.py %*
if not errorlevel 1 goto done

echo.
echo [ERROR] Failed to start the tool.
echo         Python 3 was not found, or its tkinter module is missing.
echo         Install from https://www.python.org/downloads/ and tick
echo         "Add Python to PATH" during setup, then run this file again.
echo.
pause

:done
endlocal
