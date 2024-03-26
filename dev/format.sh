#!/bin/bash

# sudo apt install clang-tidy-12 clang-format-12 clang-12
# pip3 install cmake-format
# sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-12 20 --slave /usr/bin/clang++ clang++ /usr/bin/clang++-12 --slave /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-12 --slave /usr/bin/clang-format clang-format /usr/bin/clang-format-12

find . ! \( -path ./build/\* \) \( -name \*.[hc] -o -name \*.[hc]pp \) -print -exec clang-format -style=LLVM -i -fallback-style=none {} \;
find . ! \( -path ./build/\* \) \( -name CMakeLists.txt -o -name \*.cmake \) -print -exec cmake-format -i {} \;
