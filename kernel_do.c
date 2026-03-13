#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched/signal.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>

#define MODULE_NAME "fikanpauser"
#define MAX_INSTRUCTION_SIZE 2048

/* Telemetry: Gather state for Gemini */
static int fika_state_show(struct seq_file *m, void *v) {
    struct task_struct *task;
    struct mm_struct *mm;
    unsigned long rss;

    seq_printf(m, "{\"timestamp\":%llu,\"tasks\":[", ktime_get_real_ns());

    rcu_read_lock();
    bool first = true;
    for_each_process(task) {
        if (!task || !task->mm) continue;
        
        mm = task->mm;
        rss = get_mm_rss(mm) << (PAGE_SHIFT - 10); // RSS in KB

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

/* Enforcement: Execute Gemini's "pause.fika" instructions */
static ssize_t fika_instruction_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos) {
    char *buf;
    char *token, *sptr;
    int target_pid;
    struct pid *pid_struct;
    struct task_struct *task;

    if (count > MAX_INSTRUCTION_SIZE) return -EINVAL;

    buf = kmalloc(count + 1, GFP_KERNEL);
    if (!buf) return -ENOMEM;

    if (copy_from_user(buf, ubuf, count)) {
        kfree(buf);
        return -EFAULT;
    }
    buf[count] = '\0';

    /* Parse the 'pause.fika' csv list: "123,456,789" */
    sptr = buf;
    while ((token = strsep(&sptr, ",")) != NULL) {
        if (sscanf(token, "%d", &target_pid) == 1) {
            pid_struct = find_get_pid(target_pid);
            if (pid_struct) {
                task = get_pid_task(pid_struct, PIDTYPE_PID);
                if (task) {
                    /* The Kernel enforces the AI's will */
                    send_sig(SIGSTOP, task, 0); //this forces the kernel to reschedule the processes
                    put_task_struct(task);
                    printk(KERN_INFO "FIkanpauser: Suspended PID %d via AI instruction\n", target_pid);
                }
                put_pid(pid_struct);
            }
        }
    }

    kfree(buf);
    return count;
}
//------------------------------------------------------------------------------------------------
static int fika_state_open(struct inode *inode, struct file *file) {
    return single_open(file, fika_state_show, NULL);
}

static const struct proc_ops state_fops = {
    .proc_open = fika_state_open,
    .proc_read = seq_read,
    .proc_release = single_release,
};

static const struct proc_ops instruction_fops = {
    .proc_write = fika_instruction_write,
};

static int __init fika_init(void) {
    proc_create("state.fika", 0444, NULL, &state_fops);
    proc_create("pause.fika", 0222, NULL, &instruction_fops);
    printk(KERN_INFO "FIkanpauser: AI-Enforcement Engine Online\n");
    return 0;
}

static void __exit fika_exit(void) {
    remove_proc_entry("state.fika", NULL);
    remove_proc_entry("pause.fika", NULL);
    printk(KERN_INFO "FIkanpauser: AI-Enforcement Engine Offline\n");
}

module_init(fika_init);
module_exit(fika_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("FIkanpauser Team");