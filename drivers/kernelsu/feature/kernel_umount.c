#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/task_work.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/nsproxy.h>
#include <linux/path.h>
#include <linux/printk.h>
#include <linux/susfs.h>
#include <linux/types.h>
#ifndef KSU_HAS_PATH_UMOUNT
#include <linux/syscalls.h>
#endif

#include "kernel_umount.h"
#include "klog.h" // IWYU pragma: keep
#include "policy/allowlist.h"
#include "selinux/selinux.h"
#include "policy/feature.h"
#include "runtime/ksud_boot.h"
#include "ksu.h"
#include "compat/kernel_compat.h"

static bool ksu_kernel_umount_enabled = true;

#ifdef CONFIG_KSU_SUSFS
extern bool susfs_is_mnt_devname_ksu(struct path *path);
#endif

#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
struct susfs_default_umount {
	const char *path;
	bool check_mnt;
	int flags;
};

static const struct susfs_default_umount susfs_default_umounts[] = {
	{ "/system", true, 0 },
	{ "/system_ext", true, 0 },
	{ "/vendor", true, 0 },
	{ "/product", true, 0 },
	{ "/odm", true, 0 },
	{ "/oem", true, 0 },
	{ "/data/adb/modules", false, MNT_DETACH },
	{ "/debug_ramdisk", false, MNT_DETACH },
};
#endif

static int kernel_umount_feature_get(u64 *value)
{
	*value = ksu_kernel_umount_enabled ? 1 : 0;
	return 0;
}

static int kernel_umount_feature_set(u64 value)
{
	bool enable = value != 0;
	ksu_kernel_umount_enabled = enable;
	pr_info("kernel_umount: set to %d\n", enable);
	return 0;
}

static const struct ksu_feature_handler kernel_umount_handler = {
	.feature_id = KSU_FEATURE_KERNEL_UMOUNT,
	.name = "kernel_umount",
	.get_handler = kernel_umount_feature_get,
	.set_handler = kernel_umount_feature_set,
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0) ||                           \
	defined(KSU_HAS_PATH_UMOUNT)
extern int path_umount(struct path *path, int flags);
static void ksu_umount_mnt(const char *mnt, struct path *path, int flags)
{
	int err = path_umount(path, flags);
	if (err) {
		pr_info("umount %s failed: %d\n", mnt, err);
	}
}
#else
static void ksu_sys_umount(const char *mnt, int flags)
{
	char __user *usermnt = (char __user *)mnt;
	mm_segment_t old_fs;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
	ksys_umount(usermnt, flags);
#else
	sys_umount(usermnt, flags); // cuz asmlinkage long sys##name
#endif
	set_fs(old_fs);
}

#define ksu_umount_mnt(mnt, __unused, flags)                                   \
	({                                                                     \
		path_put(__unused);                                            \
		ksu_sys_umount(mnt, flags);                                    \
	})

#endif

void ksu_try_umount(const char *mnt, bool check_mnt, int flags, uid_t uid)
{
	struct path path;
	int err = kern_path(mnt, 0, &path);
	if (err) {
		return;
	}

	if (path.dentry != path.mnt->mnt_root) {
		// it is not root mountpoint, maybe umounted by others already.
		path_put(&path);
		return;
	}

#ifdef CONFIG_KSU_SUSFS
	if (check_mnt && !susfs_is_mnt_devname_ksu(&path)) {
		path_put(&path);
		return;
	}

#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
	if (susfs_is_log_enabled)
		pr_info("susfs: umounting '%s' for uid: %d\n", mnt, uid);
#endif
#endif

	ksu_umount_mnt(mnt, &path, flags);
}

static void try_umount(const char *mnt, int flags)
{
	ksu_try_umount(mnt, false, flags, 0);
}

struct umount_tw {
	struct callback_head cb;
};

static void umount_tw_func(struct callback_head *cb)
{
	struct umount_tw *tw = container_of(cb, struct umount_tw, cb);
	const struct cred *saved = override_creds(ksu_cred);

    struct mount_entry *entry;
    down_read(&mount_list_lock);
    list_for_each_entry(entry, &mount_list, list) {
        pr_info("%s: unmounting: %s flags: 0x%x\n", __func__, entry->umountable, entry->flags);
        try_umount(entry->umountable, entry->flags);
    }
    up_read(&mount_list_lock);

	revert_creds(saved);

	kfree(tw);
}

#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
static void susfs_try_default_umounts(uid_t uid)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(susfs_default_umounts); i++) {
		ksu_try_umount(susfs_default_umounts[i].path,
			       susfs_default_umounts[i].check_mnt,
			       susfs_default_umounts[i].flags, uid);
	}
}

void susfs_try_umount_all(uid_t uid)
{
	const struct cred *saved;
	struct mount_entry *entry;

	if (!ksu_kernel_umount_enabled || !ksu_cred)
		return;

	saved = override_creds(ksu_cred);

	susfs_try_umount(uid);
	susfs_try_default_umounts(uid);

	down_read(&mount_list_lock);
	list_for_each_entry(entry, &mount_list, list)
		try_umount(entry->umountable, entry->flags);
	up_read(&mount_list_lock);

	revert_creds(saved);
}
#endif

int ksu_handle_umount(uid_t old_uid, uid_t new_uid)
{
	struct umount_tw *tw;

	// if there isn't any module mounted, just ignore it!
	if (!ksu_module_mounted) {
		return 0;
	}

	if (!ksu_kernel_umount_enabled) {
		return 0;
	}

	if (!ksu_cred) {
		return 0;
	}

    // There are 6 scenarios:
    // 1. Normal app: zygote -> appuid
    // 2. Isolated process forked from zygote: zygote -> isolated_process
    // 3. App zygote forked from zygote: zygote -> appuid
    // 4. Webview zygote forked from zygote: zygote -> WEBVIEW_ZYGOTE_UID (no need to handle, app cannot run custom code)
    // 5. Isolated process forked from app zygote: appuid -> isolated_process (already handled by 3)
    // 6. Isolated process forked from webview zygote (no need to handle, app cannot run custom code)
    if (!is_appuid(new_uid) && !is_isolated_process(new_uid)) {
        return 0;
    }

	if (!ksu_uid_should_umount(new_uid) && !is_isolated_process(new_uid)) {
		return 0;
	}

#ifdef CONFIG_KSU_SUSFS
	current->susfs_task_state |= TASK_STRUCT_NON_ROOT_USER_APP_PROC;
#endif

	// check old process's selinux context, if it is not zygote, ignore it!
	// because some su apps may setuid to untrusted_app but they are in global mount namespace
	// when we umount for such process, that is a disaster!
	// also handle case 4 and 5
	bool is_zygote_child = is_zygote(current_cred());
	if (!is_zygote_child) {
		pr_info("handle umount ignore non zygote child: %d\n", current->pid);
		return 0;
	}
	// umount the target mnt
	pr_info("handle umount for uid: %d, pid: %d\n", new_uid, current->pid);

	tw = kzalloc(sizeof(*tw), GFP_ATOMIC);
	if (!tw)
		return 0;

	tw->cb.func = umount_tw_func;

	int err = task_work_add(current, &tw->cb, TWA_RESUME);
	if (err) {
		kfree(tw);
		pr_warn("unmount add task_work failed\n");
	}

	return 0;
}

void __init ksu_kernel_umount_init(void)
{
	if (ksu_register_feature_handler(&kernel_umount_handler)) {
		pr_err("Failed to register kernel_umount feature handler\n");
	}
}

void __exit ksu_kernel_umount_exit(void)
{
	ksu_unregister_feature_handler(KSU_FEATURE_KERNEL_UMOUNT);
}
