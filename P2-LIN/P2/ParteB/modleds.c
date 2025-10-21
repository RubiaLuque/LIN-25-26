#include <linux/module.h> 
#include <asm-generic/errno.h>
#include <linux/init.h>
#include <linux/tty.h>      /* For fg_console */
#include <linux/kd.h>       /* For KDSETLED */
#include <linux/vt_kern.h>
#include <linux/syscalls.h>
#include <linux/kernel.h>

#define ALL_LEDS_ON 0x7
#define ALL_LEDS_OFF 0


struct tty_driver* kbd_driver= NULL;


/* Get driver handler */
struct tty_driver* get_kbd_driver_handler(void){
   printk(KERN_INFO "modleds: loading\n");
   printk(KERN_INFO "modleds: fgconsole is %x\n", fg_console);
   return vc_cons[fg_console].d->port.tty->driver;
}

/* Set led state to that specified by mask */
static inline int set_leds(struct tty_driver* handler, unsigned int mask){
    return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,mask);
}


SYSCALL_DEFINE1(lectl, unsigned int, leds){
    //Valor soportado de leds
    if(leds<0 || leds > 7) return -1;

    //Inicializacion
    kbd_driver= get_kbd_driver_handler();

    return set_leds(kbd_driver,(leds&0x1) | ((leds&0x4)>> 1) | ((leds&0x2) << 1));
}


