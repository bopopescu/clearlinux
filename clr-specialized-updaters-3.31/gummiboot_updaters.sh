#!/bin/bash

#######################################
# Clear Linux gummiboot installer
# Author Brad Peters <brad.t.peters@intel.com>
# gummiboot_updater.sh provides a minimal-copy wrapper which finesses
#  the VFAT /boot fs to minimize writes, while implementing Clear Linux
#  update/install and fix policies
#######################################

set -e
set -x

VERSION=1.02

function printUsage()
{
    echo "gummiboot_updater.sh [--path <path to chroot>]"
    echo "    -v | --version : Prints version of gummiboot_updater.sh"
    echo "    -h | --help    : Prints usage"
    echo "    -p | --path    : Optional. Used when creating new image "
    echo "                 in a subdir. Should be absolute path"
}

function cleanDuplicates()
{
    echo "Inside cleanDuplicates"
    srcfile="$SUBDIR/usr/lib/gummiboot/gummibootx64.efi"
    destfile="$SUBDIR/boot/EFI/gummiboot/gummibootx64.efi"

    if [ ! -f $srcfile ]
    then
        echo "  gummibootx64 not found in /usr/lib/gummiboot. Nothing to do."
        exit 1;
    fi

    if [ ! -f $destfile ]
    then
        echo "$destfile - not found, no cleanup needed."
        return 0
    fi

    srcsha=$(sha1sum $srcfile)
    destsha=$(sha1sum $destfile)

    srcsha=$(echo $srcsha | awk '{print $1}')
    destsha=$(echo $destsha | awk '{print $1}')

    echo "Shas: src: $srcsha , dest: $destsha"

    if [ "$srcsha" != "$destsha" ]
    then
        newname="${destfile/gummibootx64/gummibootx64_old}"
    #   echo "Renaming $destfile to $newname prior to replacement"
        mv $destfile $newname
    fi

    return 0
}


##################### Main #####################
if [[ $# > 2 ]]
then
    printUsage
fi

# Parse cmdline
if [[ $# > 0 ]]
then
  case $1 in
    -h|--help)
    printUsage
    exit 0
    ;;
    -v|--version)
    echo "gummiboot_updater.sh version $VERSION"
    exit 0
    ;;
    -p|--path)
    if [[ $# = 1 ]]
    then
        echo "Invalid arguments.  --path requires a second argument."
        exit 1
    fi
    SUBDIR="$2"
    ;;
    *)
    echo "Unrecognized argument ($1).  Try again"
    printUsage
    exit 1
    ;;
  esac
fi

if [[ $EUID -ne 0 ]]; then
    echo "Must be root to execute gummiboot_updater.sh"
    exit 1
fi

if [ -z "${SUBDIR}" ]; then
    IS_MOUNTED=$(mount | grep boot)
    if [ -z "${IS_MOUNTED}" ]; then
        mount /dev/disk/by-partlabel/ESP /boot -o rw -t vfat
    fi
fi

echo "Installing new gummiboot binary... please wait..."
cleanDuplicates
ret=$?

echo "cleanDuplicates returned : $ret"

# Sync after cleanup to protect against VFAT corruption
pushd "$SUBDIR/boot"
sync
popd

if [ ! -d "$SUBDIR/boot/EFI/gummiboot/" ]; then
    mkdir -p "$SUBDIR/boot/EFI/gummiboot"
fi

echo "Copying gummiboot executable to /boot"
cp -n "$SUBDIR/usr/lib/gummiboot/gummibootx64.efi" "$SUBDIR/boot/EFI/gummiboot/"
if [ ! -d "$SUBDIR/boot/EFI/Boot/" ]; then
    mkdir -p "$SUBDIR/boot/EFI/Boot"
fi
cp -n "$SUBDIR/usr/lib/gummiboot/gummibootx64.efi" "$SUBDIR/boot/EFI/Boot/BOOTX64.EFI"

# Sync after copy to protect against VFAT corruption
pushd "$SUBDIR/boot"
sync
popd

if [[ $ret = 1 ]]
then
    echo "Cleaning up old gummiboot binary"
    rm -f "$SUBDIR/boot/EFI/gummiboot/gummibootx64_old.efi"
fi

echo "gummiboot update complete."
