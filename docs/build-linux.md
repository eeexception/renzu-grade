# Linux x86_64: CPU build, tests and installation

Use a glibc-based x86_64 system, a C++17 GCC or Clang compiler, CMake 3.24+ and
GNU make. Resolve is not needed for standalone tests. No Python, CUDA/OpenCL SDK
or Metal toolchain is needed by the plugin build itself.

```bash
./scripts/build-linux.sh --test --package
# Optional sanitizer build, separate from the distribution:
./scripts/build-linux.sh --debug --sanitize --test --build-dir build-linux-asan
```

The script disables Metal. `--package` requires Release without sanitizers and
runs tests before creating the archive. Equivalent direct CMake commands:

```bash
cmake -S . -B build-linux -G 'Unix Makefiles' \
  -DCMAKE_BUILD_TYPE=Release -DRENZU_ENABLE_METAL=OFF -DRENZU_BUILD_TESTS=ON
cmake --build build-linux --parallel
ctest --test-dir build-linux --output-on-failure --no-tests=error
cpack --config build-linux/CPackConfig.cmake
./scripts/verify-linux.sh build-linux/RenzuGrade.ofx.bundle
```

## Runtime compatibility

The GitHub distribution job runs in the maintained
[`manylinux_2_28_x86_64` build image](https://github.com/pypa/manylinux), using glibc
2.28 as the baseline. It checks the binary's actual GLIBC symbol requirements and
rejects an archive requiring anything newer. The private C++ runtime is linked
statically; only the OpenFX entry points are exported, avoiding exposure of its
C++ runtime symbols to the host. CUDA, OpenCL and external C++ runtime libraries
are not runtime dependencies of the plugin.

This targets Rocky Linux 8 and newer glibc-based distributions at the binary
level. A local build on a newer distro may require that distro's newer glibc;
it is not automatically equivalent to the CI distribution. A compatible ELF
binary is not a guarantee of Resolve host acceptance. Alpine/musl, ARM64 and
glibc older than 2.28 are outside this build's support scope.

## Output and installation

```text
build-linux/RenzuGrade.ofx.bundle/
  Contents/Linux-x86-64/RenzuGrade.ofx
build-linux/dist/
  renzu-grade-<version>-linux-x86_64-cpu-only.tar.gz
  renzu-grade-<version>-linux-x86_64-cpu-only.tar.gz.sha256
```

The archive includes `INSTALL.txt`, licenses, documentation, and the corresponding
source/build scripts under `Source/renzu-grade/`. From its download directory,
check the checksum with `sha256sum --check <archive>.sha256`, then extract it with
`tar -xzf <archive>.tar.gz`. Actions downloads add an outer ZIP; extract that first.

1. Quit DaVinci Resolve.
2. Move any existing `/usr/OFX/Plugins/RenzuGrade.ofx.bundle` outside Plugins as a backup.
3. From the extracted distribution directory, copy the whole bundle:

   ```bash
   sudo mkdir -p /usr/OFX/Plugins
   sudo cp -a RenzuGrade.ofx.bundle /usr/OFX/Plugins/
   ```

4. Restart Resolve. Open Effects and find **Renzu Grade**, in **Color Grading**.

The final binary path must be
`/usr/OFX/Plugins/RenzuGrade.ofx.bundle/Contents/Linux-x86-64/RenzuGrade.ofx`, following
the [OpenFX packaging specification](https://openfx.readthedocs.io/en/latest/Reference/ofxPackaging.html).
To uninstall, quit Resolve and move only that bundle outside Plugins. References
remain in `$XDG_DATA_HOME/renzu-grade/references/`, normally
`~/.local/share/renzu-grade/references/`.

## Verification and limits

GitHub Actions runs the four CPU/OpenFX suites, checks exported functions,
architecture, dependencies and glibc requirements, verifies the archive checksum,
loads/tests the extracted plugin, and rebuilds/tests the accompanying source.
A separate Ubuntu job runs ASan/UBSan tests. Logs and archives are separate Actions
artifacts. Reference storage tests include Unicode paths, replacement, concurrent
threads and separate processes.

Sanitized development builds share the test host's dynamic C++ runtime, avoiding
an extra static-runtime exception pool at plugin unload. Distribution builds
keep the private static runtime; the scripts do not package sanitizer builds.

All controls, Auto, WB picker and Match use CPU processing. Playback/export speed
depends on the CPU. Resolve still requires its own supported GPU and drivers.
Before host acceptance, complete the [manual checklist](manual-test-plan.md) on
the intended Linux/Resolve system. For discovery problems inspect Video Plugins
preferences and host logs; `ldd` on the installed `.ofx` must report no missing
libraries. Never replace the plugin while Resolve is running.
