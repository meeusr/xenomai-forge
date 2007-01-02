/**
 * @file
 * @note Copyright (C) 2006,2007 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * \ingroup timebase
 */

/*!
 * \ingroup nucleus
 * \defgroup timebase Time base services.
 *
 * Xenomai implements the notion of time base, by which software
 * timers that belong to different skins may be clocked separately
 * according to distinct frequencies, or aperiodically. In the
 * periodic case, delays and timeouts are given in counts of ticks;
 * the duration of a tick is specified by the time base. In the
 * aperiodic case, timings are directly specified in nanoseconds.
 *
 * Only a single aperiodic (i.e. tick-less) time base may exist in the
 * system, and the nucleus provides for it through the nktbase
 * object. All skins depending on aperiodic timings should bind to the
 * latter (see xntbase_alloc()), also known as the master time
 * base.
 *
 * Skins depending on periodic timings may create and bind to their
 * own time base. Such a periodic time base is managed as a timed
 * slave object of the master time base.  A cascading software timer
 * fired by the master time base according to the appropriate
 * frequency, triggers in turn the update process of the associated
 * timed slave, which eventually fires the elapsed software timers
 * controlled by the periodic time base. In other words, Xenomai
 * emulates periodic timing over an aperiodic policy.
 *
 * Xenomai always controls the underlying timer hardware in a
 * tick-less fashion, also known as the oneshot mode.
 *
 *@{*/

#include <nucleus/pod.h>
#include <nucleus/timer.h>
#include <nucleus/ltt.h>

DECLARE_XNQUEUE(nktimebaseq);

#ifdef CONFIG_XENO_OPT_TIMING_PERIODIC

/*!
 * \fn int xntbase_alloc(const char *name,u_long period,xntbase_t **basep)
 * \brief Allocate a time base.
 *
 * A time base is an abstraction used to provide private clocking
 * information to real-time skins, by which they may operate either in
 * aperiodic or periodic mode, possibly according to distinct clock
 * frequencies in the latter case. This abstraction is required in
 * order to support several RTOS emulators running concurrently, which
 * may exhibit different clocking policies and/or period.
 *
 * Once allocated, a time base may be attached to all software timers
 * created directly or indirectly by a given skin, and influences all
 * timed services accordingly.
 *
 * The xntbase_alloc() service allocates a new time base to the
 * caller, and returns the address of its descriptor. The new time
 * base is left in a disabled state (unless @a period equals
 * XN_APERIODIC_TICK), calling xntbase_start() is needed to enable it.
 *
 * @param name The symbolic name of the new time base. This
 * information is used to report status information when reading from
 * /proc/xenomai/timebases; it has currently no other usage.
 *
 * @param period The duration of the clock tick for the new time base,
 * given as a count of nanoseconds. The special @a XN_APERIODIC_TICK
 * value may be used to retrieve the master - aperiodic - time base,
 * which is always up and running when a real-time skin has called the
 * xnpod_init() service. All other values are meant to define the
 * clock rate of a periodic time base. For instance, passing 1000000
 * (ns) in the @a period parameter will create a periodic time base
 * clocked at a frequency of 1Khz.
 *
 * @param basep A pointer to a memory location which will be written
 * upon success with the address of the allocated time base. If @a
 * period equals XN_APERIODIC_TICK, the address of the built-in master
 * time base descriptor will be copied back to this location.
 *
 * @return 0 is returned on success. Otherwise:
 *
 * - -ENOMEM is returned if no system memory is available to allocate
 * a new time base descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization code
 *
 * Rescheduling: never.
 *
 * @note Any periodic time base allocated by a real-time skin must be
 * released by a call to xntbase_free() before the kernel module
 * implementing the skin may be unloaded.
 */

int xntbase_alloc(const char *name, u_long period, xntbase_t **basep)
{
	xntslave_t *slave;
	xntbase_t *base;
	spl_t s;

	if (period == XN_APERIODIC_TICK) {
		*basep = &nktbase;
		xnarch_declare_tbase(&nktbase);
		return 0;
	}

	slave = (xntslave_t *)xnarch_sysalloc(sizeof(*slave));

	if (!slave)
		return -ENOMEM;

	base = &slave->base;
	base->tickvalue = period;
	base->ticks2sec = 1000000000UL / period;
	base->wallclock_offset = 0;
	base->jiffies = 0;
	base->hook = NULL;
	base->ops = &nktimer_ops_periodic;
	base->name = name;
	inith(&base->link);
	xntslave_init(slave);
	base->status = 0;	/* Not running, no time set, unlocked. */
	*basep = base;
	xnlock_get_irqsave(&nklock, s);
	appendq(&nktimebaseq, &base->link);
	xnlock_put_irqrestore(&nklock, s);

	xnarch_declare_tbase(base);

	return 0;
}

/*!
 * \fn void xntbase_free(xntbase_t *base)
 * \brief Free a time base.
 *
 * This service disarms all outstanding timers from the affected
 * periodic time base, destroys the aperiodic cascading timer,
 * then releases the time base descriptor.
 *
 * @param base The address of the time base descriptor to release.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 *
 * Rescheduling: never.
 *
 * @note Requests to free the master time base are silently caught and
 * discarded; in such a case, outstanding aperiodic timers are left
 * untouched.
 */

void xntbase_free(xntbase_t *base)
{
	spl_t s;

	if (base == &nktbase)
		return;

	xntslave_destroy(base2slave(base));

	xnlock_get_irqsave(&nklock, s);
	removeq(&nktimebaseq, &base->link);
	xnlock_put_irqrestore(&nklock, s);

	xnarch_sysfree(base, sizeof(*base));
}

/*!
 * \fn int xntbase_update(xntbase_t *base, u_long period)
 * \brief Change the period of a time base.
 *
 * @param base The address of the time base descriptor to update.
 *
 * @param period The duration of the clock tick for the time base,
 * given as a count of nanoseconds. This value is meant to define the
 * new clock rate of the affected periodic time base (i.e. 1e9 /
 * period).
 *
 * @return 0 is returned on success. Otherwise:
 *
 * - -EINVAL is returned if an attempt is made to set a null
 * period.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 *
 * @note Requests to update the master time base are silently caught
 * and discarded. The master time base has a fixed aperiodic policy
 * which may not be changed.
 */

int xntbase_update(xntbase_t *base, u_long period)
{
	spl_t s;

	if (base == &nktbase || base->tickvalue == period)
		return 0;

	if (period == XN_APERIODIC_TICK)
		return -EINVAL;

	xnlock_get_irqsave(&nklock, s);
	base->tickvalue = period;
	base->ticks2sec = 1000000000UL / period;
	xntslave_update(base2slave(base), period);
	xnlock_put_irqrestore(&nklock, s);

	return 0;
}

/*!
 * \fn int xntbase_switch(const char *name,u_long period,xntbase_t **basep)
 * \brief Replace a time base.
 *
 * This service is useful for switching the current time base of a
 * real-time skin between aperiodic and periodic modes, by providing a
 * new time base descriptor as needed. The original time base
 * descriptor is freed as a result of this operation (unless it refers
 * to the master time base). The new time base is automatically
 * started by a call to xntbase_start() if the original time base was
 * enabled at the time of the call, or left in a disabled state
 * otherwise.
 *
 * This call handles all mode transitions and configuration changes
 * carefully, i.e. periodic <-> periodic, aperiodic <-> aperiodic,
 * periodic <-> aperiodic.
 *
 * @param name The symbolic name of the new time base. This
 * information is used to report status information when reading from
 * /proc/xenomai/timebases; it has currently no other usage.
 *
 * @param period The duration of the clock tick for the time base,
 * given as a count of nanoseconds. This value is meant to define the
 * new clock rate of the new periodic time base (i.e. 1e9 / period).
 *
 * @param basep A pointer to a memory location which will be first
 * read to pick the address of the original time base to be replaced,
 * then written back upon success with the address of the new time
 * base.  A null pointer is allowed on input in @a basep, in which
 * case the new time base will be created as if xntbase_alloc() had
 * been called directly.
 *
 * @return 0 is returned on success. Otherwise:
 *
 * - -ENOMEM is returned if no system memory is available to allocate
 * a new time base descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization code
 *
 * Rescheduling: never.
 */

int xntbase_switch(const char *name, u_long period, xntbase_t **basep)
{
	xntbase_t *oldbase = *basep, *newbase;
	int err = 0;

	if (!*basep)
		/* Switching from no time base to a valid one is ok,
		 we only need to assume that the old time base was the
		 master one. */
		oldbase = &nktbase;

	if (period == XN_APERIODIC_TICK) {
		if (xntbase_periodic_p(oldbase)) {
			/* The following call cannot fail. */
			xntbase_alloc(name, XN_APERIODIC_TICK, basep);
			xntbase_free(oldbase);
		}
	} else {
		if (xntbase_periodic_p(oldbase))
			xntbase_update(oldbase, period);
		else {
			err = xntbase_alloc(name, period, &newbase);
			if (!err) {
				int enabled = xntbase_enabled_p(oldbase);
				*basep = newbase;
				xntbase_free(oldbase);
				if (enabled)
					xntbase_start(newbase);
			}
		}
	}

	return err;
}

/*!
 * \fn void xntbase_start(xntbase_t *base)
 * \brief Start a time base.
 *
 * This service enables a time base, using a cascading timer running
 * in the master time base as the source of periodic clock
 * ticks. Timers attached to the started time base are immediated
 * armed.
 *
 * @param base The address of the time base descriptor to start.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 *
 * @note Requests to enable the master time base are silently caught
 * and discarded; only the internal service xnpod_enable_timesource()
 * is allowed to start the latter. The master time base remains
 * enabled until no real-time skin remains attached to the nucleus.
 */

void xntbase_start(xntbase_t *base)
{
	if (base == &nktbase || (base->status & XNTBRUN) != 0)
		return;

	xntslave_start(base2slave(base), base->tickvalue);
	base->status |= XNTBRUN;
}

/*!
 * \fn void xntbase_stop(xntbase_t *base)
 * \brief Stop a time base.
 *
 * This service disables a time base, stopping the cascading timer
 * running in the master time base which is used to clock it.
 * Outstanding timers attached to the stopped time base are immediated
 * disarmed.
 *
 * Stopping a time base also invalidates its clock
 * setting. xntbase_set_time() should be called anew to reset it.
 *
 * @param base The address of the time base descriptor to stop.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization code
 * - Kernel-based task
 * - User-space task
 *
 * @note Requests to disable the master time base are silently caught
 * and discarded; only the internal service xnpod_disable_timesource()
 * is allowed to stop the latter. The master time base remains enabled
 * until no real-time skin remains attached to the nucleus.
 */

void xntbase_stop(xntbase_t *base)
{
	if (base == &nktbase || (base->status & XNTBRUN) == 0)
		return;

	xntslave_stop(base2slave(base));
	base->status &= ~(XNTBRUN | XNTBSET);
}

/*!
 * \fn void xntbase_tick(xntbase_t *base)
 * \brief Announce a clock tick to a time base.
 *
 * This service announces a new clock tick to a time base. Normally,
 * only specialized nucleus code would announce clock ticks. However,
 * under certain circumstances, it may be useful to allow client code
 * to send such notifications on their own.
 *
 * Notifying a clock tick to a time base causes the timer management
 * code to check for outstanding timers, which may in turn fire off
 * elapsed timeout handlers. Additionally, periodic time bases
 * (i.e. all but the master time base) would also update their count
 * of elapsed jiffies, in case the current processor has been defined
 * as the internal time keeper (i.e. CPU# == XNTIMER_KEEPER_ID).
 *
 * @param base The address of the time base descriptor to announce a
 * tick to.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Interrupt context only.
 *
 * Rescheduling: never.
 */

void xntbase_tick(xntbase_t *base)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);

	if (base == &nktbase)
		xntimer_tick_aperiodic();
	else {
		unsigned cpu = xnarch_current_cpu();
		struct percpu_cascade *pc = &base2slave(base)->cascade[cpu];
		xntimer_tick_periodic(&pc->timer);
	}

	xnlock_put_irqrestore(&nklock, s);
}

EXPORT_SYMBOL(xntbase_alloc);
EXPORT_SYMBOL(xntbase_free);
EXPORT_SYMBOL(xntbase_update);
EXPORT_SYMBOL(xntbase_switch);
EXPORT_SYMBOL(xntbase_start);
EXPORT_SYMBOL(xntbase_stop);
EXPORT_SYMBOL(xntbase_tick);

#endif /* CONFIG_XENO_OPT_TIMING_PERIODIC */

/*! 
 * \fn void xntbase_set_time(xntbase_t *base, xnticks_t newtime)
 * \brief Set the clock time for a given time base.
 *
 * Each time base tracks the current time as a monotonously increasing
 * count of ticks since the epoch. The epoch is initially the same as
 * the underlying machine time. This service changes the epoch for the
 * specified time base.
 *
 * @param base The address of the affected time base.
 *
 * @param newtime The new time to set for the specified time base.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

void xntbase_set_time(xntbase_t *base, xnticks_t newtime)
{
	spl_t s;

	xnlock_get_irqsave(&nklock, s);
	base->wallclock_offset += newtime - xntbase_get_time(base);
	__setbits(base->status, XNTBSET);
	xnltt_log_event(xeno_ev_timeset, newtime);
	xnlock_put_irqrestore(&nklock, s);
}

/* The master time base - the most precise one, aperiodic, always valid. */

xntbase_t nktbase = {
	.ops = &nktimer_ops_aperiodic,
	.jiffies = 0,	/* Unused. */
	.hook = NULL,
	.wallclock_offset = 0,
	.tickvalue = 1,
	.ticks2sec = 1000000000UL,
	.status = 0,
	.name = "master"
};

EXPORT_SYMBOL(xntbase_set_time);
EXPORT_SYMBOL(nktbase);

/*@}*/