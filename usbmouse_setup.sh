#!/bin/bash
set -e

echo "[STEP 1] Compiling kernel module with make..."
make

echo "[STEP 2] Compiling userspace program..."
gcc userspace.c -o userprog

echo "[STEP 3] Please plug in your USB mouse now."
read -p "Press ENTER once the mouse is connected."

echo "[STEP 4] Detecting USB mouse..."

mouse_dev_dir=$(find /sys/bus/usb/devices/ -maxdepth 1 -type d \
    -exec bash -c '[[ -f "$1/bInterfaceClass" && $(cat "$1/bInterfaceClass") == "03"]] && \
                    [[-f "$1/bInterfaceSubClass" && $(cat "$1/bInterfaceSubClass") == "01"]] && \
                    [[-f "$1/bInterfaceProtocol" && $(cat "$1/bInterfaceProtocol") == "02"]] && echo "$1"' _ {} \; | head -n1)

if [[ -z "$mouse_dev_dir" ]]; then
    echo "[ERROR] No USB mouse detected. Please ensure your mouse is connected and try again."
    exit 1
fi

mouse_interface=$(basename "$mouse_dev_dir")
usb_interface="${mouse_interface}:1.0"

echo "[INFO] Found USB HID mouse interface: $usb_interface"

if [[ ! -e /sys/bus/usb/drivers/usbhid/$usb_interface ]]; then  # Check if mouse is already unbound
    echo "[INFO] Mouse already unbound from usbhid driver."
else
    echo "[STEP 5] Unbinding mouse from usbhid driver..."
    echo "$usb_interface" | sudo tee /sys/bus/usb/drivers/usbhid/unbind
fi

echo "[STEP 6] Inserting USB mouse driver module into kernel..."
sudo insmod driver.ko

echo  "[STEP 7] dmesg input for USB mouse driver initialisation confirmation."
dmesg | tail -n 10

echo "[STEP 8] Launching user interface..."
sudo ./userprog
