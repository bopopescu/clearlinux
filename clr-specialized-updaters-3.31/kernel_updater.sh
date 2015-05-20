#!/bin/bash

#######################################
# Clear Linux Kernel update script
# Author Brad Peters <brad.t.peters@intel.com>
# kernel_updater.sh: Provides a minimal-copy wrapper which finesses
#  the VFAT /boot fs to minimize writes, while implementing Clear Linux
#  update/install and fix policies
#######################################

VERSION=3.08
HASH="/usr/bin/sha256sum"

function printUsage()
{
    echo "kernel_updater.sh [--path <path to chroot>]"
    echo "    -v | --version : Prints version of kernel_updater.sh"
    echo "    -h | --help    : Prints usage"
    echo "    -p | --path    : Optional. Used when creating new image "
    echo "                 in a subdir. Should be absolute path"
}

function cleanDuplicates()
{
    # Move duplicate kernels out of the way in /boot, if necessary.
    # Kernels in /usr are considered 'correct'
    SRCFILES=*.efi

    for f in $SRCFILES
    do
        destfile="$SUBDIR/boot/EFI/Linux/$f"
        if [ ! -f $destfile ]
        then
            continue
        fi

        srcsha=$(sha1sum $f)
        destsha=$(sha1sum $destfile)

        srcsha=$(echo $srcsha | awk '{print $1}')
        destsha=$(echo $destsha | awk '{print $1}')

        if [ "$srcsha" != "$destsha" ]
        then
            newname="${destfile/clearlinux/clearTMP}"
#            echo "Renaming $destfile to $newname prior to replacement"
            mv $destfile $newname
        fi
    done
}


function cleanOld()
{
    # Clean up old kernels

    # Get list of all kernels in reverse sorted order (based only on release ID)
    LS_KERNELS_SORTED=$(ls org.clearlinux*.efi | sort --field-separator=- -n -k3 -r)

    i=0
    for k in $LS_KERNELS_SORTED
    do
        if [[ "$i" -ge 3 ]]
        then
            cur_kernel=$(uname -r | cut -f1,2,3 -d.)
            ls_kernel=$(echo $k | cut -f4,5,6 -d.)

            #Do not clean up currently running kernel
            if [[ $ls_kernel = $cur_kernel ]]
            then
                echo "Keeping $k. Is currently running kernel"
                continue;
            fi

            echo "Clean up old kernel: $k"
            echo "Clean up modules $SUBDIR/lib/modules/$ls_kernel"
            rm "$k"
            rm -Rf "$SUBDIR/lib/modules/$ls_kernel"
        fi
        i=$((i + 1))
    done
}


# Copies kernel from /usr to /boot. Checks each kernel to
#  ensure modules are installed, and only copies if so
function copyKernelBlobs()
{
    pushd "$SUBDIR/usr/lib/kernel" > /dev/null
    LS_KERNELS_SORTED=$(ls org.clearlinux*.efi | sort --field-separator=- -n -k3 -r)
    popd > /dev/null

    for k in $LS_KERNELS_SORTED
    do
        kernel_version=$(echo "$k" | cut -f4,5,6 -d.)
        kernel_blob=$(echo "$k" | cut -f5 -d/)
        echo "Kernel version = $kernel_version , blob = $kernel_blob"
        kmods=$(ls $SUBDIR/lib/modules | sed -n "/$kernel_version/p")
        kmods_len=${#kmods}

        # Proceed with cp if kernel modules are present and kernel is not 
        #  already installed
        if [ $kmods_len -gt 0 ] && [ ! -e "$SUBDIR/boot/EFI/Linux/$kernel_blob" ]
        then
            echo "   Installing $k to $SUBDIR/boot/EFI/Linux"
            ret=$(cp -n $k $SUBDIR/boot/EFI/Linux)

            if [ ! $ret ]
            then
                echo "Installed $k to /boot/EFI/Linux"
            else
                echo "Failed to copy $k to /boot"
            fi

        else
            if [ ! $kmods_len -gt 0 ]
            then
                echo "Skipping kernel $kernel_version - No modules found."
            fi
            if [ -e "$SUBDIR/boot/EFI/Linux/$kernel_blob" ]
            then
                echo "Skipping kernel $kernel_version - Already installed."
            fi
        fi

    done
}

##################### Main #####################
if [[ $# > 2 ]]
then
    printUsage
fi

# Parse cmdline
if [[ $# > 0 ]]
then
  case "$1" in
    -h|--help)
    printUsage
    exit 0
    ;;
    -v|--version)
    echo "kernel_updater.sh version $VERSION"
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
    echo "Must be root to execute kernel_updater.sh"
    exit 1
fi

# Clean up /usr/lib/kernel to the minimal 3 kernels we want to install
pushd "$SUBDIR/usr/lib/kernel/" > /dev/null

have_blobs=0
for test in org.clearlinux*.efi; do
    have_blobs=1
    break;
done
if [[ "$have_blobs" -eq 1 ]]; then
    cleanOld
fi

if [ -z "${SUBDIR}" ]; then
    # Get boot partition UUID
    uuid=$(blkid | sed -n '/vfat/p' | sed -n 's/.*\ UUID=\"\([^"]*\).*/\1/p')
    strlen=${#uuid}

    IS_MOUNTED=$(mount | sed -n '/boot/p')
    if [ -z $IS_MOUNTED ]; then
        if [ $strlen -gt 0 ]
        then
            echo "Mounting boot partition with UUID=$uuid"
            mount UUID=$uuid /boot -o rw -t vfat
        else
            echo "Failed to identify boot partition using /sys/firmware/efi/efivars."
            echo "  Attempting to mount by-partlabel"
            mount /dev/disk/by-partlabel/ESP /boot -o rw -t vfat
        fi
    else
        if [ $strlen -gt 0 ]
        then
            echo "Mounting boot partition with UUID=$uuid"
            mount -o remount,rw UUID=$uuid /boot -t vfat
        else
            echo "Failed to identify boot partition using /sys/firmware/efi/efivars."
            echo "  Attempting to remount by-partlabel"
            mount -o remount,rw /dev/disk/by-partlabel/ESP /boot -t vfat
        fi
    fi
fi

echo "Updating kernel... please wait..."
# Identify kernels in /boot/EFI/Linux which are different from those
#  in /usr. Assume /usr is 'correct', and remove those in /boot
cleanDuplicates

# Sync after cleanup to protect against VFAT corruption
pushd "$SUBDIR/boot" > /dev/null
sync
popd > /dev/null

if [ ! -d "$SUBDIR/boot/EFI/Linux" ]; then
    mkdir -p "$SUBDIR/boot/EFI/Linux"
fi
# Copy new EFI blobs from /usr/lib/kernel into /boot/EFI/Linux
copyKernelBlobs

# Step 3 - Cleanup kernels which were repaired, then sync
pushd "$SUBDIR/boot/EFI/Linux/" > /dev/null
rm -f *clearTMP* 2>/dev/null
sync
popd > /dev/null

# Finally, clean up kernels which are no longer needed
pushd "$SUBDIR/boot/EFI/Linux" > /dev/null
cleanOld
sync
popd > /dev/null

if [ -z "${SUBDIR}" ]; then
    umount /boot
fi

popd > /dev/null

echo "kernel update complete."
