#!/usr/bin/env bash
set -euo pipefail

MEMORY=${1:-2048}
VM_NAME="Experiment64"
RAW_DISK="image.hdd"
RAW_IDE_DISK="image2.ide"
VDI_DISK="image.vdi"
VDI_IDE_DISK="image2.vdi"

if VBoxManage showvminfo "$VM_NAME" >/dev/null 2>&1; then
  if VBoxManage showvminfo "$VM_NAME" | grep -q "running"; then
    VBoxManage controlvm "$VM_NAME" poweroff;
    sleep 2;
  fi;
  VBoxManage unregistervm "$VM_NAME" --delete-all;
fi

rm -f "$VDI_DISK" "$VDI_IDE_DISK" || true
VBoxManage convertfromraw "$RAW_DISK" "$VDI_DISK" --format VDI
VBoxManage convertfromraw "$RAW_IDE_DISK" "$VDI_IDE_DISK" --format VDI
VBoxManage createvm --name "$VM_NAME" --register --basefolder .
VBoxManage storagectl "$VM_NAME" --name "SATA" --add sata --controller IntelAhci --portcount 1 --bootable on
VBoxManage storagectl "$VM_NAME" --name "IDE" --add ide --controller PIIX4 --bootable on
VBoxManage storageattach "$VM_NAME" --storagectl "SATA" --port 0 --device 0 --type hdd --medium "$VDI_DISK"
VBoxManage storageattach "$VM_NAME" --storagectl "IDE" --port 1 --device 0 --type hdd --medium "$VDI_IDE_DISK"
VBoxManage modifyvm "$VM_NAME" --memory "$MEMORY" --vram 16 --graphicscontroller vboxvga
VBoxManage modifyvm "$VM_NAME" --nic1 nat --nictype1 82540EM
VBoxManage modifyvm "$VM_NAME" --ioapic on --cpus 8 --chipset piix3
VBoxManage modifyvm "$VM_NAME" --boot1 disk --boot2 none --boot3 none --boot4 none
VBoxManage modifyvm "$VM_NAME" --firmware efi
VBoxManage modifyvm "$VM_NAME" --uart1 0x3F8 4 --uartmode1 file "$(pwd)/vbox.log"
VBoxManage setextradata "$VM_NAME" GUI/ScaleFactor "1.75"
VBoxManage setextradata "$VM_NAME" GUI/DefaultCloseAction "poweroff"
VBoxManage startvm "$VM_NAME"
