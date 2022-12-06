/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright © 2001-2007 Red Hat, Inc.
 * Copyright © 2004-2010 David Woodhouse <dwmw2@infradead.org>
 *
 * Created by David Woodhouse <dwmw2@infradead.org>
 *
 * For licensing information, see the file 'LICENCE' in this directory.
 *
 */


#include "phoenix-rtos.h"
#include "nodelist.h"


static void jffs2_garbage_collect_thread(void *);

void jffs2_garbage_collect_trigger(struct jffs2_sb_info *c)
{
	assert_spin_locked(&c->erase_completion_lock);
	if (c->gc_task && jffs2_thread_should_wake(c)) {
		condSignal(c->erase_wait.cond);
	}
}

/* This must only ever be called when no GC thread is currently running */
int jffs2_start_garbage_collect_thread(struct jffs2_sb_info *c)
{
	struct task_struct *tsk;
	int ret = 0;

	BUG_ON(c->gc_task);

	init_completion(&c->gc_thread_start);
	init_completion(&c->gc_thread_exit);

	tsk = kthread_run(jffs2_garbage_collect_thread, c, "jffs2_gcd_mtd%d", c->mtd->index);
	if (IS_ERR(tsk)) {
		pr_warn("fork failed for JFFS2 garbage collect thread: %ld\n",
			-PTR_ERR(tsk));
		complete(&c->gc_thread_exit);
		ret = PTR_ERR(tsk);
	} else {
		/* Wait for it... */
		jffs2_dbg(1, "Garbage collect thread is pid %d\n", tsk->pid);
		wait_for_completion(&c->gc_thread_start);
		ret = tsk->pid;
	}

	return ret;
}

void jffs2_stop_garbage_collect_thread(struct jffs2_sb_info *c)
{
	jffs2_partition_t *part = (jffs2_partition_t *)(OFNI_BS_2SFFJ(c)->s_part);
	int wait = 0;
	spin_lock(&c->erase_completion_lock);
	if (c->gc_task) {
		jffs2_dbg(1, "Killing GC task %d\n", c->gc_task->pid);
		part->stop_gc = 1;
		condSignal(c->erase_wait.cond);
		wait = 1;
	}
	spin_unlock(&c->erase_completion_lock);
	if (wait)
		wait_for_completion(&c->gc_thread_exit);
}

static void jffs2_garbage_collect_thread(void *_c)
{
	struct jffs2_sb_info *c = _c;
	jffs2_partition_t *part = (jffs2_partition_t *)(OFNI_BS_2SFFJ(c)->s_part);
	sigset_t hupmask;
	struct task_struct gc_task;
	int stop_gc;

	siginitset(&hupmask, sigmask(SIGHUP));
	allow_signal(SIGKILL);
	allow_signal(SIGSTOP);
	allow_signal(SIGHUP);

	gc_task.pid = getpid();
	c->gc_task = &gc_task;
	complete(&c->gc_thread_start);

	set_user_nice(current, 10);

	set_freezable();
	for (;;) {
		sigprocmask(SIG_UNBLOCK, &hupmask, NULL);
	again:
		spin_lock(&c->erase_completion_lock);
		stop_gc = part->stop_gc;
		if (!stop_gc && !jffs2_thread_should_wake(c)) {
			set_current_state (TASK_INTERRUPTIBLE);
			spin_unlock(&c->erase_completion_lock);
			jffs2_dbg(1, "%s(): sleeping...\n", __func__);
			mutexLock(c->erase_wait.lock);
			condWait(c->erase_wait.cond, c->erase_wait.lock, 1000000);
			mutexUnlock(c->erase_wait.lock);
		} else {
			spin_unlock(&c->erase_completion_lock);
		}

		if (stop_gc) {
			goto die;
		}
		/* Problem - immediately after bootup, the GCD spends a lot
		 * of time in places like jffs2_kill_fragtree(); so much so
		 * that userspace processes (like gdm and X) are starved
		 * despite plenty of cond_resched()s and renicing.  Yield()
		 * doesn't help, either (presumably because userspace and GCD
		 * are generally competing for a higher latency resource -
		 * disk).
		 * This forces the GCD to slow the hell down.   Pulling an
		 * inode in with read_inode() is much preferable to having
		 * the GC thread get there first. */
		schedule_timeout_interruptible(msecs_to_jiffies(50));

		if (kthread_should_stop()) {
			jffs2_dbg(1, "%s(): kthread_stop() called\n", __func__);
			goto die;
		}

		/* Put_super will send a SIGKILL and then wait on the sem.
		 */
		while (signal_pending(current) || freezing(current)) {
			unsigned long signr;

			if (try_to_freeze())
				goto again;

			signr = kernel_dequeue_signal(NULL);

			switch(signr) {
			case SIGSTOP:
				jffs2_dbg(1, "%s(): SIGSTOP received\n",
					  __func__);
				kernel_signal_stop();
				break;

			case SIGKILL:
				jffs2_dbg(1, "%s(): SIGKILL received\n",
					  __func__);
				goto die;

			case SIGHUP:
				jffs2_dbg(1, "%s(): SIGHUP received\n",
					  __func__);
				break;
			default:
				jffs2_dbg(1, "%s(): signal %ld received\n",
					  __func__, signr);
			}
		}
		/* We don't want SIGHUP to interrupt us. STOP and KILL are OK though. */
		sigprocmask(SIG_BLOCK, &hupmask, NULL);

		jffs2_dbg(1, "%s(): pass\n", __func__);
		if (jffs2_garbage_collect_pass(c) == -ENOSPC) {
			pr_notice("No space for garbage collection. Aborting GC thread\n");
			goto die;
		}
	}
 die:
	spin_lock(&c->erase_completion_lock);
	c->gc_task = NULL;
	spin_unlock(&c->erase_completion_lock);
	complete_and_exit(&c->gc_thread_exit, 0);
}
