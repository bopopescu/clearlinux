#!/bin/bash
# Creates an entry with the kernel version every time a kernel is booted.
# Used to track which kernels have successfully booted, to enable rational
# cleanup after kernel updates

/usr/bin/touch /usr/lib/kernel/k_booted_$(/usr/bin/uname -r)

