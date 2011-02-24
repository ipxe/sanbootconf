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

#pragma warning(disable:4100)  /* unreferenced formal parameter */
#pragma warning(disable:4327)  /* indirection alignment mismatch */

#include <ntddk.h>
#include <ntstrsafe.h>
#include "sanbootconf.h"
#include "registry.h"
#include "nic.h"
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
 * Convert subnet mask prefix to subnet mask
 *
 * @v prefix		Subnet mask prefix
 * @ret mask		Subnet mask
 */
static ULONG ibft_subnet_mask ( UCHAR prefix ) {
	return RtlUlongByteSwap ( 0xffffffffUL << ( 32 - prefix ) );
}

/**
 * Fill in an IP address field within iBFT
 *
 * @v ipaddr		IP address field
 * @v in		IPv4 address
 */
static void ibft_set_ipaddr ( PIBFT_IPADDR ipaddr, ULONG in ) {
	memset ( ipaddr, 0, sizeof ( *ipaddr ) );
	if ( in ) {
		ipaddr->in = in;
		ipaddr->ones = 0xffff;
	}
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
 * @v pdo		Physical device object
 * @v netcfginstanceid	Interface name within registry
 * @v opaque		iBFT NIC structure
 * @ret ntstatus	NT status
 */
static NTSTATUS store_tcpip_parameters ( PDEVICE_OBJECT pdo,
					 LPCWSTR netcfginstanceid,
					 PVOID opaque ) {
	LPCWSTR key_name_prefix = ( L"\\Registry\\Machine\\SYSTEM\\"
				    L"CurrentControlSet\\Services\\"
				    L"Tcpip\\Parameters\\Interfaces\\" );
	PIBFT_NIC nic = opaque;
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
	subnet_mask = ibft_subnet_mask ( nic->subnet_mask_prefix );
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
 * Parse iBFT NIC structure
 *
 * @v ibft		iBFT
 * @v nic		NIC structure
 */
static VOID parse_ibft_nic ( PIBFT_TABLE ibft, PIBFT_NIC nic ) {
	PIBFT_HEADER header = &nic->header;
	ULONG subnet_mask;
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
	DbgPrint ( "  IP = %s/", ibft_ipaddr ( &nic->ip_address ) );
	subnet_mask = ibft_subnet_mask ( nic->subnet_mask_prefix );
	DbgPrint ( "%s\n", inet_ntoa ( subnet_mask ) );
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

	/* Try to configure NIC */
	status = find_nic ( nic->mac_address, store_tcpip_parameters, nic );
	if ( NT_SUCCESS ( status ) ) {
		DbgPrint ( "Successfully configured iBFT NIC %d\n",
			   header->index );
	} else {
		DbgPrint ( "Could not configure iBFT NIC %d: %x\n",
			   header->index, status );
	}
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
	DbgPrint ( "  LUN = %02x%02x-%02x%02x-%02x%02x-%02x%02x\n",
		   target->boot_lun[0], target->boot_lun[1],
		   target->boot_lun[2], target->boot_lun[3],
		   target->boot_lun[4], target->boot_lun[5],
		   target->boot_lun[6], target->boot_lun[7] );
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
 * Iterate over entries in the iBFT
 *
 * @v entry		Structure
 * @v TYPE		Type of structure
 * @v ibft		iBFT
 * @v offset		Loop counter
 */
#define for_each_ibft_entry( entry, TYPE, ibft, offset )		      \
	for ( offset = &ibft->control.extensions,			      \
	      entry = ( ( PIBFT_ ## TYPE ) ( ( ( PUCHAR ) ibft ) + *offset ));\
	      ( ( PUCHAR ) offset ) < ( ( ( PUCHAR ) &ibft->control ) +	      \
					ibft->control.header.length ) ;	      \
	      offset++,							      \
	      entry = ( ( PIBFT_ ## TYPE ) ( ( ( PUCHAR ) ibft ) + *offset )))\
		if ( ( *offset == 0 ) ||				      \
		     ( entry->header.structure_id !=			      \
		       IBFT_STRUCTURE_ID_ ## TYPE ) ) continue;		      \
		else

/**
 * Parse iBFT
 *
 * @v acpi		ACPI description header
 */
VOID parse_ibft ( PACPI_DESCRIPTION_HEADER acpi ) {
	PIBFT_TABLE ibft = ( PIBFT_TABLE ) acpi;
	PUSHORT initiator_offset;
	PIBFT_INITIATOR initiator;
	PUSHORT nic_offset;
	PIBFT_NIC nic;
	PUSHORT target_offset;
	PIBFT_TARGET target;
	ULONG gateway;
	ULONG network;
	ULONG netmask;
	ULONG attached_targets;

	/* Scan through all iBFT entries */
	for_each_ibft_entry ( initiator, INITIATOR, ibft, initiator_offset )
		parse_ibft_initiator ( ibft, initiator );
	for_each_ibft_entry ( nic, NIC, ibft, nic_offset )
		parse_ibft_nic ( ibft, nic );
	for_each_ibft_entry ( target, TARGET, ibft, target_offset )
		parse_ibft_target ( ibft, target );

	/* If a gateway is specified in the iBFT, the Microsoft iSCSI
	 * initiator will create a static route to the iSCSI target
	 * via that gateway.  (See the knowledge base article at
	 * http://support.microsoft.com/kb/960104 for details.)  If
	 * the target is in the same subnet then this is undesirable,
	 * since it will mean duplicating every outbound packet on the
	 * network.
	 *
	 * We work around this by replacing the gateway address as
	 * follows:
	 *
	 *    a) if no targets are directly attached to this NIC's
	 *       subnet, we leave the gateway address as-is,
	 *
	 *    b) if exactly one target is directly attached to this
	 *       NIC's subnet, we set the gateway address to the
	 *       target's own IP address, thus giving a target-
	 *       specific route that will remain correct even if the
	 *       subnet route is later removed.
	 *
	 *    c) if more than one target is directly attached to this
	 *       NIC's subnet, we blank out the gateway address, thus
	 *       preventing the creation of the undesirable routes.
	 *
	 * Note that none of this affects the normal TCP/IP stack
	 * configuration, which has already been carried out; this
	 * affects only the dedicated routes created by the Microsoft
	 * iSCSI initiator.
	 */
	for_each_ibft_entry ( nic, NIC, ibft, nic_offset ) {
		gateway = nic->gateway.in;
		if ( ! gateway )
			continue;
		netmask = ibft_subnet_mask ( nic->subnet_mask_prefix );
		network = ( nic->ip_address.in & netmask );
		attached_targets = 0;
		for_each_ibft_entry ( target, TARGET, ibft, target_offset ) {
			if ( ( target->ip_address.in & netmask ) != network )
				continue;
			gateway = ( ( attached_targets == 0 ) ?
				    target->ip_address.in : 0 );
			attached_targets++;
		}
		DbgPrint ( "Found %d target(s) directly attached via iBFT NIC "
			   "%d\n", attached_targets, nic->header.index );
		if ( gateway != nic->gateway.in ) {
			DbgPrint ( "Amending gateway for iBFT NIC %d from %s",
				   nic->header.index,
				   ibft_ipaddr ( &nic->gateway ) );
			ibft_set_ipaddr ( &nic->gateway, gateway );
			DbgPrint ( " to %s\n", ibft_ipaddr ( &nic->gateway ) );
		}
	}
}
