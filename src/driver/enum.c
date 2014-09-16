/*
 * Copyright (C) 2014 Michael Brown <mbrown@fensystems.co.uk>.
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
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>
#include "sanbootconf.h"
#include "registry.h"
#include "enum.h"

/** Device enumerator */
struct device_enumerator {
	/** Bus name */
	LPCWSTR bus;
	/** Device ID */
	LPCWSTR id;
};

/* Root enumeration key name */
static const LPCWSTR key_name_enum =
	L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Enum";

/**
 * Enumerate all instances of a device with a given id on a bus
 *
 * @v opaque		Enumerator
 * @v id		Device instance
 * @ret ntstatus	NT status
 */
static NTSTATUS enum_bus_id_instance ( VOID *opaque, LPCWSTR instance ) {
	struct device_enumerator *enumerator = opaque;
	HANDLE reg_key;
	LPWSTR class_guid;
	LPWSTR driver;
	LPWSTR service;
	NTSTATUS status;

	/* Open Enum\<bus>\<id>\<instance> key */
	status = reg_open ( &reg_key, key_name_enum, enumerator->bus,
			    enumerator->id, instance, NULL );
	if ( ! NT_SUCCESS ( status ) )
		goto err_reg_open;

	/* Get ClassGUID, Driver, and Service values, if present */
	reg_fetch_sz ( reg_key, L"ClassGUID", &class_guid );
	reg_fetch_sz ( reg_key, L"Driver", &driver );
	reg_fetch_sz ( reg_key, L"Service", &service );

	DbgPrint ( "Found device %S\\%S\\%S class %S driver %S service %S\n",
		   enumerator->bus, enumerator->id, instance,
		   ( class_guid ? class_guid : L"(unknown)" ),
		   ( driver ? driver : L"(unknown)" ),
		   ( service ? service : L"(unknown)" ) );

	if ( service )
		ExFreePool ( service );
	if ( driver )
		ExFreePool ( driver );
	if ( class_guid )
		ExFreePool ( class_guid );
	reg_close ( reg_key );
 err_reg_open:
	return status;
}

/**
 * Enumerate all devices with a given id on a bus
 *
 * @v opaque		Enumerator
 * @v id		Device ID
 * @ret ntstatus	NT status
 */
static NTSTATUS enum_bus_id ( VOID *opaque, LPCWSTR id ) {
	struct device_enumerator *enumerator = opaque;
	HANDLE reg_key;
	NTSTATUS status;

	/* Open Enum\<bus>\<id> key */
	status = reg_open ( &reg_key, key_name_enum, enumerator->bus, id,
			    NULL );
	if ( ! NT_SUCCESS ( status ) )
		goto err_reg_open;

	/* Enumerate Enum\<bus> key */
	enumerator->id = id;
	status = reg_enum_subkeys ( reg_key, enum_bus_id_instance,
				    enumerator );
	if ( ! NT_SUCCESS ( status ) )
		goto err_enum_subkeys;

 err_enum_subkeys:
	reg_close ( reg_key );
 err_reg_open:
	return status;
}

/**
 * Enumerate all devices on a bus
 *
 * @v opaque		Enumerator
 * @v bus		Bus name
 * @ret ntstatus	NT status
 */
static NTSTATUS enum_bus ( VOID *opaque, LPCWSTR bus ) {
	struct device_enumerator *enumerator = opaque;
	HANDLE reg_key;
	NTSTATUS status;

	/* Open Enum\<bus> key */
	status = reg_open ( &reg_key, key_name_enum, bus, NULL );
	if ( ! NT_SUCCESS ( status ) )
		goto err_reg_open;

	/* Enumerate Enum\<bus> key */
	enumerator->bus = bus;
	status = reg_enum_subkeys ( reg_key, enum_bus_id, enumerator );
	if ( ! NT_SUCCESS ( status ) )
		goto err_enum_subkeys;

 err_enum_subkeys:
	reg_close ( reg_key );
 err_reg_open:
	return status;
}

/**
 * Enumerate all devices
 *
 * @ret ntstatus	NT status
 */
NTSTATUS enum_all ( VOID ) {
	struct device_enumerator enumerator;
	HANDLE reg_key;
	NTSTATUS status;

	/* Initialise enumerator */
	RtlZeroMemory ( &enumerator, sizeof ( enumerator ) );

	/* Open Enum key */
	status = reg_open ( &reg_key, key_name_enum, NULL );
	if ( ! NT_SUCCESS ( status ) )
		goto err_reg_open;

	/* Enumerate Enum key */
	status = reg_enum_subkeys ( reg_key, enum_bus, &enumerator );
	if ( ! NT_SUCCESS ( status ) )
		goto err_enum_subkeys;

 err_enum_subkeys:
	reg_close ( reg_key );
 err_reg_open:
	return status;
}
