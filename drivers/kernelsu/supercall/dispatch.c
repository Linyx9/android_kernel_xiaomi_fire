#include <linux/capability.h>
#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/susfs.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "uapi/supercall.h"
#include "supercall/internal.h"
#include "arch.h" // IWYU pragma: keep
#include "policy/allowlist.h"
#include "policy/feature.h"
#include "klog.h" // IWYU pragma: keep
#include "ksu.h"
#include "runtime/ksud_boot.h"
#include "feature/kernel_umount.h"
#include "manager/manager_identity.h"
#include "selinux/selinux.h"
#include "infra/file_wrapper.h"
#include "hook/hook_manager.h"
#include "policy/app_profile.h"
#include "supercall/supercall.h"

#include "tiny_sulog.h"

static int do_grant_root(void __user *arg)
{
	// we already check uid above on allowed_for_su()

    write_sulog('i'); // log ioctl escalation

    pr_info("allow root for: %d\n", current_uid().val);
    escape_with_root_profile();

	return 0;
}

static int do_get_info(void __user *arg)
{
	struct ksu_get_info_cmd cmd = {.version = KERNEL_SU_VERSION, .flags = 0};

	if (ksuver_override) {
		cmd.version = ksuver_override;
	}

#ifdef MODULE
	cmd.flags |= KSU_GET_INFO_FLAG_LKM;
#endif

	if (is_manager()) {
		cmd.flags |= KSU_GET_INFO_FLAG_MANAGER;
	}
	if (ksu_late_loaded) {
		cmd.flags |= KSU_GET_INFO_FLAG_LATE_LOAD;
	}
	cmd.features = KSU_FEATURE_MAX;

	if (copy_to_user(arg, &cmd, sizeof(cmd))) {
		pr_err("get_version: copy_to_user failed\n");
		return -EFAULT;
	}

	return 0;
}

static int do_report_event(void __user *arg)
{
	struct ksu_report_event_cmd cmd;

	if (copy_from_user(&cmd, arg, sizeof(cmd))) {
		return -EFAULT;
	}

	switch (cmd.event) {
	case EVENT_POST_FS_DATA: {
		static bool post_fs_data_lock = false;
		if (!post_fs_data_lock) {
			post_fs_data_lock = true;
			if (ksu_late_loaded) {
				pr_info("post-fs-data skipped (late load)\n");
			} else {
				pr_info("post-fs-data triggered\n");
				on_post_fs_data();
			}
		}
		break;
	}
	case EVENT_BOOT_COMPLETED: {
		static bool boot_complete_lock = false;
		if (!boot_complete_lock) {
			boot_complete_lock = true;
			if (ksu_late_loaded) {
				pr_info("boot_complete skipped (late load)\n");
			} else {
				pr_info("boot_complete triggered\n");
				on_boot_completed();
			}
		}
		break;
	}
	case EVENT_MODULE_MOUNTED: {
		pr_info("module mounted!\n");
		on_module_mounted();
		break;
	}
	default:
		break;
	}

	return 0;
}

static int do_set_sepolicy(void __user *arg)
{
	struct ksu_set_sepolicy_cmd cmd;

	if (copy_from_user(&cmd, arg, sizeof(cmd))) {
		return -EFAULT;
	}

	return handle_sepolicy((void __user *)cmd.data, cmd.data_len);
}

static int do_check_safemode(void __user *arg)
{
	struct ksu_check_safemode_cmd cmd;

	cmd.in_safe_mode = ksu_is_safe_mode();

	if (cmd.in_safe_mode) {
		pr_warn("safemode enabled!\n");
	}

	if (copy_to_user(arg, &cmd, sizeof(cmd))) {
		pr_err("check_safemode: copy_to_user failed\n");
		return -EFAULT;
	}

	return 0;
}

static int do_new_get_allow_list_common(void __user *arg, bool allow)
{
    struct ksu_new_get_allow_list_cmd cmd;
    int *arr = NULL;
    int err = 0;

	if (copy_from_user(&cmd, arg, sizeof(cmd))) {
		return -EFAULT;
	}

    if (cmd.count) {
        // kmalloc_array safely checks for mathematical overflows before allocating
		arr = kmalloc_array(cmd.count, sizeof(int), GFP_KERNEL);
        if (!arr) {
            return -ENOMEM;
        }
    }

    bool success =
        ksu_get_allow_list(arr, cmd.count, &cmd.count, &cmd.total_count, allow);

    if (!success) {
        err = -EFAULT;
        goto out;
    }

    if (copy_to_user(arg, &cmd, sizeof(cmd))) {
        pr_err("new_get_allow_list: copy_to_user count failed\n");
        err = -EFAULT;
        goto out;
    }

    if (cmd.count &&
        copy_to_user(&((struct ksu_new_get_allow_list_cmd *)arg)->uids, arr,
                     sizeof(int) * cmd.count)) {
        pr_err("new_get_allow_list: copy_to_user uids failed\n");
        err = -EFAULT;
    }

out:
    if (arr) {
        kfree(arr);
    }
    return err;
}

static int do_new_get_deny_list(void __user *arg)
{
    return do_new_get_allow_list_common(arg, false);
}

static int do_new_get_allow_list(void __user *arg)
{
    return do_new_get_allow_list_common(arg, true);
}

static int do_get_allow_list_common(void __user *arg, bool allow)
{
    int *arr = NULL;
    int err = 0;
    u16 count;
    u32 out_count;
    static const u16 kSize = 128;

    arr = kmalloc(sizeof(int) * kSize, GFP_KERNEL);
    if (!arr) {
        return -ENOMEM;
    }

    bool success = ksu_get_allow_list(arr, kSize, &count, NULL, allow);

    if (!success) {
        err = -EFAULT;
        goto out;
    }

    out_count = count;

    if (copy_to_user(arg + offsetof(struct ksu_get_allow_list_cmd, count),
                     &out_count, sizeof(u32))) {
        pr_err("get_allow_list: copy_to_user count failed\n");
        err = -EFAULT;
        goto out;
    }

    if (copy_to_user(arg, arr, sizeof(u32) * count)) {
        pr_err("get_allow_list: copy_to_user uids failed\n");
        err = -EFAULT;
    }

out:
    if (arr) {
        kfree(arr);
    }
    return err;
}

static int do_get_deny_list(void __user *arg)
{
    return do_get_allow_list_common(arg, false);
}

static int do_get_allow_list(void __user *arg)
{
    return do_get_allow_list_common(arg, true);
}

static int do_uid_granted_root(void __user *arg)
{
	struct ksu_uid_granted_root_cmd cmd;

	if (copy_from_user(&cmd, arg, sizeof(cmd))) {
		return -EFAULT;
	}

	cmd.granted = ksu_is_allow_uid_for_current(cmd.uid);

	if (copy_to_user(arg, &cmd, sizeof(cmd))) {
		pr_err("uid_granted_root: copy_to_user failed\n");
		return -EFAULT;
	}

	return 0;
}

static int do_uid_should_umount(void __user *arg)
{
	struct ksu_uid_should_umount_cmd cmd;

	if (copy_from_user(&cmd, arg, sizeof(cmd))) {
		return -EFAULT;
	}

	cmd.should_umount = ksu_uid_should_umount(cmd.uid);

	if (copy_to_user(arg, &cmd, sizeof(cmd))) {
		pr_err("uid_should_umount: copy_to_user failed\n");
		return -EFAULT;
	}

	return 0;
}

static int do_get_manager_appid(void __user *arg)
{
	struct ksu_get_manager_appid_cmd cmd;

	cmd.appid = ksu_get_manager_appid();

	if (copy_to_user(arg, &cmd, sizeof(cmd))) {
		pr_err("get_manager_appid: copy_to_user failed\n");
		return -EFAULT;
	}

	return 0;
}

static int do_get_app_profile(void __user *arg)
{
#ifdef CONFIG_KSU_DISABLE_POLICY
    return -EOPNOTSUPP;
#endif

	struct ksu_get_app_profile_cmd cmd;

	if (copy_from_user(&cmd, arg, sizeof(cmd))) {
		pr_err("get_app_profile: copy_from_user failed\n");
		return -EFAULT;
	}

	if (!ksu_get_app_profile(&cmd.profile)) {
		return -ENOENT;
	}

	if (copy_to_user(arg, &cmd, sizeof(cmd))) {
		pr_err("get_app_profile: copy_to_user failed\n");
		return -EFAULT;
	}

	return 0;
}

static int do_set_app_profile(void __user *arg)
{
#ifdef CONFIG_KSU_DISABLE_POLICY
    return -EOPNOTSUPP;
#endif

    struct ksu_set_app_profile_cmd cmd;
    int ret;

	if (copy_from_user(&cmd, arg, sizeof(cmd))) {
		pr_err("set_app_profile: copy_from_user failed\n");
		return -EFAULT;
	}

    ret = ksu_set_app_profile(&cmd.profile);
    if (!ret) {
        ksu_persistent_allow_list();
#ifdef KSU_KPROBES_HOOK
        ksu_mark_running_process();
#endif
    }
    return ret;
}

static int do_get_feature(void __user *arg)
{
	struct ksu_get_feature_cmd cmd;
	bool supported;
	int ret;

	if (copy_from_user(&cmd, arg, sizeof(cmd))) {
		pr_err("get_feature: copy_from_user failed\n");
		return -EFAULT;
	}


	ret = ksu_get_feature(cmd.feature_id, &cmd.value, &supported);
	cmd.supported = supported ? 1 : 0;

	if (ret && supported) {
		pr_err("get_feature: failed for feature %u: %d\n", cmd.feature_id, ret);
		return ret;
	}

	if (copy_to_user(arg, &cmd, sizeof(cmd))) {
		pr_err("get_feature: copy_to_user failed\n");
		return -EFAULT;
	}

	return 0;
}

static int do_set_feature(void __user *arg)
{
	struct ksu_set_feature_cmd cmd;
	int ret;

	if (copy_from_user(&cmd, arg, sizeof(cmd))) {
		pr_err("set_feature: copy_from_user failed\n");
		return -EFAULT;
	}


	ret = ksu_set_feature(cmd.feature_id, cmd.value);
	if (ret) {
		pr_err("set_feature: failed for feature %u: %d\n", cmd.feature_id, ret);
		return ret;
	}

	return 0;
}

static int do_get_wrapper_fd(void __user *arg) {
	if (!ksu_file_sid) {
		return -EINVAL;
	}

	struct ksu_get_wrapper_fd_cmd cmd;
    if (copy_from_user(&cmd, arg, sizeof(cmd))) {
        pr_err("get_wrapper_fd: copy_from_user failed\n");
        return -EFAULT;
	}

	return ksu_install_file_wrapper(cmd.fd);
}

static int do_manage_mark(void __user *arg)
{
#ifdef KSU_KPROBES_HOOK
	struct ksu_manage_mark_cmd cmd;
	int ret = 0;

	if (copy_from_user(&cmd, arg, sizeof(cmd))) {
		pr_err("manage_mark: copy_from_user failed\n");
		return -EFAULT;
	}

	switch (cmd.operation) {
	case KSU_MARK_GET: {
		// Get task mark status
		ret = ksu_get_task_mark(cmd.pid);
		if (ret < 0) {
			pr_err("manage_mark: get failed for pid %d: %d\n", cmd.pid, ret);
			return ret;
		}
		cmd.result = (u32)ret;
		break;
	}
	case KSU_MARK_MARK: {
		if (cmd.pid == 0) {
			ksu_mark_all_process();
		} else {
			ret = ksu_set_task_mark(cmd.pid, true);
			if (ret < 0) {
				pr_err("manage_mark: set_mark failed for pid %d: %d\n", cmd.pid,
					ret);
				return ret;
			}
		}
		break;
	}
	case KSU_MARK_UNMARK: {
		if (cmd.pid == 0) {
			ksu_unmark_all_process();
		} else {
			ret = ksu_set_task_mark(cmd.pid, false);
			if (ret < 0) {
				pr_err("manage_mark: set_unmark failed for pid %d: %d\n",
					cmd.pid, ret);
				return ret;
			}
		}
		break;
	}
	case KSU_MARK_REFRESH: {
		ksu_mark_running_process();
		pr_info("manage_mark: refreshed running processes\n");
		break;
	}
	default: {
		pr_err("manage_mark: invalid operation %u\n", cmd.operation);
		return -EINVAL;
	}
	}
	if (copy_to_user(arg, &cmd, sizeof(cmd))) {
		pr_err("manage_mark: copy_to_user failed\n");
		return -EFAULT;
	}

	return 0;
#else
	// We don't care, just return -ENOTSUPP
	pr_warn("manage_mark: this supercalls is not implemented for manual hook.\n");
	return -ENOTSUPP;
#endif
}

static int do_get_hook_mode(void __user *arg)
{
	struct ksu_get_hook_mode_cmd cmd = {0};
	const char *type = "Kprobes";

#ifndef KSU_KPROBES_HOOK
	type = "Manual";
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	strscpy(cmd.mode, type, sizeof(cmd.mode));
#else
	strlcpy(cmd.mode, type, sizeof(cmd.mode));
#endif

	if (copy_to_user(arg, &cmd, sizeof(cmd))) {
		pr_err("get_hook_mode: copy_to_user failed\n");
		return -EFAULT;
	}

	return 0;
}

static int do_get_version_tag(void __user *arg)
{
	struct ksu_get_version_tag_cmd cmd = {0};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 13, 0)
	strscpy(cmd.tag, KERNEL_SU_VERSION_TAG, sizeof(cmd.tag));
#else
	strlcpy(cmd.tag, KERNEL_SU_VERSION_TAG, sizeof(cmd.tag));
#endif

	if (copy_to_user(arg, &cmd, sizeof(cmd))) {
		pr_err("get_version_tag: copy_to_user failed\n");
		return -EFAULT;
	}

	return 0;
}

static int do_nuke_ext4_sysfs(void __user *arg)
{
    struct ksu_nuke_ext4_sysfs_cmd cmd;
    char mnt[256];
    long ret;

    if (copy_from_user(&cmd, arg, sizeof(cmd)))
        return -EFAULT;

    if (!cmd.arg)
        return -EINVAL;

    memset(mnt, 0, sizeof(mnt));

    ret = strncpy_from_user(mnt, cmd.arg, sizeof(mnt));
    if (ret < 0) {
        pr_err("nuke ext4 copy mnt failed: %ld\\n", ret);
        return -EFAULT;   // 或者 return ret;
    }

    if (ret == sizeof(mnt)) {
        pr_err("nuke ext4 mnt path too long\\n");
        return -ENAMETOOLONG;
    }

    pr_info("do_nuke_ext4_sysfs: %s\n", mnt);

    return nuke_ext4_sysfs(mnt);
}

struct list_head mount_list = LIST_HEAD_INIT(mount_list);
DECLARE_RWSEM(mount_list_lock);

static int add_try_umount(void __user *arg)
{
    struct mount_entry *new_entry, *entry, *tmp;
    struct ksu_add_try_umount_cmd cmd;
    char buf[256] = {0};

    if (copy_from_user(&cmd, arg, sizeof cmd))
        return -EFAULT;

    switch (cmd.mode) {
        case KSU_UMOUNT_WIPE: {
            struct mount_entry *entry, *tmp;
            down_write(&mount_list_lock);
            list_for_each_entry_safe(entry, tmp, &mount_list, list) {
                pr_info("wipe_umount_list: removing entry: %s\n", entry->umountable);
                list_del(&entry->list);
                kfree(entry->umountable);
                kfree(entry);
            }
            up_write(&mount_list_lock);

            return 0;
        }

        case KSU_UMOUNT_ADD: {
            long len = strncpy_from_user(buf, (const char __user *)cmd.arg, 256);
            if (len <= 0)
                return -EFAULT;

            buf[sizeof(buf) - 1] = '\0';

            new_entry = kzalloc(sizeof(*new_entry), GFP_KERNEL);
            if (!new_entry)
                return -ENOMEM;

            new_entry->umountable = kstrdup(buf, GFP_KERNEL);
            if (!new_entry->umountable) {
                kfree(new_entry);
                return -ENOMEM;
            }

            down_write(&mount_list_lock);

            // disallow dupes
            // if this gets too many, we can consider moving this whole task to a kthread
            list_for_each_entry(entry, &mount_list, list) {
                if (!strcmp(entry->umountable, buf)) {
                    pr_info("cmd_add_try_umount: %s is already here!\n", buf);
                    up_write(&mount_list_lock);
                    kfree(new_entry->umountable);
                    kfree(new_entry);
                    return -EEXIST;
                }
            }

            // now check flags and add
            // this also serves as a null check
            if (cmd.flags)
                new_entry->flags = cmd.flags;
            else
                new_entry->flags = 0;

            // debug
            list_add(&new_entry->list, &mount_list);
            up_write(&mount_list_lock);
            pr_info("cmd_add_try_umount: %s added!\n", buf);

            return 0;
        }

        // this is just strcmp'd wipe anyway
        case KSU_UMOUNT_DEL: {
            long len = strncpy_from_user(buf, (const char __user *)cmd.arg, sizeof(buf) - 1);
            if (len <= 0)
                return -EFAULT;

            buf[sizeof(buf) - 1] = '\0';

            down_write(&mount_list_lock);
            list_for_each_entry_safe(entry, tmp, &mount_list, list) {
                if (!strcmp(entry->umountable, buf)) {
                    pr_info("cmd_add_try_umount: entry removed: %s\n", entry->umountable);
                    list_del(&entry->list);
                    kfree(entry->umountable);
                    kfree(entry);
                }
            }
            up_write(&mount_list_lock);

            return 0;
        }

		// this way userspace can deduce the memory it has to prepare.
		case KSU_UMOUNT_GETSIZE: {
			// check for pointer first
			if (!cmd.arg)
				return -EFAULT;

			size_t total_size = 0; // size of list in bytes

			down_read(&mount_list_lock);
			list_for_each_entry(entry, &mount_list, list) {
				total_size = total_size + strlen(entry->umountable) + 1; // + 1 for \0
			}
			up_read(&mount_list_lock);

			pr_info("cmd_add_try_umount: total_size: %zu\n", total_size);

			if (copy_to_user((size_t __user *)cmd.arg, &total_size, sizeof(total_size)))
				return -EFAULT;

			return 0;
		}

		// WARNING! this is straight up pointerwalking.
		// this way we dont need to redefine the ioctl defs.
		// this also avoids us needing to kmalloc
		// userspace have to send pointer to memory (malloc/alloca) or pointer to a VLA.
		case KSU_UMOUNT_GETLIST: {
			if (!cmd.arg)
				return -EFAULT;

			void *user_buf = (void *)cmd.arg;

			down_read(&mount_list_lock);
			list_for_each_entry(entry, &mount_list, list) {
				pr_info("cmd_add_try_umount: entry: %s\n", entry->umountable);

				if (copy_to_user(user_buf, entry->umountable, strlen(entry->umountable) + 1 )) {
					up_read(&mount_list_lock);
					return -EFAULT;
				}

				// walk it! +1 for null terminator
				user_buf = (char *)user_buf + strlen(entry->umountable) + 1;
			}
			up_read(&mount_list_lock);

			return 0;
		}

        default: {
            pr_err("cmd_add_try_umount: invalid operation %u\n", cmd.mode);
            return -EINVAL;
        }

    } // switch(cmd.mode)

    return 0;
}

static int do_set_init_pgrp(void __user *arg)
{
	int err = -EPERM;
	struct task_struct *p;
	struct pid *init_group;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
    struct pid *pids[PIDTYPE_MAX] = { 0 };
#endif

	write_lock_irq(&tasklist_lock);

	p = current->group_leader;
	init_group = task_pgrp(&init_task);

	if (task_session(p) != task_session(&init_task))
		goto out;

	err = 0;
	if (task_pgrp(p) != init_group) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
        change_pid(pids, p, PIDTYPE_PGID, init_group);
#else
        change_pid(p, PIDTYPE_PGID, init_group);
#endif
    }

out:
	write_unlock_irq(&tasklist_lock);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 15, 0)
    free_pids(pids);
#endif

	return err;
}

#ifdef CONFIG_KSU_SUSFS
static int do_susfs_show_version(void __user *arg)
{
	size_t len = strlen(SUSFS_VERSION) + 1;

	return copy_to_user(arg, SUSFS_VERSION, len) ? -EFAULT : 0;
}

static int do_susfs_show_variant(void __user *arg)
{
	size_t len = strlen(SUSFS_VARIANT) + 1;

	return copy_to_user(arg, SUSFS_VARIANT, len) ? -EFAULT : 0;
}

static int do_susfs_show_enabled_features(void __user *arg)
{
	u64 enabled_features = 0;

#ifdef CONFIG_KSU_SUSFS_SUS_PATH
	enabled_features |= (1 << 0);
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
	enabled_features |= (1 << 1);
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT
	enabled_features |= (1 << 2);
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT
	enabled_features |= (1 << 3);
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
	enabled_features |= (1 << 4);
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_OVERLAYFS
	enabled_features |= (1 << 5);
#endif
#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
	enabled_features |= (1 << 6);
#endif
#ifdef CONFIG_KSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT
	enabled_features |= (1 << 7);
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME
	enabled_features |= (1 << 8);
#endif
#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
	enabled_features |= (1 << 9);
#endif
#ifdef CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS
	enabled_features |= (1 << 10);
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
	enabled_features |= (1 << 11);
#endif
#ifdef CONFIG_KSU_SUSFS_OPEN_REDIRECT
	enabled_features |= (1 << 12);
#endif
#ifdef CONFIG_KSU_SUSFS_HAS_MAGIC_MOUNT
	enabled_features |= (1 << 14);
#endif

	return copy_to_user(arg, &enabled_features, sizeof(enabled_features)) ?
		       -EFAULT :
		       0;
}

#ifdef CONFIG_KSU_SUSFS_SUS_PATH
static int do_susfs_add_sus_path(void __user *arg)
{
	return susfs_add_sus_path(arg);
}
#endif

#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
static int do_susfs_add_sus_mount(void __user *arg)
{
	return susfs_add_sus_mount(arg);
}
#endif

#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
static int do_susfs_add_sus_kstat(void __user *arg)
{
	return susfs_add_sus_kstat(arg);
}

static int do_susfs_update_sus_kstat(void __user *arg)
{
	return susfs_update_sus_kstat(arg);
}
#endif

#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
static int do_susfs_add_try_umount(void __user *arg)
{
	return susfs_add_try_umount(arg);
}

extern void susfs_run_try_umount_for_current_mnt_ns(void);
static int do_susfs_run_umount_for_current_mnt_ns(void __user *arg)
{
	susfs_run_try_umount_for_current_mnt_ns();
	return 0;
}
#endif

#ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME
static int do_susfs_set_uname(void __user *arg)
{
	return susfs_set_uname(arg);
}
#endif

#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
static int do_susfs_enable_log(void __user *arg)
{
	unsigned long value = (unsigned long)arg;

	if (value > 1) {
		bool enabled;

		if (copy_from_user(&enabled, arg, sizeof(enabled)))
			return -EFAULT;
		value = enabled ? 1 : 0;
	}

	susfs_set_log(value != 0);
	return 0;
}
#endif

#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
static int do_susfs_set_cmdline_or_bootconfig(void __user *arg)
{
	return susfs_set_cmdline_or_bootconfig(arg);
}
#endif

#ifdef CONFIG_KSU_SUSFS_OPEN_REDIRECT
static int do_susfs_add_open_redirect(void __user *arg)
{
	return susfs_add_open_redirect(arg);
}
#endif

#ifdef CONFIG_KSU_SUSFS_SUS_SU
static int do_susfs_show_sus_su_working_mode(void __user *arg)
{
	int mode = susfs_get_sus_su_working_mode();

	return copy_to_user(arg, &mode, sizeof(mode)) ? -EFAULT : 0;
}

static int do_susfs_sus_su(void __user *arg)
{
	return susfs_sus_su(arg);
}

static int do_susfs_is_sus_su_ready(void __user *arg)
{
	int ready = 1;

	return copy_to_user(arg, &ready, sizeof(ready)) ? -EFAULT : 0;
}
#endif
#endif

// IOCTL handlers mapping table
// clang-format off
static const struct ksu_ioctl_cmd_map ksu_ioctl_handlers[] = {
    {
        .cmd = KSU_IOCTL_GRANT_ROOT,
        .name = "GRANT_ROOT",
        .handler = do_grant_root,
        .perm_check = allowed_for_su
    },
    {
        .cmd = KSU_IOCTL_GET_INFO,
        .name = "GET_INFO",
        .handler = do_get_info,
        .perm_check = always_allow
    },
	{
        .cmd = KSU_IOCTL_REPORT_EVENT,
        .name = "REPORT_EVENT",
        .handler = do_report_event,
        .perm_check = only_root
    },
    {
        .cmd = KSU_IOCTL_SET_SEPOLICY,
        .name = "SET_SEPOLICY",
        .handler = do_set_sepolicy,
        .perm_check = only_root
    },
    {
        .cmd = KSU_IOCTL_CHECK_SAFEMODE,
        .name = "CHECK_SAFEMODE",
        .handler = do_check_safemode,
        .perm_check = always_allow
    },
    {
        .cmd = KSU_IOCTL_GET_ALLOW_LIST,
        .name = "GET_ALLOW_LIST",
        .handler = do_get_allow_list,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_GET_DENY_LIST,
        .name = "GET_DENY_LIST",
        .handler = do_get_deny_list,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_NEW_GET_ALLOW_LIST,
        .name = "NEW_GET_ALLOW_LIST",
        .handler = do_new_get_allow_list,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_NEW_GET_DENY_LIST,
        .name = "NEW_GET_DENY_LIST",
        .handler = do_new_get_deny_list,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_UID_GRANTED_ROOT,
        .name = "UID_GRANTED_ROOT",
        .handler = do_uid_granted_root,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_UID_SHOULD_UMOUNT,
        .name = "UID_SHOULD_UMOUNT",
        .handler = do_uid_should_umount,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_GET_MANAGER_APPID,
        .name = "GET_MANAGER_APPID",
        .handler = do_get_manager_appid,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_GET_APP_PROFILE,
        .name = "GET_APP_PROFILE",
        .handler = do_get_app_profile,
        .perm_check = only_manager
    },
    {
        .cmd = KSU_IOCTL_SET_APP_PROFILE,
        .name = "SET_APP_PROFILE",
        .handler = do_set_app_profile,
        .perm_check = only_manager
    },
    {
        .cmd = KSU_IOCTL_GET_FEATURE,
        .name = "GET_FEATURE",
        .handler = do_get_feature,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_SET_FEATURE,
        .name = "SET_FEATURE",
        .handler = do_set_feature,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_GET_WRAPPER_FD,
        .name = "GET_WRAPPER_FD",
        .handler = do_get_wrapper_fd,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_MANAGE_MARK,
        .name = "MANAGE_MARK",
        .handler = do_manage_mark,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_NUKE_EXT4_SYSFS,
        .name = "NUKE_EXT4_SYSFS",
        .handler = do_nuke_ext4_sysfs,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_ADD_TRY_UMOUNT,
        .name = "ADD_TRY_UMOUNT",
        .handler = add_try_umount,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_SET_INIT_PGRP,
        .name = "SET_INIT_PGRP",
        .handler = do_set_init_pgrp,
        .perm_check = only_root
    },
    {
        .cmd = KSU_IOCTL_GET_HOOK_MODE,
        .name = "GET_HOOK_MODE",
        .handler = do_get_hook_mode,
        .perm_check = manager_or_root
    },
    {
        .cmd = KSU_IOCTL_GET_VERSION_TAG,
        .name = "GET_VERSION_TAG",
        .handler = do_get_version_tag,
        .perm_check = manager_or_root
    },
#ifdef CONFIG_KSU_SUSFS
#ifdef CONFIG_KSU_SUSFS_SUS_PATH
    {
        .cmd = CMD_SUSFS_ADD_SUS_PATH,
        .name = "SUSFS_ADD_SUS_PATH",
        .handler = do_susfs_add_sus_path,
        .perm_check = manager_or_root
    },
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_MOUNT
    {
        .cmd = CMD_SUSFS_ADD_SUS_MOUNT,
        .name = "SUSFS_ADD_SUS_MOUNT",
        .handler = do_susfs_add_sus_mount,
        .perm_check = manager_or_root
    },
#endif
#ifdef CONFIG_KSU_SUSFS_SUS_KSTAT
    {
        .cmd = CMD_SUSFS_ADD_SUS_KSTAT,
        .name = "SUSFS_ADD_SUS_KSTAT",
        .handler = do_susfs_add_sus_kstat,
        .perm_check = manager_or_root
    },
    {
        .cmd = CMD_SUSFS_UPDATE_SUS_KSTAT,
        .name = "SUSFS_UPDATE_SUS_KSTAT",
        .handler = do_susfs_update_sus_kstat,
        .perm_check = manager_or_root
    },
    {
        .cmd = CMD_SUSFS_ADD_SUS_KSTAT_STATICALLY,
        .name = "SUSFS_ADD_SUS_KSTAT_STATICALLY",
        .handler = do_susfs_add_sus_kstat,
        .perm_check = manager_or_root
    },
#endif
#ifdef CONFIG_KSU_SUSFS_TRY_UMOUNT
    {
        .cmd = CMD_SUSFS_ADD_TRY_UMOUNT,
        .name = "SUSFS_ADD_TRY_UMOUNT",
        .handler = do_susfs_add_try_umount,
        .perm_check = manager_or_root
    },
    {
        .cmd = CMD_SUSFS_RUN_UMOUNT_FOR_CURRENT_MNT_NS,
        .name = "SUSFS_RUN_UMOUNT_FOR_CURRENT_MNT_NS",
        .handler = do_susfs_run_umount_for_current_mnt_ns,
        .perm_check = manager_or_root
    },
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_UNAME
    {
        .cmd = CMD_SUSFS_SET_UNAME,
        .name = "SUSFS_SET_UNAME",
        .handler = do_susfs_set_uname,
        .perm_check = manager_or_root
    },
#endif
#ifdef CONFIG_KSU_SUSFS_ENABLE_LOG
    {
        .cmd = CMD_SUSFS_ENABLE_LOG,
        .name = "SUSFS_ENABLE_LOG",
        .handler = do_susfs_enable_log,
        .perm_check = manager_or_root
    },
#endif
#ifdef CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG
    {
        .cmd = CMD_SUSFS_SET_CMDLINE_OR_BOOTCONFIG,
        .name = "SUSFS_SET_CMDLINE_OR_BOOTCONFIG",
        .handler = do_susfs_set_cmdline_or_bootconfig,
        .perm_check = manager_or_root
    },
#endif
#ifdef CONFIG_KSU_SUSFS_OPEN_REDIRECT
    {
        .cmd = CMD_SUSFS_ADD_OPEN_REDIRECT,
        .name = "SUSFS_ADD_OPEN_REDIRECT",
        .handler = do_susfs_add_open_redirect,
        .perm_check = manager_or_root
    },
#endif
    {
        .cmd = CMD_SUSFS_SHOW_VERSION,
        .name = "SUSFS_SHOW_VERSION",
        .handler = do_susfs_show_version,
        .perm_check = manager_or_root
    },
    {
        .cmd = CMD_SUSFS_SHOW_ENABLED_FEATURES,
        .name = "SUSFS_SHOW_ENABLED_FEATURES",
        .handler = do_susfs_show_enabled_features,
        .perm_check = manager_or_root
    },
    {
        .cmd = CMD_SUSFS_SHOW_VARIANT,
        .name = "SUSFS_SHOW_VARIANT",
        .handler = do_susfs_show_variant,
        .perm_check = manager_or_root
    },
#ifdef CONFIG_KSU_SUSFS_SUS_SU
    {
        .cmd = CMD_SUSFS_SHOW_SUS_SU_WORKING_MODE,
        .name = "SUSFS_SHOW_SUS_SU_WORKING_MODE",
        .handler = do_susfs_show_sus_su_working_mode,
        .perm_check = manager_or_root
    },
    {
        .cmd = CMD_SUSFS_IS_SUS_SU_READY,
        .name = "SUSFS_IS_SUS_SU_READY",
        .handler = do_susfs_is_sus_su_ready,
        .perm_check = manager_or_root
    },
    {
        .cmd = CMD_SUSFS_SUS_SU,
        .name = "SUSFS_SUS_SU",
        .handler = do_susfs_sus_su,
        .perm_check = manager_or_root
    },
#endif
#endif
    {
        .cmd = 0,
        .name = NULL,
        .handler = NULL,
        .perm_check = NULL
    } // Sentinel
};
// clang-format on

long ksu_supercall_handle_ioctl(unsigned int cmd, void __user *argp)
{
	int i;

#ifdef CONFIG_KSU_DEBUG
	pr_info("ksu ioctl: cmd=0x%x from uid=%d\n", cmd, current_uid().val);
#endif

	for (i = 0; ksu_ioctl_handlers[i].handler; i++) {
		if (cmd == ksu_ioctl_handlers[i].cmd) {
			// Check permission first
			if (ksu_ioctl_handlers[i].perm_check &&
			    !ksu_ioctl_handlers[i].perm_check()) {
				pr_warn("ksu ioctl: permission denied for cmd=0x%x uid=%d\n",
					cmd, current_uid().val);
				return -EPERM;
			}
			// Execute handler
			return ksu_ioctl_handlers[i].handler(argp);
		}
	}

	pr_warn("ksu ioctl: unsupported command 0x%x\n", cmd);
	return -ENOTTY;
}

void __init ksu_supercall_dump_commands(void)
{
    int i;

    pr_info("KernelSU IOCTL Commands:\n");
    for (i = 0; ksu_ioctl_handlers[i].handler; i++) {
        pr_info("  %-18s = 0x%08x\n", ksu_ioctl_handlers[i].name, ksu_ioctl_handlers[i].cmd);
    }
}

void ksu_supercall_cleanup_state(void)
{
    struct mount_entry *entry, *tmp;

    down_write(&mount_list_lock);
    list_for_each_entry_safe (entry, tmp, &mount_list, list) {
        list_del(&entry->list);
        kfree(entry->umountable);
        kfree(entry);
    }
    up_write(&mount_list_lock);
}
