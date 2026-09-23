# Upstream external/vcpkg/triplets/community/x64-osx.cmake, plus a deployment target so the
# dependencies (and therefore the release binary's x86_64 slice) run on macOS 11 and later.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES x86_64)
set(VCPKG_OSX_DEPLOYMENT_TARGET "11.0")
