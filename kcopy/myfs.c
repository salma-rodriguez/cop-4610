#include <linux/kernel.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/dcache.h>
#include <linux/limits.h>
#include <linux/linkage.h>
#include <linux/dnotify.h>
#include <linux/prefetch.h>
#include <asm/string.h>
#include <linux/stat.h>

__cacheline_aligned_in_smp DEFINE_SPINLOCK(dcache_lock);

ssize_t (*read)(struct file *, char *, size_t, loff_t *);
ssize_t (*write)(struct file *, const char *, size_t, loff_t *);

#define myread(file)                                                            \
if (file) {                                                                     \
if (file->f_mode & FMODE_READ) {                                            	\
	ret = locks_verify_area(FLOCK_VERIFY_READ, file->f_dentry->d_inode,     \
		file, file->f_pos, count);                                  	\
	if (!ret) {                                                             \
		ssize_t (*read)(struct file *, char *, size_t, loff_t *);       \
           	ret = -EINVAL;                                                  \
        if (file->f_op && (read = file->f_op->read) != NULL)                	\
                ret = read(file, buf, count, &file->f_pos);                     \
        }                                                                       \
}                                                                           	\
if (ret > 0)                                                                	\
	dnotify_parent(file->f_dentry, DN_ACCESS);                              \
	fput(file);                                                             \
}

asmlinkage long sys_mkdirat(int dfd, const char __user * pathname, umode_t mode);

static int fillonedir(void * __buf, const char * name,
	int namelen, loff_t offset, u64 ino, unsigned int d_type)
{
	struct file *ff;
	char *file_pathname = kmalloc(strlen((char*)__buf) + strlen(name) + 2, GFP_KERNEL);
	file_pathname = strcpy(file_pathname, (char*)__buf);
	file_pathname = strcat(file_pathname, "/");
	file_pathname = strcat(file_pathname, name);

	// Open files associated with d_entry
	ff = filp_open(file_pathname, O_RDONLY, 0);
	if (!IS_ERR(ff)) {
		filp_close(ff, NULL);
		kfree((void*)file_pathname);
		return 0;
	}
	kfree((void*)file_pathname);
	return -1;
}

asmlinkage long file_copy(const char *srcF, const char *destF)
{
	char *buf;
	int permflags;
	ssize_t sz;
	mm_segment_t old;
	struct file *sf, *df;

	old = get_fs();
	set_fs(KERNEL_DS);

	permflags = S_IRUSR | S_IWUSR | S_IROTH | S_IRGRP;

	sf = filp_open(srcF, O_RDONLY | O_LARGEFILE, 0);
	df = filp_open(destF, O_RDWR | O_CREAT | O_LARGEFILE, permflags);

	if (IS_ERR(sf)) {
		printk(KERN_ERR "error opening source file\n");
		return -EBADF;
	}

	if (IS_ERR(df)) {
		printk(KERN_ERR "error opening destination file\n");
		return -EBADF;
	}

	buf = kmalloc(sizeof(char *) * 512UL, GFP_KERNEL);

	do {
		sz = vfs_read(sf, buf, 512UL, &sf->f_pos);
		if (!(sz|0)) break;
		else if (sz < 0) {
			printk(KERN_ERR "error reading from source file\n");
			return -EIO;
		} else {
			if((sz = vfs_write(df, buf, sz, &df->f_pos)) < 0) {
				printk(KERN_ERR "error writing to destination file\n");
				return -EIO;
			}
		}
	} while(1);

	kfree(buf);
	filp_close(sf, NULL);
	filp_close(df, NULL);
	set_fs(old);
	return 0;
}

char* concat(int psiz, int numargs, ...) {
	char *s, *cat;
	va_list argp;
	/* 
	 * it's better to use zalloc
	 * because we want an empty buffer
	 * and concatenation won't work otherwise
	 */
	s = kzalloc(psiz, 0);
	va_start(argp, numargs);
	while(numargs--) {
		cat = va_arg(argp, char *);
		s = strcat(s, cat);
	}
	va_end(argp);
	return s;
}

asmlinkage long dir_copy(const char *srcD, const char *destD)
{
	mm_segment_t old;
	struct dentry *tmp;
	int permflags, psiz;
	struct file *sf, *df;
	char *d,*s,*src,*des,*pname;

	old = get_fs();
	set_fs(KERNEL_DS);

	sf = filp_open(srcD, O_DIRECTORY | O_RDONLY | O_LARGEFILE, 0);
	df = filp_open(destD, O_DIRECTORY | O_LARGEFILE, 0x777);

	if (IS_ERR(sf)) {
		printk(KERN_ERR "error opening source file\n");
		return -EBADF;
	}

	if (IS_ERR(df)) {
		printk(KERN_ERR "error opening destination file\n");
		return -EBADF;
	}
	
	sf->f_op->readdir(sf, (void *)srcD, &fillonedir);

	tmp = NULL;
	pname = (char *)sf->f_dentry->d_name.name;
	
	psiz = strlen(srcD) + 1;
	src = concat(psiz, 2, (char *)srcD, "\0");
	
	psiz = strlen(destD) + strlen(pname) + 2;
	des = concat(psiz, 3, (char *)destD, pname, "/\0");

	permflags = S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;

	sys_mkdirat(O_DIRECTORY, des, permflags);
	tmp = dget(sf->f_dentry);

	list_for_each_entry(tmp, &sf->f_dentry->d_subdirs, d_u.d_child)
	{
		if(!tmp->d_inode) /* entry null */ {
			continue;
		} else /* entry not null */ {
			struct qstr *qstr = &tmp->d_name;
			const char *name = qstr->name;
			umode_t mode = tmp->d_inode->i_mode;

			if (S_ISDIR(mode)) {
				psiz = strlen(src) + strlen(name) + 2;
				s = concat(psiz, 3, src, name, "/\0");
				psiz = strlen(des) + 1;
				d = concat(psiz, 2, des, "\0");
				dir_copy(s, d);
				kfree(s);
				kfree(d);
			} else /* it's a file */ {
				psiz = strlen(src) + strlen(name) + 1;
				s = concat(psiz, 3, src, name, "\0");
				psiz = strlen(des) + strlen(name) + 1;
				d = concat(psiz, 3, des, name, "\0");
				file_copy(s, d);
				kfree(s);
				kfree(d);
			}
		}

	}
	kfree(src);
	kfree(des);
	set_fs(old);
	return 0;
}
