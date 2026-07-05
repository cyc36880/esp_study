@echo off
REM ============================
REM  ESP-IDF 5.4.3 环境启动脚本
REM ============================

set IDF_PATH=D:\esp\v5.4.3\firmware\v5.4.3\esp-idf
set IDF_TOOLS_PATH=D:\esp\v5.4.3\tool
set IDF_PYTHON_ENV_PATH=D:\esp\v5.4.3\tool\python_env\idf5.4_py3.11_env

REM 记录脚本所在目录（即你存放 bat 的文件夹）
set SCRIPT_DIR=%~dp0

REM 一行完成：进入 IDF 目录、激活环境、再回到脚本目录
cd /d %IDF_PATH% && call export.bat && cd /d %SCRIPT_DIR%

cmd /k