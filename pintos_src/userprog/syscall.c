#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "threads/vaddr.h"
#include "threads/init.h"
#include "userprog/process.h"
#include <list.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "devices/input.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"

static void syscall_handler (struct intr_frame *);

//ADDITIONAL
void debug_(int *t);
typedef int pid_t;
int sys_close (int fd);
int sys_write (int fd, const void *buffer, unsigned length);
static struct lock fl_lock;
static struct file *search_file (int fd);
void debug_(int *t);
void validate_address (void *address);
struct file_descriptor
{
    int fd;
    struct file *file;
    struct list_elem elem;
    struct list_elem thread_elem;
};  
static struct list fl_list;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  list_init (&fl_list);
  lock_init (&fl_lock);
}

void debug_(int *t)
{
  int i;
  int argc = t[1];
  char **argv;
  argv = (char **) t[2];
  printf("ARGC:%d ARGV:%x\n", argc, (unsigned int)argv);
  for (i = 0; i < argc; i++)
     printf("Argv[%d] = %x pointing at %s\n",i, (unsigned int)argv[i], argv[i]);
  printf("\nDone");
}

int
sys_close(int fd)
{
  struct file_descriptor *fl;
  struct list_elem *l;
  struct thread *t = thread_current ();
  for (l = list_begin (&t->files); l != list_end (&t->files); l = list_next (l))
  {
      fl = list_entry (l, struct file_descriptor, thread_elem);
      if (fl->fd == fd && fl) 
      {
        file_close (fl->file);
        list_remove (&fl->elem);
        list_remove (&fl->thread_elem);
        free (fl);
        break;
      }
  } 
  return 0;
}

static int
get_fid (void)
{
  /*fids 0,1 are already fixed for stdin and stdout.*/
  static int fid = 2;
  return fid++;
}

int
sys_exit (int status)
{
  struct thread *t = thread_current ();
  struct list_elem *l;
  while (!list_empty (&t->files))
  {
      l = list_begin (&t->files);
      sys_close (list_entry (l, struct file_descriptor, thread_elem)->fd);
  }
  t->exit_status = status;
  thread_exit ();
  return -1;
}

static void
syscall_handler (struct intr_frame *f) 
{
  int *p = f->esp;
  validate_address (f->esp);
  switch(*p)
  {
    case SYS_EXIT:
    {
        if (!is_user_vaddr (p + 1))
        {
           sys_exit (-1);
        }
        int status = (int)(*(p+1));
        f->eax = sys_exit(status);
        break;
    }
    case SYS_EXEC:
    {
        if (!is_user_vaddr (p + 1))
        {
           sys_exit (-1);
        }
        char *cmd=(char*)*(p+1);
        if (!cmd || !is_user_vaddr (cmd)) f->eax = -1;
        lock_acquire (&fl_lock);
        f->eax = process_execute (cmd);
        lock_release (&fl_lock);
        break;
    }
    case SYS_WAIT:
    {
        if (!is_user_vaddr (p + 1))
        {
           sys_exit (-1);
        }
        f->eax = process_wait ((int)(*(p+1)));
        break;
    }
    case SYS_WRITE:
    {
          if (!(is_user_vaddr (p + 5) && is_user_vaddr (p + 6) && is_user_vaddr (p + 7) && is_user_vaddr (*(p + 6))))
          {
             sys_exit (-1);
          }
          struct file * fl;
          int fd=*(p+5);
    	  const void *buffer=*(p+6);
    	  unsigned length=*(p+7);
		  int ret = -1;
		  lock_acquire (&fl_lock);
		  if (fd == STDOUT_FILENO)
		  { 
		    putbuf (buffer, length);
		  }
		  else if (fd == STDIN_FILENO) 
		  {
		      lock_release (&fl_lock);
			  f->eax = ret;
	          break;
		  }
		  else if (!is_user_vaddr (buffer) || !is_user_vaddr (buffer + length))
		  {
		      lock_release (&fl_lock);
		      sys_exit (-1);
		  }
		  else
		  {
		      fl = search_file (fd);
		      if (!fl)
		      {
		          lock_release (&fl_lock);
				  f->eax = ret;
		          break;
		      }
		      ret = file_write (fl, buffer, length);
		  }   
		  lock_release (&fl_lock);
		  f->eax = ret;
          break;
    }
    case SYS_OPEN:
    {
	      if (!is_user_vaddr (p + 1))
	      {
	           sys_exit (-1);
	      }
          struct file *fe;
		  struct file_descriptor *desc;
		  int ret = -1;
		  if (!*(p+1))
		  {
		  	   f->eax = ret;
		       break;
		  }
		  if (!is_user_vaddr (*(p+1))) sys_exit(-1);	
		  lock_acquire(&fl_lock);  
		  fe = filesys_open (*(p+1));
		  if (!fe) goto done1;
		  desc = (struct file_descriptor *)malloc(sizeof(struct file_descriptor));
		  if (!desc) 
		  {
		      file_close(fe);
		      goto done1;
		  } 
		  desc->file = fe;
		  desc->fd = get_fid();
		  list_push_back (&fl_list, &desc->elem);
		  list_push_back (&thread_current()->files, &desc->thread_elem);
		  ret = desc->fd;
		  done1:
		  lock_release (&fl_lock);
		  f->eax = ret;
		  break;
    }
    case SYS_FILESIZE:
    {
    	  if (!is_user_vaddr (p + 1))
	      {
	           sys_exit (-1);
	      }
    	  struct file *fl;
    	  lock_acquire (&fl_lock);
		  fl = search_file (*(p+1));
		  if (!fl)
		  {
		   		f->eax=-1;
		  }
		  else
		  {
		  		f->eax=file_length (fl);
		  }
		  lock_release (&fl_lock);
		  break;
    }
    case SYS_CREATE:
    {
	      if (!is_user_vaddr (p + 4) || !is_user_vaddr (p + 5))
	      {
	           sys_exit (-1);
	      }
    	  if (!*(p+4)) sys_exit(-1);
		  else
		  {
		  	    lock_acquire (&fl_lock);
		  		f->eax = filesys_create (*(p+4), *(p+5));
		  		lock_release (&fl_lock);
		  }
		  break;
    } 
    case SYS_REMOVE:
    {
    	if (!is_user_vaddr(p + 1))
	    {
	           sys_exit (-1);
	    }
        if (!*(p+1))f->eax = false;
		else if(!is_user_vaddr (*(p+1)))
	  	{
	  		 sys_exit (-1); 
	  	}
		else
		{
     		lock_acquire (&fl_lock);
			f->eax=filesys_remove (*(p+1));
			lock_release (&fl_lock);
		} 
	  	break;
    }
    case SYS_READ:
    {
    	if (!(is_user_vaddr (p + 5) && is_user_vaddr (p + 6) && is_user_vaddr (p + 7) && is_user_vaddr (*(p + 6))))
        {
           sys_exit (-1);
        }
    	int fd=*(p+5);
    	void *buffer=*(p+6);
    	unsigned size=*(p+7);
		struct file * fl;
		unsigned i;
		int ret = -1; 
		lock_acquire (&fl_lock);
		if (fd == STDIN_FILENO) 
		{
		      for (i = 0; i != size; ++i) *(uint8_t *)(buffer + i) = input_getc ();
		      ret = size;
		      lock_release (&fl_lock);
			  f->eax=ret;
			  break;
		}
		else if (fd == STDOUT_FILENO)
		{
			  lock_release (&fl_lock);
			  f->eax=ret;
			  break;
		}
		else if (!is_user_vaddr (buffer) || !is_user_vaddr (buffer + size)) 
		{
		      lock_release (&fl_lock);
		      sys_exit (-1);
		}
		else
		{
		      fl = search_file (fd);
		      if (!fl)
		      {
		      	  lock_release (&fl_lock);
				  f->eax=ret;
				  break;
		      }
		      ret = file_read (fl, buffer, size);
		}  
		lock_release (&fl_lock);
		f->eax=ret;
		break;
    }
    case SYS_CLOSE:
    {
    	if (!is_user_vaddr(p + 1))
	    {
	          f->eax = sys_exit (-1);
	    }
	    lock_acquire(&fl_lock);
	    f->eax = sys_close(*(p+1));
	    lock_release(&fl_lock);
	    break;
    }
    default:
    {
      sys_exit(-1);
      break;
    }
  }
  return;
}

void validate_address (void *address)
{
  if (address == NULL || is_kernel_vaddr (address)||pagedir_get_page (thread_current ()->pagedir, address) == NULL) sys_exit (-1);
}

static struct file *search_file (int fd)
{
  struct file_descriptor *ret;
  struct list_elem *l;
  for (l = list_begin (&fl_list); l != list_end (&fl_list); l = list_next (l))
  {
      ret = list_entry (l, struct file_descriptor, elem);
      if (ret->fd == fd) return ret->file;
  } 
  return NULL;
}

