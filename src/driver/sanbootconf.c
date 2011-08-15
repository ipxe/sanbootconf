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
#include <initguid.h>
#include <wdmsec.h>
#include <ntdddisk.h>
#include "sanbootconf.h"
#include "acpi.h"
#include "ibft.h"
#include "sbft.h"
#include "abft.h"
#include "registry.h"
#include "boottext.h"

/** Maximum time to wait for system disk, in seconds */
#define SANBOOTCONF_MAX_WAIT 120

/** Device private data */
typedef struct _SANBOOTCONF_PRIV {
	/* Copy of iBFT, if any */
	PACPI_DESCRIPTION_HEADER ibft;
	/* Copy of aBFT, if any */
	PACPI_DESCRIPTION_HEADER abft;
	/* Copy of sBFT, if any */
	PACPI_DESCRIPTION_HEADER sbft;
} SANBOOTCONF_PRIV, *PSANBOOTCONF_PRIV;

/** Unique GUID for IoCreateDeviceSecure() */
DEFINE_GUID ( GUID_SANBOOTCONF_CLASS, 0x8a2f8602, 0x8f0b, 0x4138,
	      0x8e, 0x16, 0x51, 0x9a, 0x59, 0xf3, 0x07, 0xca );

/** IoControl code to retrieve iBFT */
#define IOCTL_SANBOOTCONF_IBFT \
	CTL_CODE ( FILE_DEVICE_UNKNOWN, 0x0001, METHOD_BUFFERED, \
		   FILE_READ_ACCESS )

/** IoControl code to retrieve aBFT */
#define IOCTL_SANBOOTCONF_ABFT \
	CTL_CODE ( FILE_DEVICE_UNKNOWN, 0x0861, METHOD_BUFFERED, \
		   FILE_READ_ACCESS )

/** IoControl code to retrieve sBFT */
#define IOCTL_SANBOOTCONF_SBFT \
	CTL_CODE ( FILE_DEVICE_UNKNOWN, 0x0873, METHOD_BUFFERED, \
		   FILE_READ_ACCESS )

/** Device name */
static const WCHAR sanbootconf_device_name[] = L"\\Device\\sanbootconf";

/** Device symlinks */
static const PWCHAR sanbootconf_device_symlink[] = {
	L"\\Device\\iSCSIBoot",
	L"\\DosDevices\\iSCSIBoot",
};

/**
 * Load driver parameters
 *
 * @v key_name		Driver key name
 * @ret ntstatus	NT status
 */
static NTSTATUS load_parameters ( LPCWSTR key_name ) {
	HANDLE reg_key;
	ULONG boottext;
	NTSTATUS status;

	/* Open Parameters key */
	status = reg_open ( &reg_key, key_name, L"Parameters", NULL );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not open Parameters key: %x\n", status );
		goto err_reg_open;
	}

	/* Retrieve BootText parameter */
	status = reg_fetch_dword ( reg_key, L"BootText", &boottext );
	if ( NT_SUCCESS ( status ) ) {
		boottext_enabled = ( ( boottext != 0 ) ? TRUE : FALSE );
		DbgPrint ( "Boot screen text is %s\n",
			   ( boottext_enabled ? "enabled" : "disabled" ) );
	} else {
		DbgPrint ( "Could not read BootText parameter: %x\n", status );
		/* Treat as non-fatal error */
		status = STATUS_SUCCESS;
	}

	reg_close ( reg_key );
 err_reg_open:
	return status;
}

/**
 * Dummy IRP handler
 *
 * @v device		Device object
 * @v irp		IRP
 * @ret ntstatus	NT status
 */
static NTSTATUS sanbootconf_dummy_irp ( PDEVICE_OBJECT device, PIRP irp ) {

	irp->IoStatus.Status = STATUS_SUCCESS;
	irp->IoStatus.Information = 0;
	IoCompleteRequest ( irp, IO_NO_INCREMENT );

	( VOID ) device;
	return STATUS_SUCCESS;
}

/**
 * Fetch ACPI table copy
 *
 * @v signature		Table signature
 * @v acpi		ACPI header
 * @v buf		Buffer
 * @v len		Length of buffer
 * @ret ntstatus	NT status
 */
static NTSTATUS fetch_acpi_table_copy ( PCHAR signature,
					PACPI_DESCRIPTION_HEADER acpi,
					PCHAR buf, ULONG len ) {

	DbgPrint ( "%s requested\n", signature );

	if ( ! acpi ) {
		DbgPrint ( "No %s available!\n", signature );
		return STATUS_NO_SUCH_FILE;
	}

	if ( len > acpi->length )
		len = acpi->length;
	RtlCopyMemory ( buf, acpi, len );

	return STATUS_SUCCESS;
}

/**
 * IoControl IRP handler
 *
 * @v device		Device object
 * @v irp		IRP
 * @ret ntstatus	NT status
 */
static NTSTATUS sanbootconf_iocontrol_irp ( PDEVICE_OBJECT device, PIRP irp ) {
	PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation ( irp );
	PSANBOOTCONF_PRIV priv = device->DeviceExtension;
	PCHAR buf = irp->AssociatedIrp.SystemBuffer;
	ULONG len = irpsp->Parameters.DeviceIoControl.OutputBufferLength;
	NTSTATUS status;

	switch ( irpsp->Parameters.DeviceIoControl.IoControlCode ) {
	case IOCTL_SANBOOTCONF_IBFT:
		status = fetch_acpi_table_copy ( IBFT_SIG, priv->ibft,
						 buf, len );
		break;
	case IOCTL_SANBOOTCONF_ABFT:
		status = fetch_acpi_table_copy ( ABFT_SIG, priv->abft,
						 buf, len );
		break;
	case IOCTL_SANBOOTCONF_SBFT:
		status = fetch_acpi_table_copy ( SBFT_SIG, priv->sbft,
						 buf, len );
		break;
	default:
		DbgPrint ( "Unrecognised IoControl %x\n",
			   irpsp->Parameters.DeviceIoControl.IoControlCode );
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	irp->IoStatus.Status = status;
	irp->IoStatus.Information = 0;
	IoCompleteRequest ( irp, IO_NO_INCREMENT );
	return status;
}

/**
 * Create device object and symlinks
 *
 * @v driver		Driver object
 * @ret ntstatus	NT status
 */
static NTSTATUS create_sanbootconf_device ( PDRIVER_OBJECT driver,
					    PDEVICE_OBJECT *device ) {
	UNICODE_STRING u_device_name;
	UNICODE_STRING u_device_symlink;
	PSANBOOTCONF_PRIV priv;
	ULONG i;
	NTSTATUS status;

	/* Create device */
	RtlInitUnicodeString ( &u_device_name, sanbootconf_device_name );
	status = IoCreateDeviceSecure ( driver, sizeof ( *priv ),
					&u_device_name, FILE_DEVICE_UNKNOWN,
					FILE_DEVICE_SECURE_OPEN, FALSE,
					&SDDL_DEVOBJ_SYS_ALL_ADM_ALL,
					&GUID_SANBOOTCONF_CLASS, device );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not create device \"%S\": %x\n",
			   sanbootconf_device_name, status );
		return status;
	}
	priv = (*device)->DeviceExtension;
	RtlZeroMemory ( priv, sizeof ( *priv ) );
	(*device)->Flags &= ~DO_DEVICE_INITIALIZING;

	/* Create device symlinks */
	for ( i = 0 ; i < ( sizeof ( sanbootconf_device_symlink ) /
			    sizeof ( sanbootconf_device_symlink[0] ) ) ; i++ ){
		RtlInitUnicodeString ( &u_device_symlink,
				       sanbootconf_device_symlink[i] );
		status = IoCreateSymbolicLink ( &u_device_symlink,
						&u_device_name );
		if ( ! NT_SUCCESS ( status ) ) {
			DbgPrint ( "Could not create device symlink \"%S\": "
				   "%x\n", sanbootconf_device_symlink[i],
				   status );
			return status;
		}
	}

	return STATUS_SUCCESS;
}

/**
 * Fetch disk signature
 *
 * @v name		Disk device name
 * @v device		Disk device object
 * @v file		Disk file object
 * @v info		Partition information buffer
 * @ret status		NT status
 */
static NTSTATUS fetch_partition_info ( PUNICODE_STRING name,
				       PDEVICE_OBJECT device,
				       PFILE_OBJECT file,
				       PDISK_PARTITION_INFO info ) {
	KEVENT event;
	struct {
		DISK_GEOMETRY_EX geometry;
		DISK_PARTITION_INFO __dummy_partition_info;
		DISK_DETECTION_INFO __dummy_detection_info;
	} buf;
	IO_STATUS_BLOCK io_status;
	PIRP irp;
	PIO_STACK_LOCATION io_stack;
	PDISK_PARTITION_INFO partition_info;
	NTSTATUS status;

	/* Construct IRP to fetch drive geometry */
	KeInitializeEvent ( &event, NotificationEvent, FALSE );
	irp = IoBuildDeviceIoControlRequest ( IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
					      device, NULL, 0, &buf,
					      sizeof ( buf ), FALSE, &event,
					      &io_status );
	if ( ! irp ) {
		DbgPrint ( "Could not build IRP to retrieve geometry for "
			   "\"%wZ\"\n", name );
		return STATUS_UNSUCCESSFUL;
	}
	io_stack = IoGetNextIrpStackLocation ( irp );
	io_stack->FileObject = file;

	/* Issue IRP */
	status = IoCallDriver ( device, irp );
	if ( status == STATUS_PENDING ) {
		status = KeWaitForSingleObject ( &event, Executive, KernelMode,
						 FALSE, NULL );
	}
	if ( NT_SUCCESS ( status ) )
		status = io_status.Status;
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "IRP failed to retrieve geometry for \"%wZ\": %x\n",
			   name, status );
		return status;
	}

	/* Extract partition information */
	partition_info = DiskGeometryGetPartition ( &buf.geometry );
	memcpy ( info, partition_info, sizeof ( *info ) );

	return STATUS_SUCCESS;
}

/**
 * Check for system disk
 *
 * @v name		Disk device name
 * @v boot_info		Boot disk information
 * @ret status		NT status
 */
static NTSTATUS check_system_disk ( PUNICODE_STRING name,
				    PBOOTDISK_INFORMATION_EX boot_info ) {
	BOOLEAN must_disable;
	PFILE_OBJECT file;
	PDEVICE_OBJECT device;
	DISK_PARTITION_INFO info;
	NTSTATUS status;

	/* Enable interface if not already done */
	status = IoSetDeviceInterfaceState ( name, TRUE );
	must_disable = ( NT_SUCCESS ( status ) ? TRUE : FALSE );

	/* Get device and file object pointers */
	status = IoGetDeviceObjectPointer ( name, FILE_ALL_ACCESS, &file,
					    &device );
	if ( ! NT_SUCCESS ( status ) ) {
		/* Most probably not yet attached */
		DbgPrint ( "  Disk unavailable (%lx): \"%wZ\"\n",
			   status, name );
		goto err_iogetdeviceobjectpointer;
	}

	/* Get disk signature */
	status = fetch_partition_info ( name, device, file, &info );
	if ( ! NT_SUCCESS ( status ) )
		goto err_fetch_partition_info;

	/* Check for a matching disk signature */
	status = STATUS_UNSUCCESSFUL;
	switch ( info.PartitionStyle ) {
	case PARTITION_STYLE_MBR:
		DbgPrint ( "  MBR %08lx: \"%wZ\"\n",
			   info.Mbr.Signature, name );
		if ( boot_info->SystemDeviceIsGpt )
			goto err_not_system_disk;
		if ( info.Mbr.Signature != boot_info->SystemDeviceSignature )
			goto err_not_system_disk;
		break;
	case PARTITION_STYLE_GPT:
		DbgPrint ( "  GPT " GUID_FMT ": \"%wZ\"\n",
			   GUID_ARGS ( info.Gpt.DiskId ), name );
		if ( ! boot_info->SystemDeviceIsGpt )
			goto err_not_system_disk;
		if ( ! IsEqualGUID ( &info.Gpt.DiskId,
				     &boot_info->SystemDeviceGuid ) )
			goto err_not_system_disk;
		break;
	default:
		DbgPrint ( "  Unhandled disk style %d: \"%wZ\"\n",
			   info.PartitionStyle, name );
		status = STATUS_NOT_SUPPORTED;
		goto err_unknown_type;
	}

	/* Success */
	DbgPrint ( "Found system disk at \"%wZ\"\n", name );
	status = STATUS_SUCCESS;

 err_not_system_disk:
 err_unknown_type:
 err_fetch_partition_info:
	/* Drop object reference */
	ObDereferenceObject ( file );
 err_iogetdeviceobjectpointer:
	/* Disable interface if we had to enable it */
	if ( must_disable )
		IoSetDeviceInterfaceState ( name, FALSE );
	return status;
}

/**
 * Find system disk
 *
 * @ret status		NT status
 */
static NTSTATUS find_system_disk ( VOID ) {
	union {
		BOOTDISK_INFORMATION basic;
		BOOTDISK_INFORMATION_EX extended;
	} boot_info;
	PWSTR symlinks;
	PWSTR symlink;
	UNICODE_STRING u_symlink;
	NTSTATUS status;

	/* Get boot disk information */
	RtlZeroMemory ( &boot_info, sizeof ( boot_info ) );
	status = IoGetBootDiskInformation ( &boot_info.basic,
					    sizeof ( boot_info ) );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not get boot disk information: %x\n",
			   status );
		goto err_getbootdiskinformation;
	}
	if ( boot_info.extended.SystemDeviceIsGpt ) {
		DbgPrint ( "  System disk is GPT " GUID_FMT ",",
			   GUID_ARGS ( boot_info.extended.SystemDeviceGuid ) );
	} else if ( boot_info.extended.SystemDeviceSignature ) {
		DbgPrint ( "  System disk is MBR %08lx,",
			   boot_info.extended.SystemDeviceSignature );
	} else {
		DbgPrint ( "  System disk is <unknown>," );
	}
	if ( boot_info.extended.BootDeviceIsGpt ) {
		DbgPrint ( " boot disk is GPT " GUID_FMT "\n",
			   GUID_ARGS ( boot_info.extended.BootDeviceGuid ) );
	} else if ( boot_info.extended.BootDeviceSignature ) {
		DbgPrint ( " boot disk is MBR %08lx\n",
			   boot_info.extended.BootDeviceSignature );
	} else {
		DbgPrint ( " boot disk is <unknown>\n" );
	}

	/* Enumerate all disks */
	status = IoGetDeviceInterfaces ( &GUID_DEVINTERFACE_DISK, NULL,
					 DEVICE_INTERFACE_INCLUDE_NONACTIVE,
					 &symlinks );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not fetch disk list: %x\n", status );
		goto err_getdeviceinterfaces;
	}

	/* Look for the system disk */
	for ( symlink = symlinks ;
	      RtlInitUnicodeString ( &u_symlink, symlink ) , *symlink ;
	      symlink += ( ( u_symlink.Length / sizeof ( *symlink ) ) + 1 ) ) {
		status = check_system_disk ( &u_symlink, &boot_info.extended );
		if ( NT_SUCCESS ( status ) )
			break;
	}

	/* Free object list */
	ExFreePool ( symlinks );
 err_getdeviceinterfaces:
 err_getbootdiskinformation:
	return status;
}

/**
 * Wait for SAN system disk to appear
 *
 * @v driver		Driver object
 * @v context		Context
 * @v count		Number of times this routine has been called
 */
static VOID sanbootconf_wait ( PDRIVER_OBJECT driver, PVOID context,
			       ULONG count ) {
	LARGE_INTEGER delay;
	NTSTATUS status;

	DbgPrint ( "Waiting for SAN system disk (attempt %ld)\n", count );

	/* Check for existence of system disk */
	status = find_system_disk();
	if ( NT_SUCCESS ( status ) ) {
		DbgPrint ( "Found SAN system disk; proceeding with boot\n" );
		return;
	}

	/* Give up after too many attempts */
	if ( count >= SANBOOTCONF_MAX_WAIT ) {
		DbgPrint ( "Giving up waiting for SAN system disk\n" );
		return;
	}

	/* Sleep for a second, reschedule self */
	delay.QuadPart = -10000000L /* 1 second, relative to current time */;
	KeDelayExecutionThread ( KernelMode, FALSE, &delay );
	IoRegisterBootDriverReinitialization ( driver, sanbootconf_wait,
					       context );
}

/**
 * Try to find ACPI table
 *
 * @v signature		Table signature
 * @v label		Label for boot message
 * @v parse		Table parser
 * @ret table_copy	Copy of table, or NULL
 * @ret found		Table was found
 */
static BOOLEAN try_find_acpi_table ( PCHAR signature, PCHAR label,
				     VOID ( *parse )
					  ( PACPI_DESCRIPTION_HEADER acpi ),
				     PACPI_DESCRIPTION_HEADER *table_copy ) {
	PACPI_DESCRIPTION_HEADER table;
	NTSTATUS status;

	/* Try to find table */
	status = find_acpi_table ( signature, table_copy );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "No %s found\n", signature );
		return FALSE;
	}
	table = *table_copy;

	/* Inform user that we are attempting a SAN boot */
	BootPrint ( "%s boot via %.8s\n", label, table->oem_table_id );
	/* Warn about use of unsupported software */
	if ( strcmp ( table->oem_table_id, "gPXE" ) == 0 ) {
		BootPrint ( "WARNING: gPXE is no longer supported; "
			    "please upgrade to iPXE (http://ipxe.org)\n" );
	}

	/* Parse table */
	parse ( table );

	return TRUE;
}

/**
 * Driver entry point
 *
 * @v DriverObject	Driver object
 * @v RegistryPath	Driver-specific registry path
 * @ret ntstatus	NT status
 */
NTSTATUS DriverEntry ( IN PDRIVER_OBJECT DriverObject,
		       IN PUNICODE_STRING RegistryPath ) {
	PDEVICE_OBJECT device;
	PSANBOOTCONF_PRIV priv;
	NTSTATUS status;
	BOOLEAN found_san;

	DbgPrint ( "SAN Boot Configuration Driver initialising\n" );

	/* Load driver parameters */
	status = load_parameters ( RegistryPath->Buffer );
	if ( ! NT_SUCCESS ( status ) ) {
		/* Treat as non-fatal error */
		DbgPrint ( "Could not load parameters: %x\n", status );
		status = STATUS_SUCCESS;
	}

	/* Hook in driver methods */
	DriverObject->MajorFunction[IRP_MJ_CREATE] = sanbootconf_dummy_irp;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = sanbootconf_dummy_irp;
	DriverObject->MajorFunction[IRP_MJ_CLEANUP] = sanbootconf_dummy_irp;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] =
		sanbootconf_iocontrol_irp;

	/* Create device object */
	status = create_sanbootconf_device ( DriverObject, &device );
	if ( ! NT_SUCCESS ( status ) )
		goto err_create_sanbootconf_device;
	priv = device->DeviceExtension;

	/* Look for boot firmware tables*/
	found_san =
		( try_find_acpi_table ( IBFT_SIG, "iSCSI",
					parse_ibft, &priv->ibft ) |
		  try_find_acpi_table ( ABFT_SIG, "AoE",
					parse_abft, &priv->abft ) |
		  try_find_acpi_table ( SBFT_SIG, "SRP",
					parse_sbft, &priv->sbft ) );

	/* Wait for system disk, if booting from SAN */
	if ( found_san ) {
		DbgPrint ( "Attempting SAN boot; will wait for system disk\n");
		IoRegisterBootDriverReinitialization ( DriverObject,
						       sanbootconf_wait,
						       NULL );
	} else {
		DbgPrint ( "No SAN boot method detected\n" );
	}

 err_create_sanbootconf_device:
	( VOID ) RegistryPath;
	return status;
}
