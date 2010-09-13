#ifndef _ABFT_H
#define _ABFT_H

/*
 * Copyright (C) 2010 Michael Brown <mbrown@fensystems.co.uk>.
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

/** @file
 *
 * AoE boot firmware table
 *
 * The working draft specification for the SRP boot firmware table can
 * be found at
 *
 *   http://www.etherboot.org/wiki/aoe/abft
 *
 */

#include "acpi.h"

/** AoE Boot Firmware Table signature */
#define ABFT_SIG "aBFT"

/**
 * AoE Boot Firmware Table (aBFT)
 */
#pragma pack(1)
typedef struct _ABFT_TABLE {
	/** ACPI header */
	ACPI_DESCRIPTION_HEADER acpi;
	/** AoE shelf */
	USHORT shelf;
	/** AoE slot */
	UCHAR slot;
	/** Reserved */
	UCHAR reserved_a;
	/** MAC address */
	UCHAR mac[6];
} ABFT_TABLE, *PABFT_TABLE;
#pragma pack()

extern VOID parse_abft ( PACPI_DESCRIPTION_HEADER acpi );

#endif /* _ABFT_H */
