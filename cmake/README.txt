本目录预留给后续 CMake 辅助模块（toolchain 文件、Find 脚本等）。

当前根目录 CMakeLists.txt 已可直接使用：

  cmake -S .. -B ../build -G "Visual Studio 17 2022" -A x64
  cmake --build ../build --config Release