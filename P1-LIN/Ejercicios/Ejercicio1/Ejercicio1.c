/*
Modifica alguno de los módulos del kernel de ejemplo para que la función de carga devuelva uno de estos errores (p.ej., return -EINVAL; ) 
- ¿Qué sucede al intentar cargar el módulo cuando esta función devuelve un número negativo? 
No se carga, aunque sí se imprime por pantalla el printk.

kernel@debian:~/Documentos/P1-LIN/Ejercicios/Ejercicio1$ sudo insmod Ejercicio1.ko
insmod: ERROR: could not insert module Ejercicio1.ko: Invalid parameters

kernel@debian:~/Documentos/P1-LIN/Ejercicios/Ejercicio1$ sudo dmesg | tail
[ 3159.621469] Modulo LIN cargado. Hola kernel.

- ¿Es posible descargar el módulo a continuación con rmmod?
kernel@debian:~/Documentos/P1-LIN/Ejercicios/Ejercicio1$ sudo rmmod Ejercicio1
rmmod: ERROR: Module Ejercicio1 is not currently loaded

Al no estar cargado, no se hace printk de "Adios kernel".



*/
#include <linux/module.h> /* Requerido por todos los módulos */
#include <linux/kernel.h> /* Definición de KERN_INFO */
//#include <asm-generic/errno.h> --> YA SE INCLUYEN EN MODULE.H
//#include <asm-generic/errno-base.h> //Define EINVAL y otros errores

MODULE_LICENSE("GPL"); /* Licencia del módulo */
/* Función que se invoca cuando se carga el módulo en el kernel */
int modulo_lin_init(void)
{
    printk(KERN_INFO "Modulo LIN cargado. Hola kernel.\n");
    /* Devolver -EINVAL para indicar una carga INCORRECTA del módulo */
    return -EINVAL;
}
/* Función que se invoca cuando se descarga el módulo del kernel */
void modulo_lin_clean(void)
{
    printk(KERN_INFO "Modulo LIN descargado. Adios kernel.\n");
}


/* Declaración de funciones init y cleanup */
module_init(modulo_lin_init);
module_exit(modulo_lin_clean);