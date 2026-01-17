set breakpoint pending on

add-symbol-file ./build/kernel.elf 0xffffffff80000000
#add-symbol-file ./build/rootfs_ext2/bin/init 0x400000
add-symbol-file ./user/build/ping 0x400000
#add-symbol-file ./build/rootfs_ext2/bin/mousetest 0x400000
#add-symbol-file ./assets/fbdoom 0x400000

#break panic
