### Release Plan (for (as) static (as possible) `bearc` binaries)
- add a `BEARC_STATIC` CMake option that sets per-platform link flags
- `bearc` either require users to have lld or bundle 
#### Windows (MSYS2 UCRT64)
- link with `-static` to pull in libstdc++, libgcc, and winpthread
- remaining deps always in win10+
#### Linux (musl)
- build in Alpine and link fully static with `-static`
- needs static llvm (`llvmNN-static` or a custom llvm build with `-DLLVM_BUILD_LLVM_DYLIB=OFF`)
- needs `zstd-static` and `zlib-static`, or build llvm without zstd/zlib
   - CMake currently checks for `zstd::libzstd_shared`; relax this for static builds
- musl thins
   - since default thread stack size is 128KB: `-Wl,-z,stack-size=8388608`
   - musl `malloc` is slow: statically link mimalloc
- prob add an Alpine Dockerfile/script under `scripts/`
#### macOS
- `libSystem` and `libc++` are on every mac, just so link everything else (llvm, zstd, zlib) statically
- `otool -L bearc` should list just `libSystem` and `libc++` (w/ no `/opt/homebrew` or `/usr/local` paths)
- build `arm64` and `x86_64` (and/or a universal binary w/ `lipo`)
- set `CMAKE_OSX_DEPLOYMENT_TARGET`
- or document `xattr -d com.apple.quarantine bearc` but not a prob for winget/curl acquisitions
#### verification
- Windows: `objdump -p bearc.exe | grep "DLL Name"`
- Linux: `ldd bearc` ("not a dynamic executable")
- macOS: `otool -L bearc`
#### other styuff
- find stdlib relative to exec parent path and then walk to root searching `lib/std.br` to find a suitable path
    - make sure to allow override (env var or cli flag) 
- invoking system linker is fine but perhaps for linux statically link lld
