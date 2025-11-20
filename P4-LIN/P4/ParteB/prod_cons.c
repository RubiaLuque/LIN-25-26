#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>
#include <linux/wait.h>
#include <linux/version.h>
#include <linux/kfifo.h>
#include <linux/semaphore.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,2,1)
#define __cconst__ const
#else
#define __cconst__ 
#endif

MODULE_LICENSE("GPL");

#define DEVICE_NAME "prodcons" /* Dev name as it appears in /proc/devices   */
#define CLASS_NAME "cool"

#define MAX_ITEMS_CBUF 4
#define MAX_CHARS_KBUF 20

/*
 * Global variables are declared as static, so are global within the file.
 */

static dev_t start;
static struct cdev* chardev = NULL;
static struct class* class = NULL;
static struct device* device = NULL;

//Variables globales específicas de productor-consumidor
struct kfifo cbuf;

static struct semaphore mtx;
static struct semaphore huecos;
static struct semaphore elementos;

//PRODUCTOR --> AÑADE ELEMENTOS AL BUFFER CIRCULAR
static ssize_t prodcons_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
    char kbuf[MAX_CHARS_KBUF +1];
    int val = 0;

    if (len > MAX_CHARS_KBUF) {
        printk(KERN_INFO "prod_cons: not enough space!!\n");
        return -ENOSPC;
    }

    if (copy_from_user(&kbuf, buf, len)) {
        return -EFAULT;
    }

    if (sscanf(kbuf, "%d", &val) != 1) {
        printk(KERN_ALERT "Invalid argument\n");
        return -EINVAL;
    }

    //---SECCION CRITICA---
    if (down_interruptible(&huecos))
        return -EINTR;
        
    if(down_interruptible(&mtx)){
        up(&huecos);
        return -EINTR;
    }

    //Añade un entero al buffer circular
    kfifo_in(&cbuf, &val, sizeof(int));

    //---FIN SECCION CRITICA---

    up(&mtx);
    up(&elementos);

    (*off) += len;

    return len;
}

//CONSUMIDOR --> LEE ELEMENTOS DEL BUFFER CIRCULAR
static ssize_t prodcons_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    char kbuf[MAX_CHARS_KBUF +1];
    int nr_bytes = 0;
    int val;

    if((*off)>0)
        return 0;
        
    //---SECCION CRITICA---

    if (down_interruptible(&elementos))
        return -EINTR;

    if(down_interruptible(&mtx)){
        up(&elementos);
        return -EINTR;
    }

    kfifo_out(&cbuf, &val, sizeof(int));

    //---FIN SECCION CRITICA---
    up(&mtx);
    up(&huecos);

    nr_bytes=sprintf(kbuf,"%i\n",val);

    if(copy_to_user(buf, kbuf, nr_bytes)){
        return -EFAULT;
    }

    (*off) += len; 

    return nr_bytes;
}

static int prodcons_open(struct inode *, struct file*){
	try_module_get(THIS_MODULE);
	return SUCCESS;
}

static int prodcons_release(struct inode*, struct file*){
	module_put(THIS_MODULE);

	return 0;
}

static struct file_operations fops = {
    .read = prodcons_read,
    .write = prodcons_write,
    .open = prodcons_open,
    .release = prodcons_release
};


static char *custom_devnode(__cconst__ struct device *dev, umode_t *mode)
{
    if (!mode)
        return NULL;
    if (MAJOR(dev->devt) == MAJOR(start))
        *mode = 0666;
    return NULL;
}

int init_prodcons_module( void )
{
    int major;    /* Major number assigned to our device driver */
    int minor;    /* Minor number assigned to the associated character device */
    int ret;

    if(kfifo_alloc(&cbuf, MAX_ITEMS_CBUF * sizeof(int), GFP_KERNEL)){
        return -ENOMEM;
    }

    sema_init(&elementos, 0);
    sema_init(&huecos, MAX_ITEMS_CBUF);
    sema_init(&mtx, 1);

    /* Get available (major,minor) range */
    if ((ret = alloc_chrdev_region (&start, 0, 1, DEVICE_NAME))) {
        printk(KERN_INFO "Can't allocate chrdev_region()");
        goto error_alloc_region;
    }

    /* Create associated cdev */
    if ((chardev = cdev_alloc()) == NULL) {
        printk(KERN_INFO "cdev_alloc() failed ");
        ret = -ENOMEM;
        goto error_alloc;
    }

    cdev_init(chardev, &fops);

    if ((ret = cdev_add(chardev, start, 1))) {
        printk(KERN_INFO "cdev_add() failed ");
        goto error_add;
    }

    /* Create custom class */
    class = class_create(THIS_MODULE, CLASS_NAME);

    if (IS_ERR(class)) {
        pr_err("class_create() failed \n");
        ret = PTR_ERR(class);
        goto error_class;
    }

    /* Establish function that will take care of setting up permissions for device file */
    class->devnode = custom_devnode;

    /*Creating device*/
    device = device_create(class, NULL, start, NULL, DEVICE_NAME);

    if (IS_ERR(device)) {
        pr_err("Device_create failed\n");
        ret = PTR_ERR(device);
        goto error_device;
    }

    major = MAJOR(start);
    minor = MINOR(start);

    printk(KERN_INFO "I was assigned major number %d. To talk to\n", major);
    printk(KERN_INFO "the driver try to cat and echo to /dev/%s.\n", DEVICE_NAME);
    printk(KERN_INFO "Remove the module when done.\n");

    printk(KERN_INFO "prod_cons: Module loaded.\n");

    return 0;

    error_device:
    class_destroy(class);

    error_class:
    /* Destroy chardev */
    if (chardev) {
        cdev_del(chardev);
        chardev = NULL;
    }

    error_add:
    /* Destroy partially initialized chardev */
    if (chardev)
        kobject_put(&chardev->kobj);

    error_alloc:
    unregister_chrdev_region(start, 1);

    error_alloc_region:
    kfifo_free(&cbuf);

    return ret;
}


void exit_prodcons_module( void )
{
    if(module_refcount(THIS_MODULE) != 0){
        printk(KERN_INFO "prod_cons: Module is busy, can't be removed\n");
        return;
    }

    if (device)
        device_unregister(device);

    if (class)
        class_destroy(class);

    /* Destroy chardev */
    if (chardev)
        cdev_del(chardev);

    /*
    * Release major minor pair
    */
    unregister_chrdev_region(start, 1);

    kfifo_free(&cbuf);

    printk(KERN_INFO "prod_cons: Module unloaded.\n");
}


module_init( init_prodcons_module );
module_exit( exit_prodcons_module );
