#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched/signal.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/uaccess.h>
#include <linux/version.h>

/* --- PROTOTYPES & DEFINITIONS --- */
#define DEVICE_NAME "americanonpauser"
#define MAX_PROC_ENTRIES 1024
#define PROC_STATE_FILE "state.americano"
#define PROC_PAUSE_FILE "pause.americano"

static struct proc_dir_entry *americano_dir;
static struct proc_dir_entry *state_entry;
static struct proc_dir_entry *pause_entry;

/* Structure to keep track of system-wide metrics for the header */
struct americano_stats {
    u64 last_update_ns;
    int total_tasks;
    int stopped_tasks;
};

//------------------------------------------------------------------------------------------------

/* --- ENFORCEMENT ENGINE (PAUSE LOGIC) --- */
/* This handles the writing to /proc/pause.americano */
static ssize_t americano_write_enforcement(struct file *file, const char __user *buffer, 
                                     size_t count, loff_t *data) {
    char *input_buf;
    char *token, *curr;
    int pid_val;
    struct pid *pid_struct;
    struct task_struct *task;

    // Allocate memory on KERNEL RAM
    
    //4 Kb page size
    if (count > PAGE_SIZE) return -EINVAL;

    //GFP_KERNEL = soft realtime load
    input_buf = kmalloc(count + 1, GFP_KERNEL);
    if (!input_buf) return -ENOMEM; //failed allocation

    //write in the RAM new page
    if (copy_from_user(input_buf, buffer, count)) { //returns a non-zero if the function fails
        kfree(input_buf); //free the memory
        return -EFAULT;
    }
    input_buf[count] = '\0'; //null terminator to stop
    
    //iterate on the PIDs in the file to pause them
    
    curr = input_buf; // start position
    while ((token = strsep(&curr, ",")) != NULL) { // for each PID, find it and pause it if present
        if (kstrtoint(token, 10, &pid_val) == 0) {
            rcu_read_lock();
            pid_struct = find_get_pid(pid_val); //get the real PID pointer 
            if (pid_struct) {
                task = get_pid_task(pid_struct, PIDTYPE_PID); // get the process pointer
                if (task) {
                    send_sig(SIGSTOP, task, 0); // pause the process
                    put_task_struct(task); //release the process (decrease usage counter)
                }
                put_pid(pid_struct); // release the PID
            }
            rcu_read_unlock();
        }
    }

    kfree(input_buf); // free kernel RAM
    return count; // number of bytes written on memory (count > 0 == success)
}

/*
JSON example: 
{
  "timestamp": 1781615464000000000,
  "tasks": [
    {
      "pid": 1,
      "comm": "google",
      "state": 1,
      "cpu": 0,      //current assigned cores
      "mem": 11520   // in Kb
    },
    {
      "pid": 8421,
      "comm": "bash",
      "state": 0,
      "cpu": 1,
      "mem": 5120
    }
  ]
}

/* --- TELEMETRY ENGINE (STATE LOGIC) --- */
static int americano_telemetry_show(struct seq_file *m, void *v) {
    struct task_struct *task;
    unsigned long rss;
    
    //write timestamp
    seq_printf(m, "{\"timestamp\":%llu,\"tasks\":[", ktime_get_real_ns());

    rcu_read_lock();
    bool first = true;
    for_each_process(task) {
        if (!task) continue;
        
        // Memory calculation  
        rss = 0; //initialize Resident Set Size
        if (task->mm) {
            task_lock(task); // critcic section
            if (task->mm) { // mm descriptor has virtualized addresses
                rss = get_mm_rss(task->mm) << (PAGE_SHIFT - 10);
            }
            task_unlock(task);
        }
        
        if (!first) seq_printf(m, ",");
        seq_printf(m, "{\"pid\":%d,\"comm\":\"%s\",\"state\":%ld,\"cpu\":%d,\"mem\":%lu}",
                   task->pid, task->comm, task->state, task_cpu(task), rss);
        first = false;
    }
    rcu_read_unlock();

    seq_printf(m, "]}\n");
    return 0;
}

//------------------------------------------------------------------------------------------------

static int americano_state_open(struct inode *inode, struct file *file) {
    return single_open(file, americano_telemetry_show, NULL);
}

/* Mapping Kernel Operations to Virtual Files */
static const struct proc_ops state_fops = {
    .proc_open    = americano_state_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static const struct proc_ops pause_fops = {
    .proc_write   = americano_write_enforcement,
};

/* --- SUBSYSTEM LIFECYCLE --- */
static int __init americano_subsystem_init(void) {
    printk(KERN_INFO "[americano] Initializing High-Level Resource Orchestrator...\n");

    americano_dir = proc_mkdir("americanonpauser", NULL);
    if (!americano_dir) return -ENOMEM;

    state_entry = proc_create(PROC_STATE_FILE, 0444, americano_dir, &state_fops);
    pause_entry = proc_create(PROC_PAUSE_FILE, 0222, americano_dir, &pause_fops);

    if (!state_entry || !pause_entry) {
        remove_proc_entry("americanonpauser", NULL);
        return -ENOMEM;
    }

    printk(KERN_INFO "[americano] Operational at /proc/americanonpauser/\n");
    return 0;
}

static void __exit americano_subsystem_exit(void) {
    remove_proc_entry(PROC_STATE_FILE, americano_dir);
    remove_proc_entry(PROC_PAUSE_FILE, americano_dir);
    remove_proc_entry("americanonpauser", NULL);
    printk(KERN_INFO "[americano] Subsystem Deactivated\n");
}

module_init(americano_subsystem_init);
module_exit(americano_subsystem_exit);
