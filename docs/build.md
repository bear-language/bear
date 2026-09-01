## Building `bearc`
#### Dependencies
- CMake >= 3.20
- Ninja
### Linux/MacOS
```bash
# installing LLVM development packages:

# Ubuntu / Debian
sudo apt install llvm-dev 

# Fedora / RHEL 
sudo dnf install llvm-devel

# Arch 
sudo pacman -S llvm

# macOS
brew install llvm # follow instructions after install to put this on your path

# next build bearc:
git clone https://github.com/bear-language/bear.git
cd bear
cmake -B build -S .
cmake --build build

# convient scripts for developers:
./scripts/clean-all.sh [Release|Debug] # builds bearc and libbearc
./scripts/clean.sh     [Release|Debug] # builds bearc
./scripts/build-tests.sh               # build and run tests
                                                         # if you want BearLang on your path:
cd bearc
pwd                                                      # and copy result
echo "export PATH='result/from/pwd/:$PATH'" >> ~/.zshrc  # or .bashrc, etc.
source ~/.zshrc                                          # or .bashrc, etc.
bearc -v                                                 # check to ensure it works
```

### Windows with MSYS2 UCRT64
First, [download MSYS2](https://www.msys2.org/), and then open MSYS2 UCRT64.
```bash
pacman -Syu
pacman -S git vim     # some essentials you will need if you haven't used MSYS2 before
git clone https://github.com/zachMahan64/bear-lang.git
pacman -S mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-make      \
    mingw-w64-ucrt-x86_64-cmake
cd bear-lang && mkdir build && cd build && cmake -G "MinGW Makefiles" .. && mingw32-make
                                                          # if you want BearLang on your path:
cd bearc
pwd                                                       # and copy result
echo "export PATH='result/from/pwd/:\$PATH'" >> ~/.bashrc  
source ~/.bashrc                                          
bearc -v                                                  # check to ensure it works
```

### Building a custom LLVM distribution
```bash
# starting from project root dir
cd bearc
git clone --branch llvmorg-22.1.8 --depth 1 https://github.com/llvm/llvm-project.git

mkdir llvm-build
cd llvm-build

cmake -G Ninja ../llvm-project/llvm \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="clang;lld" \
  -DLLVM_ENABLE_RUNTIMES="" \
  -DLIBCXXABI_USE_LLVM_UNWINDER=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_DOCS=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_BUILD_EXAMPLES=OFF \
  -DLLVM_ENABLE_ASSERTIONS=OFF \
  -DLLVM_TARGETS_TO_BUILD="X86;ARM;AArch64" \
  -DCMAKE_INSTALL_PREFIX=../llvm

ninja install
```
