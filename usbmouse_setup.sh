#!/bin/bash
set -e

echo "[STEP 1] Compiling kernel module with make..."
make

echo "[STEP 2] Compiling userspace program..."
gcc userspace.c -o userprog

echo "[STEP 3] Please plug in your USB mouse now."
read -p "Press ENTER once the mouse is connected."

echo "[STEP 4] Detecting USB mouse..."

# Iterate through interfaces bound to usbhid driver
for iface in /sys/bus/usb/drivers/usbhid/*:*; do
    # Skip non-directory entries (like 'bind', 'unbind')
    [[ -d "$iface" ]] || continue


    if [[ -f "$iface/bInterfaceClass" && $(cat "$iface/bInterfaceClass") == "03" && \
          -f "$iface/bInterfaceSubClass" && $(cat "$iface/bInterfaceSubClass") == "01" && \
          -f "$iface/bInterfaceProtocol" && $(cat "$iface/bInterfaceProtocol") == "02" ]]; then
        mouse_dev_dir="$iface"
        break
    fi
done

if [[ -z "$mouse_dev_dir" ]]; then
    echo "[ERROR] No USB mouse detected bound to usbhid driver."
    exit 1
fi

usb_interface=$(basename "$mouse_dev_dir")
echo "[INFO] Found USB HID mouse interface: $usb_interface"

# Unbind from usbhid if needed
if [[ -e "/sys/bus/usb/drivers/usbhid/$usb_interface" ]]; then
    echo "[STEP 5] Unbinding mouse from usbhid driver..."
    echo "$usb_interface" | sudo tee /sys/bus/usb/drivers/usbhid/unbind
else
    echo "[INFO] Mouse already unbound from usbhid driver."
fi

if [[ -e /dev/usb_mouse_clicks ]]; then
    echo "[INFO] Device node /dev/usb_mouse_clicks already exists. Removing module to reset."
    sudo rmmod driver || true
    sleep 1
fi

echo "[STEP 6] Inserting USB mouse driver module into kernel..."
sudo insmod driver.ko

echo  "[STEP 7] dmesg input for USB mouse driver initialisation confirmation."
dmesg | tail -n 10

echo "[STEP 8] Launching user interface..."
sudo ./userprog
