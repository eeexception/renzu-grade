# OpenFX SDK headers

Unmodified `include/*.h` and `LICENSE.md` from the official Academy Software
Foundation OpenFX project, revision recorded in `REVISION`. This small,
license-compliant header subset avoids the upstream repository's build tools
and dependencies. No SDK support library is needed: Renzu Grade uses the
standard C suites directly behind RAII adapters.

Upstream: https://github.com/AcademySoftwareFoundation/openfx

Set `-DOFX_SDK_ROOT=/path/to/openfx` to use another SDK checkout. It must include
`ofxGPURender.h` with the Metal texture extension definitions; the pinned headers
provide them. Older Resolve hosts can ignore texture capabilities and use the
established Metal buffer path. Header availability does not establish host support.
