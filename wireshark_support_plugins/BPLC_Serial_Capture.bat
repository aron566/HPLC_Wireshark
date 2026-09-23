@echo off
rem BPLC serial extcap wrapper: provides NW_2021_Capture / GW_2022_Capture interfaces
rem (requires python + pyserial on PATH)
python "%~dp0bplc_serial_extcap.py" %*
