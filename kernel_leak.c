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
#define DEVICE_NAME "fikanpauser"
#define MAX_PROC_ENTRIES 1024
#define PROC_STATE_FILE "state.fika"
#define PROC_PAUSE_FILE "pause.fika"

static struct proc_dir_entry *fika_dir;
static struct proc_dir_entry *state_entry;
static struct proc_dir_entry *pause_entry;

/* Structure to keep track of system-wide metrics for the header */
struct fika_stats {
    u64 last_update_ns;
    int total_tasks;
    int stopped_tasks;
};

//------------------------------------------------------------------------------------------------

/* --- ENFORCEMENT ENGINE (PAUSE LOGIC) --- */
/* This handles the writing to /proc/pause.fika */
static ssize_t fika_write_enforcement(struct file *file, const char __user *buffer, 
                                     size_t count, loff_t *data) {
    char *input_buf;
    char *token, *curr;
    int pid_val;
    struct pid *pid_struct;
    struct task_struct *task;

    if (count > PAGE_SIZE) return -EINVAL;

    input_buf = kmalloc(count + 1, GFP_KERNEL);
    if (!input_buf) return -ENOMEM;

    if (copy_from_user(input_buf, buffer, count)) {
        kfree(input_buf);
        return -EFAULT;
    }
    input_buf[count] = '\0';

    /* Parse CSV list of PIDs: "123,456,789" */
    curr = input_buf;
    while ((token = strsep(&curr, ",")) != NULL) {
        if (kstrtoint(token, 10, &pid_val) == 0) {
            rcu_read_lock();
            pid_struct = find_get_pid(pid_val);
            if (pid_struct) {
                task = get_pid_task(pid_struct, PIDTYPE_PID);
                if (task) {
                    /* The AI Instruction: STOP the process */
                    send_sig(SIGSTOP, task, 0);
                    put_task_struct(task);
                }
                put_pid(pid_struct);
            }
            rcu_read_unlock();
        }
    }

    kfree(input_buf);
    return count;
}

/* --- TELEMETRY ENGINE (STATE LOGIC) --- */
static int fika_telemetry_show(struct seq_file *m, void *v) {
    struct task_struct *task;
    struct mm_struct *mm;
    unsigned long rss;

    seq_printf(m, "{\"timestamp\":%llu,\"tasks\":[", ktime_get_real_ns());

    rcu_read_lock();
    bool first = true;
    for_each_process(task) {
        if (!task) continue;
        
        mm = get_task_mm(task);
        rss = mm ? get_mm_rss(mm) << (PAGE_SHIFT - 10) : 0;
        if (mm) mmput(mm);
        
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

static int fika_state_open(struct inode *inode, struct file *file) {
    return single_open(file, fika_telemetry_show, NULL);
}

/* Mapping Kernel Operations to Virtual Files */
static const struct proc_ops state_fops = {
    .proc_open    = fika_state_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

static const struct proc_ops pause_fops = {
    .proc_write   = fika_write_enforcement,
};

/* --- SUBSYSTEM LIFECYCLE --- */
static int __init fika_subsystem_init(void) {
    printk(KERN_INFO "[FIKA] Initializing High-Level Resource Orchestrator...\n");

    fika_dir = proc_mkdir("fikanpauser", NULL);
    if (!fika_dir) return -ENOMEM;

    state_entry = proc_create(PROC_STATE_FILE, 0444, fika_dir, &state_fops);
    pause_entry = proc_create(PROC_PAUSE_FILE, 0222, fika_dir, &pause_fops);

    if (!state_entry || !pause_entry) {
        remove_proc_entry("fikanpauser", NULL);
        return -ENOMEM;
    }

    printk(KERN_INFO "[FIKA] Operational at /proc/fikanpauser/\n");
    return 0;
}

static void __exit fika_subsystem_exit(void) {
    remove_proc_entry(PROC_STATE_FILE, fika_dir);
    remove_proc_entry(PROC_PAUSE_FILE, fika_dir);
    remove_proc_entry("fikanpauser", NULL);
    printk(KERN_INFO "[FIKA] Subsystem Deactivated\n");
}

module_init(fika_subsystem_init);
module_exit(fika_subsystem_exit);
