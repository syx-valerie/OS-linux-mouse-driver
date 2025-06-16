#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/uaccess.h>  // For userspace intereaction
#include <linux/slab.h>     // For memory allocation
#include <linux/fs.h>       // For file operations
#include <linux/cdev.h>     // For character device registeration
#include <linux/mutex.h>    // For mutex lock

// USB Device ID table
static const struct usb_device_id usb_device_table[] = {
    {USB_INTERFACE_INFO(0x03, 0x01, 0x02)},  // Recognise generic USB mouse
    {} // Terminating entry
};
MODULE_DEVICE_TABLE(usb, usb_device_table);

// Called when USB mouse connected
static int usb_mouse_connect(struct usb_interface *interface, const struct usb_device_id *id) {
    printk(KERN_INFO "Your USB Mouse, Vendor ID: 0x%04x, Product ID: 0x%04x, has been successfully connected!\n",
           id->idVendor, id->idProduct);
    return 0;
}

// Called when USB mouse disconnected
static void usb_mouse_disconnect(struct usb_interface *interface) {
    printk(KERN_INFO "Your USB Mouse has been disconnected.\n");
}

// USB Driver Structure
static struct usb_driver usb_mouse_driver = {
    .name = "usb_mouse_driver",
    .id_table = usb_device_table,
    .probe = usb_mouse_connect,
    .disconnect = usb_mouse_disconnect,
};

// Module Load Function
static int __init usb_mouse_init(void) {
    int result;
    printk(KERN_INFO "USB Mouse Driver Module Initialising...\n");
    result = usb_register(&usb_mouse_driver);

    // Error Handling
    if (result < 0) {
        printk(KERN_ERR "Failed to register USB Mouse Driver\n");
        return result;
    }
    return 0;
}

// Module Exit Function
static void __exit usb_mouse_exit(void) {
    usb_deregister(&usb_mouse_driver);
    printk(KERN_INFO "USB Mouse Driver Module Unloading...\n");
}

module_init(usb_mouse_init);
module_exit(usb_mouse_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("USB Mouse Driver");