@echo off
echo Setting up SpeechRNT development environment on Windows...

REM Check if Node.js is installed
node --version >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: Node.js is not installed. Please install Node.js 18+ from https://nodejs.org/
    pause
    exit /b 1
)

REM Check if we have a C++ compiler (Visual Studio Build Tools)
where cl >nul 2>&1
if %errorlevel% neq 0 (
    echo Warning: Visual Studio Build Tools not found in PATH
    echo You may need to install Visual Studio 2019/2022 with C++ tools
    echo Or run this from a "Developer Command Prompt"
)

REM Setup frontend
echo.
echo Setting up frontend...
cd frontend
if not exist package.json (
    echo Creating package.json for frontend...
    call :create_frontend_package
)

call npm install
if %errorlevel% neq 0 (
    echo Error: Failed to install frontend dependencies
    pause
    exit /b 1
)

cd ..

REM Setup backend (we'll create a minimal CMakeLists.txt)
echo.
echo Setting up backend...
cd backend
if not exist CMakeLists.txt (
    echo Creating minimal CMakeLists.txt...
    call :create_cmake_file
)

REM Create basic source structure
if not exist src mkdir src
if not exist include mkdir include
if not exist src\main.cpp (
    echo Creating basic main.cpp...
    call :create_main_cpp
)

cd ..

echo.
echo Setup complete!
echo.
echo To start development:
echo   Frontend: cd frontend && npm run dev
echo   Backend:  cd backend && mkdir build && cd build && cmake .. && make
echo.
pause
goto :eof

:create_frontend_package
echo {> package.json
echo   "name": "speechrnt-frontend",>> package.json
echo   "version": "0.1.0",>> package.json
echo   "type": "module",>> package.json
echo   "scripts": {>> package.json
echo     "dev": "vite",>> package.json
echo     "build": "vite build",>> package.json
echo     "preview": "vite preview",>> package.json
echo     "lint": "eslint . --ext ts,tsx --report-unused-disable-directives --max-warnings 0">> package.json
echo   },>> package.json
echo   "dependencies": {>> package.json
echo     "react": "^18.2.0",>> package.json
echo     "react-dom": "^18.2.0">> package.json
echo   },>> package.json
echo   "devDependencies": {>> package.json
echo     "@types/react": "^18.2.43",>> package.json
echo     "@types/react-dom": "^18.2.17",>> package.json
echo     "@typescript-eslint/eslint-plugin": "^6.14.0",>> package.json
echo     "@typescript-eslint/parser": "^6.14.0",>> package.json
echo     "@vitejs/plugin-react": "^4.2.1",>> package.json
echo     "eslint": "^8.55.0",>> package.json
echo     "eslint-plugin-react-hooks": "^4.6.0",>> package.json
echo     "eslint-plugin-react-refresh": "^0.4.5",>> package.json
echo     "typescript": "^5.2.2",>> package.json
echo     "vite": "^5.0.8">> package.json
echo   }>> package.json
echo }>> package.json
goto :eof

:create_cmake_file
echo cmake_minimum_required(VERSION 3.16)> CMakeLists.txt
echo project(SpeechRNT)>> CMakeLists.txt
echo.>> CMakeLists.txt
echo set(CMAKE_CXX_STANDARD 17)>> CMakeLists.txt
echo set(CMAKE_CXX_STANDARD_REQUIRED ON)>> CMakeLists.txt
echo.>> CMakeLists.txt
echo add_executable(SpeechRNT src/main.cpp)>> CMakeLists.txt
goto :eof

:create_main_cpp
echo #include ^<iostream^>> src\main.cpp
echo.>> src\main.cpp
echo int main() {>> src\main.cpp
echo     std::cout ^<^< "SpeechRNT Backend Server Starting..." ^<^< std::endl;>> src\main.cpp
echo     return 0;>> src\main.cpp
echo }>> src\main.cpp
goto :eof