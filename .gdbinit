set architecture i386
set breakpoint pending on

add-symbol-file ./build/kernel 0x80100000
#add-symbol-file ./rootfs/bin/init 0x0
#add-symbol-file ./rootfs/bin/sh 0x0
add-symbol-file ./rootfs/bin/startwm 0x0
#add-symbol-file ./rootfs/bin/mousetest 0x0
#add-symbol-file ./assets/fbdoom 0x0

#break trapret
#break vector32
#break panic
