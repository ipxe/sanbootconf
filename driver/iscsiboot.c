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
#include <ntstrsafe.h>
#include <initguid.h>
#include <ndis.h>
#include <ndisguid.h>
#include <ntddndis.h>
#include <wdmsec.h>
#include <iscsicfg.h>
#include "ibft.h"

/** Tag to use for memory allocation */
#define ISCSIBOOT_POOL_TAG 'bcsi'

/** Start of region to scan in base memory */
#define BASEMEM_START 0x80000

/** End of region to scan in base memory */
#define BASEMEM_END 0xa0000

/** Length of region to scan in base memory */
#define BASEMEM_LEN ( BASEMEM_END - BASEMEM_START )

/** IoControl code to retrieve iSCSI boot data */
#define IOCTL_ISCSIBOOT CTL_CODE ( FILE_DEVICE_UNKNOWN, 1, METHOD_BUFFERED, \
				   FILE_READ_ACCESS )

/** Device private data */
typedef struct _ISCSIBOOT_PRIV {
	/* Copy of iBFT */
	PIBFT_TABLE ibft;
} ISCSIBOOT_PRIV, *PISCSIBOOT_PRIV;

/** Unique GUID for IoCreateDeviceSecure() */
DEFINE_GUID ( GUID_ISCSIBOOT_CLASS, 0x8a2f8602, 0x8f0b, 0x4138,
	      0x8e, 0x16, 0x51, 0x9a, 0x59, 0xf3, 0x07, 0xca );

/** iSCSI boot device name */
static const WCHAR iscsiboot_device_name[] = L"\\Device\\iSCSIBoot";

/** iSCSI boot device symlink name */
static const WCHAR iscsiboot_device_symlink[] = L"\\DosDevices\\iSCSIBoot";

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
 * Open registry key
 *
 * @v reg_key_name	Registry key name
 * @v reg_key		Registry key to fill in
 * @ret ntstatus	NT status
 */
static NTSTATUS reg_open ( LPCWSTR reg_key_name, PHANDLE reg_key ) {
	UNICODE_STRING unicode_string;
	OBJECT_ATTRIBUTES object_attrs;
	NTSTATUS status;

	RtlInitUnicodeString ( &unicode_string, reg_key_name );
	InitializeObjectAttributes ( &object_attrs, &unicode_string,
				     OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
				     NULL, NULL );
	status = ZwOpenKey ( reg_key, KEY_ALL_ACCESS, &object_attrs );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not open %S: %x\n", reg_key_name, status );
		return status;
	}

	return STATUS_SUCCESS;
}

/**
 * Close registry key
 *
 * @v reg_key		Registry key
 */
static VOID reg_close ( HANDLE reg_key ) {
	ZwClose ( reg_key );
}

/**
 * Fetch registry key value information
 *
 * @v reg_key		Registry key
 * @v value_name	Registry value name
 * @v kvi		Key value information block to allocate and fill in
 * @ret ntstatus	NT status
 *
 * The caller must eventually free the allocated key value information
 * block.
 */
static NTSTATUS fetch_reg_kvi ( HANDLE reg_key, LPCWSTR value_name,
				PKEY_VALUE_PARTIAL_INFORMATION *kvi ) {
	UNICODE_STRING u_value_name;
	ULONG kvi_len;
	NTSTATUS status;

	/* Get value length */
	RtlInitUnicodeString ( &u_value_name, value_name );
	status = ZwQueryValueKey ( reg_key, &u_value_name,
				   KeyValuePartialInformation, NULL, 0,
				   &kvi_len );
	if ( ! ( ( status == STATUS_SUCCESS ) ||
		 ( status == STATUS_BUFFER_OVERFLOW ) ||
		 ( status == STATUS_BUFFER_TOO_SMALL ) ) ) {
		DbgPrint ( "Could not get KVI length for \"%S\": %x\n",
			   value_name, status );
		goto err_zwqueryvaluekey_len;
	}

	/* Allocate value buffer */
	*kvi = ExAllocatePoolWithTag ( NonPagedPool, kvi_len,
				       ISCSIBOOT_POOL_TAG );
	if ( ! *kvi ) {
		DbgPrint ( "Could not allocate KVI for \"%S\": %x\n",
			   value_name, status );
		goto err_exallocatepoolwithtag_kvi;
	}

	/* Fetch value */
	status = ZwQueryValueKey ( reg_key, &u_value_name,
				   KeyValuePartialInformation, *kvi,
				   kvi_len, &kvi_len );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not get KVI for \"%S\": %x\n",
			   value_name, status );
		goto err_zwqueryvaluekey;
	}

	return STATUS_SUCCESS;

 err_zwqueryvaluekey:
	ExFreePool ( kvi );
 err_exallocatepoolwithtag_kvi:
 err_zwqueryvaluekey_len:
	return status;
}

/**
 * Fetch registry string value
 *
 * @v reg_key		Registry key
 * @v value_name	Registry value name
 * @v value		String value to allocate and fill in
 * @ret ntstatus	NT status
 *
 * The caller must eventually free the allocated value.
 */
static NTSTATUS fetch_reg_sz ( HANDLE reg_key, LPCWSTR value_name,
			       LPWSTR *value ) {
	PKEY_VALUE_PARTIAL_INFORMATION kvi;
	ULONG value_len;
	NTSTATUS status;

	/* Fetch key value information */
	status = fetch_reg_kvi ( reg_key, value_name, &kvi );
	if ( ! NT_SUCCESS ( status ) )
		goto err_fetch_reg_kvi;

	/* Allocate and populate string */
	value_len = ( kvi->DataLength + sizeof ( value[0] ) );
	*value = ExAllocatePoolWithTag ( NonPagedPool, value_len,
					 ISCSIBOOT_POOL_TAG );
	if ( ! *value ) {
		DbgPrint ( "Could not allocate value for \"%S\"\n",
			   value_name );
		status = STATUS_UNSUCCESSFUL;
		goto err_exallocatepoolwithtag_value;
	}
	RtlZeroMemory ( *value, value_len );
	RtlCopyMemory ( *value, kvi->Data, kvi->DataLength );

 err_exallocatepoolwithtag_value:
	ExFreePool ( kvi );
 err_fetch_reg_kvi:
	return status;
}

/**
 * Fetch registry multiple-string value
 *
 * @v reg_key		Registry key
 * @v value_name	Registry value name
 * @v values		Array of string values to allocate and fill in
 * @ret ntstatus	NT status
 *
 * The caller must eventually free the allocated values.
 */
static NTSTATUS fetch_reg_multi_sz ( HANDLE reg_key, LPCWSTR value_name,
				     LPWSTR **values ) {
	PKEY_VALUE_PARTIAL_INFORMATION kvi;
	LPWSTR string;
	ULONG num_strings;
	ULONG values_len;
	ULONG i;
	NTSTATUS status;

	/* Fetch key value information */
	status = fetch_reg_kvi ( reg_key, value_name, &kvi );
	if ( ! NT_SUCCESS ( status ) )
		goto err_fetch_reg_kvi;

	/* Count number of strings in the array.  This is a
	 * potential(ly harmless) overestimate.
	 */
	num_strings = 0;
	for ( string = ( ( LPWSTR ) kvi->Data ) ;
	      string < ( ( LPWSTR ) ( kvi->Data + kvi->DataLength ) ) ;
	      string++ ) {
		if ( ! *string )
			num_strings++;
	}
	
	/* Allocate and populate string array */
	values_len = ( ( ( num_strings + 1 ) * sizeof ( values[0] ) ) +
		       kvi->DataLength + sizeof ( values[0][0] ) );
	*values = ExAllocatePoolWithTag ( NonPagedPool, values_len,
					  ISCSIBOOT_POOL_TAG );
	if ( ! *values ) {
		DbgPrint ( "Could not allocate value array for \"%S\"\n",
			   value_name );
		status = STATUS_UNSUCCESSFUL;
		goto err_exallocatepoolwithtag_value;
	}
	RtlZeroMemory ( *values, values_len );
	string = ( ( LPWSTR ) ( *values + num_strings + 1 ) );
	RtlCopyMemory ( string, kvi->Data, kvi->DataLength );
	for ( i = 0 ; i < num_strings ; i++ ) {
		(*values)[i] = string;
		while ( *string )
			string++;
		while ( ! *string )
			string++;
	}

 err_exallocatepoolwithtag_value:
	ExFreePool ( kvi );
 err_fetch_reg_kvi:
	return status;
}

/**
 * Store registry string value
 *
 * @v reg_key		Registry key
 * @v value_name	Registry value name
 * @v value		String value to store
 * @ret ntstatus	NT status
 */
static NTSTATUS reg_store_sz ( HANDLE reg_key, LPCWSTR value_name,
			       LPWSTR value ) {
	UNICODE_STRING u_value_name;
	SIZE_T value_len;
	NTSTATUS status;

	RtlInitUnicodeString ( &u_value_name, value_name );
	value_len = ( ( wcslen ( value ) + 1 ) * sizeof ( value[0] ) );
	status = ZwSetValueKey ( reg_key, &u_value_name, 0, REG_SZ,
				 value, ( ( ULONG ) value_len ) );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not store value \"%S\": %x\n",
			   value_name, status );
		return status;
	}

	return STATUS_SUCCESS;
}

/**
 * Store registry string value
 *
 * @v reg_key		Registry key
 * @v value_name	Registry value name
 * @v ...		String values to store (NULL terminated)
 * @ret ntstatus	NT status
 */
static NTSTATUS reg_store_multi_sz ( HANDLE reg_key, LPCWSTR value_name,
				     ... ) {
	UNICODE_STRING u_value_name;
	va_list args;
	LPCWSTR string;
	SIZE_T values_len;
	LPWSTR values;
	LPWSTR value;
	SIZE_T values_remaining;
	SIZE_T value_len;
	NTSTATUS status;

	/* Calculate total buffer length */
	values_len = sizeof ( string[0] );
	va_start ( args, value_name );
	while ( ( string = va_arg ( args, LPCWSTR ) ) != NULL ) {
		values_len += ( ( wcslen ( string ) + 1 ) *
				sizeof ( string[0] ) );
	}
	va_end ( args );

	/* Allocate buffer */
	values = ExAllocatePoolWithTag ( NonPagedPool, values_len,
					 ISCSIBOOT_POOL_TAG );
	if ( ! values ) {
		DbgPrint ( "Could not allocate value buffer for \"%S\"\n" );
		status = STATUS_UNSUCCESSFUL;
		goto err_exallocatepoolwithtag;
	}

	/* Copy strings into buffer */
	RtlZeroMemory ( values, values_len );
	value = values;
	values_remaining = values_len;
	va_start ( args, value_name );
	while ( ( string = va_arg ( args, LPCWSTR ) ) != NULL ) {
		RtlStringCbCatW ( value, values_remaining, string );
		value_len = ( ( wcslen ( value ) + 1 ) * sizeof ( value[0] ) );
		value += ( value_len / sizeof ( value[0] ) );
		values_remaining -= value_len;
	}
	va_end ( args );

	/* Store value */
	RtlInitUnicodeString ( &u_value_name, value_name );
	status = ZwSetValueKey ( reg_key, &u_value_name, 0, REG_MULTI_SZ,
				 values, ( ( ULONG ) values_len ) );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not store value \"%S\": %x\n",
			   value_name, status );
		goto err_zwsetvaluekey;
	}

 err_zwsetvaluekey:
	ExFreePool ( values );
 err_exallocatepoolwithtag:
	return STATUS_SUCCESS;
}

/**
 * Store registry dword value
 *
 * @v reg_key		Registry key
 * @v value_name	Registry value name
 * @v value		String value to store, or NULL
 * @ret ntstatus	NT status
 */
static NTSTATUS reg_store_dword ( HANDLE reg_key, LPCWSTR value_name,
				  ULONG value ) {
	UNICODE_STRING u_value_name;
	NTSTATUS status;

	RtlInitUnicodeString ( &u_value_name, value_name );
	status = ZwSetValueKey ( reg_key, &u_value_name, 0, REG_DWORD,
				 &value, sizeof ( value ) );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not store value \"%S\": %x\n",
			   value_name, status );
		return status;
	}

	return STATUS_SUCCESS;
}

/**
 * Search for iBFT
 *
 * @v start		Region in which to start searching
 * @v len		Length of region
 * @ret ibft_copy	Copy of iBFT, or NULL
 *
 * The returned iBFT is allocated using ExAllocatePool().
 */
static NTSTATUS find_ibft ( PIBFT_TABLE *ibft_copy ) {
	PHYSICAL_ADDRESS basemem_phy;
	PUCHAR basemem;
	ULONG offset;
	PIBFT_TABLE ibft;
	NTSTATUS status;

	/* Map base memory */
	basemem_phy.QuadPart = BASEMEM_START;
	basemem = MmMapIoSpace ( basemem_phy, BASEMEM_LEN, MmNonCached );
	if ( ! basemem ) {
		DbgPrint ( "Could not map base memory\n" );
		status = STATUS_UNSUCCESSFUL;
		goto err_mmmapiospace;
	}

	/* Scan for iBFT */
	status = STATUS_NO_SUCH_FILE;
	for ( offset = 0 ; offset < BASEMEM_LEN ; offset += 16 ) {
		ibft = ( ( PIBFT_TABLE ) ( basemem + offset ) );
		if ( memcmp ( ibft->acpi.signature, IBFT_SIG,
			      sizeof ( ibft->acpi.signature ) ) != 0 )
			continue;
		if ( ( offset + ibft->acpi.length ) > BASEMEM_LEN )
			continue;
		if ( byte_sum ( ( ( PUCHAR ) ibft ), ibft->acpi.length ) != 0 )
			continue;
		DbgPrint ( "Found iBFT at %05x OEM ID \"%.6s\" OEM table ID "
			   "\"%.8s\"\n", ( BASEMEM_START + offset ),
			   ibft->acpi.oem_id, ibft->acpi.oem_table_id );
		/* Create copy of iBFT */
		*ibft_copy = ExAllocatePoolWithTag ( NonPagedPool,
						     ibft->acpi.length,
						     ISCSIBOOT_POOL_TAG );
		if ( ! *ibft_copy ) {
			DbgPrint ( "Could not allocate iBFT copy\n" );
			status = STATUS_NO_MEMORY;
			goto err_exallocatepoolwithtag;
		}
		RtlCopyMemory ( *ibft_copy, ibft, ibft->acpi.length );
		status = STATUS_SUCCESS;
		break;
	}

 err_exallocatepoolwithtag:
	MmUnmapIoSpace ( basemem, BASEMEM_LEN );
 err_mmmapiospace:
	return status;
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
					   ISCSIBOOT_POOL_TAG );
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
 * @v ibft		iBFT
 */
static VOID parse_ibft ( PIBFT_TABLE ibft ) {
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

/**
 * Dummy IRP handler
 *
 * @v device		Device object
 * @v irp		IRP
 * @ret ntstatus	NT status
 */
static NTSTATUS iscsiboot_dummy_irp ( PDEVICE_OBJECT device, PIRP irp ) {

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
static NTSTATUS iscsiboot_iocontrol_irp ( PDEVICE_OBJECT device, PIRP irp ) {
	PIO_STACK_LOCATION irpsp = IoGetCurrentIrpStackLocation ( irp );
	PISCSIBOOT_PRIV priv = device->DeviceExtension;
	ULONG len;
	NTSTATUS status;

	switch ( irpsp->Parameters.DeviceIoControl.IoControlCode ) {
	case IOCTL_ISCSIBOOT:
		DbgPrint ( "iSCSI boot parameters requested\n" );
		len = irpsp->Parameters.DeviceIoControl.OutputBufferLength;
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
 * Create device object and symlink
 *
 * @v driver		Driver object
 * @ret ntstatus	NT status
 */
static NTSTATUS create_iscsiboot_device ( PDRIVER_OBJECT driver,
					  PIBFT_TABLE ibft ) {
	UNICODE_STRING u_device_name;
	UNICODE_STRING u_device_symlink;
	PISCSIBOOT_PRIV priv;
	PDEVICE_OBJECT device;
	NTSTATUS status;

	/* Create device */
	RtlInitUnicodeString ( &u_device_name, iscsiboot_device_name );
	status = IoCreateDeviceSecure ( driver, sizeof ( *priv ),
					&u_device_name, FILE_DEVICE_UNKNOWN,
					FILE_DEVICE_SECURE_OPEN, FALSE,
					&SDDL_DEVOBJ_SYS_ALL_ADM_ALL,
					&GUID_ISCSIBOOT_CLASS, &device );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not create device \"%S\": %x\n",
			   iscsiboot_device_name, status );
		return status;
	}
	priv = device->DeviceExtension;
	priv->ibft = ibft;
	device->Flags &= ~DO_DEVICE_INITIALIZING;

	/* Create device symlink */
	RtlInitUnicodeString ( &u_device_symlink, iscsiboot_device_symlink );
	status = IoCreateSymbolicLink ( &u_device_symlink, &u_device_name );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "Could not create device symlink \"%S\": %x\n",
			   iscsiboot_device_symlink, status );
		return status;
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
	PIBFT_TABLE ibft;
	NTSTATUS status;

	DbgPrint ( "iSCSI Boot Parameter Driver initialising\n" );

	/* Scan for iBFT */
	status = find_ibft ( &ibft );
	if ( ! NT_SUCCESS ( status ) ) {
		DbgPrint ( "No iBFT found\n" );
		/* Lack of an iBFT is not necessarily an error */
		status = STATUS_SUCCESS;
		goto err_no_ibft;
	}

	/* Parse iBFT */
	parse_ibft ( ibft );

	/* Hook in driver methods */
	DriverObject->MajorFunction[IRP_MJ_CREATE] = iscsiboot_dummy_irp;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = iscsiboot_dummy_irp;
	DriverObject->MajorFunction[IRP_MJ_CLEANUP] = iscsiboot_dummy_irp;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] =
		iscsiboot_iocontrol_irp;

	/* Create device object */
	status = create_iscsiboot_device ( DriverObject, ibft );
	if ( ! NT_SUCCESS ( status ) )
		goto err_create_iscsiboot_device;

	DbgPrint ( "iSCSI Boot Parameter Driver initialisation complete\n" );

 err_create_iscsiboot_device:
 err_no_ibft:
	( VOID ) RegistryPath;
	return status;
}
