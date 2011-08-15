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

#pragma warning(disable:4201)  /* nameless struct/union warning */
#pragma warning(disable:4214)  /* non-int bitfield warning */

#include <ntddk.h>
#include <initguid.h>
#include <ntstrsafe.h>
#include <ndis.h>
#include <ndisguid.h>
#include <ntddndis.h>
#include "sanbootconf.h"
#include "boottext.h"
#include "registry.h"
#include "nic.h"

/**
 * Fetch NIC MAC address
 *
 * @v name		NDIS device name
 * @v device		NDIS device object
 * @v file		NDIS file object
 * @v mac		MAC address buffer
 * @v mac_len		MAC address buffer length
 * @ret ntstatus	NT status
 */
static NTSTATUS fetch_mac ( PUNICODE_STRING name, PDEVICE_OBJECT device,
			    PFILE_OBJECT file, PUCHAR mac, ULONG mac_len ) {
	KEVENT event;
	ULONG in_buf;
	IO_STATUS_BLOCK io_status;
	PIRP irp;
	PIO_STACK_LOCATION io_stack;
	ULONG i;
	NTSTATUS status;

	/* Construct IRP to query MAC address */
	KeInitializeEvent ( &event, NotificationEvent, FALSE );
	in_buf = OID_802_3_CURRENT_ADDRESS;
	irp = IoBuildDeviceIoControlRequest ( IOCTL_NDIS_QUERY_GLOBAL_STATS,
					      device, &in_buf,
					      sizeof ( in_buf ), mac, mac_len,
					      FALSE, &event, &io_status );
	if ( ! irp ) {
		DbgPrint ( "Could not build IRP to retrieve MAC for \"%wZ\"\n",
			   name );
		return STATUS_UNSUCCESSFUL;
	}
	io_stack = IoGetNextIrpStackLocation( irp );
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
		DbgPrint ( "IRP failed to retrieve MAC for \"%wZ\": %x\n",
			   name, status );
		return status;
	}

	/* Dump MAC address */
	DbgPrint ( "Found NIC with MAC address" );
	for ( i = 0 ; i < mac_len ; i++ )
		DbgPrint ( "%c%02x", ( i ? ':' : ' ' ), mac[i] );
	DbgPrint ( " at \"%wZ\"\n", name );

	return STATUS_SUCCESS;
}

/**
 * Fetch NIC PDO
 *
 * @v name		NDIS device name
 * @v device		NDIS device object
 * @v pdo		Associated physical device object
 * @ret ntstatus	NT status
 */
static NTSTATUS fetch_pdo ( PUNICODE_STRING name, PDEVICE_OBJECT device,
			    PDEVICE_OBJECT *pdo ) {
	KEVENT event;
	IO_STATUS_BLOCK io_status;
	PIRP irp;
	PIO_STACK_LOCATION io_stack;
	PDEVICE_RELATIONS relations;
	NTSTATUS status;

	/* Construct IRP to query MAC address */
	KeInitializeEvent ( &event, NotificationEvent, FALSE );
	irp = IoBuildSynchronousFsdRequest ( IRP_MJ_PNP, device, NULL, 0, NULL,
					     &event, &io_status );
	if ( ! irp ) {
		DbgPrint ( "Could not build IRP to retrieve PDO for \"%wZ\"\n",
			   name );
		return STATUS_UNSUCCESSFUL;
	}
	io_stack = IoGetNextIrpStackLocation( irp );
	io_stack->MinorFunction = IRP_MN_QUERY_DEVICE_RELATIONS;
	io_stack->Parameters.QueryDeviceRelations.Type = TargetDeviceRelation;

	/* Issue IRP */
	status = IoCallDriver ( device, irp );
	if ( status == STATUS_PENDING ) {
		status = KeWaitForSingleObject ( &event, Executive, KernelMode,
						 FALSE, NULL );
	}
	if ( NT_SUCCESS ( status ) )
		status = io_status.Status;
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "IRP failed to retrieve PDO for \"%wZ\": %x\n",
			   name, status );
		return status;
	}

	/* Extract PDO */
	relations = ( ( PDEVICE_RELATIONS ) io_status.Information );
	*pdo = relations->Objects[0];

	/* Free the relations list allocated by the IRP */
	ExFreePool ( relations );

	return STATUS_SUCCESS;
}

/**
 * Fetch NetCfgInstanceId registry value
 *
 * @v pdo		Physical device object
 * @v netcfginstanceid	Value to allocate and fill in
 * @ret ntstatus	NT status
 *
 * The caller must eventually free the allocated value.
 */
static NTSTATUS fetch_netcfginstanceid ( PDEVICE_OBJECT pdo,
					 LPWSTR *netcfginstanceid ) {
	HANDLE reg_key;
	NTSTATUS status;

	/* Open driver registry key */
	status = IoOpenDeviceRegistryKey ( pdo, PLUGPLAY_REGKEY_DRIVER,
					   KEY_READ, &reg_key );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not open driver registry key for PDO %p: "
			   "%x\n", pdo, status );
		goto err_ioopendeviceregistrykey;
	}

	/* Read NetCfgInstanceId value */
	status = reg_fetch_sz ( reg_key, L"NetCfgInstanceId",
				netcfginstanceid );
	if ( ! NT_SUCCESS ( status ) )
		goto err_reg_fetch_wstr;

 err_reg_fetch_wstr:
	ZwClose ( reg_key );
 err_ioopendeviceregistrykey:
	return status;
}

/**
 * Try processing NIC
 *
 * @v mac		MAC address
 * @v name		NDIS device name
 * @v process		Processing function
 * @v opaque		Argument to processing function
 * @ret found		NIC found
 * @ret ntstatus	NT status
 */
static NTSTATUS try_nic ( PUCHAR mac, PUNICODE_STRING name, 
			  NTSTATUS ( *process ) ( PDEVICE_OBJECT pdo,
						  LPWSTR netcfginstanceid,
						  PVOID opaque ),
			  PVOID opaque,
			  PBOOLEAN found ) {
	BOOLEAN must_disable;
	PFILE_OBJECT file;
	PDEVICE_OBJECT device;
	UCHAR this_mac[6];
	PDEVICE_OBJECT pdo;
	LPWSTR netcfginstanceid;
	NTSTATUS status;

	/* Mark as not yet found */
	*found = FALSE;

	/* Enable interface if not already done */
	status = IoSetDeviceInterfaceState ( name, TRUE );
	must_disable = ( NT_SUCCESS ( status ) ? TRUE : FALSE );

	/* Get device and file object pointers */
	status = IoGetDeviceObjectPointer ( name, FILE_ALL_ACCESS, &file,
					    &device );
	if ( ! NT_SUCCESS ( status ) ) {
		/* Not an error, apparently; IoGetDeviceInterfaces()
		 * seems to return a whole load of interfaces that
		 * aren't attached to any objects.
		 */
		goto err_iogetdeviceobjectpointer;
	}

	/* See if NIC matches */
	status = fetch_mac ( name, device, file, this_mac,
			     sizeof ( this_mac ) );
	if ( ! NT_SUCCESS ( status ) )
		goto err_fetch_mac;
	if ( memcmp ( mac, this_mac, sizeof ( this_mac ) ) != 0 )
		goto err_compare_mac;
	DbgPrint ( "NIC %02x:%02x:%02x:%02x:%02x:%02x is interface \"%wZ\"\n",
		   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], name );
	*found = TRUE;

	/* Get matching PDO */
	status = fetch_pdo ( name, device, &pdo );
	if ( ! NT_SUCCESS ( status ) )
		goto err_fetch_pdo;
	DbgPrint ( "NIC %02x:%02x:%02x:%02x:%02x:%02x is PDO %p\n",
		   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], pdo );

	/* Get NetCfgInstanceId */
	status = fetch_netcfginstanceid ( pdo, &netcfginstanceid );
	if ( ! NT_SUCCESS ( status ) )
		goto err_fetch_netcfginstanceid;
	DbgPrint ( "NIC %02x:%02x:%02x:%02x:%02x:%02x is NetCfgInstanceId "
		   "\"%S\"\n",  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
		   netcfginstanceid );

	/* Store registry values */
	status = process ( pdo, netcfginstanceid, opaque );
	if ( ! NT_SUCCESS ( status ) )
		goto err_process;

 err_process:
	ExFreePool ( netcfginstanceid );
 err_fetch_netcfginstanceid:
 err_fetch_pdo:
 err_compare_mac:
 err_fetch_mac:
	/* Drop object reference */
	ObDereferenceObject ( file );
 err_iogetdeviceobjectpointer:
	/* Disable interface if we had to enable it */
	if ( must_disable )
		IoSetDeviceInterfaceState ( name, FALSE );
	return status;
}

/**
 * Try processing NIC
 *
 * @v mac		MAC address
 * @v process		Processing function
 * @v opaque		Argument to processing function
 * @ret ntstatus	NT status
 */
NTSTATUS find_nic ( PUCHAR mac,
		    NTSTATUS ( *process ) ( PDEVICE_OBJECT pdo,
					    LPWSTR netcfginstanceid,
					    PVOID opaque ),
		    PVOID opaque ) {
	PWSTR symlinks;
	PWSTR symlink;
	UNICODE_STRING u_symlink;
	BOOLEAN found;
	NTSTATUS status;

	/* Get list of all objects providing GUID_NDIS_LAN_CLASS interface */
	status = IoGetDeviceInterfaces ( &GUID_NDIS_LAN_CLASS, NULL,
					 DEVICE_INTERFACE_INCLUDE_NONACTIVE,
					 &symlinks );
	if ( ! NT_SUCCESS ( status ) ) {
		BootPrint ( "Could not fetch NIC list: %x\n", status );
		return status;
	}

	/* Look for a matching NIC */
	for ( symlink = symlinks ;
	      RtlInitUnicodeString ( &u_symlink, symlink ) , *symlink ;
	      symlink += ( ( u_symlink.Length / sizeof ( *symlink ) ) + 1 ) ) {
		status = try_nic ( mac, &u_symlink, process, opaque, &found );
		if ( found )
			goto done;
	}
	status = STATUS_NO_SUCH_FILE;
	BootPrint ( "ERROR: %02x:%02x:%02x:%02x:%02x:%02x not found\n",
		    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5] );

 done:
	/* Free object list */
	ExFreePool ( symlinks );

	return status;
}
