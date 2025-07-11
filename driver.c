/* USB Mouse Driver with two character devices:
 * - one char device counts mouse left-clicks
 * - another char device tracks mouse movements in terms of relative movement and packet data
 * 
 * This kernel module creates a USB mouse driver with functionality to track mouse left-clicks and movements.
 * 
 * Userspace interaction via:
 * 1. /dev/usb_mouse_clicks
 *    Read: returns total number of left-clicks
 *    Write: accepts "start", "stop" and "reset" commands to control click counting
 * 
 * 2. /dev/usb_mouse_movements
 *    Read: returns current (x,y) position and latest raw data packet
 *    Write: accepts "start", "stop" and "reset" commands to control movement tracking
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/uaccess.h>  // For userspace intereaction
#include <linux/slab.h>     // For memory allocation
#include <linux/fs.h>       // For file operations
#include <linux/cdev.h>     // For character device registeration
#include <linux/mutex.h>    // For mutex lock

// USB mouse structure (holds mouse state, data buffers, character devices and sync primitives)
struct usb_mouse {
    struct usb_device *usbdev;
    struct urb *irq;
    unsigned char *data;
    dma_addr_t data_dma;
    int click_count;
    int pkt_len;
    bool enabled;
    int x_pos;
    int y_pos;

    // Click counter char device
    struct cdev click_cdev;
    dev_t click_devt;
    struct class *click_class;
    struct device *click_device;

    // Movement tracking char device
    struct cdev move_cdev;
    dev_t move_devt;
    struct class *move_class;
    struct device *move_device;

    // Mutex for movement data read sync
    struct mutex move_mutex;
    unsigned char last_packet[8];
    bool packet_available;

};

// Global variables
static struct class *click_class;
static struct class *move_class;

static const struct usb_device_id usb_device_table[] = {
    {USB_INTERFACE_INFO(0x03, 0x01, 0x02)},  // Recognise generic USB mouse
    {} // Terminating entry
};
MODULE_DEVICE_TABLE(usb, usb_device_table);

static void usb_mouse_irq(struct urb *urb) // Triggered when mouse sends data
{
    struct usb_mouse *mouse = urb->context;
    int status = urb->status;
    if (status == 0 && mouse-> enabled) {
        static int last_left = 0;
        int left_pressed = mouse->data[0] & 0x01;
        if (left_pressed && !last_left) {
            mouse->click_count++;
            printk(KERN_INFO "[Mouse Click] Mouse button clicked. Total count: %d\n", mouse->click_count);
        }
        last_left = left_pressed;

        // Track mouse movement
        int16_t dx = (int16_t)((mouse->data[3] << 8) | mouse->data[2]);
        int16_t dy = (int16_t)((mouse->data[5] << 8) | mouse->data[4]);
        printk(KERN_INFO "Interpreted dx: %d, dy: %d\n", dx, dy);
        mouse->x_pos += dx;
        mouse->y_pos -= dy;

        // Check for movement data in kernel logs
        printk(KERN_INFO "Full Raw Packet:");
        for (int i = 0; i < 8; i++) {
            printk(KERN_CONT " 0x%02x", mouse->data[i]);
        }
        printk(KERN_CONT "\n");

        // Save last packet data for movement device and updates availability flag, protected by mutex
        mutex_lock(&mouse->move_mutex);
        memcpy(mouse->last_packet, mouse->data, mouse->pkt_len);
        mouse->packet_available = true;
        mutex_unlock(&mouse->move_mutex);
    } else if (status != 0){
        printk(KERN_WARNING "URB error status: %d\n", status);
    }
    usb_submit_urb(urb, GFP_ATOMIC);
    return;
}


// --- click char device handlers
static int click_open(struct inode *inode, struct file *file)
{
    struct usb_mouse *mouse = container_of(inode->i_cdev, struct usb_mouse, click_cdev);
    file->private_data = mouse;
    return 0;
}

static ssize_t click_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct usb_mouse *mouse = file->private_data;
    char buffer[64];
    int len = snprintf(buffer, sizeof(buffer), "Click count: %d\n", mouse->click_count);
    *ppos = 0;
    return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

// Accepts "start", "stop" or "reset" commands from userspace.c to control click counter
static ssize_t click_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
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
        printk(KERN_INFO "[Click] User issued RESET command\n");

    } else if (strncmp(buffer, "stop", 4) == 0) {
        mouse->enabled = false;
        printk(KERN_INFO "[Click] User issued STOP command\n");
        
    } else if (strncmp(buffer, "start", 5) == 0) {
        mouse->enabled = true;
        printk(KERN_INFO "[Click] User issued START command\n");
}   else {
        printk(KERN_WARNING "[Click] Unknown command received: %s\n", buffer);
    }      
    return count;

}

static const struct file_operations click_fops = {
    .owner = THIS_MODULE,
    .read = click_read,
    .write = click_write,
    .open = click_open,
};


// --- movement char device handlers
static int move_open(struct inode *inode, struct file *file)
{
    struct usb_mouse *mouse = container_of(inode->i_cdev, struct usb_mouse, move_cdev);
    file->private_data = mouse;
    return 0;
}

static ssize_t move_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    struct usb_mouse *mouse = file->private_data;
    char buffer[64];
    int len;

    mutex_lock(&mouse->move_mutex);
    if (!mouse->packet_available) {
        mutex_unlock(&mouse->move_mutex);
        return 0;
    }

    len = snprintf(buffer, sizeof(buffer), "Position: (%d, %d)\nRaw packet: 0x%02x 0x%02x 0x%02x\n", 
        mouse->x_pos, mouse->y_pos,
        mouse->last_packet[0], mouse->last_packet[1], mouse->last_packet[2]);
    mutex_unlock(&mouse->move_mutex);

    *ppos = 0;
    return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

// Accepts "start", "stop" or "reset" commands from userspace.c to control movement tracking
static ssize_t move_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    struct usb_mouse *mouse = file->private_data;
    char buffer[16];

    if (count > sizeof(buffer) - 1)
        return -EINVAL;
    if (copy_from_user(buffer, buf, count))
        return -EFAULT;
    buffer[count] = '\0';
    
    if (strncmp(buffer, "reset", 5) == 0) {
        mouse->x_pos = 0;
        mouse->y_pos = 0;
        printk(KERN_INFO "[Move] User issued RESET command\n");
    } else if (strncmp(buffer, "stop", 4) == 0) {
        mouse->enabled = false;
        printk(KERN_INFO "[Move] User issued STOP command\n");
        
    } else if (strncmp(buffer, "start", 5) == 0) {
        mouse->enabled = true;
        printk(KERN_INFO "[Move] User issued START command\n");
    } else {
    printk(KERN_WARNING "[Move] Unknown command received: %s\n", buffer);
    }
    return count;
}

static const struct file_operations move_fops = {
    .owner = THIS_MODULE,
    .read = move_read,
    .write = move_write,
    .open = move_open,
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
    mutex_init(&mouse->move_mutex);
    mouse->packet_available = false;

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

    // click char device setup
    if (alloc_chrdev_region(&mouse->click_devt, 0, 1, "usb_mouse_clicks"))
        goto error3;

    cdev_init(&mouse->click_cdev, &click_fops);
    mouse->click_cdev.owner = THIS_MODULE;
    if (cdev_add(&mouse->click_cdev, mouse->click_devt, 1))
        goto error4;

    mouse->click_class = class_create("usb_mouse_click_class");
    if (IS_ERR(mouse->click_class))
        goto error5;

    mouse->click_device = device_create(mouse->click_class, NULL, mouse->click_devt, NULL, "usb_mouse_clicks");
    if (IS_ERR(mouse->click_device))
        goto error6;


    //movement char device setup
    if (alloc_chrdev_region(&mouse->move_devt, 0, 1, "usb_mouse_movements"))
        goto error7;
    
    cdev_init(&mouse->move_cdev, &move_fops);

    mouse->move_cdev.owner = THIS_MODULE;
    if (cdev_add(&mouse->move_cdev, mouse->move_devt, 1))
        goto error8;
    
    mouse->move_class = class_create("usb_mouse_move_class");
    if (IS_ERR(mouse->move_class))
        goto error9;

    mouse->move_device = device_create(mouse->move_class, NULL, mouse->move_devt, NULL, "usb_mouse_movements");
    if (IS_ERR(mouse->move_device))
        goto error10;
    return 0;
error10: // Failed to create movement char device
    class_destroy(mouse->move_class);
error9:  //  Failed to create movement class
    cdev_del(&mouse->move_cdev);
error8:  // Failed to add movement char device
    unregister_chrdev_region(mouse->move_devt, 1);
error7:  // Failed to allocate movement device number
    device_destroy(mouse->click_class, mouse->click_devt);
error6:  // Failed to create click char device
    class_destroy(mouse->click_class);
error5:  // Failed to create click class
    cdev_del(&mouse->click_cdev);
error4:  // Failed to add click char device
    unregister_chrdev_region(mouse->click_devt, 1);
error3:  // Failed to submit URB
    usb_free_urb(mouse->irq);
error2:  // Failed to allocate URB
    usb_free_coherent(mouse->usbdev, mouse->pkt_len, mouse->data, mouse->data_dma);
error1:  // Failed to allocate memory for mouse structure
    kfree(mouse);
    return -ENOMEM;

}


// Called when USB mouse disconnected
static void usb_mouse_disconnect(struct usb_interface *interface) {
    struct usb_mouse *mouse = usb_get_intfdata(interface);

    usb_kill_urb(mouse->irq);
    usb_free_urb(mouse->irq);
    usb_free_coherent(mouse->usbdev, mouse->pkt_len, mouse->data, mouse->data_dma);

    device_destroy(mouse->click_class, mouse->click_devt);
    class_destroy(mouse->click_class);
    cdev_del(&mouse->click_cdev);
    unregister_chrdev_region(mouse->click_devt, 1);

    device_destroy(mouse->move_class, mouse->move_devt);
    class_destroy(mouse->move_class);
    cdev_del(&mouse->move_cdev);
    unregister_chrdev_region(mouse->move_devt, 1);

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

    // Initialise global classes
    click_class = NULL;
    move_class = NULL;

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
