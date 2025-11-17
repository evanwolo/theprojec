# Quick Start Guide

## Windows

### Option 1: Visual Studio Developer Command Prompt
1. Open "Developer Command Prompt for VS 2022" or "x64 Native Tools Command Prompt"
2. Navigate to project directory:
   ```
   cd e:\theprojec
   ```
3. Run build script:
   ```
   build.bat
   ```
4. Run simulation:
   ```
   cd build
   Release\KernelSim.exe
   ```

### Option 2: Direct Compilation (if CMake not available)
In Visual Studio Developer Command Prompt:
```cmd
cd e:\theprojec
mkdir build
cd build
cl /std:c++17 /O2 /EHsc /W4 /I..\include ^
   ..\src\Kernel.cpp ^
   ..\src\KernelSnapshot.cpp ^
   ..\src\main_kernel.cpp ^
   /Fe:KernelSim.exe

KernelSim.exe
```

### Option 3: MinGW (if installed)
```cmd
cd e:\theprojec
mkdir build
cd build
g++ -std=c++17 -O3 -march=native -Wall -Wextra -I..\include ^
    ..\src\Kernel.cpp ^
    ..\src\KernelSnapshot.cpp ^
    ..\src\main_kernel.cpp ^
    -o KernelSim.exe

KernelSim.exe
```

## Linux / macOS

```bash
cd /path/to/theprojec
chmod +x build.sh
./build.sh
./build/KernelSim
```

Or manually:
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
./KernelSim
```

## Test Commands

Once running, try these commands:

```
metrics
step 10
metrics
run 100 10
state traits
quit
```

The `run` command will create `data/metrics.csv` with simulation metrics over time.

## Performance Expectations

- **50,000 agents**: ~50-100ms per tick (depending on CPU)
- **Memory usage**: ~10MB
- **File output**: JSON snapshots, CSV metrics

## Troubleshooting

**"cmake not found"**: 
- Windows: Install CMake from https://cmake.org or use Visual Studio installer
- Linux: `sudo apt install cmake` (Ubuntu/Debian) or `sudo yum install cmake` (RHEL/CentOS)
- macOS: `brew install cmake`

**"No compiler found"**:
- Windows: Install Visual Studio 2022 (free Community Edition) with C++ workload
- Linux: `sudo apt install build-essential`
- macOS: `xcode-select --install`

**Slow compilation**:
- Use Release mode: `-DCMAKE_BUILD_TYPE=Release`
- The first build compiles everything; subsequent builds are incremental

**Runtime errors**:
- Ensure `data/` directory exists (created automatically by build script)
- Check available memory (50k agents needs ~10MB)
