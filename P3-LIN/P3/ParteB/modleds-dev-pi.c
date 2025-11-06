#include <linux/module.h>
#include <asm-generic/errno.h>
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/fs.h>
#include <linux/uaccess.h> /* for copy_to_user */
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/kernel.h>  
#include <linux/string.h>

#define ALL_LEDS_ON 0x7
#define ALL_LEDS_OFF 0
#define NR_GPIO_LEDS 3

MODULE_DESCRIPTION("ModledsPi_gpiod Kernel Module - FDI-UCM");
MODULE_AUTHOR("Juan Carlos Saez");
MODULE_LICENSE("GPL");

/*
 *  Prototypes
 */
int init_module(void);
void cleanup_module(void);
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);

#define SUCCESS 0
#define DEVICE_NAME "modledspi" /* Dev name as it appears in /proc/devices   */
#define CLASS_NAME "cool"
#define BUF_LEN 10 /* Max length of the message from the device */

static dev_t start;
static struct cdev *modledspi = NULL;
static struct class *class = NULL;

struct device_data
{
  int Device_Open;   /* Is device open?  Used to prevent multiple access to device */
  char msg[BUF_LEN]; /* The msg the device will give when asked */
  char *msg_Ptr;     /* This will be initialized every time the
                        device is opened successfully */
  int counter;       /* Tracks the number of times the character
                           device has been opened */
  struct device *device;
  dev_t major_minor;
};

static struct file_operations fops = {
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .release = device_release};

/* Actual GPIOs used for controlling LEDs */
const int led_gpio[NR_GPIO_LEDS] = {25, 27, 4};

/* Array to hold gpio descriptors */
struct gpio_desc *gpio_descriptors[NR_GPIO_LEDS];

static char *cool_devnode(struct device *dev, umode_t *mode)
{
  if (!mode)
    return NULL;
  if (MAJOR(dev->devt) == MAJOR(start))
    *mode = 0666;
  return NULL;
}

/* Set led state to that specified by mask */
inline int set_pi_leds(unsigned int mask) {
  int i;
  for (i = 0; i < NR_GPIO_LEDS; i++)
    gpiod_set_value(gpio_descriptors[i], (mask >> i) & 0x1);
  return 0;
}

int init_module(void)
{
  int i, j;
  int err = 0;
  char gpio_str[10];

  // INICIALIZACION DEL DISPOSITIVO CARACTERES

  int major; /* Major number assigned to our device driver */
  int minor; /* Minor number assigned to the associated character device */
  int ret = 0;
  struct device_data *ddata;

  /* Get available (major,minor) range */
  if ((ret = alloc_chrdev_region(&start, 0, 1, DEVICE_NAME)))
  {
    printk(KERN_INFO "Can't allocate chrdev_region()");
    return ret;
  }

  /* Create associated cdev */
  if ((modledspi = cdev_alloc()) == NULL)
  {
    printk(KERN_INFO "cdev_alloc() failed ");
    ret = -ENOMEM;
    goto error_alloc;
  }

  cdev_init(modledspi, &fops);

  if ((ret = cdev_add(modledspi, start, 1)))
  {
    printk(KERN_INFO "cdev_add() failed ");
    goto error_add;
  }

  /* Create custom class */
  class = class_create(THIS_MODULE, CLASS_NAME);

  if (IS_ERR(class))
  {
    pr_err("class_create() failed \n");
    ret = PTR_ERR(class);
    goto error_class;
  }

  /* Establish function that will take care of setting up permissions for device file */
  class->devnode = cool_devnode;

  /* Allocate device state structure and zero fill it */
  if ((ddata = kzalloc(sizeof(struct device_data), GFP_KERNEL)) == NULL)
  {
    ret = -ENOMEM;
    goto error_alloc_state;
  }

  /* Proper initialization */
  ddata->Device_Open = 0;
  ddata->counter = 0;
  ddata->msg_Ptr = NULL;
  ddata->major_minor = start; /* Only one device */

  /* Creating device */
  ddata->device = device_create(class, NULL, start, ddata, "%s%d", DEVICE_NAME, 0);

  if (IS_ERR(ddata->device))
  {
    pr_err("Device_create failed\n");
    ret = PTR_ERR(ddata->device);
    goto error_device;
  }

  major = MAJOR(start);
  minor = MINOR(start);

  // INICIALIZACION DE LOS LEDS GPIO

  for (i = 0; i < NR_GPIO_LEDS; i++)
  {
    /* Build string ID */
    sprintf(gpio_str, "led_%d", i);
    /* Request GPIO */
    if ((err = gpio_request(led_gpio[i], gpio_str)))
    {
      pr_err("Failed GPIO[%d] %d request\n", i, led_gpio[i]);
      goto err_handle;
    }

    /* Transforming into descriptor */
    if (!(gpio_descriptors[i] = gpio_to_desc(led_gpio[i])))
    {
      pr_err("GPIO[%d] %d is not valid\n", i, led_gpio[i]);
      err = -EINVAL;
      goto err_handle;
    }

    gpiod_direction_output(gpio_descriptors[i], 0);
  }

  set_pi_leds(ALL_LEDS_OFF);
  return 0;

// ERRORES

err_handle:
  for (j = 0; j < i; j++)
    gpiod_put(gpio_descriptors[j]);
  
  if (ddata)
  kfree(ddata);
  class_destroy(class);

  /* Destroy modled */
  if (modledspi)
  {
    cdev_del(modledspi);
    modledspi = NULL;
  }

  goto error_alloc;
  return err;

error_device:
  if (ddata)
    kfree(ddata);

error_alloc_state:
  class_destroy(class);

error_class:
  /* Destroy modled */
  if (modledspi)
  {
    cdev_del(modledspi);
    modledspi = NULL;
  }

error_add:
  /* Destroy partially initialized modledpi */
  if (modledspi)
    kobject_put(&modledspi->kobj);

error_alloc:
  unregister_chrdev_region(start, 1);

  return ret;


}

void cleanup_module(void)
{
  int i = 0;

  set_pi_leds(ALL_LEDS_OFF);

  for (i = 0; i < NR_GPIO_LEDS; i++)
    gpiod_put(gpio_descriptors[i]);

  struct device *device = class_find_device_by_devt(class, start);
  struct device_data *ddata;

  if (device)
  {
    ddata = dev_get_drvdata(device);

    if (ddata)
      kfree(ddata);

    device_destroy(class, device->devt);
  }

  class_destroy(class);

  if (modledspi)
    cdev_del(modledspi);

  unregister_chrdev_region(start, 1);
}

// Se llama cuando un proceso abre el dispositivo con cat /dev/modledspi
static int device_open(struct inode *inode, struct file *file)
{
  struct device *device;
  struct device_data *ddata;

  /* Retrieve device from major minor of the device file */
  device = class_find_device_by_devt(class, inode->i_rdev);

  if (!device)
    return -ENODEV;

  /* Retrieve driver's private data from device */
  ddata = dev_get_drvdata(device);

  if (!ddata)
    return -ENODEV;

  if (ddata->Device_Open)
    return -EBUSY;

  ddata->Device_Open++;

  /* Initialize msg */
  printk(KERN_ALERT "GPI dev module successfully opened\n");

  /* Initially, this points to the beginning of the message */
  ddata->msg_Ptr = ddata->msg;

  /* save our object in the file's private structure */
  file->private_data = ddata;

  /* Increment the module's reference counter */
  try_module_get(THIS_MODULE);

  return SUCCESS;
}

static int device_release(struct inode *inode, struct file *file)
{
  struct device_data *ddata = file->private_data;

  if (ddata == NULL)
    return -ENODEV;

  ddata->Device_Open--; /* We're now ready for our next caller */

  /*
   * Decrement the usage count, or else once you opened the file, you'll
   * never get get rid of the module.
   */
  module_put(THIS_MODULE);

  return 0;
}

static ssize_t device_write(struct file *file, const char *buffer, size_t length, loff_t *offset)
{
  char kbuf[BUF_LEN];
  unsigned int led_mask = ALL_LEDS_OFF;

  if (length > BUF_LEN - 1)
    return -ENOSPC;

  if (copy_from_user(kbuf, buffer, length))
    return -EFAULT;

  kbuf[length] = '\0';

  if(strcmp(kbuf, "\n")==0){
    set_pi_leds(ALL_LEDS_OFF);
    return length;
  }

  if (sscanf(kbuf, "%u", &led_mask) != 1) {
    printk(KERN_ALERT "Invalid argument\n");
    return -EINVAL;
  }

  if (led_mask > ALL_LEDS_ON) {
    printk(KERN_ALERT "Invalid argument\n");
    return -EINVAL;
  }

  led_mask= ((led_mask&0x1) << 2)| (led_mask&0x2) | ((led_mask&0x4) >> 2);
  set_pi_leds(led_mask);

  return length;
}

static ssize_t device_read(struct file *filp, char *buffer, size_t length, loff_t * offset)
{
    return -EPERM;
}

