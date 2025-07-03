# Linux Mouse Driver Module Guide
By default, the Pi's default usb driver (usbhid) will be handling USB connections. Hence, manual identification
of the mouse's file path is necessary to unbind the mouse from the default usbhid driver.

## Step 1
`scp` files from computer to preferred Pi directory

## Step 2
`cd` to the preferred directory and use `make` to compile kernel Module

## Step 3
Plug in your preferred USB mouse into your Pi so that the Pi can detect your mouse

## Step 4
Check connected USB devices and look for your mouse (Etc `Bus 001 Device 005: ID 046d:c542 Logitech, Inc. M185 compact wireless mouse`)\
Take note of the device ID\
```lsusb```

To find our which is your mouse folder (Etc ID = "046D")\
```grep -r "<your-mouse-vendor-ID>" /sys/bus/usb/devices/*/idVendor```

Example command:
```grep -r "046d" /sys/bus/usb/devices/*/idVendor```

Expected Output Example: (mouse folder will be 1-1.4 in this case)
```/sys/bus/usb/devices/1-1.4/idVendor:046d```


## Step 5
Unbind the default usbhid driver\
```echo '1-1.4:1.0' | sudo tee /sys/bus/usb/drivers/usbhid/unbind```

## Step 6
Insert custom driver\
```sudo insmod driver.ko```

## Step 7
Check the custom driver is successfully inserted and utilised\
```dmesg | tail -n 10```

Expected Output:
```
[ 1106.514858] USB Mouse Driver Module Unloading... 
[ 1110.764633] USB Mouse Driver Module Initialising... 
[ 1110.764751] Your USB Mouse, Vendor ID: 0x046d, Product ID: 0xc542, has been successfully connected!
```