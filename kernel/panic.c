/*
 *  linux/kernel/panic.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * This function is used through-out the kernel (including mm and fs)
 * to indicate a major problem.
 */
#include <linux/debug_locks.h>
#include <linux/interrupt.h>
#include <linux/kmsg_dump.h>
#include <linux/kallsyms.h>
#include <linux/notifier.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/kexec.h>
#include <linux/sched.h>
#include <linux/sysrq.h>
#include <linux/init.h>
#include <linux/nmi.h>
#include <linux/dmi.h>

int panic_on_oops;
static unsigned long tainted_mask;
static int pause_on_oops;
static int pause_on_oops_flag;
static DEFINE_SPINLOCK(pause_on_oops_lock);

#ifndef CONFIG_PANIC_TIMEOUT
#define CONFIG_PANIC_TIMEOUT 0
#endif
int panic_timeout = CONFIG_PANIC_TIMEOUT;

ATOMIC_NOTIFIER_HEAD(panic_notifier_list);

EXPORT_SYMBOL(panic_notifier_list);

/* Returns how long it waited in ms */
long (*panic_blink)(long time);
EXPORT_SYMBOL(panic_blink);

static void panic_blink_one_second(void)
{
	static long i = 0, end;

	if (panic_blink) {
		end = i + MSEC_PER_SEC;

		while (i < end) {
			i += panic_blink(i);
			mdelay(1);
			i++;
		}
	} else {
		/*
		 * When running under a hypervisor a small mdelay may get
		 * rounded up to the hypervisor timeslice. For example, with
		 * a 1ms in 10ms hypervisor timeslice we might inflate a
		 * mdelay(1) loop by 10x.
		 *
		 * If we have nothing to blink, spin on 1 second calls to
		 * mdelay to avoid this.
		 */
		mdelay(MSEC_PER_SEC);
	}
}

/**
 *	panic - halt the system
 *	@fmt: The text string to print
 *
 *	Display a message, then perform cleanups.
 *
 *	This function never returns.
 */

/* [ULS] using sdio_debug_ch variables : start - yocheol.kim@lge.com - 20110725 */

// buffers used by ULS client
#ifdef CONFIG_LGE_SDIO_DEBUG_CH

#define ULS_LOG_FORMAT_VAL_MODE			(1)			// 1: uls mode, 0: test mode - always 1 is used
#define ULS_LOG_FORMAT_LEN_FILE_NAME	(50)		// refer to 	ERR_LOG_MAX_FILE_LEN in errlog.h
#define ULS_LOG_FORMAT_LEN_LINE			(4)			// uint32 in errlog.h
#define ULS_LOG_FORMAT_LEN_ERR_MSG		(80)		// refer to ERR_LOG_MAX_MSG_LEN in errlog.h
#define ULS_LOG_FORMAT_LEN_SW_VER		(100)		//[ULS] for s/w ver display < wj1208.jo@lge.com >
typedef struct 
{
	short	mode; 			// mode is always 1 (0: test mode, 1: ULS mode)
	short	total_size; 	// header + body
} tULS_Rx_Header;
typedef struct  
{
	tULS_Rx_Header header;
	char 	filename[ULS_LOG_FORMAT_LEN_FILE_NAME];
	int		line;
	char 	msg[ULS_LOG_FORMAT_LEN_ERR_MSG];	
	char 	sw_ver[ULS_LOG_FORMAT_LEN_SW_VER];	//[ULS] for s/w ver display < wj1208.jo@lge.com >
} tULS_Rx_Data;

typedef struct
{
	bool				valid;		// 0: invalid, 1: valid
	tULS_Rx_Data		rx_data;	// contains debugging information
} tULS_PRx_Data;


// for UI display
extern tULS_PRx_Data		g_uls_rx_data;	// refer to sdio_debug_ch.c
extern int uls_mdm_crash_fatal_flag;		// refer to mdm.c
extern int uls_mdm_status_low_flag;		// refer to mdm.c
extern int uls_the_kind_of_subsys;	// refer to subsystem_restart.c
extern int g_mdm_status_change_counter;

// for subsystem discrimination
enum {
	ULS_NO_SUBSYSTEM,			// no subsystem crash was occurred.
	ULS_SUBSYSTEM_MDM,		// mdm  subsystem crash was occurred.
	ULS_SUBSYSTEM_MODEM,		// AP 8k modem subsystem crash was occured.
	ULS_SUBSYSTEM_LPASS,		// AP lpass subsystem crash was occured.
	ULS_SUBSYSTEM_OTHER		// this should not be used.
};

#endif  /* CONFIG_LGE_SDIO_DEBUG_CH */
/* [ULS] using sdio_debug_ch variables : end - yocheol.kim@lge.com - 20110725 */

NORET_TYPE void panic(const char * fmt, ...)
{
	static char buf[1024];
	va_list args;
	long i;

	/*
	 * It's possible to come here directly from a panic-assertion and
	 * not have preempt disabled. Some functions called from here want
	 * preempt to be disabled. No point enabling it later though...
	 */
	preempt_disable();

	console_verbose();
	bust_spinlocks(1);
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
#ifdef CONFIG_LGE_HANDLE_PANIC
	set_crash_store_enable();
#endif
	printk(KERN_EMERG "Kernel panic - not syncing: %s\n",buf);
	printk(KERN_EMERG "Kernel Version: %s", linux_banner);
 
/* [ULS] Display MDM Crash : start - yocheol.kim@lge.com - 20110725 */
#ifdef CONFIG_LGE_SDIO_DEBUG_CH
	printk(KERN_EMERG "g_mdm_status_change_counter = %d",g_mdm_status_change_counter);
// only operated for subsystem crash
if (uls_the_kind_of_subsys > ULS_NO_SUBSYSTEM)
{
	// err fatal was occured
	if(uls_mdm_crash_fatal_flag == true)	// uls_the_kind_of_subsys == ULS_SUBSYSTEM_MDM
	{
		printk(KERN_EMERG "\n\nCheck MP side operation (9k_modem)(%d)(%d)!!\n", 
								uls_mdm_crash_fatal_flag, 
								uls_mdm_status_low_flag); 
		
	if(g_uls_rx_data.valid == true)
	{
	printk(KERN_EMERG "\n\nMDM CRASH!!!\n");
	printk(KERN_EMERG "ULS_CRASH_POINT: %s  line : %d\n", g_uls_rx_data.rx_data.filename, g_uls_rx_data.rx_data.line);
	printk(KERN_EMERG "ULS_CRASH_MSG: %s \n", g_uls_rx_data.rx_data.msg);
	printk(KERN_EMERG "SW_VERSION : %s \n", g_uls_rx_data.rx_data.sw_ver); //[ULS] for s/w ver display < wj1208.jo@lge.com >
		}
		else 
		{
			printk(KERN_EMERG "MDM Crash occured! (but, NO ULS LOG)(%d)(%d)",
										uls_mdm_crash_fatal_flag,
										uls_mdm_status_low_flag);
		}
	}
	else	// other subsystem crash (8k modem, lpass) or mdm2ap status is low
	{
		extern int smsm_check_for_modem_crash(void);

		if (uls_the_kind_of_subsys == ULS_SUBSYSTEM_MODEM)
		{
			printk(KERN_EMERG "\n\nCheck AP side operation(8k_modem)!!\n");
			smsm_check_for_modem_crash();
		}
		else if (uls_the_kind_of_subsys == ULS_SUBSYSTEM_LPASS)
		{
			printk(KERN_EMERG "\n\nCheck AP side operation(lpass)!!\n");
			smsm_check_for_modem_crash();
		}
		else {
			if (uls_mdm_status_low_flag == true)
			{
				printk(KERN_EMERG "\n\nCheck MDM side operation(%d)(%d)!!\n", 
										uls_mdm_crash_fatal_flag, 
										uls_mdm_status_low_flag);

				if(g_uls_rx_data.valid == true)
				{
				printk(KERN_EMERG "\n\nMDM CRASH!!!\n");
				printk(KERN_EMERG "ULS_CRASH_POINT: %s  line : %d\n", g_uls_rx_data.rx_data.filename, g_uls_rx_data.rx_data.line);
				printk(KERN_EMERG "ULS_CRASH_MSG: %s \n", g_uls_rx_data.rx_data.msg);
				}
			}
			else {
				printk(KERN_EMERG "\n\nCheck AP side operation(Analyse kernel log)!!\n");				
			}
		}			
	}
}
else {
	; // may be kernel panic - no operation..
}

// initialization
uls_the_kind_of_subsys = ULS_NO_SUBSYSTEM;
uls_mdm_crash_fatal_flag = false;
uls_mdm_status_low_flag = false;

#endif /*CONFIG_LGE_SDIO_DEBUG_CH */
/* [ULS] Display MDM Crash : end - yocheol.kim@lge.com - 20110725 */

#ifdef CONFIG_LGE_HANDLE_PANIC
	set_crash_store_disable();
#endif
#ifdef CONFIG_DEBUG_BUGVERBOSE
	dump_stack();
#endif

	/*
	 * If we have crashed and we have a crash kernel loaded let it handle
	 * everything else.
	 * Do we want to call this before we try to display a message?
	 */
	crash_kexec(NULL);

	kmsg_dump(KMSG_DUMP_PANIC);

	/*
	 * Note smp_send_stop is the usual smp shutdown function, which
	 * unfortunately means it may not be hardened to work in a panic
	 * situation.
	 */
	smp_send_stop();

	atomic_notifier_call_chain(&panic_notifier_list, 0, buf);

	bust_spinlocks(0);

	if (panic_timeout > 0) {
		/*
		 * Delay timeout seconds before rebooting the machine.
		 * We can't use the "normal" timers since we just panicked.
		 */
		printk(KERN_EMERG "Rebooting in %d seconds..", panic_timeout);

		for (i = 0; i < panic_timeout; i++) {
			touch_nmi_watchdog();
			panic_blink_one_second();
		}
		/*
		 * This will not be a clean reboot, with everything
		 * shutting down.  But if there is a chance of
		 * rebooting the system it will be rebooted.
		 */
		emergency_restart();
	}
#ifdef __sparc__
	{
		extern int stop_a_enabled;
		/* Make sure the user can actually press Stop-A (L1-A) */
		stop_a_enabled = 1;
		printk(KERN_EMERG "Press Stop-A (L1-A) to return to the boot prom\n");
	}
#endif
#if defined(CONFIG_S390)
	{
		unsigned long caller;

		caller = (unsigned long)__builtin_return_address(0);
		disabled_wait(caller);
	}
#endif
	local_irq_enable();
	while (1) {
		touch_softlockup_watchdog();
		panic_blink_one_second();
	}
}

EXPORT_SYMBOL(panic);


struct tnt {
	u8	bit;
	char	true;
	char	false;
};

static const struct tnt tnts[] = {
	{ TAINT_PROPRIETARY_MODULE,	'P', 'G' },
	{ TAINT_FORCED_MODULE,		'F', ' ' },
	{ TAINT_UNSAFE_SMP,		'S', ' ' },
	{ TAINT_FORCED_RMMOD,		'R', ' ' },
	{ TAINT_MACHINE_CHECK,		'M', ' ' },
	{ TAINT_BAD_PAGE,		'B', ' ' },
	{ TAINT_USER,			'U', ' ' },
	{ TAINT_DIE,			'D', ' ' },
	{ TAINT_OVERRIDDEN_ACPI_TABLE,	'A', ' ' },
	{ TAINT_WARN,			'W', ' ' },
	{ TAINT_CRAP,			'C', ' ' },
	{ TAINT_FIRMWARE_WORKAROUND,	'I', ' ' },
};

/**
 *	print_tainted - return a string to represent the kernel taint state.
 *
 *  'P' - Proprietary module has been loaded.
 *  'F' - Module has been forcibly loaded.
 *  'S' - SMP with CPUs not designed for SMP.
 *  'R' - User forced a module unload.
 *  'M' - System experienced a machine check exception.
 *  'B' - System has hit bad_page.
 *  'U' - Userspace-defined naughtiness.
 *  'D' - Kernel has oopsed before
 *  'A' - ACPI table overridden.
 *  'W' - Taint on warning.
 *  'C' - modules from drivers/staging are loaded.
 *  'I' - Working around severe firmware bug.
 *
 *	The string is overwritten by the next call to print_tainted().
 */
const char *print_tainted(void)
{
	static char buf[ARRAY_SIZE(tnts) + sizeof("Tainted: ") + 1];

	if (tainted_mask) {
		char *s;
		int i;

		s = buf + sprintf(buf, "Tainted: ");
		for (i = 0; i < ARRAY_SIZE(tnts); i++) {
			const struct tnt *t = &tnts[i];
			*s++ = test_bit(t->bit, &tainted_mask) ?
					t->true : t->false;
		}
		*s = 0;
	} else
		snprintf(buf, sizeof(buf), "Not tainted");

	return buf;
}

int test_taint(unsigned flag)
{
	return test_bit(flag, &tainted_mask);
}
EXPORT_SYMBOL(test_taint);

unsigned long get_taint(void)
{
	return tainted_mask;
}

void add_taint(unsigned flag)
{
	/*
	 * Can't trust the integrity of the kernel anymore.
	 * We don't call directly debug_locks_off() because the issue
	 * is not necessarily serious enough to set oops_in_progress to 1
	 * Also we want to keep up lockdep for staging development and
	 * post-warning case.
	 */
	if (flag != TAINT_CRAP && flag != TAINT_WARN && __debug_locks_off())
		printk(KERN_WARNING "Disabling lock debugging due to kernel taint\n");

	set_bit(flag, &tainted_mask);
}
EXPORT_SYMBOL(add_taint);

static void spin_msec(int msecs)
{
	int i;

	for (i = 0; i < msecs; i++) {
		touch_nmi_watchdog();
		mdelay(1);
	}
}

/*
 * It just happens that oops_enter() and oops_exit() are identically
 * implemented...
 */
static void do_oops_enter_exit(void)
{
	unsigned long flags;
	static int spin_counter;

	if (!pause_on_oops)
		return;

	spin_lock_irqsave(&pause_on_oops_lock, flags);
	if (pause_on_oops_flag == 0) {
		/* This CPU may now print the oops message */
		pause_on_oops_flag = 1;
	} else {
		/* We need to stall this CPU */
		if (!spin_counter) {
			/* This CPU gets to do the counting */
			spin_counter = pause_on_oops;
			do {
				spin_unlock(&pause_on_oops_lock);
				spin_msec(MSEC_PER_SEC);
				spin_lock(&pause_on_oops_lock);
			} while (--spin_counter);
			pause_on_oops_flag = 0;
		} else {
			/* This CPU waits for a different one */
			while (spin_counter) {
				spin_unlock(&pause_on_oops_lock);
				spin_msec(1);
				spin_lock(&pause_on_oops_lock);
			}
		}
	}
	spin_unlock_irqrestore(&pause_on_oops_lock, flags);
}

/*
 * Return true if the calling CPU is allowed to print oops-related info.
 * This is a bit racy..
 */
int oops_may_print(void)
{
	return pause_on_oops_flag == 0;
}

/*
 * Called when the architecture enters its oops handler, before it prints
 * anything.  If this is the first CPU to oops, and it's oopsing the first
 * time then let it proceed.
 *
 * This is all enabled by the pause_on_oops kernel boot option.  We do all
 * this to ensure that oopses don't scroll off the screen.  It has the
 * side-effect of preventing later-oopsing CPUs from mucking up the display,
 * too.
 *
 * It turns out that the CPU which is allowed to print ends up pausing for
 * the right duration, whereas all the other CPUs pause for twice as long:
 * once in oops_enter(), once in oops_exit().
 */
void oops_enter(void)
{
	tracing_off();
	/* can't trust the integrity of the kernel anymore: */
	debug_locks_off();
	do_oops_enter_exit();
}

/*
 * 64-bit random ID for oopses:
 */
static u64 oops_id;

static int init_oops_id(void)
{
	if (!oops_id)
		get_random_bytes(&oops_id, sizeof(oops_id));
	else
		oops_id++;

	return 0;
}
late_initcall(init_oops_id);

static void print_oops_end_marker(void)
{
	init_oops_id();
	printk(KERN_WARNING "---[ end trace %016llx ]---\n",
		(unsigned long long)oops_id);
}

/*
 * Called when the architecture exits its oops handler, after printing
 * everything.
 */
void oops_exit(void)
{
	do_oops_enter_exit();
	print_oops_end_marker();
	kmsg_dump(KMSG_DUMP_OOPS);
}

#ifdef WANT_WARN_ON_SLOWPATH
struct slowpath_args {
	const char *fmt;
	va_list args;
};

static void warn_slowpath_common(const char *file, int line, void *caller,
				 unsigned taint, struct slowpath_args *args)
{
	const char *board;

	printk(KERN_WARNING "------------[ cut here ]------------\n");
	printk(KERN_WARNING "WARNING: at %s:%d %pS()\n", file, line, caller);
	board = dmi_get_system_info(DMI_PRODUCT_NAME);
	if (board)
		printk(KERN_WARNING "Hardware name: %s\n", board);

	if (args)
		vprintk(args->fmt, args->args);

	print_modules();
	dump_stack();
	print_oops_end_marker();
	add_taint(taint);
}

void warn_slowpath_fmt(const char *file, int line, const char *fmt, ...)
{
	struct slowpath_args args;

	args.fmt = fmt;
	va_start(args.args, fmt);
	warn_slowpath_common(file, line, __builtin_return_address(0),
			     TAINT_WARN, &args);
	va_end(args.args);
}
EXPORT_SYMBOL(warn_slowpath_fmt);

void warn_slowpath_fmt_taint(const char *file, int line,
			     unsigned taint, const char *fmt, ...)
{
	struct slowpath_args args;

	args.fmt = fmt;
	va_start(args.args, fmt);
	warn_slowpath_common(file, line, __builtin_return_address(0),
			     taint, &args);
	va_end(args.args);
}
EXPORT_SYMBOL(warn_slowpath_fmt_taint);

void warn_slowpath_null(const char *file, int line)
{
	warn_slowpath_common(file, line, __builtin_return_address(0),
			     TAINT_WARN, NULL);
}
EXPORT_SYMBOL(warn_slowpath_null);
#endif

#ifdef CONFIG_CC_STACKPROTECTOR

/*
 * Called when gcc's -fstack-protector feature is used, and
 * gcc detects corruption of the on-stack canary value
 */
void __stack_chk_fail(void)
{
	panic("stack-protector: Kernel stack is corrupted in: %p\n",
		__builtin_return_address(0));
}
EXPORT_SYMBOL(__stack_chk_fail);

#endif

core_param(panic, panic_timeout, int, 0644);
core_param(pause_on_oops, pause_on_oops, int, 0644);
