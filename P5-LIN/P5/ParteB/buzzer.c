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
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>



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
static int buzzer_open(struct inode *, struct file *);
static int buzzer_release(struct inode *, struct file *);
static ssize_t buzzer_read(struct file *, char *, size_t, loff_t *);
static ssize_t buzzer_write(struct file *, const char *, size_t, loff_t *);

#define SUCCESS 0
#define MANUAL_DEBOUNCE
#define DEVICE_NAME "buzzer"
#define PWM_DEVICE_NAME "pwmchip0"
#define BUF_LEN 10

#define GPIO_BUTTON 22
struct gpio_desc* desc_button = NULL;
static int gpio_button_irqn = -1;

static struct pwm_device *pwm_device = NULL;
static struct pwm_state pwm_state;

/* Structure to represent a note or rest in a melodic line  */
struct music_step
{
    unsigned int freq : 24; /* Frequency in centihertz */
    unsigned int len : 8;	/* Duration of the note */
};

/* Work descriptors, timer + spinlock */
struct work_struct play;
struct work_struct button;
struct timer_list timer;
DEFINE_SPINLOCK(sp);

//Melodia + beat
struct music_step* melody;
static unsigned int beat = 120;

static struct music_step* next_note=NULL; /* Puntero a la siguiente nota de la melodía 
											actual  (solo alterado por tarea diferida) */

typedef enum {
    BUZZER_STOPPED, /* Buzzer no reproduce nada (la melodía terminó o no ha comenzado) */
    BUZZER_PAUSED,	/* Reproducción pausada por el usuario */
    BUZZER_PLAYING	/* Buzzer reproduce actualmente la melodía */
} buzzer_state_t;

static buzzer_state_t buzzer_state=BUZZER_STOPPED; /* Estado actual de la reproducción */

typedef enum {
    REQUEST_START,		/* Usuario pulsó SW1 durante estado BUZZER_STOPPED */
    REQUEST_RESUME,		/* Usuario pulsó SW1 durante estado BUZZER_PAUSED */
    REQUEST_PAUSE,		/* Usuario pulsó SW1 durante estado BUZZER_PLAYING */
    REQUEST_CONFIG,		/* Usuario está configurando actualmente una nueva melodía vía /dev/buzzer  */
    REQUEST_NONE			/* Indicador de petición ya gestionada (a establecer por tarea diferida) */
} buzzer_request_t;

static buzzer_request_t buzzer_request=REQUEST_NONE;

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

//Manejador del trabajo del boton
static void button_wq_function(struct work_struct* work){
    unsigned long flags;
    buzzer_state_t state;

    spin_lock_irqsave(&sp, flags);
    state=buzzer_state;
    switch(state){
        case BUZZER_STOPPED:
            buzzer_request=REQUEST_START;
        break;
        case BUZZER_PAUSED:
            buzzer_request=REQUEST_RESUME;
        break;
        case BUZZER_PLAYING:
            buzzer_request=REQUEST_PAUSE;
        break;
    }

    spin_unlock_irqrestore(&sp, flags);
    schedule_work(&play);
}

static void play_wq_function(struct work_struct* work){
    unsigned long flags;
    buzzer_state_t state;
    buzzer_request_t request;
    struct music_step note;
    int play_next = 0;
    int curr_beat;

    spin_lock_irqsave(&sp, flags);

    state = buzzer_state;
    request = buzzer_request;
    curr_beat = beat;

    switch(request){
        case REQUEST_START:
            next_note = melody;
            if(next_note!=NULL && !is_end_marker(next_note)){
                buzzer_state = BUZZER_PLAYING;
                state = BUZZER_PLAYING;
            }
            buzzer_request = REQUEST_NONE;
        break;

        case REQUEST_RESUME:
            buzzer_state = BUZZER_PLAYING;
            state = BUZZER_PLAYING;
            buzzer_request = REQUEST_NONE;
        break;

        case REQUEST_PAUSE:
            buzzer_state = BUZZER_PAUSED;
            buzzer_request = REQUEST_NONE;
            spin_unlock_irqrestore(&sp, flags);
            pwm_disable(pwm_device);
            return;
        break;

        case REQUEST_CONFIG:
            buzzer_state = BUZZER_STOPPED;
            buzzer_request = REQUEST_NONE;
            next_note = NULL;
            spin_unlock_irqrestore(&sp, flags);
            pwm_disable(pwm_device);
            return;
        break;

        case REQUEST_NONE:
            //Nada que gestionar
        break;

    }

    //Se pasa a la siguiente nota de la melodia
    if(next_note != NULL && !is_end_marker(next_note) && state==BUZZER_PLAYING){
        note=*next_note;
        play_next = 1;
        next_note++;

        if(is_end_marker(next_note)){
            next_note = NULL;
            buzzer_state = BUZZER_STOPPED;
        }
    }

    spin_unlock_irqrestore(&sp, flags);

    //Se toca la siguiente nota si es posible
    if(play_next){
        pwm_state.period = freq_to_period_ns(note.freq);
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

        mod_timer(&timer, jiffies + msecs_to_jiffies(calculate_delay_ms(note.len, curr_beat)));
    }
}

static void fire_timer(struct timer_list *t){
    unsigned long flags;
    int play_next = 0; 

    spin_lock_irqsave(&sp, flags);

    if(buzzer_state==BUZZER_PLAYING && next_note!=NULL && !is_end_marker(next_note)){
        play_next = 1; //Se toca la siguiente nota
    }
    spin_unlock_irqrestore(&sp, flags);

    /**
         * Disable temporarily to allow repeating the same consecutive
         * notes in the melodic line
		 **/
    pwm_disable(pwm_device);

    if(play_next){
        schedule_work(&play);
    }

}

//Manejador interrupcion del boton al ser pulsado
static irqreturn_t gpio_irq_handler(int irq, void *dev_id){
    #ifdef MANUAL_DEBOUNCE
    static unsigned long last_interrupt = 0;
    unsigned long diff = jiffies - last_interrupt;
    if (diff < 20)
        return IRQ_HANDLED;

    last_interrupt = jiffies;
    #endif

    
    schedule_work(&button);

    return IRQ_HANDLED;
}



static int buzzer_open(struct inode *inode, struct file *file){
    if(!try_module_get(THIS_MODULE)){
        return -EBUSY;
    }
    return SUCCESS;
}

static int buzzer_release(struct inode *inode, struct file *file){
    module_put(THIS_MODULE);
    
    return 0;
}

static ssize_t buzzer_read(struct file *file, char __user * buffer, size_t len, loff_t *off){
    char kbuf[BUF_LEN];
    unsigned long flags;
    int _beat;
    
    if((*off)>0)
    return 0;
    
    spin_lock_irqsave(&sp, flags);
    _beat = beat;
    spin_unlock_irqrestore(&sp, flags);
    
    sprintf(kbuf, "beat=%d\n", _beat);
    if(len < strlen(kbuf)){
        return -ENOSPC;
    }
    
    if(copy_to_user(buffer, kbuf, strlen(kbuf))){
        return -EFAULT;
    }
    
    (*off)+= strlen(kbuf);
    return strlen(kbuf);
}

static ssize_t buzzer_write(struct file *file, const char __user * buffer, size_t len, loff_t *off){
    unsigned long flags;
    
    char* kbuf;
    unsigned int _beat;
    char* aux;
    char* token;
    struct music_step* aux_melody;
    
    kbuf = kmalloc(len + 1, GFP_KERNEL);
    
    if(!kbuf)
    return -ENOMEM;
    
    if(copy_from_user(kbuf, buffer, len)){
        kfree(kbuf);
        return -EFAULT;
    }
    
    kbuf[len] = '\0';
    aux = kbuf;
    //El usuario scribe el beat de la cancion
    if(sscanf(kbuf, "beat %u", &_beat) == 1){
        spin_lock_irqsave(&sp, flags);
        beat = _beat;
        spin_unlock_irqrestore(&sp, flags);
        
        
    }
    //El usuario escribe la melodia
    else if(strncmp(aux, "music ", 6)==0){
        spin_lock_irqsave(&sp, flags);
        if(buzzer_state == BUZZER_PLAYING){
            spin_unlock_irqrestore(&sp, flags);
            kfree(kbuf);
            printk(KERN_INFO "Unable to change music while playing.\n");
            return -EBUSY;
        }
        
        spin_unlock_irqrestore(&sp, flags);
        
        //Se avanza el puntero 6 espacios para que se posicione en el primer caracter de la melodia escrita
        aux+=6; 
        
        aux_melody = melody;
        while((token = strsep(&aux, ","))!=NULL){
            unsigned int _len, _freq;
            if(sscanf(token, "%u:%x", &_freq, &_len)){
                aux_melody->freq = _freq;
                aux_melody->len = _len;
                aux_melody++;
            }
            else{
                kfree(kbuf);
                return -EINVAL;
            }
        }
        
        //Terminacion final de la melodia
        aux_melody->freq = 0;
        aux_melody->len = 0;
        
        spin_lock_irqsave(&sp, flags);
        buzzer_request = REQUEST_CONFIG;
        spin_unlock_irqrestore(&sp, flags);
		schedule_work(&play);
        
        
        
    }
    else{
        kfree(kbuf);
        return -EINVAL;
    }
    
    kfree(kbuf);
    return len;
    
}

static struct file_operations fops = {
    .read = buzzer_read,
    .write = buzzer_write,
    .open = buzzer_open,
    .release = buzzer_release
};

static struct miscdevice pwm_misc_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &fops,
    .mode = 0666
};

static int pwm_module_init(void)
{
    int ret;
    int err = 0;
    unsigned char gpio_out_ok = 0;
    struct device *device;
    
    /* Request utilization of PWM0 device */
    pwm_device = pwm_request(0, PWM_DEVICE_NAME);
    
    if (IS_ERR(pwm_device)) {
        pr_err("Device_create failed\n");
        ret = PTR_ERR(pwm_device);
        goto err_handler;
    }

    ret = misc_register(&pwm_misc_device);
    if(ret){
        pr_err("Couldn't register pwm misc device\n");
        err = ret;
        goto err_handler;
    }

    device = pwm_misc_device.this_device;

    //Requesting GPIO Button
    if((err = gpio_request(GPIO_BUTTON, "button"))){
        pr_err("ERROR: GPIO %d request\n", GPIO_BUTTON);
        goto err_handler;
    }

    //Button descriptor
    if(!(desc_button = gpio_to_desc(GPIO_BUTTON))){
        pr_err("GPIO %d is not valid\n", GPIO_BUTTON);
        err = -EINVAL;
        goto err_handler;
    }

    gpio_out_ok = 1;

    //GPIO de boton como input
    gpiod_direction_input(desc_button);

    //Obtener numero IRQ para GPIO
    gpio_button_irqn=gpiod_to_irq(desc_button);
    pr_info("IRQ Number = %d\n", gpio_button_irqn);

    if(request_irq(gpio_button_irqn,
                   gpio_irq_handler,
                   IRQF_TRIGGER_RISING,
                   "button_leds",
                   NULL)){
        pr_err("my_device: cannot register IRQ");
        err = -EINVAL;
        goto err_handler;
    }

    melody = vmalloc(PAGE_SIZE);
    if(!melody){
        pr_err("Unable to allocate memory for melody\n");
        err = -ENOMEM;
        free_irq(gpio_button_irqn, NULL);
        goto err_handler;
    }
    memset(melody, 0, PAGE_SIZE);
    //Timer setup
    timer_setup(&timer, fire_timer, 0);

	/* Initialize work structure (with function) */
	INIT_WORK(&play, play_wq_function);
    INIT_WORK(&button, button_wq_function);

    pwm_init_state(pwm_device, &pwm_state);

	return 0;

err_handler:
    pwm_free(pwm_device);
    misc_deregister(&pwm_misc_device);
    if(gpio_out_ok){
        gpiod_put(desc_button);
    }
    
    return err;
}

static void pwm_module_exit(void)
{
	/* Wait until defferred work has finished */
    del_timer_sync(&timer);
    free_irq(gpio_button_irqn, NULL);
    gpiod_put(desc_button);
	flush_work(&play);
    flush_work(&button);
    vfree(melody);
	/* Release PWM device */
	pwm_free(pwm_device);
    misc_deregister(&pwm_misc_device);
}

module_init(pwm_module_init);
module_exit(pwm_module_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PWM buzzer");
