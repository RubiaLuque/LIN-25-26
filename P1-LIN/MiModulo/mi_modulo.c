#include <linux/module.h>	/* Requerido por todos los módulos */
#include <linux/kernel.h>	/* Definición de KERN_INFO */
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/slab.h.h> //Usar kmalloc y vmalloc con memoria contigua 
#include <linux/uaccess.h> //Copy from user y copy to user
MODULE_LICENSE("GPL"); 	/*  Licencia del modulo */

#define MAXLEN_W 30
#define MAXLEN_R 256

struct list_head myList; //Nodo fantasma de cabecera de la lista enlazada

//Nodos de la lista
struct list_item {
	int data;
	struct list_head links;
};

static struct proc_dir_entry *proc_entry;

/* Función que se invoca cuando se carga el módulo en el kernel */
int modulo_lin_init(void)
{
	int ret = 0;
    proc_entry = proc_create( "modlist", 0666, NULL, &proc_entry_fops);
    if (proc_entry == NULL) {
      ret = -ENOMEM;
      printk(KERN_INFO "Modlist: Can't create /proc entry\n");
    } else {
      printk(KERN_INFO "Modlist: Module loaded\n");
    }
	return ret;
}

/* Función que se invoca cuando se descarga el módulo del kernel */
void modulo_lin_clean(void)
{
	remove_proc_entry("modlist", NULL);
  	printk(KERN_INFO "Modlist: Module unloaded.\n");
}

//buf es un puntero al espacio de usuario y len el numero de caracteres almacenados en buf
static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off){
	int num;
	int available_space = MAXLEN_W-1;
	char kbuf[MAXLEN_W];

	if (len > available_space) {
    	printk(KERN_INFO "Modlist: not enough space!!\n");
    	return -ENOSPC;
  	}

	//LEE DEL ESPACIO DE USUARIO Y LO GUARDA EN KBUF
	if(copy_from_user(&kbuf, buf, len)){
		return -EFAULT;
	}

	kbuf[len] = '\0'; //Terminacion de cadena

	if(sscanf(kbuf, "add %i", &num)==1){
		//INSERCION EN LA LISTA
	}
	else if(sscanf(kbuf, "remove %i", &num)==1){
		//ELIMINACION EN LA LISTA
	}
	else if(strcmp(kbuf,"cleanup\n")==1){ // el \n es por el salto de linea que hace echo
		//BORRAR LISTA
	}

	(*off)+=len;  //Se avanza en el desriptor de fichero
	return len;
}

static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off){
	int read_bytes;
	char kbuf[MAXLEN_R];
	char *dest = kbuf;

	if ((*off) > 0) /* Tell the application that there is nothing left to read */
      return 0;

	if (len<read_bytes)
    	return -ENOSPC;

	list_for_each(){
		//No es segura porque hay que hacer control de desbordamiento de la lista
	}



}

static const struct proc_ops proc_entry_fops = {
    .proc_read = modlist_read,
    .proc_write = modlist_write,    
};

/* Declaración de funciones init y exit */
module_init(modulo_lin_init);
module_exit(modulo_lin_clean);

