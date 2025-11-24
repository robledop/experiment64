#!/usr/bin/env bash

MEMORY=${1:-2048} 
VM_NAME="Experiment64"

if VBoxManage showvminfo "$VM_NAME" >/dev/null 2>&1; then
  if VBoxManage showvminfo "$VM_NAME" | grep -q "running"; then
    VBoxManage controlvm "$VM_NAME" poweroff;
    sleep 2;
  fi;
  VBoxManage unregistervm "$VM_NAME" --delete-all;
fi

rm -f image.vdi || true
VBoxManage convertfromraw image.hdd image.vdi --format VDI
VBoxManage createvm --name "$VM_NAME" --register --basefolder .
VBoxManage storagectl "$VM_NAME" --name "IDE" --add ide --controller PIIX4
VBoxManage storageattach "$VM_NAME" --storagectl "IDE" --port 0 --device 0 --type hdd --medium image.vdi
VBoxManage modifyvm "$VM_NAME" --memory "$MEMORY" --vram 16 --graphicscontroller vboxvga
VBoxManage modifyvm "$VM_NAME" --nic1 nat --nictype1 82540EM
VBoxManage modifyvm "$VM_NAME" --ioapic on --cpus 4 --chipset piix3
VBoxManage modifyvm "$VM_NAME" --uart1 0x3F8 4 --uartmode1 file "$(pwd)/vbox.log"
VBoxManage setextradata "$VM_NAME" GUI/ScaleFactor "1.75"
VBoxManage setextradata "$VM_NAME" GUI/DefaultCloseAction "poweroff"
VBoxManage startvm "$VM_NAME"
