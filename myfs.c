#include <linux/kernel.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/dcache.h>
#include <linux/linkage.h>
#include <linux/dnotify.h>
#include <linux/prefetch.h>
#include <asm/string.h>
#include <linux/stat.h>

#define BUFSIZE 1024
#define MAX_INT 4294967295

__cacheline_aligned_in_smp DEFINE_SPINLOCK(dcache_lock);

ssize_t (*read)(struct file *, char *, size_t, loff_t *);
ssize_t (*write)(struct file *, const char *, size_t, loff_t *);

#define myread(file)                                                            \
if (file) {                                                                     \
    if (file->f_mode & FMODE_READ) {                                            \
        ret = locks_verify_area(FLOCK_VERIFY_READ, file->f_dentry->d_inode,     \
                    file, file->f_pos, count);                                  \
        if (!ret) {                                                             \
            ssize_t (*read)(struct file *, char *, size_t, loff_t *);           \
            ret = -EINVAL;                                                      \
            if (file->f_op && (read = file->f_op->read) != NULL)                \
                ret = read(file, buf, count, &file->f_pos);                     \
        }                                                                       \
    }                                                                           \
    if (ret > 0)                                                                \
        dnotify_parent(file->f_dentry, DN_ACCESS);                              \
    fput(file);                                                                 \
}

asmlinkage long sys_mkdirat(int dfd, const char __user * pathname, umode_t mode);



static int prepend(char **buffer, int *buflen, const char *str, int namelen) {\
    *buflen -= namelen;
    if (*buflen < 0)
        return -ENAMETOOLONG;
    *buffer -= namelen;
    memcpy(*buffer, str, namelen);
    return 0;
}

static int prepend_name(char **buffer, int *buflen, struct qstr *name) {
    return prepend(buffer, buflen, name->name, name->len);
}

char *get_path(struct dentry *dentry, char *buf, int buflen)
{
    char *end = buf + buflen;
    char *retval;

    spin_lock(&dcache_lock);
    prepend(&end, &buflen, "\0", 1);
    if (d_unlinked(dentry) && (prepend(&end, &buflen, "//deleted", 9) != 0))
        goto Elong;
    if (buflen < 1)
        goto Elong;
    // Get '/' right
    retval = end-1;
    *retval = '/';

    while (!IS_ROOT(dentry)) {
        struct dentry *parent = dentry->d_parent;

        prefetch(parent);
        if ((prepend_name(&end, &buflen, &dentry->d_name) != 0) ||
                (prepend(&end, &buflen, "/", 1) != 0))
                    goto Elong;

        retval = end;
        dentry = parent;
    }
    spin_unlock(&dcache_lock);
    return retval;

Elong:
    spin_unlock(&dcache_lock);
    return ERR_PTR(-ENAMETOOLONG);
}

static int fillonedir(void * __buf, const char *name,
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
        printk("After filp_open.\n");
        filp_close(ff, NULL);
        kfree((void*)file_pathname);
        return 0;
    }
    kfree((void*)file_pathname);
    return -1;
}

// typedef fillonedir_t int (*fillonedir);

asmlinkage long file_copy(const char *srcF, const char *destF)
{
    printk("file_copy is working!\n");
    printk("source: %s\n", srcF);
    printk("destination: %s\n", destF);

    char *buf;
    ssize_t sz;
    mm_segment_t old;
    old = get_fs();
    set_fs(KERNEL_DS);
    struct file *sf, *df;

    sf = filp_open(srcF, O_RDONLY | O_LARGEFILE, 0);
    df = filp_open(destF, O_RDWR | O_CREAT | O_LARGEFILE, S_IRUSR | S_IWUSR | S_IROTH | S_IRGRP);

    if(IS_ERR(sf) || IS_ERR(df))
        return -EINVAL;

    printk("After filp_open inside file_copy sys call\n");
    buf	= kmalloc(sizeof(char *) * 512UL, GFP_KERNEL);
    printk("After kmalloc\n");

    do {
        sz = vfs_read(sf, buf, 512UL, &sf->f_pos);
        printk("After read: %lu\n", sz);
        if (sz > 0) {
            printk("%s\n", buf);
            printk("bytes written: %lu\n", vfs_write(df, buf, sz, &df->f_pos));
            printk("After write\n");
        }
    } while(sz > 0);

    // dnotify_parent(sf->f_dentry, DN_ACCESS);

    printk("Before kfree\n");
    kfree(buf);
    printk("After kfree\n");
    filp_close(sf, NULL);
    filp_close(df, NULL);
    printk("After filp_close\n");
    set_fs(old);
    return 0;
}

asmlinkage long dir_copy(const char *srcD, const char *destD)
{
    printk("dir_copy is working!\n");
    char *pname;
    char *src, *des;
    char *d, *s, *f;
    struct dentry *tmp;
    mm_segment_t old;
    struct file *sf, *df;
	old = get_fs();
    set_fs(KERNEL_DS);

    sf = filp_open(srcD, O_DIRECTORY | O_RDONLY | O_LARGEFILE, 0);
    df = filp_open(destD, O_DIRECTORY | O_LARGEFILE, 0x777);

    if(IS_ERR(sf) || IS_ERR(df))
        return -EINVAL;

    printk("After filp_open %s\n",srcD);

    sf->f_op->readdir(sf, srcD, fillonedir);

    tmp = NULL; // = sf->f_path.dentry;
    pname = sf->f_dentry->d_name.name;

    src = kmalloc(strlen(srcD) + 1, 0);
    des = kmalloc(strlen(destD) + strlen(pname) + 2, 0);

    printk("After kmalloc src & des\n");

    src = strcpy(src, srcD);
    src = strcat(src, "\0");
    des = strcpy(des, destD);
    des = strcat(des, pname);
    des = strcat(des, "/");

    /* if (S_IFMT & (sf->f_dentry->d_inode->i_mode) == S_IFDIR) {
        des = strcat(des, pname);
        des = strcat(des, "/");
    } */

    des = strcat(des, "\0");

    sys_mkdirat(O_DIRECTORY, des, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    printk("after sys_mkdirat\n");


    tmp = dget(sf->f_dentry);
    printk("After dentry\n");

    list_for_each_entry(tmp, &sf->f_dentry->d_subdirs, d_u.d_child)
    {
        printk("Inside list_for_each\n");
        if(!tmp->d_inode) {
            printk("ENTRY NULL!!!!!!!\n");
            continue;
        } else {
            printk("ENTRY NOT NULL!!!!!!!\n");
            struct qstr *qstr = &tmp->d_name;
            const char *name = qstr->name;

            printk("My name is: %s\n", name);

            umode_t mode = tmp->d_inode->i_mode;
            printk("mode value: %lu\n", mode);

            // if ((S_IFMT & (i_mode))== S_IFDIR) {
            if (S_ISDIR(mode)) {
                printk("I am a directory! And my name is: %s\n", name);
                s = kmalloc(strlen(src) + strlen(name) + 2, 0);
                d = kmalloc(strlen(des) + strlen(name) + 2, 0);
                s = strcpy(s, src);
                s = strcat(s, name);
                s = strcat(s, "/\0");
                d = strcpy(d, des);
                d = strcat(d, "\0");
                dir_copy(s, d);
                kfree(s);
                kfree(d);
            } else /* it's a file */ {
                printk("I am a file! And my name is: %s\n", name);
                s = kmalloc(strlen(src) + strlen(name) + 1, 0);
                d = kmalloc(strlen(des) + strlen(name) + 1, 0);
                s = strcpy(s, src);
                s = strcat(s, name);
                s = strcat(s, "\0");
                d = strcpy(d, des);
                d = strcat(d, name);
                d = strcat(d, "\0");
                file_copy(s, d);
                kfree(s);
                kfree(d);
            }
        }

        /* switch(S_IFMT & tmp->d_inode->i_mode) {
            case (S_IFDIR) :
                printk("I am a directory!\n");
                // qstr = &tmp->d_name;
                // printk("the name of my d_entry is: %s\n", qstr->name);
                break;
        } */
    }
    kfree(src);
    kfree(des);
    set_fs(old);
    return 0;
}
