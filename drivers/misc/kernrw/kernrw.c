// SPDX-License-Identifier: GPL-2.0
/*
 * KernRW - Kernel memory reader for Android 6.1.138 (GKI)
 * Designed for Snapdragon 8 Gen 3 (Redmi K70 Pro)
 * Features: single read, pointer-chain read, batch read
 * Anti-detection: hidden module, bypass VMA check, no syscall traces
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/kallsyms.h>
#include <linux/ioctl.h>

#define KERNRW_IOC_MAGIC        'K'
#define KERNRW_READ             _IOWR(KERNRW_IOC_MAGIC, 1, struct kernrw_args)
#define KERNRW_CHAIN_READ       _IOWR(KERNRW_IOC_MAGIC, 2, struct kernrw_chain)
#define KERNRW_BATCH_READ       _IOWR(KERNRW_IOC_MAGIC, 3, struct kernrw_batch)

#define MAX_CHAIN_DEPTH         8
#define MAX_BATCH_COUNT         64

// ----- 用户态交互结构体 -----
struct kernrw_args {
    pid_t pid;
    unsigned long va;
    size_t size;               // 1,2,4,8
    int error;
    unsigned char data[8];
};

struct kernrw_chain {
    pid_t pid;
    unsigned long base;
    int offsets[MAX_CHAIN_DEPTH];
    unsigned int offset_count;
    size_t final_size;
    int error;
    unsigned char data[8];
};

struct kernrw_batch_item {
    unsigned long va;
    size_t size;
    int error;
    unsigned char data[8];
};

struct kernrw_batch {
    pid_t pid;
    unsigned int count;
    struct kernrw_batch_item __user *items;
};

// ----- 内部函数声明 -----
static int manual_read_raw(pid_t pid, unsigned long va, void *buf, size_t size);

// ----- 全局符号指针（动态解析）-----
static void *(*phys_to_virt_symbol)(phys_addr_t phys) = NULL;

// ----- 核心：手动遍历页表（绕过VMA）-----
static int manual_read_raw(pid_t pid, unsigned long user_va, void *kbuf, size_t size) {
    struct task_struct *task;
    pgd_t *pgdp;
    p4d_t *p4d;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    phys_addr_t phys;
    void *map_ptr;
    size_t done = 0;
    unsigned long addr, offset;
    int ret = 0;

    if (!phys_to_virt_symbol) {
        pr_err("kernrw: phys_to_virt not available\n");
        return -ENOENT;
    }

    rcu_read_lock();
    task = find_task_by_vpid(pid);
    if (!task) {
        rcu_read_unlock();
        return -ESRCH;
    }
    get_task_struct(task);
    rcu_read_unlock();

    mmap_read_lock(task->mm);

    while (done < size) {
        addr = user_va + done;
        pgdp = pgd_offset(task->mm, addr);
        if (pgd_none(*pgdp) || pgd_bad(*pgdp)) { ret = -EFAULT; break; }

        p4d = p4d_offset(pgdp, addr);
        if (p4d_none(*p4d) || p4d_bad(*p4d)) { ret = -EFAULT; break; }

        pud = pud_offset(p4d, addr);
        if (pud_none(*pud) || pud_bad(*pud)) { ret = -EFAULT; break; }

        pmd = pmd_offset(pud, addr);
        if (pmd_none(*pmd) || pmd_bad(*pmd)) { ret = -EFAULT; break; }

        // 2MB 大页 (PMD巨页)
        if (pmd_leaf(*pmd)) {
            phys = (pmd_val(*pmd) & ~((1UL << PMD_SHIFT) - 1));
            map_ptr = phys_to_virt_symbol(phys);
            offset = addr & ((1UL << PMD_SHIFT) - 1);
            size_t chunk = min_t(size_t, size - done, (1UL << PMD_SHIFT) - offset);
            memcpy(kbuf + done, map_ptr + offset, chunk);
            done += chunk;
            continue;
        }

        // 4KB 小页 (PTE)
        pte = pte_offset_kernel(pmd, addr);
        if (!pte_present(*pte)) { ret = -EFAULT; break; }
        phys = (pte_val(*pte) & ~((1UL << PAGE_SHIFT) - 1));
        map_ptr = phys_to_virt_symbol(phys);
        offset = addr & (PAGE_SIZE - 1);
        size_t chunk = min_t(size_t, size - done, PAGE_SIZE - offset);
        memcpy(kbuf + done, map_ptr + offset, chunk);
        done += chunk;
    }

    mmap_read_unlock(task->mm);
    put_task_struct(task);
    return (ret < 0) ? ret : (int)done;
}

// ----- 链式读取处理 -----
static int handle_chain(struct kernrw_chain *chain) {
    unsigned long va = chain->base;
    uint64_t ptr_val;
    int ret, i;

    if (chain->offset_count == 0) {
        return manual_read_raw(chain->pid, va, chain->data, chain->final_size);
    }
    if (chain->offset_count > MAX_CHAIN_DEPTH) return -EINVAL;

    for (i = 0; i < chain->offset_count; i++) {
        ret = manual_read_raw(chain->pid, va, &ptr_val, sizeof(ptr_val));
        if (ret < 0) return ret;
        if (ptr_val == 0) return -EFAULT;
        va = ptr_val + chain->offsets[i];
    }
    return manual_read_raw(chain->pid, va, chain->data, chain->final_size);
}

// ----- 批量读取处理 -----
static int handle_batch(struct kernrw_batch *batch) {
    struct kernrw_batch_item __user *u_items = batch->items;
    struct kernrw_batch_item k_item;
    unsigned int i;
    int total_err = 0;

    if (batch->count > MAX_BATCH_COUNT) return -EINVAL;

    for (i = 0; i < batch->count; i++) {
        if (copy_from_user(&k_item, &u_items[i], sizeof(k_item))) {
            total_err = -EFAULT;
            break;
        }
        if (k_item.size == 0 || k_item.size > 8) {
            k_item.error = -EINVAL;
        } else {
            int ret = manual_read_raw(batch->pid, k_item.va, k_item.data, k_item.size);
            k_item.error = (ret < 0) ? ret : 0;
        }
        if (copy_to_user(&u_items[i], &k_item, sizeof(k_item))) {
            total_err = -EFAULT;
            break;
        }
    }
    return total_err;
}

// ----- ioctl 主路由 -----
static long kernrw_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    void __user *uarg = (void __user *)arg;
    int ret;

    switch (cmd) {
        case KERNRW_READ: {
            struct kernrw_args karg;
            if (copy_from_user(&karg, uarg, sizeof(karg))) return -EFAULT;
            if (karg.size == 0 || karg.size > 8) {
                karg.error = -EINVAL;
                ret = -EINVAL;
            } else {
                ret = manual_read_raw(karg.pid, karg.va, karg.data, karg.size);
                karg.error = (ret < 0) ? ret : 0;
            }
            if (copy_to_user(uarg, &karg, sizeof(karg))) return -EFAULT;
            return (ret < 0) ? ret : 0;
        }

        case KERNRW_CHAIN_READ: {
            struct kernrw_chain kc;
            if (copy_from_user(&kc, uarg, sizeof(kc))) return -EFAULT;
            ret = handle_chain(&kc);
            kc.error = (ret < 0) ? ret : 0;
            if (copy_to_user(uarg, &kc, sizeof(kc))) return -EFAULT;
            return ret;
        }

        case KERNRW_BATCH_READ: {
            struct kernrw_batch kb;
            if (copy_from_user(&kb, uarg, sizeof(kb))) return -EFAULT;
            return handle_batch(&kb);
        }

        default:
            return -ENOTTY;
    }
}

// ----- procfs 操作集 -----
static const struct proc_ops fops = {
    .proc_ioctl = kernrw_ioctl,
};

// ----- 隐藏模块（避免 lsmod）-----
static struct list_head *module_kset_list;
static struct module *this_mod;

static void hide_self(void) {
    this_mod = THIS_MODULE;
    module_kset_list = (struct list_head *)kallsyms_lookup_name("module_kset");
    if (module_kset_list) {
        list_del_init(&this_mod->list);
        pr_info("kernrw: module hidden from lsmod\n");
    } else {
        pr_warn("kernrw: module_kset not found, hiding failed\n");
    }
}

// ----- 模块加载入口 -----
static int __init kernrw_init(void) {
    // 1. 解析关键内核符号
    phys_to_virt_symbol = (void *)kallsyms_lookup_name("phys_to_virt");
    if (!phys_to_virt_symbol) {
        pr_err("kernrw: phys_to_virt not found, aborting\n");
        return -ENOENT;
    }

    // 2. 创建 /proc/kernrw 入口
    if (!proc_create("kernrw", 0666, NULL, &fops)) {
        pr_err("kernrw: failed to create proc entry\n");
        return -ENOMEM;
    }

    // 3. 隐藏模块自身
    hide_self();

    pr_info("kernrw: loaded successfully (Snapdragon 8 Gen 3)\n");
    return 0;
}

// ----- 模块卸载入口（不建议卸载，因隐藏后可能panic）-----
static void __exit kernrw_exit(void) {
    remove_proc_entry("kernrw", NULL);
    pr_info("kernrw: unloaded (may cause panic if hidden)\n");
}

module_init(kernrw_init);
module_exit(kernrw_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("KSU Developer");
MODULE_DESCRIPTION("Undetected memory reader for Redmi K70 Pro (6.1.138)");
MODULE_VERSION("1.1");
