#ifndef _ACPI_H
#define _ACPI_H

/** @file
 *
 * ACPI data structures
 *
 */

/**
 * An ACPI description header
 *
 * This is the structure common to the start of all ACPI system
 * description tables.
 */
#pragma pack(1)
typedef struct _ACPI_DESCRIPTION_HEADER {
	/** ACPI signature (4 ASCII characters) */
	CHAR signature[4];
	/** Length of table, in bytes, including header */
	ULONG length;
	/** ACPI Specification minor version number */
	UCHAR revision;
	/** To make sum of entire table == 0 */
	UCHAR checksum;
	/** OEM identification */
	CHAR oem_id[6];
	/** OEM table identification */
	CHAR oem_table_id[8];
	/** OEM revision number */
	ULONG oem_revision;
	/** ASL compiler vendor ID */
	CHAR asl_compiler_id[4];
	/** ASL compiler revision number */
	ULONG asl_compiler_revision;
} ACPI_DESCRIPTION_HEADER, *PACPI_DESCRIPTION_HEADER;
#pragma pack()

extern NTSTATUS find_acpi_table ( PCHAR signature,
				  PACPI_DESCRIPTION_HEADER *table_copy );

#endif /* _ACPI_H */
