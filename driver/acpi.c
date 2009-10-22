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

#include <ntddk.h>
#include "sanbootconf.h"
#include "acpi.h"

/** Start of region to scan in base memory */
#define BASEMEM_START 0x80000

/** End of region to scan in base memory */
#define BASEMEM_END 0xa0000

/** Length of region to scan in base memory */
#define BASEMEM_LEN ( BASEMEM_END - BASEMEM_START )

/**
 * Calculate byte checksum
 *
 * @v data		Region to checksum
 * @v len		Length of region
 * @ret checksum	Byte checksum
 */
static UCHAR byte_sum ( PUCHAR data, ULONG len ) {
	UCHAR checksum = 0;
	ULONG offset;

	for ( offset = 0 ; offset < len ; offset++ )
		checksum = ( ( UCHAR ) ( checksum + data[offset] ) );

	return checksum;
}

/**
 * Search for ACPI table in base memory
 *
 * @v signature		Table signature
 * @ret table_copy	Copy of table, or NULL
 *
 * The returned table is allocated using ExAllocatePool().
 */
NTSTATUS find_acpi_table ( PCHAR signature,
			   PACPI_DESCRIPTION_HEADER *table_copy ) {
	PHYSICAL_ADDRESS basemem_phy;
	PUCHAR basemem;
	ULONG offset;
	PACPI_DESCRIPTION_HEADER table;
	NTSTATUS status;

	/* Map base memory */
	basemem_phy.QuadPart = BASEMEM_START;
	basemem = MmMapIoSpace ( basemem_phy, BASEMEM_LEN, MmNonCached );
	if ( ! basemem ) {
		DbgPrint ( "Could not map base memory\n" );
		status = STATUS_UNSUCCESSFUL;
		goto err_mmmapiospace;
	}

	/* Scan for table */
	status = STATUS_NO_SUCH_FILE;
	for ( offset = 0 ; offset < BASEMEM_LEN ; offset += 16 ) {
		table = ( ( PACPI_DESCRIPTION_HEADER ) ( basemem + offset ) );
		if ( memcmp ( table->signature, signature,
			      sizeof ( table->signature ) ) != 0 )
			continue;
		if ( ( offset + table->length ) > BASEMEM_LEN )
			continue;
		if ( byte_sum ( ( ( PUCHAR ) table ), table->length ) != 0 )
			continue;
		DbgPrint ( "Found ACPI table \"%.4s\" at %05x OEM ID "
			   "\"%.6s\" OEM table ID \"%.8s\"\n", signature,
			   ( BASEMEM_START + offset ), table->oem_id,
			   table->oem_table_id );
		/* Create copy of table */
		*table_copy = ExAllocatePoolWithTag ( NonPagedPool,
						      table->length,
						      SANBOOTCONF_POOL_TAG );
		if ( ! *table_copy ) {
			DbgPrint ( "Could not allocate table copy\n" );
			status = STATUS_NO_MEMORY;
			goto err_exallocatepoolwithtag;
		}
		RtlCopyMemory ( *table_copy, table, table->length );
		status = STATUS_SUCCESS;
		break;
	}

 err_exallocatepoolwithtag:
	MmUnmapIoSpace ( basemem, BASEMEM_LEN );
 err_mmmapiospace:
	return status;
}
