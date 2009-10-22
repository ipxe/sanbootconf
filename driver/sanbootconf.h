#ifndef _SANBOOTCONF_H
#define _SANBOOTCONF_H

/*
 * Copyright (C) 2008 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/** DbgPrintEx() wrapper
 *
 * For Vista and above, using DbgPrint() will cause debug messages to
 * be hidden unless explicitly enabled.  We don't want this; it's hard
 * enough already getting diagnostic reports from users.
 */
#if NTDDI_VERSION >= NTDDI_WINXP
#undef DbgPrint
#define DbgPrint(...) DbgPrintEx ( DPFLTR_IHVDRIVER_ID, \
				   DPFLTR_ERROR_LEVEL, __VA_ARGS__ )
#endif /* NTDDI_WINXP */

/** Tag to use for memory allocation */
#define SANBOOTCONF_POOL_TAG 'fcbs'

#endif /* _SANBOOTCONF_H */
