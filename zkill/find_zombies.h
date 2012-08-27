#define BUFSIZE 16
#define kthread(k, threadfn, namefmt, ...)				\
	k = kthread_run(threadfn, NULL, namefmt, ## __VA_ARGS__);		
#define allocate()							\
	({								\
	 struct task_struct **v;					\
	 								\
	 v = kmalloc(BUFSIZE * sizeof(struct task_struct), GFP_KERNEL);	\
	 v;								\
	 })

#define release(name) \
	kfree(name);

struct task_struct *p;
struct task_struct *c;
struct task_struct *k;

int find_zombies(void *data);
int kill_zombies(void *data);
