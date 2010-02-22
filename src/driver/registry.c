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
#include <ntstrsafe.h>
#include "sanbootconf.h"
#include "registry.h"

/**
 * Open registry key
 *
 * @v reg_key_name	Registry key name
 * @v reg_key		Registry key to fill in
 * @ret ntstatus	NT status
 */
NTSTATUS reg_open ( LPCWSTR reg_key_name, PHANDLE reg_key ) {
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
VOID reg_close ( HANDLE reg_key ) {
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
NTSTATUS fetch_reg_kvi ( HANDLE reg_key, LPCWSTR value_name,
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
				       SANBOOTCONF_POOL_TAG );
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
	ExFreePool ( *kvi );
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
NTSTATUS fetch_reg_sz ( HANDLE reg_key, LPCWSTR value_name, LPWSTR *value ) {
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
					 SANBOOTCONF_POOL_TAG );
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
NTSTATUS fetch_reg_multi_sz ( HANDLE reg_key, LPCWSTR value_name,
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
					  SANBOOTCONF_POOL_TAG );
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
NTSTATUS reg_store_sz ( HANDLE reg_key, LPCWSTR value_name, LPWSTR value ) {
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
 * Store registry multiple-string value
 *
 * @v reg_key		Registry key
 * @v value_name	Registry value name
 * @v ...		String values to store (NULL terminated)
 * @ret ntstatus	NT status
 */
NTSTATUS reg_store_multi_sz ( HANDLE reg_key, LPCWSTR value_name, ... ) {
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
					 SANBOOTCONF_POOL_TAG );
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
NTSTATUS reg_store_dword ( HANDLE reg_key, LPCWSTR value_name, ULONG value ) {
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
