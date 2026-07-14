#!/bin/sh
# Build the image (once) and start the long-lived Linux rig container.
# The repo is bind-mounted; build trees + caches live in named volumes so
# they survive container recreation. Everything then runs via
#   docker exec orkige-ci <cmd>
# Configure inside with the linux-* presets; on an arm64 host add
#   -DVCPKG_INSTALL_OPTIONS="--clean-after-build;--allow-unsupported"
# (ogre-next's vcpkg supports-list has no linux&arm64 entry). Run windowed
# tests the way CI does:
#   LVP_ICD=$(ls /usr/share/vulkan/icd.d/lvp_icd*.json); \
#   VK_DRIVER_FILES=$LVP_ICD xvfb-run -a \
#     -s "-screen 0 1280x1024x24 +extension RANDR" ctest ...
set -e
REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
docker build -t orkige-ci-linux "$(dirname "$0")"
docker rm -f orkige-ci 2>/dev/null || true
docker run -d --name orkige-ci \
  -v "$REPO_ROOT":/work/orkige \
  -v orkige-ci-build:/work/orkige/build \
  -v orkige-ci-vcpkg-cache:/vcpkg-cache \
  -v orkige-ci-ccache:/ccache \
  -w /work/orkige \
  orkige-ci-linux sleep infinity
echo "ready: docker exec -it orkige-ci bash"
