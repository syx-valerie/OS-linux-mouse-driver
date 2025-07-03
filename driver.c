#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/uaccess.h>  // For userspace intereaction
#include <linux/slab.h>     // For memory allocation
#include <linux/fs.h>       // For file operations
#include <linux/cdev.h>     // For character device registeration
#include <linux/mutex.h>    // For mutex lock

// USB mouse structure
struct usb_mouse {
    struct usb_device *usbdev;
    struct input_dev *inputdev;
    struct urb *irq;
    unsigned char *data;
    dma_addr_t data_dma;
    int click_count;
    int pkt_len;
    bool enabled;

    //char device
    struct cdev cdev;
    dev_t devt;
    struct class *class;
    struct device *device;

};

// Global variables
static struct class *mouse_class;

static const struct usb_device_id usb_device_table[] = {
    {USB_INTERFACE_INFO(0x03, 0x01, 0x02)},  // Recognise generic USB mouse
    {} // Terminating entry
};
MODULE_DEVICE_TABLE(usb, usb_device_table);

static void usb_mouse_irq(struct urb *urb) //triggered when mouse sends data
{
    struct usb_mouse *mouse = urb->context;
    int status = urb->status;
    if (status == 0 && mouse-> enabled) {
        static int last_left = 0;
        int left_pressed = mouse->data[0] & 0x01;
        if (left_pressed && !last_left) {
            mouse->click_count++;
            printk(KERN_INFO "Mouse clicked! Total: %d\n", mouse->click_count);
        }
        last_left = left_pressed;
    } else if (status != 0){
        printk(KERN_WARNING "URB error status: %d\n", status);
    }
    usb_submit_urb(urb, GFP_ATOMIC);
    return;
}



// --- char device handlers
static int mouse_open(struct inode *inode, struct file *file)
{
    struct usb_mouse *mouse = container_of(inode->i_cdev, struct usb_mouse, cdev);
    file->private_data = mouse;
    return 0;
}

static ssize_t mouse_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct usb_mouse *mouse = file->private_data;
    char buffer[64];
    int len = snprintf(buffer, sizeof(buffer), "Click count: %d\n", mouse->click_count);
    *ppos = 0;
    return simple_read_from_buffer(buf, count, ppos, buffer, len);
}



static ssize_t mouse_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    struct usb_mouse *mouse = file->private_data;
    char buffer[16];
    if (count > sizeof(buffer) - 1)
        return -EINVAL;
    if (copy_from_user(buffer, buf, count))
        return -EFAULT;
    buffer[count] = '\0';
    if (strncmp(buffer, "reset", 5) == 0) {
        mouse->click_count = 0;
        printk(KERN_INFO "Mouse click counter reset.\n");

    } else if (strncmp(buffer, "stop", 4) == 0) {
        mouse->enabled = false;
        printk(KERN_INFO "Mouse click counter paused.\n");
    } else if (strncmp(buffer, "start", 5) == 0) {
        mouse->enabled = true;
        printk(KERN_INFO "Mouse click counter resumed.\n");
    }
    return count;

}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = mouse_read,
    .write = mouse_write,
    .open = mouse_open,
};



// -------- usb probe & disconnect --------
static int usb_mouse_connect(struct usb_interface *interface, const struct usb_device_id *id) {
    struct usb_mouse *mouse;
    struct usb_device *dev = interface_to_usbdev(interface);
    struct usb_host_interface *iface_desc = interface->cur_altsetting;
    struct usb_endpoint_descriptor *endpoint =  NULL;

    int i;
    for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
        struct usb_endpoint_descriptor *ep = &iface_desc->endpoint[i].desc;
        if (usb_endpoint_is_int_in(ep)) {
            endpoint = ep;
            break;
        }
    }
    printk(KERN_INFO "Mouse connected! Vendor: 0x%04x, Product: 0x%04x\n",
           dev->descriptor.idVendor, dev->descriptor.idProduct);
    mouse = kzalloc(sizeof(struct usb_mouse), GFP_KERNEL);
    if (!mouse)
        return -ENOMEM;
    mouse->usbdev = dev;
    mouse->pkt_len = endpoint->wMaxPacketSize;
    if (mouse->pkt_len == 0)
        mouse->pkt_len = 8;
    mouse->enabled = true;
    mouse->data = usb_alloc_coherent(dev, mouse->pkt_len, GFP_ATOMIC, &mouse->data_dma);
    if (!mouse->data)
        goto error1;
    mouse->irq = usb_alloc_urb(0, GFP_KERNEL);
    if (!mouse->irq)
        goto error2;
    usb_fill_int_urb(mouse->irq, dev,
                     usb_rcvintpipe(dev, endpoint->bEndpointAddress),
                     mouse->data, mouse->pkt_len,
                     usb_mouse_irq, mouse, endpoint->bInterval);
    mouse->irq->transfer_dma = mouse->data_dma;
    mouse->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
    usb_set_intfdata(interface, mouse);
    if (usb_submit_urb(mouse->irq, GFP_KERNEL))
        goto error3;

    // char device setup
    if (alloc_chrdev_region(&mouse->devt, 0, 1, "usb_mouse_clicks"))
        goto error3;
    cdev_init(&mouse->cdev, &fops);
    mouse->cdev.owner = THIS_MODULE;
    if (cdev_add(&mouse->cdev, mouse->devt, 1))
        goto error4;
    mouse->class = class_create("usb_mouse_class");
    device_create(mouse_class, NULL, mouse->devt, NULL, "usb_mouse_clicks");
    if (IS_ERR(mouse->class))
        goto error5;
    mouse->device = device_create(mouse->class, NULL, mouse->devt, NULL, "usb_mouse_clicks");
    if (IS_ERR(mouse->device))
        goto error6;
    printk(KERN_INFO "Click counter initialized.\n");
    return 0;
error6:
    class_destroy(mouse->class);
error5:
    cdev_del(&mouse->cdev);
error4:
    unregister_chrdev_region(mouse->devt, 1);
error3:
    usb_free_urb(mouse->irq);
error2:
    usb_free_coherent(mouse->usbdev, mouse->pkt_len, mouse->data, mouse->data_dma);
error1:
    kfree(mouse);
    return -ENOMEM;

}

// Called when USB mouse disconnected
static void usb_mouse_disconnect(struct usb_interface *interface) {
    struct usb_mouse *mouse = usb_get_intfdata(interface);
    usb_kill_urb(mouse->irq);
    usb_free_urb(mouse->irq);
    usb_free_coherent(mouse->usbdev, mouse->pkt_len, mouse->data, mouse->data_dma);
    device_destroy(mouse->class, mouse->devt);
    class_destroy(mouse->class);
    cdev_del(&mouse->cdev);
    unregister_chrdev_region(mouse->devt, 1);
    kfree(mouse);
    printk(KERN_INFO "USB Mouse Driver unloaded.\n");
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







