#!/bin/bash
# install_audio_dkms.sh
set -e

if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (sudo)"
  exit 1
fi

MODULE_NAME="berryauto-audio"
MODULE_VERSION="1.0"
DKMS_SRC="/usr/src/${MODULE_NAME}-${MODULE_VERSION}"

# 1. Check if the module is already installed and remove it if so
if dkms status | grep -q "${MODULE_NAME}/${MODULE_VERSION}"; then
    echo "Removing existing DKMS module..."
    dkms remove -m ${MODULE_NAME} -v ${MODULE_VERSION} --all
fi

# 2. Copy our source code to the system's DKMS source directory
echo "Copying source files to ${DKMS_SRC}..."
rm -rf ${DKMS_SRC}
mkdir -p ${DKMS_SRC}
cp -r ./dkms/${MODULE_NAME}-${MODULE_VERSION}/* ${DKMS_SRC}/

# 3. Add, Build, and Install the module via DKMS
echo "Adding module to DKMS..."
dkms add -m ${MODULE_NAME} -v ${MODULE_VERSION}

echo "Building module (this may take a minute)..."
dkms build -m ${MODULE_NAME} -v ${MODULE_VERSION}

echo "Installing module..."
dkms install -m ${MODULE_NAME} -v ${MODULE_VERSION}

# 4. Load the newly built module into the active kernel
echo "Loading snd-berryauto kernel module..."
modprobe snd-berryauto

echo "Done! Run 'aplay -l' to see your new BerryAuto soundcard."
