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

/** Maximum time to wait for boot disk, in seconds */
#define SANBOOTCONF_MAX_WAIT 60

/** Device private data */
typedef struct _SANBOOTCONF_PRIV {
	/* Copy of iBFT, if any */
	PIBFT_TABLE ibft;
	/* Copy of sBFT, if any */
	PSBFT_TABLE sbft;
} SANBOOTCONF_PRIV, *PSANBOOTCONF_PRIV;

/** Unique GUID for IoCreateDeviceSecure() */
DEFINE_GUID ( GUID_SANBOOTCONF_CLASS, 0x8a2f8602, 0x8f0b, 0x4138,
	      0x8e, 0x16, 0x51, 0x9a, 0x59, 0xf3, 0x07, 0xca );

/** IoControl code to retrieve iBFT */
#define IOCTL_SANBOOTCONF_IBFT \
	CTL_CODE ( FILE_DEVICE_UNKNOWN, 0x0001, METHOD_BUFFERED, \
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
 * IoControl IRP handler
 *
 * @v device		Device object
 * @v irp		IRP
 * @ret ntstatus	NT status
 */
static NTSTATUS sanbootconf_iocontrol_irp ( PDEVICE_OBJECT device, PIRP irp ) {
	PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation ( irp );
	PSANBOOTCONF_PRIV priv = device->DeviceExtension;
	ULONG len = irpsp->Parameters.DeviceIoControl.OutputBufferLength;
	NTSTATUS status;

	switch ( irpsp->Parameters.DeviceIoControl.IoControlCode ) {
	case IOCTL_SANBOOTCONF_IBFT:
		DbgPrint ( "iBFT requested\n" );
		if ( priv->ibft ) {
			if ( len > priv->ibft->acpi.length )
				len = priv->ibft->acpi.length;
			RtlCopyMemory ( irp->AssociatedIrp.SystemBuffer,
					priv->ibft, len );
			status = STATUS_SUCCESS;
		} else {
			DbgPrint ( "No iBFT available!\n" );
			status = STATUS_NO_SUCH_FILE;
		}
		break;
	case IOCTL_SANBOOTCONF_SBFT:
		DbgPrint ( "sBFT requested\n" );
		if ( priv->sbft ) {
			if ( len > priv->sbft->acpi.length )
				len = priv->sbft->acpi.length;
			RtlCopyMemory ( irp->AssociatedIrp.SystemBuffer,
					priv->sbft, len );
			status = STATUS_SUCCESS;
		} else {
			DbgPrint ( "No sbft available!\n" );
			status = STATUS_NO_SUCH_FILE;
		}
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
	switch ( partition_info->PartitionStyle ) {
	case PARTITION_STYLE_MBR:
		DbgPrint ( "MBR partition table with signature %08lx\n",
			   partition_info->Mbr.Signature );
		break;
	case PARTITION_STYLE_GPT:
		DbgPrint ( "GPT partition table\n" );
		break;
	default:
		DbgPrint ( "Unknown partition table type\n" );
		break;
	}

	return STATUS_SUCCESS;
}

/**
 * Check for boot disk
 *
 * @v name		Disk device name
 * @ret status		NT status
 */
static NTSTATUS check_boot_disk ( PUNICODE_STRING name ) {
	BOOLEAN must_disable;
	PFILE_OBJECT file;
	PDEVICE_OBJECT device;
	DISK_PARTITION_INFO info;
	NTSTATUS status;

	DbgPrint ( "Found disk \"%wZ\"\n", name );

	/* Enable interface if not already done */
	status = IoSetDeviceInterfaceState ( name, TRUE );
	must_disable = ( NT_SUCCESS ( status ) ? TRUE : FALSE );
	DbgPrint ( "...%s enabled\n",
		   ( must_disable ? "forcibly" : "already" ) );

	/* Get device and file object pointers */
	status = IoGetDeviceObjectPointer ( name, FILE_ALL_ACCESS, &file,
					    &device );
	if ( ! NT_SUCCESS ( status ) ) {

		/* Not an error, apparently; IoGetDeviceInterfaces()
		 * seems to return a whole load of interfaces that
		 * aren't attached to any objects.
		 */
		DbgPrint ( "...could not get device object pointer: %x\n",
			   status );
		goto err_iogetdeviceobjectpointer;
	}

	/* Get disk signature */
	status = fetch_partition_info ( name, device, file, &info );
	if ( ! NT_SUCCESS ( status ) )
		goto err_fetch_partition_info;


	status = STATUS_UNSUCCESSFUL;

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
 * Find boot disk
 *
 * @ret status		NT status
 */
static NTSTATUS find_boot_disk ( VOID ) {
	PWSTR symlinks;
	PWSTR symlink;
	UNICODE_STRING u_symlink;
	NTSTATUS status;

	/* Enumerate all disks */
	status = IoGetDeviceInterfaces ( &GUID_DEVINTERFACE_DISK, NULL,
					 DEVICE_INTERFACE_INCLUDE_NONACTIVE,
					 &symlinks );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not fetch disk list: %x\n", status );
		return status;
	}

	/* Look for the boot disk */
	for ( symlink = symlinks ;
	      RtlInitUnicodeString ( &u_symlink, symlink ) , *symlink ;
	      symlink += ( ( u_symlink.Length / sizeof ( *symlink ) ) + 1 ) ) {
		status = check_boot_disk ( &u_symlink );
		if ( NT_SUCCESS ( status ) )
			goto out;
	}
	status = STATUS_UNSUCCESSFUL;

 out:
	/* Free object list */
	ExFreePool ( symlinks );

	return status;
}

/**
 * Wait for SAN boot disk to appear
 *
 * @v driver		Driver object
 * @v context		Context
 * @v count		Number of times this routine has been called
 */
static VOID sanbootconf_wait ( PDRIVER_OBJECT driver, PVOID context,
			       ULONG count ) {
	LARGE_INTEGER delay;
	NTSTATUS status;

	DbgPrint ( "Waiting for SAN boot disk (attempt %ld)\n", count );

	/* Check for existence of boot disk */
	status = find_boot_disk();
	if ( NT_SUCCESS ( status ) )
		return;

	if ( count >= 10 ) {
		DbgPrint ( "*** HACK ***\n" );
		return;
	}

	/* Give up after too many attempts */
	if ( count >= SANBOOTCONF_MAX_WAIT ) {
		DbgPrint ( "Giving up waiting for SAN boot disk\n" );
		return;
	}

	/* Sleep for a second, reschedule self */
	delay.QuadPart = -10000000L /* 1 second, relative to current time */;
	KeDelayExecutionThread ( KernelMode, FALSE, &delay );
	IoRegisterBootDriverReinitialization ( driver, sanbootconf_wait,
					       context );
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
	PACPI_DESCRIPTION_HEADER table;
	NTSTATUS status;
	BOOLEAN found_san = FALSE;

	DbgPrint ( "SAN Boot Configuration Driver initialising\n" );

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

	/* Look for an iBFT */
	status = find_acpi_table ( IBFT_SIG, &table );
	if ( NT_SUCCESS ( status ) ) {
		priv->ibft = ( ( PIBFT_TABLE ) table );
		parse_ibft ( priv->ibft );
		found_san = TRUE;
	} else {
		/* Lack of an iBFT is not necessarily an error */
		DbgPrint ( "No iBFT found\n" );
		status = STATUS_SUCCESS;
	}

	/* Look for an sBFT */
	status = find_acpi_table ( SBFT_SIG, &table );
	if ( NT_SUCCESS ( status ) ) {
		priv->sbft = ( ( PSBFT_TABLE ) table );
		parse_sbft ( priv->sbft );
		found_san = TRUE;
	} else {
		/* Lack of an sBFT is not necessarily an error */
		DbgPrint ( "No sBFT found\n" );
		status = STATUS_SUCCESS;
	}

	/* Wait for boot disk, if booting from SAN */
	if ( found_san ) {
		DbgPrint ( "Attempting SAN boot; will wait for boot disk\n" );
		IoRegisterBootDriverReinitialization ( DriverObject,
						       sanbootconf_wait,
						       NULL );
	} else {
		DbgPrint ( "No SAN boot method detected\n" );
	}

	DbgPrint ( "SAN Boot Configuration Driver initialisation complete\n" );

 err_create_sanbootconf_device:
	( VOID ) RegistryPath;
	return status;
}
