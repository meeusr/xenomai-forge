/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@xenomai.org>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _POSIX_SEM_H
#define _POSIX_SEM_H

#include <nucleus/thread.h>       /* For pse51_current_thread and
				   pse51_thread_t definition. */
#include <nucleus/registry.h>     /* For assocq */

#ifndef __XENO_SIM__

typedef struct {
    u_long uaddr;
    unsigned refcnt;
    pse51_assoc_t assoc;

#define assoc2usem(laddr) \
    ((pse51_usem_t *)((unsigned long) (laddr) - offsetof(pse51_usem_t, assoc)))
} pse51_usem_t;

void pse51_sem_usems_cleanup(pse51_queues_t *q);

#endif /* !__XENO_SIM__ */

void pse51_semq_cleanup(pse51_kqueues_t *q);

void pse51_sem_pkg_init(void);

void pse51_sem_pkg_cleanup(void);

int sem_post_inner(struct pse51_sem *sem, pse51_kqueues_t *ownq);

#endif /* !_POSIX_SEM_H */
