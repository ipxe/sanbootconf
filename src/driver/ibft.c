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
#pragma warning(disable:4327)  /* indirection alignment mismatch */

#include <ntddk.h>
#include <initguid.h>
#include <ntstrsafe.h>
#include <ndis.h>
#include <ndisguid.h>
#include <ntddndis.h>
#include "sanbootconf.h"
#include "registry.h"
#include "ibft.h"

/**
 * Convert IPv4 address to string
 *
 * @v ipaddr		IP address
 * @ret ipaddr		IP address string
 *
 * This function returns a static buffer.
 */
static LPSTR inet_ntoa ( ULONG ipaddr ) {
	static CHAR buf[16];

	RtlStringCbPrintfA ( buf, sizeof ( buf ), "%d.%d.%d.%d",
			     ( ( ipaddr >> 0  ) & 0xff ),
			     ( ( ipaddr >> 8  ) & 0xff ),
			     ( ( ipaddr >> 16 ) & 0xff ),
			     ( ( ipaddr >> 24 ) & 0xff ) );
	return buf;
}

/**
 * Check to see if iBFT string exists
 *
 * @v string		iBFT string
 * @ret exists		String exists
 */
static BOOLEAN ibft_string_exists ( PIBFT_STRING string ) {
	return ( ( BOOLEAN ) ( string->offset != 0 ) );
}

/**
 * Read iBFT string
 *
 * @v ibft		iBFT
 * @v string		iBFT string
 * @ret string		Standard C string
 */
static LPSTR ibft_string ( PIBFT_TABLE ibft, PIBFT_STRING string ) {
	if ( string->offset ) {
		return ( ( ( PCHAR ) ibft ) + string->offset );
	} else {
		return "";
	}
}

/**
 * Check to see if iBFT IP address exists
 *
 * @v ipaddr		IP address
 * @ret exists		IP address exists
 */
static BOOLEAN ibft_ipaddr_exists ( PIBFT_IPADDR ipaddr ) {
	return ( ( BOOLEAN ) ( ipaddr->in != 0 ) );
}

/**
 * Convert iBFT IP address to string
 *
 * @v ipaddr		IP address
 * @ret ipaddr		IP address string
 *
 * This function returns a static buffer, as per inet_ntoa().
 */
static LPSTR ibft_ipaddr ( PIBFT_IPADDR ipaddr ) {
	return inet_ntoa ( ipaddr->in );
}

/**
 * Parse iBFT initiator structure
 *
 * @v ibft		iBFT
 * @v initiator		Initiator structure
 */
static VOID parse_ibft_initiator ( PIBFT_TABLE ibft,
				   PIBFT_INITIATOR initiator ) {
	PIBFT_HEADER header = &initiator->header;

	/* Dump structure information */
	DbgPrint ( "Found iBFT Initiator %d:\n", header->index );
	DbgPrint ( "  Flags = %#02x%s%s\n", header->flags,
		   ( header->flags & IBFT_FL_INITIATOR_BLOCK_VALID
		     ? ", valid" : "" ),
		   ( header->flags & IBFT_FL_INITIATOR_FIRMWARE_BOOT_SELECTED
		     ? ", boot selected" : "" ) );
	if ( ! ( header->flags & IBFT_FL_INITIATOR_BLOCK_VALID ) )
		return;
	DbgPrint ( "  iSNS = %s\n", ibft_ipaddr ( &initiator->isns_server ) );
	DbgPrint ( "  SLP = %s\n", ibft_ipaddr ( &initiator->slp_server ) );
	DbgPrint ( "  Radius = %s", ibft_ipaddr ( &initiator->radius[0] ) );
	DbgPrint ( ", %s\n", ibft_ipaddr ( &initiator->radius[1] ) );
	DbgPrint ( "  Name = %s\n",
		   ibft_string ( ibft, &initiator->initiator_name ) );
}

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
	status = fetch_reg_sz ( reg_key, L"NetCfgInstanceId",
				netcfginstanceid );
	if ( ! NT_SUCCESS ( status ) )
		goto err_fetch_reg_wstr;

 err_fetch_reg_wstr:
	ZwClose ( reg_key );
 err_ioopendeviceregistrykey:
	return status;
}

/**
 * Store IPv4 parameter into a string registry value
 *
 * @v reg_key		Registry key
 * @v value_name	Registry value name
 * @v ipaddr		IPv4 address
 * @ret ntstatus	NT status
 */
static NTSTATUS store_ipv4_parameter_sz ( HANDLE reg_key, LPCWSTR value_name,
					  ULONG ipaddr ) {
	WCHAR buf[16];
	LPWSTR value;

	if ( ipaddr ) {
		RtlStringCbPrintfW ( buf, sizeof ( buf ),
				     L"%S", inet_ntoa ( ipaddr ) );
		value = buf;
	} else {
		value = L"";
	}

	return reg_store_sz ( reg_key, value_name, value );
}

/**
 * Store IPv4 parameter into a multi-string registry value
 *
 * @v reg_key		Registry key
 * @v value_name	Registry value name
 * @v ipaddr		IPv4 address
 * @ret ntstatus	NT status
 */
static NTSTATUS store_ipv4_parameter_multi_sz ( HANDLE reg_key,
						LPCWSTR value_name,
						ULONG ipaddr ) {
	WCHAR buf[16];
	LPWSTR value;

	if ( ipaddr ) {
		RtlStringCbPrintfW ( buf, sizeof ( buf ),
				     L"%S", inet_ntoa ( ipaddr ) );
		value = buf;
	} else {
		value = NULL;
	}

	return reg_store_multi_sz ( reg_key, value_name, value, NULL );
}

/**
 * Store TCP/IP parameters in registry
 *
 * @v nic		iBFT NIC structure
 * @v netcfginstanceid	Interface name within registry
 * @ret ntstatus	NT status
 */
static NTSTATUS store_tcpip_parameters ( PIBFT_NIC nic,
					 LPCWSTR netcfginstanceid ) {
	LPCWSTR key_name_prefix = ( L"\\Registry\\Machine\\SYSTEM\\"
				    L"CurrentControlSet\\Services\\"
				    L"Tcpip\\Parameters\\Interfaces\\" );
	LPWSTR key_name;
	SIZE_T key_name_len;
	HANDLE reg_key;
	ULONG subnet_mask;
	NTSTATUS status;

	/* Allocate key name */
	key_name_len = ( ( wcslen ( key_name_prefix ) +
			   wcslen ( netcfginstanceid ) + 1 ) *
			 sizeof ( key_name[0] ) );
	key_name = ExAllocatePoolWithTag ( NonPagedPool, key_name_len,
					   SANBOOTCONF_POOL_TAG );
	if ( ! key_name ) {
		DbgPrint ( "Could not allocate TCP/IP key name\n" );
		status = STATUS_UNSUCCESSFUL;
		goto err_exallocatepoolwithtag;
	}

	/* Construct key name */
	RtlStringCbCopyW ( key_name, key_name_len, key_name_prefix );
	RtlStringCbCatW ( key_name, key_name_len, netcfginstanceid );

	/* Open key */
	status = reg_open ( key_name, &reg_key );
	if ( ! NT_SUCCESS ( status ) )
		goto err_reg_open;

	/* Store IP address */
	status = store_ipv4_parameter_multi_sz ( reg_key, L"IPAddress",
						 nic->ip_address.in );
	if ( ! NT_SUCCESS ( status ) )
		goto err_reg_store;

	/* Store subnet mask */
	subnet_mask = RtlUlongByteSwap ( 0xffffffffUL << ( 32 - nic->subnet_mask_prefix ) );
	status = store_ipv4_parameter_multi_sz ( reg_key, L"SubnetMask",
						 subnet_mask );
	if ( ! NT_SUCCESS ( status ) )
		goto err_reg_store;

	/* Store default gateway */
	status = store_ipv4_parameter_multi_sz ( reg_key, L"DefaultGateway",
						 nic->gateway.in );
	if ( ! NT_SUCCESS ( status ) )
		goto err_reg_store;

	/* Store DNS servers */
	status = store_ipv4_parameter_sz ( reg_key, L"NameServer",
					   nic->dns[0].in );
	if ( ! NT_SUCCESS ( status ) )
		goto err_reg_store;

	/* Disable DHCP */
	status = reg_store_dword ( reg_key, L"EnableDHCP", 0 );
	if ( ! NT_SUCCESS ( status ) )
		goto err_reg_store;

 err_reg_store:
	reg_close ( reg_key );
 err_reg_open:
	ExFreePool ( key_name );
 err_exallocatepoolwithtag:
	return status;
}

/**
 * Try to configure NIC from iBFT NIC structure
 *
 * @v nic		iBFT NIC structure
 * @v name		NDIS device name
 * @ret ntstatus	NT status
 */
static NTSTATUS try_configure_nic ( PIBFT_NIC nic, PUNICODE_STRING name ) {
	BOOLEAN must_disable;
	PFILE_OBJECT file;
	PDEVICE_OBJECT device;
	UCHAR mac[6];
	PDEVICE_OBJECT pdo;
	LPWSTR netcfginstanceid;
	NTSTATUS status;

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
	status = fetch_mac ( name, device, file, mac, sizeof ( mac ) );
	if ( ! NT_SUCCESS ( status ) )
		goto err_fetch_mac;
	if ( memcmp ( nic->mac_address, mac, sizeof ( mac ) ) != 0 )
		goto err_compare_mac;
	DbgPrint ( "iBFT NIC %d is interface \"%wZ\"\n",
		   nic->header.index, name );

	/* Get matching PDO */
	status = fetch_pdo ( name, device, &pdo );
	if ( ! NT_SUCCESS ( status ) )
		goto err_fetch_pdo;
	DbgPrint ( "iBFT NIC %d is PDO %p\n", nic->header.index, pdo );

	/* Get NetCfgInstanceId */
	status = fetch_netcfginstanceid ( pdo, &netcfginstanceid );
	if ( ! NT_SUCCESS ( status ) )
		goto err_fetch_netcfginstanceid;
	DbgPrint ( "iBFT NIC %d is NetCfgInstanceId \"%S\"\n",
		   nic->header.index, netcfginstanceid );

	/* Store registry values */
	status = store_tcpip_parameters ( nic, netcfginstanceid );
	if ( ! NT_SUCCESS ( status ) )
		goto err_store_tcpip_parameters;

 err_store_tcpip_parameters:
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
 * Parse iBFT NIC structure
 *
 * @v ibft		iBFT
 * @v nic		NIC structure
 */
static VOID parse_ibft_nic ( PIBFT_TABLE ibft, PIBFT_NIC nic ) {
	PIBFT_HEADER header = &nic->header;
	PWSTR symlinks;
	PWSTR symlink;
	UNICODE_STRING u_symlink;
	NTSTATUS status;

	/* Dump structure information */
	DbgPrint ( "Found iBFT NIC %d:\n", header->index );
	DbgPrint ( "  Flags = %#02x%s%s\n", header->flags,
		   ( header->flags & IBFT_FL_NIC_BLOCK_VALID
		     ? ", valid" : "" ),
		   ( header->flags & IBFT_FL_NIC_FIRMWARE_BOOT_SELECTED
		     ? ", boot selected" : "" ),
		   ( header->flags & IBFT_FL_NIC_GLOBAL
		     ? ", global address" : ", link local address" ) );
	if ( ! ( header->flags & IBFT_FL_NIC_BLOCK_VALID ) )
		return;
	DbgPrint ( "  IP = %s/%d\n", ibft_ipaddr ( &nic->ip_address ),
		   nic->subnet_mask_prefix );
	DbgPrint ( "  Origin = %d\n", nic->origin );
	DbgPrint ( "  Gateway = %s\n", ibft_ipaddr ( &nic->gateway ) );
	DbgPrint ( "  DNS = %s", ibft_ipaddr ( &nic->dns[0] ) );
	DbgPrint ( ", %s\n", ibft_ipaddr ( &nic->dns[1] ) );
	DbgPrint ( "  DHCP = %s\n", ibft_ipaddr ( &nic->dhcp ) );
	DbgPrint ( "  VLAN = %04x\n", nic->vlan );
	DbgPrint ( "  MAC = %02x:%02x:%02x:%02x:%02x:%02x\n",
		   nic->mac_address[0], nic->mac_address[1],
		   nic->mac_address[2], nic->mac_address[3],
		   nic->mac_address[4], nic->mac_address[5] );
	DbgPrint ( "  PCI = %02x:%02x.%x\n",
		   ( ( nic->pci_bus_dev_func >> 8 ) & 0xff ),
		   ( ( nic->pci_bus_dev_func >> 3 ) & 0x1f ),
		   ( ( nic->pci_bus_dev_func >> 0 ) & 0x07 ) );
	DbgPrint ( "  Hostname = %s\n", ibft_string ( ibft, &nic->hostname ) );

	/* Get list of all objects providing GUID_NDIS_LAN_CLASS interface */
	status = IoGetDeviceInterfaces ( &GUID_NDIS_LAN_CLASS, NULL,
					 DEVICE_INTERFACE_INCLUDE_NONACTIVE,
					 &symlinks );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not fetch NIC list: %x\n", status );
		return;
	}

	/* Configure any matching NICs */
	for ( symlink = symlinks ;
	      RtlInitUnicodeString ( &u_symlink, symlink ) , *symlink ;
	      symlink += ( ( u_symlink.Length / sizeof ( *symlink ) ) + 1 ) ) {
		try_configure_nic ( nic, &u_symlink );
	}

	/* Free object list */
	ExFreePool ( symlinks );
}

/**
 * Parse iBFT target structure
 *
 * @v ibft		iBFT
 * @v target		Target structure
 */
static VOID parse_ibft_target ( PIBFT_TABLE ibft, PIBFT_TARGET target ) {
	PIBFT_HEADER header = &target->header;

	/* Dump structure information */
	DbgPrint ( "Found iBFT target %d:\n", header->index );
	DbgPrint ( "  Flags = %#02x%s%s\n", header->flags,
		   ( header->flags & IBFT_FL_TARGET_BLOCK_VALID
		     ? ", valid" : "" ),
		   ( header->flags & IBFT_FL_TARGET_FIRMWARE_BOOT_SELECTED
		     ? ", boot selected" : "" ),
		   ( header->flags & IBFT_FL_TARGET_USE_CHAP
		     ? ", Radius CHAP" : "" ),
		   ( header->flags & IBFT_FL_TARGET_USE_RCHAP
		     ? ", Radius rCHAP" : "" ) );
	if ( ! ( header->flags & IBFT_FL_TARGET_BLOCK_VALID ) )
		return;
	DbgPrint ( "  IP = %s\n",
		   ibft_ipaddr ( &target->ip_address ) );
	DbgPrint ( "  Port = %d\n", target->socket );
	DbgPrint ( "  LUN = %04x-%04x-%04x-%04x\n",
		   ( ( target->boot_lun >> 48 ) & 0xffff ),
		   ( ( target->boot_lun >> 32 ) & 0xffff ),
		   ( ( target->boot_lun >> 16 ) & 0xffff ),
		   ( ( target->boot_lun >> 0  ) & 0xffff ) );
	DbgPrint ( "  CHAP type = %d (%s)\n", target->chap_type,
		   ( ( target->chap_type == IBFT_CHAP_NONE ) ? "None" :
		     ( ( target->chap_type == IBFT_CHAP_ONE_WAY ) ? "One-way" :
		       ( ( target->chap_type == IBFT_CHAP_MUTUAL ) ? "Mutual" :
			 "Unknown" ) ) ) );
	DbgPrint ( "  NIC = %d\n", target->nic_association );
	DbgPrint ( "  Name = %s\n",
		   ibft_string ( ibft, &target->target_name ) );
	DbgPrint ( "  CHAP name = %s\n",
		   ibft_string ( ibft, &target->chap_name ) );
	DbgPrint ( "  CHAP secret = %s\n",
		   ( ibft_string_exists ( &target->chap_secret ) ?
		     "<omitted>" : "" ) );
	DbgPrint ( "  Reverse CHAP name = %s\n",
		   ibft_string ( ibft, &target->reverse_chap_name ) );
	DbgPrint ( "  Reverse CHAP secret = %s\n",
		   ( ibft_string_exists ( &target->reverse_chap_secret ) ?
		     "<omitted>" : "" ) );
}

/**
 * Parse iBFT
 *
 * @v acpi		ACPI description header
 */
VOID parse_ibft ( PACPI_DESCRIPTION_HEADER acpi ) {
	PIBFT_TABLE ibft = ( PIBFT_TABLE ) acpi;
	PIBFT_CONTROL control = &ibft->control;
	PUSHORT offset;
	PIBFT_HEADER header;

	/* Scan through all entries in the Control structure */
	for ( offset = &control->extensions ;
	      ( ( PUCHAR ) offset ) <
		      ( ( ( PUCHAR ) control ) + control->header.length ) ;
	      offset++ ) {
		if ( ! *offset )
			continue;
		header = ( ( PIBFT_HEADER ) ( ( ( PUCHAR ) ibft ) + *offset ));
		switch ( header->structure_id ) {
		case IBFT_STRUCTURE_ID_INITIATOR :
			parse_ibft_initiator ( ibft,
					       ( ( PIBFT_INITIATOR ) header ));
			break;
		case IBFT_STRUCTURE_ID_NIC :
			parse_ibft_nic ( ibft, ( ( PIBFT_NIC ) header ) );
			break;
		case IBFT_STRUCTURE_ID_TARGET :
			parse_ibft_target ( ibft,
					    ( ( PIBFT_TARGET ) header ) );
			break;
		default :
			DbgPrint ( "Ignoring unknown iBFT structure ID %d "
				   "index %d\n", header->structure_id,
				   header->index );
			break;
		}
	}
}
