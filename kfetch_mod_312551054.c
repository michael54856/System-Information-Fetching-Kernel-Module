#include <linux/atomic.h> 
#include <linux/cdev.h> 
#include <linux/delay.h> 
#include <linux/device.h> 
#include <linux/fs.h> 
#include <linux/init.h> 
#include <linux/kernel.h> 
#include <linux/module.h> 
#include <linux/printk.h> 
#include <linux/types.h> 
#include <linux/uaccess.h> 
#include <linux/version.h> 
#include <asm/errno.h> 
#include <linux/net.h>
#include <linux/utsname.h>
#include <linux/cpu.h>
#include <linux/proc_fs.h>
#include <asm/processor.h>
#include <linux/ktime.h>


#define DEVICE_NAME "kfetch"
#define CLASS_NAME "kfetch_class"
#define KFETCH_BUF_SIZE 1024

#define KFETCH_NUM_INFO 6
#define KFETCH_RELEASE   (1 << 0)
#define KFETCH_NUM_CPUS  (1 << 1)
#define KFETCH_CPU_MODEL (1 << 2)
#define KFETCH_MEM       (1 << 3)
#define KFETCH_UPTIME    (1 << 4)
#define KFETCH_NUM_PROCS (1 << 5)

#define KFETCH_FULL_INFO ((1 << KFETCH_NUM_INFO) - 1)

static char kfetch_buf[KFETCH_BUF_SIZE];
static char readBuffer[8192];
static int mask = KFETCH_FULL_INFO;
static int major_number;
static struct class* kfetch_class;

enum 
{ 
    CDEV_NOT_USED = 0, 
    CDEV_EXCLUSIVE_OPEN = 1, 
}; 
static atomic_t already_open = ATOMIC_INIT(CDEV_NOT_USED); 

void getInfo_Kernel(void)
{
    struct new_utsname *uts;
    uts = utsname();
    strcat(kfetch_buf, "Kernel:\t");
    strcat(kfetch_buf, uts->release);
}

void getInfo_CPU(void)
{
    char temp[512] = {};
    char *result;
    int i = 13;
    struct file *file = filp_open("/proc/cpuinfo", O_RDONLY, 0);
    kernel_read(file,readBuffer,8192,&file->f_pos);
    result = strstr(readBuffer, "model name");
    while(1)
    {
        if(result[i] == '\n')
        {
            break;
        }
        temp[i-13] = result[i];
        i++;
    }
    strcat(kfetch_buf, "CPU:\t");
    strcat(kfetch_buf, temp);
    filp_close(file, NULL);
}

void getInfo_CPUs(void)
{
    char temp[512] = {};
    int totalCPU = 0;
    int onlineCPU = 0;
    int i;
    for_each_present_cpu(i)
    {
        totalCPU++;
    }
    for_each_online_cpu(i)
    {
        onlineCPU++;
    }

    sprintf(temp,"CPUs:\t%d / %d",onlineCPU,totalCPU);
    strcat(kfetch_buf, temp);
}

void getInfo_mem(void)
{
    char temp[512] = {};
    struct sysinfo mem_info;
    unsigned long total;
    unsigned long free;
    
    si_meminfo(&mem_info);
    
    total = ((unsigned long)mem_info.totalram * mem_info.mem_unit) >> 20;
    free = ((unsigned long)mem_info.freeram * mem_info.mem_unit) >> 20;
    
    
    sprintf(temp,"Mem:\t%lu MB / %lu MB",free,total);
    strcat(kfetch_buf, temp);
    
}

void getInfo_threads(void)
{
    char temp[512] = {};
    struct task_struct *task;
    int process_count = 0;
    for_each_process(task) {
        process_count++;
    }
    sprintf(temp,"Procs:\t%d",process_count);
    strcat(kfetch_buf, temp);
    
}

void getInfo_uptime(void)
{
    char temp[512] = {};
    s64 uptime_sec;
    uptime_sec = ktime_divns(ktime_get_coarse_boottime(), NSEC_PER_SEC);
    uptime_sec /= 60; //to min
    if(uptime_sec <= 1)
    {
        sprintf(temp,"Uptime:\t%lld min",uptime_sec);
    }
    else
    {
        sprintf(temp,"Uptime:\t%lld mins",uptime_sec);
    }
    strcat(kfetch_buf, temp);
}

void getInfo(int *tempMask)
{
   
    if(*tempMask == 0)
    {
        return;
    }
    
    if(KFETCH_RELEASE & (*tempMask))
    {
        *tempMask -= KFETCH_RELEASE;
        getInfo_Kernel();
        return;
    }
    if(KFETCH_CPU_MODEL & (*tempMask))
    {
        *tempMask -= KFETCH_CPU_MODEL;
        getInfo_CPU();
        return;
    }
    if(KFETCH_NUM_CPUS & (*tempMask))
    {
        *tempMask -= KFETCH_NUM_CPUS;
        getInfo_CPUs();
        return;
    }
    if(KFETCH_MEM & (*tempMask))
    {
        *tempMask -= KFETCH_MEM;
        getInfo_mem();
        return;
    }
    if(KFETCH_NUM_PROCS & (*tempMask))
    {
        *tempMask -= KFETCH_NUM_PROCS;
        getInfo_threads();
        return;
    }
    if(KFETCH_UPTIME & (*tempMask))
    {
        *tempMask -= KFETCH_UPTIME;
        getInfo_uptime();
        return;
    }
    
}

static int kfetch_open(struct inode *inode, struct file *filp) 
{
    if (atomic_cmpxchg(&already_open, CDEV_NOT_USED, CDEV_EXCLUSIVE_OPEN)) 
    {
        pr_err("kfetch: Device in used by another process\n");
        return -EBUSY;
    }
    try_module_get(THIS_MODULE);
    return 0;
}

static int kfetch_release(struct inode *inode, struct file *filp) 
{
    atomic_set(&already_open, CDEV_NOT_USED);
    module_put(THIS_MODULE); 
    return 0;
}

static ssize_t kfetch_read(struct file *filp, char __user *buffer, size_t length, loff_t *offset) 
{
    int len;
    struct new_utsname *uts;
    int tempMask;
    int hostNameLength;
    char hostnameStr[128] = {};
    
    tempMask = mask;
    
    memset(kfetch_buf, 0, KFETCH_BUF_SIZE);
    

    uts = &current->nsproxy->uts_ns->name;
    strcat(kfetch_buf,"                    ");
    strcat(kfetch_buf,uts->nodename);
    strcat(kfetch_buf,"\n");
    
    hostNameLength = strlen(uts->nodename);
    for(int i = 0; i < hostNameLength; i++)
    {
        hostnameStr[i] = '-';
    }
    
    strcat(kfetch_buf,"        .-.         ");
    strcat(kfetch_buf,hostnameStr);
    strcat(kfetch_buf,"\n");
    
    strcat(kfetch_buf,"       (.. |        ");
    getInfo(&tempMask);
    strcat(kfetch_buf,"\n");
    
    strcat(kfetch_buf,"       <>  |        ");
    getInfo(&tempMask);
    strcat(kfetch_buf,"\n");
    
    strcat(kfetch_buf,"      / --- \\       ");
    getInfo(&tempMask);
    strcat(kfetch_buf,"\n");
    
    strcat(kfetch_buf,"     ( |   | |      ");
    getInfo(&tempMask);
    strcat(kfetch_buf,"\n");
    
    strcat(kfetch_buf,"   |\\\\_)___/\\)/\\    ");
    getInfo(&tempMask);
    strcat(kfetch_buf,"\n");
    
    strcat(kfetch_buf,"  <__)------(__/    ");
    getInfo(&tempMask);
    strcat(kfetch_buf,"\n");
    
    len = strlen(kfetch_buf)+1;
    
  

    if (copy_to_user(buffer, kfetch_buf, len)) {
        pr_alert("Failed to copy data to user");
        return -EFAULT;
    }

    return len;
}

static ssize_t kfetch_write(struct file *filp, const char __user *buffer, size_t length, loff_t *offset) 
{
    int mask_info;
    if (copy_from_user(&mask_info, buffer, length)) 
    {
        pr_alert("Failed to copy data from user");
        return -EFAULT;
    }
    mask = mask_info;
    return 0;
}

static const struct file_operations kfetch_fops = {
    .open = kfetch_open,
    .release = kfetch_release,
    .read = kfetch_read,
    .write = kfetch_write,
};

static int __init kfetch_init(void) \
{
    major_number = register_chrdev(0, DEVICE_NAME, &kfetch_fops); 

    if (major_number < 0) 
    { 
        pr_alert("kfetch: Failed to allocate a major number\n"); 
        return major_number; 

    } 

    #if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0) 

    kfetch_class = class_create(DEVICE_NAME); 
 
    #else 

    kfetch_class = class_create(THIS_MODULE, DEVICE_NAME); 

    #endif 
    
    device_create(kfetch_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME); 

    return 0; 
}

static void __exit kfetch_exit(void) 
{
    device_destroy(kfetch_class, MKDEV(major_number, 0)); 
    class_destroy(kfetch_class); 
    unregister_chrdev(major_number, DEVICE_NAME); 
}

module_init(kfetch_init);
module_exit(kfetch_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("312551054");
MODULE_DESCRIPTION("kfetch kernel module");

