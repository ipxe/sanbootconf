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
#include "sanbootconf.h"
#include "acpi.h"
#include "ibft.h"

/** Device private data */
typedef struct _SANBOOTCONF_PRIV {
	/* Copy of iBFT */
	PIBFT_TABLE ibft;
} SANBOOTCONF_PRIV, *PSANBOOTCONF_PRIV;

/** Unique GUID for IoCreateDeviceSecure() */
DEFINE_GUID ( GUID_SANBOOTCONF_CLASS, 0x8a2f8602, 0x8f0b, 0x4138,
	      0x8e, 0x16, 0x51, 0x9a, 0x59, 0xf3, 0x07, 0xca );

/** IoControl code to retrieve iBFT */
#define IOCTL_SANBOOTCONF_IBFT \
	CTL_CODE ( FILE_DEVICE_UNKNOWN, 1, METHOD_BUFFERED, FILE_READ_ACCESS )

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
		if ( len > priv->ibft->acpi.length )
			len = priv->ibft->acpi.length;
		RtlCopyMemory ( irp->AssociatedIrp.SystemBuffer,
				priv->ibft, len );
		status = STATUS_SUCCESS;
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
	unsigned int i;
	NTSTATUS status;

	/* Create device */
	RtlInitUnicodeString ( &u_device_name, sanbootconf_device_name );
	status = IoCreateDeviceSecure ( driver, sizeof ( SANBOOTCONF_PRIV ),
					&u_device_name, FILE_DEVICE_UNKNOWN,
					FILE_DEVICE_SECURE_OPEN, FALSE,
					&SDDL_DEVOBJ_SYS_ALL_ADM_ALL,
					&GUID_SANBOOTCONF_CLASS, device );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not create device \"%S\": %x\n",
			   sanbootconf_device_name, status );
		return status;
	}
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
	} else {
		/* Lack of an iBFT is not necessarily an error */
		DbgPrint ( "No iBFT found\n" );
		status = STATUS_SUCCESS;
	}

	DbgPrint ( "SAN Boot Configuration Driver initialisation complete\n" );

 err_create_sanbootconf_device:
	( VOID ) RegistryPath;
	return status;
}
