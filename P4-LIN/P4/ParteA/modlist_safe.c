#include <linux/module.h>	/* Requerido por todos los módulos */
#include <linux/kernel.h>	/* Definición de KERN_INFO */
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/slab.h> //Usar kmalloc y vmalloc con memoria contigua 
#include <linux/uaccess.h> //Copy from user y copy to user
#include <linux/spinlock.h> //Uso de spinlocks


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

LIST_HEAD(myList);
DEFINE_RWLOCK(rw_lock); //Define un spinlock

//USO: escribe/elimina en la lista lo que se recibe desde espacio de usuario
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
		//Se espacio para un nuevo nodo de la lista
		struct list_item* node = kmalloc(sizeof(struct list_item), GFP_KERNEL);
		node->data = num;
        write_lock(&rw_lock); //SECCION CRITICA
		list_add_tail(&node->links, &myList); //Añade al final de la lista un nodo nuevo con el numero dicho
        write_unlock(&rw_lock); //FIN SECCION CRITICA
	}
	else if(sscanf(kbuf, "remove %i", &num)==1){
		struct list_head* cur_node=NULL;
		struct list_head* aux = NULL;
        write_lock(&rw_lock); //SECCION CRITICA
		list_for_each_safe(cur_node,aux, &myList){ //esta list for each safe permite eliminar un modo durante el recorrido de la lista
			struct list_item* item = list_entry(cur_node, struct list_item, links);
			if(item->data == num){
				list_del(&item->links);
				kfree(item);
			}
		}
        write_unlock(&rw_lock); //FIN SECCION CRITICA
	}
	else if(strcmp(kbuf,"cleanup\n")==0){ // el \n es por el salto de linea que hace echo
		struct list_head* cur_node=NULL;
		struct list_head* aux = NULL;
        write_lock(&rw_lock); //SECCION CRITICA
		list_for_each_safe(cur_node,aux, &myList){ //esta list for each safe permite eliminar un modo durante el recorrido de la lista
			struct list_item* item = list_entry(cur_node, struct list_item, links);
			list_del(&item->links);
			kfree(item); //libera la memoria del nodo
		}
        write_unlock(&rw_lock); //FIN SECCION CRITICA
	}
	else{
		return -EINVAL;
	}
	
	(*off)+=len;  //Se avanza en el desriptor de fichero
	return len;
}

//USO: lee de la lista lo que haya dentro y lo muestra en espacio de usuario
static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off){
	int read_bytes = 0;
	char kbuf[MAXLEN_R];
	char *dest = kbuf; //Puntero que recorre kbuf
	
	if ((*off) > 0){ /* Tell the application that there is nothing left to read */
		return 0;
	}
	
	struct list_head* cur_node = NULL;
    read_lock(&rw_lock); //SECCION CRITICA
	// Copia los datos de la lista a kbuf
	list_for_each(cur_node, &myList){
		struct list_item* item = list_entry(cur_node, struct list_item, links);
		char aux[10];
		int aux_chars = sprintf(aux, "%d\n", item->data);
		if(read_bytes + aux_chars >= MAXLEN_R-1){
			printk(KERN_INFO "Buffer overflow!");
            read_unlock(&rw_lock); //FIN SECCION CRITICA
			return -ENOSPC; //OVERFLOW
		}

		int num_chars = sprintf(dest, "%d\n", item->data);
		
		dest+=num_chars; //Avanza el numero de caracteres leidos
		read_bytes+=num_chars; //Numero de caracteres leidos en total
		
	}
    read_unlock(&rw_lock); //FIN SECCION CRITICA
	
	if (len<read_bytes)
		return -ENOSPC; //No hay espacio suficiente en el buffer de usuario
	
	if(copy_to_user(buf, kbuf, read_bytes))
		return -EINVAL; //Error al copiar a espacio de usuario
	
	(*off)+=len; //Avanza el descriptor de fichero
	return read_bytes;
	
}


/* Función que se invoca cuando se carga el módulo en el kernel */
int modulo_lin_init(void)
{
	int ret = 0;
	proc_entry = proc_create( "modlist_safe", 0666, NULL, &proc_entry_fops);
	if (proc_entry == NULL) {
		ret = -ENOMEM;
		printk(KERN_INFO "Modlist Safe: Can't create /proc entry\n");
	} else {
		printk(KERN_INFO "Modlist Safe: Module loaded\n");
	}
	
	return ret;
}

/* Función que se invoca cuando se descarga el módulo del kernel */
void modulo_lin_clean(void)
{
	remove_proc_entry("modlist_safe", NULL);
	printk(KERN_INFO "Modlist Safe: Module unloaded.\n");
}

static int modlist_open(struct inode *, struct file*){
	try_module_get(THIS_MODULE);
	return SUCCESS;
}

static int modlist_release(struct inode*, struct file*){
	module_put(THIS_MODULE);
	
	return 0;
}

/* Declaración de funciones init y exit */
static const struct proc_ops proc_entry_fops = {
	.proc_read = modlist_read,
	.proc_write = modlist_write,
	.proc_open = modlist_open,
	.proc_release = modlist_release    
};

module_init(modulo_lin_init);
module_exit(modulo_lin_clean);

