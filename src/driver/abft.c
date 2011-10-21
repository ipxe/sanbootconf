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

#pragma warning(disable:4100)  /* unreferenced formal parameter */

#include <ntddk.h>
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>
#include "sanbootconf.h"
#include "boottext.h"
#include "nic.h"
#include "abft.h"

/**
 * Do nothing with NIC
 *
 * @v pdo		Physical device object
 * @v netcfginstanceid	Interface name within registry
 * @v opaque		iBFT NIC structure
 * @ret ntstatus	NT status
 */
static NTSTATUS abft_dummy ( PDEVICE_OBJECT pdo,
			     LPCWSTR netcfginstanceid,
			     PVOID opaque ) {
	return STATUS_SUCCESS;
}

/**
 * Parse aBFT
 *
 * @v acpi		ACPI description header
 */
VOID parse_abft ( PACPI_DESCRIPTION_HEADER acpi ) {
	PABFT_TABLE abft = ( PABFT_TABLE ) acpi;
	NTSTATUS status;

	/* Dump structure information */
	DbgPrint ( "Found aBFT target e%d.%d\n", abft->shelf, abft->slot );
	DbgPrint ( "Found aBFT NIC %02x:%02x:%02x:%02x:%02x:%02x\n",
		   abft->mac[0], abft->mac[1], abft->mac[2],
		   abft->mac[3], abft->mac[4], abft->mac[5] );

	/* Print compressed information on boot splash screen */
	BootPrint ( "NIC %02x:%02x:%02x:%02x:%02x:%02x target e%d.%d\n",
		    abft->mac[0], abft->mac[1], abft->mac[2],
		    abft->mac[3], abft->mac[4], abft->mac[5],
		    abft->shelf, abft->slot );

	/* Check for existence of NIC */
	status = find_nic ( abft->mac, abft_dummy, NULL );
	if ( NT_SUCCESS ( status ) ) {
		DbgPrint ( "Successfully identified aBFT NIC\n" );
	} else {
		DbgPrint ( "Could not identify aBFT NIC\n" );
	}
}
