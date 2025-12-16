#include <linux/module.h>
#include <asm-generic/errno.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>

#define MANUAL_DEBOUNCE

#define ALL_LEDS_ON 0x7
#define ALL_LEDS_OFF 0

#define NR_GPIO_LEDS  3

const int led_gpio[NR_GPIO_LEDS] = {25, 27, 4};

/* Array to hold gpio descriptors */
struct gpio_desc* gpio_descriptors[NR_GPIO_LEDS];

#define GPIO_BUTTON 22
struct gpio_desc* desc_button = NULL;
static int gpio_button_irqn = -1;
static int led_state = ALL_LEDS_ON;

static unsigned int sequence_array[] = {000, 001, 010, 011, 100, 101, 110, 111};
static int seq_index = 0;
static int activate_timer = 0; //0--> No esta activado, 1--> Si esta activado

static unsigned int timer_period_ms = 1000; //1 segundo
static unsigned long timer_period_jiffies;
module_param(timer_period_ms, unsigned int, 0666);
MODULE_PARM_DESC(timer_period_ms, "Timer duration (in ms)");

//Timer
struct timer_list timer;

//Workqueue + spinlock
struct work_struct work;
DEFINE_SPINLOCK(timer_spinlock);


/* Set led state to that specified by mask */
static inline int set_pi_leds(unsigned int mask) {
  int i;
  for (i = 0; i < NR_GPIO_LEDS; i++)
    gpiod_set_value(gpio_descriptors[i], (mask >> i) & 0x1 );
  return 0;
}

static void timer_work_handler(struct work_struct* work){
    unsigned long irq_flags;

    //Desactiva las interrupciones y guarda las flags. Obtiene el spinlock
    spin_lock_irqsave(&timer_spinlock, irq_flags); 

    if(activate_timer){
        activate_timer = 0;
        spin_unlock_irqrestore(&timer_spinlock, irq_flags);
        del_timer_sync(&timer);
        printk(KERN_INFO "Pause\n");
    }
    else{
        activate_timer = 1;
        timer.expires = jiffies + timer_period_jiffies;
        add_timer(&timer);
        spin_unlock_irqrestore(&timer_spinlock, irq_flags);
        printk(KERN_INFO "Resume\n");
    }


}

/* Interrupt handler for button **/
static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
    #ifdef MANUAL_DEBOUNCE
    static unsigned long last_interrupt = 0;
    unsigned long diff = jiffies - last_interrupt;
    if (diff < 20)
        return IRQ_HANDLED;

    last_interrupt = jiffies;
    #endif

    //Difiere el trabajo usando una workqueue para detener/reanudar el timer
    schedule_work(&work);

    return IRQ_HANDLED;
}

static int leds_seq(unsigned int led_mask){
    led_mask=((led_mask&0x1) << 2)| (led_mask&0x2) | ((led_mask&0x4) >> 2);
    set_pi_leds(led_mask);
}

//Funcion a invocar cuando el timer acaba
static void fire_timer(struct timer_list *t){

    if(seq_index!=-1){
        leds_seq(sequence_array[seq_index]);
        seq_index++;
    }
    else{ //Vuelve a empezar la sequencia
        seq_index = 0;
    }

    
    mod_timer(t, jiffies + timer_period_jiffies);
}

static int __init timerleds_init(void){
    int i, j;
    int err = 0;
    char gpio_str[10];
    unsigned char gpio_out_ok = 0;

    //----------INICIO GESTION GPIOs------------
    for(i = 0; i< NR_GPIO_LEDS; ++i){
        //String ID
        sprintf(gpio_str, "led_%d", i);

        //Requesting GPIO LEDS
        if((err=gpio_request(led_gpio[i], gpio_str))){
            pr_err("Failed GPIO[%d] %d request\n", i, led_gpio[i]);
            goto err_handle;
        }

        //Transforming into descriptor
        if(!(gpio_descriptors[i] = gpio_to_desc(led_gpio[i]))){
            pr_err("GPIO[%d] %d is not valid\n", i, led_gpio[i]);
            err = -EINVAL;
            goto err_handle;
        }

        gpiod_direction_output(gpio_descriptors[i], 0);
    } 

    //Requesting GPIO Button
    if((err = gpio_request(GPIO_BUTTON, "button"))){
        pr_err("ERROR: GPIO %d request\n", GPIO_BUTTON);
        goto err_handle;
    }

    //Button descriptor
    if(!(desc_button = gpio_to_desc(GPIO_BUTTON))){
        pr_err("GPIO %d is not valid\n", GPIO_BUTTON);
        err = -EINVAL;
        goto err_handle;
    }

    gpio_out_ok = 1;

    //GPIO de boton como input
    gpiod_direction_input(desc_button);
    //-----------FIN GESTION GPIOs---------------

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
        goto err_handle;
    }

    //-----GESTION TIMER------
    timer_setup(&timer, fire_timer, 0);
    timer.expires = jiffies + 1; //Se activa en el siguiente tick
    add_timer(&timer);
    timer_period_jiffies = msecs_to_jiffies(timer_period_ms);

    //------GESTION WORKQUEUE-----
    INIT_WORK(&work, timer_work_handler);

    return 0;

err_handle:
    for(j=0;j<i;j++)
        gpiod_put(gpio_descriptors[j]);
    
    if(gpio_out_ok)
        gpio_put(desc_button);
    
    del_timer_sync(&timer);

    return err;
}

static void __exit timerleds_exit(void){
    int i = 0;
    free_irq(gpio_button_irqn, NULL);
    set_pi_leds(ALL_LEDS_OFF);

    for(i=0; i<NR_GPIO_LEDS; ++i){
        gpiod_put(gpio_descriptors[i]);
    }
    gpiod_put(desc_button);
    kfree(sequence_array);
    del_timer_sync(&timer);
    flush_scheduled_work(&work);
}

module_init(timerleds_init);
module_exit(timerleds_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Modleds");