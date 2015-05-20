#!/bin/bash

# Test execution script which runs kernel_updater.sh on the ./test dir,
# then checks results to ensure they match expected and align with
# Clear Linux policy

if [ ! -e "./kernel_updater.sh" ] || [ ! -e "./test" ]
then
    echo "Test can only be run from the root source dir of the clr-specialized-updaters project"
    exit 1
fi

# Check state - need to ensure test is in untouched state, else bail
test=$(ls test/boot/EFI/Linux/*INSTALL*.efi 2>/dev/null)
if [ ${#test} -gt 0 ]
then
    echo "./test dir has already been updated. Use 'git stash' or clean it up manually"
    exit 1
fi

if [[ $EUID -ne 0 ]]; then
    echo "Must be root to execute."
    exit 1
fi

retain=$(ls test/usr/lib/kernel/*RETAIN*.efi)
delete=$(ls test/usr/lib/kernel/*DELETE*.efi)

./kernel_updater.sh --path $(pwd)/test &>/dev/null


test=$(ls test/boot/EFI/Linux/*INSTALL*.efi 2>/dev/null)
if [ ${#test} -eq 0 ]
then
    ls=$(ls test/usr/lib/kernel/*INSTALL*.efi)
    echo "Fail - Kernel which should have been installed was not: $ls"
else
    echo "#1 Success - installed correct kernel ($test)"
fi

test1=$(ls test/usr/lib/kernel/*RETAIN*.efi 2>/dev/null)
test2=$(ls test/boot/EFI/Linux/*RETAIN*.efi 2>/dev/null)
if [ ${#test1} -eq 0 ] || [ ${#test2} -eq 0 ]
then
    echo "Fail - Kernel which should have been kept around and currently installed was not found in both test/boot and test/usr: $retain"
else
    echo "#2 Success - kernel retained as expected ($retain)"
fi

test=$(ls test/usr/lib/kernel/*DELETE*.efi 2>/dev/null)
if [ ${#test} -gt 0 ]
then
    echo "Fail - Kernel which should have been deleted was NOT: $delete"
else
    echo "#3 Success - kernel cleaned up as expected ($delete)"
fi

test1=$(ls test/usr/lib/kernel/*NOCOPY*.efi 2>/dev/null)
test2=$(ls test/boot/EFI/Linux/*NOCOPY*.efi 2>/dev/null)
if [ ${#test1} -eq 0 ] || [ ${#test2} -gt 0 ]
then
    echo "Fail - Kernel was deleted when it should have been retained, or was installed when it should not have been, due to modules missing - $test1" 
else
    echo "#4 Success - kernel without modules retained as expected ($test1)"
fi


exit 0

