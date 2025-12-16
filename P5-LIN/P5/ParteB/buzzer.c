#include <linux/module.h>
#include <asm-generic/errno.h>
#include <linux/init.h>
#include <linux/tty.h> /* For fg_console */
#include <linux/kd.h>  /* For KDSETLED */
#include <linux/vt_kern.h>
#include <linux/pwm.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/string.h>


/* Frequency of selected notes in centihertz */
#define C4 26163
#define D4 29366
#define E4 32963
#define F4 34923
#define G4 39200
#define C5 52325

/*
 *  Prototypes
 */
static int pwm_module_init(void);
static void pwm_module_exit(void);

static ssize_t pwm_module_read(struct file *, char *, size_t, loff_t *);
static ssize_t pwm_module_write(struct file *, const char *, size_t, loff_t *);


#define PWM_DEVICE_NAME "buzzer"
#define PWM_CLASS_NAME "pwm_buzzer"

static struct pwm_device *pwm_device = NULL;
static struct pwm_state pwm_state;
static struct cdev *pwm_cdev = NULL;
static struct class *pwm_class = NULL;
static dev_t start;

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

/* Work descriptor */
struct work_struct my_work;

/* Structure to represent a note or rest in a melodic line  */
struct music_step
{
	unsigned int freq : 24; /* Frequency in centihertz */
	unsigned int len : 8;	/* Duration of the note */
};

struct music_step melody;

static struct file_operations fops = {
    .read = pwm_module_read,
    .write = pwm_module_write,
    .open = pwmchip_add,
    .release = pwmchip_release
};

static char *cool_devnode(struct device *dev, umode_t *mode)
{
  if (!mode)
    return NULL;
  if (MAJOR(dev->devt) == MAJOR(start))
    *mode = 0666;
  return NULL;
}

/* Transform frequency in centiHZ into period in nanoseconds */
static inline unsigned int freq_to_period_ns(unsigned int frequency)
{
	if (frequency == 0)
		return 0;
	else
		return DIV_ROUND_CLOSEST_ULL(100000000000UL, frequency);
}

/* Check if the current step is and end marker */
static inline int is_end_marker(struct music_step *step)
{
	return (step->freq == 0 && step->len == 0);
}

/**
 *  Transform note length into ms,
 * taking the beat of a quarter note as reference
 */
static inline int calculate_delay_ms(unsigned int note_len, unsigned int qnote_ref)
{
	unsigned char duration = (note_len & 0x7f);
	unsigned char triplet = (note_len & 0x80);
	unsigned char i = 0;
	unsigned char current_duration;
	int total = 0;

	/* Calculate the total duration of the note
	 * as the summation of the figures that make
	 * up this note (bits 0-6)
	 */
	while (duration) {
		current_duration = (duration) & (1 << i);

		if (current_duration) {
			/* Scale note accordingly */
			if (triplet)
				current_duration = (current_duration * 3) / 2;
			/*
			 * 24000/qnote_ref denote number of ms associated
			 * with a whole note (redonda)
			 */
			total += (240000) / (qnote_ref * current_duration);
			/* Clear bit */
			duration &= ~(1 << i);
		}
		i++;
	}
	return total;
}


/* Work's handler function */
static void my_wq_function(struct work_struct *work)
{
	struct music_step melodic_line[] = {
		{C4, 4}, {E4, 4}, {G4, 4}, {C5, 4}, 
		{0, 2}, {C5, 4}, {G4, 4}, {E4, 4}, 
		{C4, 4}, {0, 0} /* Terminator */
	};
	const int beat = 120; /* 120 quarter notes per minute */
	struct music_step *next;

	pwm_init_state(pwm_device, &pwm_state);

	/* Play notes sequentially until end marker is found */
	for (next = melodic_line; !is_end_marker(next); next++) {
		/* Obtain period from frequency */
		pwm_state.period = freq_to_period_ns(next->freq);

		/**
		 * Disable temporarily to allow repeating the same consecutive
		 * notes in the melodic line
		 **/
		pwm_disable(pwm_device);

		/* If period==0, its a rest (silent note) */
		if (pwm_state.period > 0) {
			/* Set duty cycle to 70 to maintain the same timbre */
			pwm_set_relative_duty_cycle(&pwm_state, 70, 100);
			pwm_state.enabled = true;
			/* Apply state */
			pwm_apply_state(pwm_device, &pwm_state);
		} else {
			/* Disable for rest */
			pwm_disable(pwm_device);
		}

		/* Wait for duration of the note or reset */
		msleep(calculate_delay_ms(next->len, beat));
	}

	pwm_disable(pwm_device);
}

static int pwmchip_add(struct inode *, struct file *){

}

static int pwmchip_release(struct inode *, struct file *){
    struct device_data *ddata = file->private_data;

  if (ddata == NULL)
    return -ENODEV;

  ddata->Device_Open--; /* We're now ready for our next caller */

  /*
   * Decrement the usage count, or else once you opened the file, you'll
   * never get get rid of the module.
   */
  pwm_disable(pwm_device);
  module_put(THIS_MODULE);

  return 0;
}

static ssize_t pwm_module_read(struct file *, char *, size_t, loff_t *){

}

static ssize_t pwm_module_write(struct file *, const char *, size_t, loff_t *){

}


static int pwm_module_init(void)
{
    int major;      /* Major number assigned to our device driver */
    int minor;      /* Minor number assigned to the associated character device */
    int ret;

    /* Get available (major,minor) range */
    if ((ret = alloc_chrdev_region (&start, 0, 1, PWM_DEVICE_NAME))) {
        printk(KERN_INFO "Can't allocate chrdev_region()");
        return ret;
    }

    /* Create associated cdev */
    if ((pwm_cdev = cdev_alloc()) == NULL) {
        printk(KERN_INFO "cdev_alloc() failed ");
        ret = -ENOMEM;
        goto error_alloc;
    }

    cdev_init(pwm_cdev, &fops);

    if ((ret = cdev_add(pwm_cdev, start, 1))) {
        printk(KERN_INFO "cdev_add() failed ");
        goto error_add;
    }

    /* Create custom class */
    pwm_class = class_create(THIS_MODULE, PWM_CLASS_NAME);

    if (IS_ERR(pwm_class)) {
        pr_err("class_create() failed \n");
        ret = PTR_ERR(pwm_class);
        goto error_class;
    }

    /* Establish function that will take care of setting up permissions for device file */
    pwm_class->devnode = cool_devnode;

    
    /* Request utilization of PWM0 device */
    pwm_device = pwm_request(0, PWM_DEVICE_NAME);
    
    
    if (IS_ERR(pwm_device)) {
        pr_err("Device_create failed\n");
        ret = PTR_ERR(pwm_device);
        goto error_device;
    }

    major = MAJOR(start);
    minor = MINOR(start);

    printk(KERN_INFO "I was assigned major number %d. To talk to\n", major);
    printk(KERN_INFO "the driver try to cat and echo to /dev/%s.\n", PWM_DEVICE_NAME);
    printk(KERN_INFO "Remove the module when done.\n");


    
	/* Initialize work structure (with function) */
	INIT_WORK(&my_work, my_wq_function);
    
	/* Enqueue work */
	schedule_work(&my_work);
    
	return 0;

    error_device:
        class_destroy(pwm_class);
    error_class:
        /* Destroy chardev */
        if (pwm_cdev) {
            cdev_del(pwm_cdev);
            pwm_cdev = NULL;
        }
    error_add:
        /* Destroy partially initialized chardev */
        if (pwm_cdev)
            kobject_put(&pwm_cdev->kobj);
    error_alloc:
        unregister_chrdev_region(start, 1);
    
        return ret;
}

static void pwm_module_exit(void)
{
	/* Wait until defferred work has finished */
	flush_work(&my_work);

	/* Release PWM device */
	pwm_free(pwm_device);

    /* Destroy the device and the class */
    if (pwm_device)
        device_destroy(pwm_class, pwm_device->devt);

    if (pwm_class)
        class_destroy(pwm_class);

    /* Destroy chardev */
    if (pwm_cdev)
        cdev_del(pwm_cdev);

    /*
     * Release major minor pair
     */
    unregister_chrdev_region(start, 1);
}

module_init(pwm_module_init);
module_exit(pwm_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PWM buzzer");
