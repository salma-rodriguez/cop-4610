#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include "find_zombies.h"
#include <linux/semaphore.h>

#define BUFSIZE 16

struct semaphore full, empty;
int i, j, count;
struct task_struct **v;
DEFINE_SEMAPHORE(mutex);

#define to_kthread(tsk) \
	container_of((tsk)->vfork_done, struct kthread, exited)

struct kthread {
	int should_stop;
	void *data;
	struct completion exited;
};

int my_kthread_stop(struct task_struct *k)
{
	struct kthread *kthread;
	int ret;

	// trace_sched_kthread_stop(k);
	get_task_struct(k);

	kthread = to_kthread(k);
	barrier();
	if (k -> vfork_done != NULL) {
		kthread -> should_stop = 1;
		up(&full);
		wake_up_process(k);
		// up(&full);
		wait_for_completion(&kthread->exited);
	}
	ret = k -> exit_code;

	put_task_struct(k);
	// trace_sched_kthread_stop_ret(ret);

	return ret;
}

static int __init mymodule_init(void)
{
	i = j = count = 0;
	v = allocate();
	full = (struct semaphore)__SEMAPHORE_INITIALIZER(full, 0);
	empty = (struct semaphore)__SEMAPHORE_INITIALIZER(empty, BUFSIZE);
	// sema_init(&full, 0);
	// sema_init(&empty, BUFSIZE);
	printk(KERN_INFO "Looking for zombie processes...\n");
	kthread(p, find_zombies, "find_zombies");
	kthread(c, kill_zombies, "kill_zombies1");
	kthread(k, kill_zombies, "kill_zombies2");
	return 0;
}

/*int kthread_function(void *t)
  {
// daemonize("new_thread");
// set_current_state(TASK_INTERRUPTIBLE);
while(!kthread_should_stop()) {
msleep(100);
// Do something here
// set_current_state(TASK_INTERRUPTIBLE);
// schedule();
}
// set_current_state(TASK_RUNNING);
return 0;
}*/

int find_zombies(void *t)
{
	struct task_struct *p;
	while (1) {
		for_each_process(p) {
			if (p -> exit_state == EXIT_ZOMBIE)
			{
				printk(KERN_INFO "%d found\n", p -> pid);
				down_interruptible(&empty);
				/* while (count == BUFSIZE) {
				// msleep(100);
				schedule();
				if (kthread_should_stop())
				return 0;
				}*/
				if (kthread_should_stop())
					return 0;
				down_interruptible(&mutex);
				v[i] = p;
				i = (i + 1) % BUFSIZE;
				up(&mutex);
				up(&full);
			}
		}
		msleep(500);
		if (kthread_should_stop())
			return 0;
	}
}

int kill_zombies(void *t)
{
	while (1)
	{
		down_interruptible(&full);
		/* while (count == 0) {
		// msleep(100);
		schedule();
		if (kthread_should_stop())
		return 0;
		}*/
		if (kthread_should_stop())
			return 0;
		printk("%d killed ", v[j] -> pid);
		down_interruptible(&mutex);
		printk("address: %p\n", v[j]); 
		release_task(v[j]);
		printk("after release_task\n");
		j = (j + 1) % BUFSIZE;
		up(&mutex);
		up(&empty);
		schedule();
		if (kthread_should_stop())
			return 0;
	}
}

static void __exit mymodule_exit(void)
{
	up(&empty);
	kthread_stop(p);
	my_kthread_stop(c);
	my_kthread_stop(k);
	release(v);
	printk("Exiting module...\n");
}

module_init(mymodule_init);
module_exit(mymodule_exit);

MODULE_LICENSE("GPL");
