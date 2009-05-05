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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "registry.h"

#define eprintf(...) fprintf ( stderr, __VA_ARGS__ )
#define array_size(x) ( sizeof ( (x) ) / sizeof ( (x)[0] ) )

#pragma warning(disable: 4702) /* Unreachable code */

/*****************************************************************************
 *
 * Generic routines for registry access
 *
 *****************************************************************************
 */

/**
 * Open registry key
 *
 * @v key		Registry key
 * @v subkey_name	Registry subkey name, or NULL
 * @v subkey		Opened key
 * @ret err		Error status
 */
LONG reg_open ( HKEY key, LPCWSTR subkey_name, PHKEY subkey ) {
	LONG err;

	err = RegOpenKeyExW ( key, subkey_name, 0, ( KEY_READ | KEY_WRITE ),
			      subkey );
	if ( err != ERROR_SUCCESS ) {
		eprintf ( "Could not open \"%S\": %x\n", subkey_name, err );
		return err;
	}

	return ERROR_SUCCESS;
}

/**
 * Close registry key
 *
 * @v key		Registry key
 */
VOID reg_close ( HKEY key ) {
	RegCloseKey ( key );
}

/**
 * Check existence of registry key
 *
 * @v key		Registry key
 * @v subkey_name	Registry subkey name, or NULL
 * @ret err		Error status
 */
LONG reg_key_exists ( HKEY key, LPCWSTR subkey_name ) {
	HKEY subkey;
	LONG err;

	err = reg_open ( key, subkey_name, &subkey );
	if ( err != ERROR_SUCCESS )
		return err;

	reg_close ( subkey );
	return ERROR_SUCCESS;
}

/**
 * Read raw registry value
 *
 * @v key		Registry key
 * @v subkey_name	Registry subkey name, or NULL
 * @v value_name	Registry value name
 * @v buffer		Buffer to allocate and fill in
 * @v len		Length of buffer to fill in
 * @ret err		Error status
 *
 * The caller must free() the returned value.
 */
LONG reg_query_value ( HKEY key, LPCWSTR subkey_name, LPCWSTR value_name,
		       LPBYTE *buffer, LPDWORD len ) {
	HKEY subkey;
	LONG err;

	/* Open subkey */
	err = reg_open ( key, subkey_name, &subkey );
	if ( err != ERROR_SUCCESS )
		goto err_open_subkey;

	/* Determine length */
	err = RegQueryValueExW ( subkey, value_name, NULL, NULL, NULL, len );
	if ( err != ERROR_SUCCESS ) {
		if ( err != ERROR_FILE_NOT_FOUND ) {
			eprintf ( "Could not determine length of \"%S\": %x\n",
				  value_name, err );
		}
		goto err_get_length;
	}

	/* Allocate buffer for string + extra NUL (for safety) */
	*buffer = malloc ( *len );
	if ( ! *buffer ) {
		eprintf ( "Could not allocate buffer for \"%S\": %x\n",
			  value_name, err );
		err = ERROR_NOT_ENOUGH_MEMORY;
		goto err_malloc;
	}
	memset ( *buffer, 0, *len );

	/* Read data */
	err = RegQueryValueExW ( subkey, value_name, NULL, NULL,
				 *buffer, len );
	if ( err != ERROR_SUCCESS ) {
		eprintf ( "Could not read data for \"%S\": %x\n",
			  value_name, err );
		goto err_read_data;
	}

	reg_close ( subkey );
	return ERROR_SUCCESS;

 err_read_data:
	free ( *buffer );
	*buffer = NULL;
 err_malloc:
 err_get_length:
	reg_close ( subkey );
 err_open_subkey:
	return err;
}

/**
 * Write raw registry value
 *
 * @v key		Registry key
 * @v subkey_name	Registry subkey name, or NULL
 * @v value_name	Registry value name
 * @v data		Data to store
 * @v len		Length of data
 * @ret err		Error status
 */
LONG reg_set_value ( HKEY key, LPCWSTR subkey_name, LPCWSTR value_name,
		     DWORD type, LPBYTE data, DWORD len ) {
	HKEY subkey;
	LONG err;

	/* Open subkey */
	err = reg_open ( key, subkey_name, &subkey );
	if ( err != ERROR_SUCCESS )
		goto err_open_subkey;

	/* Store value */
	err = RegSetValueExW ( subkey, value_name, 0, type, data, len );
	if ( err != ERROR_SUCCESS ) {
		eprintf ( "Could not write data for \"%S\": %x\n",
			  value_name, err );
		goto err_write_data;
	}

	/* Success */
	err = 0;

 err_write_data:
	reg_close ( subkey );
 err_open_subkey:
	return err;
}

/**
 * Check existence of registry value
 *
 * @v key		Registry key
 * @v subkey_name	Registry subkey name, or NULL
 * @v value_name	Registry value name
 * @ret err		Error status
 */
LONG reg_value_exists ( HKEY key, LPCWSTR subkey_name, LPCWSTR value_name ) {
	LPBYTE buffer;
	DWORD len;
	LONG err;

	err = reg_query_value ( key, subkey_name, value_name, &buffer, &len );
	if ( err != ERROR_SUCCESS )
		return err;
	free ( buffer );
	return ERROR_SUCCESS;
}

/**
 * Read REG_SZ value
 *
 * @v key		Registry key
 * @v subkey_name	Registry subkey name, or NULL
 * @v value_name	Registry value name
 * @v sz		String to allocate and fill in
 * @ret err		Error status
 *
 * The caller must free() the returned value "sz".
 */
LONG reg_query_sz ( HKEY key, LPCWSTR subkey_name, LPCWSTR value_name,
		    LPWSTR *sz ) {
	LPBYTE buffer;
	DWORD len;
	DWORD sz_len;
	LONG err;

	/* Read raw data */
	err = reg_query_value ( key, subkey_name, value_name, &buffer, &len );
	if ( err != ERROR_SUCCESS )
		goto err_query_raw;

	/* Allocate buffer for string + extra NUL (for safety) */
	sz_len = ( len + sizeof ( sz[0] ) );
	*sz = malloc ( sz_len );
	if ( ! *sz ) {
		eprintf ( "Could not allocate string for \"%S\": %x\n",
			  value_name, err );
		err = ERROR_NOT_ENOUGH_MEMORY;
		goto err_malloc;
	}
	memset ( *sz, 0, sz_len );

	/* Copy string data */
	memcpy ( *sz, buffer, len );

	/* Success */
	free ( buffer );
	return ERROR_SUCCESS;

	free ( *sz );
	*sz = NULL;
 err_malloc:
	free ( buffer );
 err_query_raw:
	return err;
}

/**
 * Read REG_MULTI_SZ value
 *
 * @v key		Registry key
 * @v subkey_name	Registry subkey name, or NULL
 * @v value_name	Registry value name
 * @v multi_sz		String array to allocate and fill in
 * @ret err		Error status
 *
 * The caller must free() the returned value "multi_sz".
 */
LONG reg_query_multi_sz ( HKEY key, LPCWSTR subkey_name, LPCWSTR value_name,
			  LPWSTR **multi_sz ) {
	LPBYTE buffer;
	DWORD len;
	DWORD num_szs;
	LPWSTR sz;
	DWORD multi_sz_len;
	DWORD i;
	LONG err;

	/* Read raw data */
	err = reg_query_value ( key, subkey_name, value_name, &buffer, &len );
	if ( err != ERROR_SUCCESS )
		goto err_query_raw;

	/* Count number of strings in the array.  This is a
	 * potential(ly harmless) overestimate.
	 */
	num_szs = 0;
	for ( sz = ( ( LPWSTR ) buffer ) ;
	      sz < ( ( LPWSTR ) ( buffer + len ) ) ; sz++ ) {
		if ( ! *sz )
			num_szs++;
	}
	
	/* Allocate and populate string array */
	multi_sz_len = ( ( ( num_szs + 1 ) * sizeof ( multi_sz[0] ) ) +
			 len + sizeof ( multi_sz[0][0] ) );
	*multi_sz = malloc ( multi_sz_len );
	if ( ! *multi_sz ) {
		eprintf ( "Could not allocate string array for \"%S\"\n",
			   value_name );
		err = ERROR_NOT_ENOUGH_MEMORY;
		goto err_malloc;
	}
	memset ( *multi_sz, 0, multi_sz_len );
	sz = ( ( LPWSTR ) ( *multi_sz + num_szs + 1 ) );
	memcpy ( sz, buffer, len );
	for ( i = 0 ; i < num_szs ; i++ ) {
		if ( ! *sz )
			break;
		(*multi_sz)[i] = sz;
		while ( *sz )
			sz++;
		sz++;
	}

	/* Success */
	free ( buffer );
	return ERROR_SUCCESS;

	free ( *multi_sz );
	*multi_sz = NULL;
 err_malloc:
	free ( buffer );
 err_query_raw:
	return err;
}

/**
 * Write REG_MULTI_SZ value
 *
 * @v key		Registry key
 * @v subkey_name	Registry subkey name, or NULL
 * @v value_name	Registry value name
 * @v multi_sz		String array to allocate and fill in
 * @ret err		Error status
 *
 * The caller must free() the returned value "multi_sz".
 */
LONG reg_set_multi_sz ( HKEY key, LPCWSTR subkey_name, LPCWSTR value_name,
			LPWSTR *multi_sz ) {
	LPWSTR *sz;
	SIZE_T sz_len;
	SIZE_T len;
	LPBYTE buffer;
	SIZE_T used;
	LONG err;

	/* Calculate total length and allocate block */
	len = sizeof ( sz[0][0] ); /* List-terminating NUL */
	for ( sz = multi_sz ; *sz ; sz++ ) {
		sz_len = ( ( wcslen ( *sz ) + 1 ) * sizeof ( sz[0][0] ) );
		len += sz_len;
	}
	buffer = malloc ( len );
	if ( ! buffer ) {
		eprintf ( "Could not allocate string array for \"%S\"\n",
			  value_name );
		err = ERROR_NOT_ENOUGH_MEMORY;
		goto err_malloc;
	}

	/* Populate block */
	memset ( buffer, 0, len );
	used = 0;
	for ( sz = multi_sz ; *sz ; sz++ ) {
		sz_len = ( ( wcslen ( *sz ) + 1 ) * sizeof ( sz[0][0] ) );
		memcpy ( ( buffer + used ), *sz, sz_len );
		used += sz_len;
	}

	/* Write block to registry */
	err = reg_set_value ( key, subkey_name, value_name, REG_MULTI_SZ,
			      buffer, ( ( DWORD ) len ) );
	if ( err != ERROR_SUCCESS )
		goto err_set_value;

	/* Success */
	err = ERROR_SUCCESS;

 err_set_value:
	free ( buffer );
 err_malloc:
	return err;
}

/**
 * Read REG_DWORD value
 *
 * @v key		Registry key
 * @v subkey_name	Registry subkey name, or NULL
 * @v value_name	Registry value name
 * @v dword		Dword to fill in
 * @ret err		Error status
 */
LONG reg_query_dword ( HKEY key, LPCWSTR subkey_name, LPCWSTR value_name,
		       PDWORD dword ) {
	LPBYTE buffer;
	DWORD len;
	LONG err;

	/* Read raw data */
	err = reg_query_value ( key, subkey_name, value_name, &buffer, &len );
	if ( err != ERROR_SUCCESS )
		goto err_query_raw;

	/* Sanity check */
	if ( len != sizeof ( *dword ) ) {
		eprintf ( "Bad size for dword \"%S\"\n", value_name );
		goto err_bad_size;
	}

	/* Copy dword data */
	memcpy ( dword, buffer, sizeof ( *dword ) );

	/* Success */
	err = ERROR_SUCCESS;

 err_bad_size:
	free ( buffer );
 err_query_raw:
	return err;
}

/**
 * Write REG_DWORD value
 *
 * @v key		Registry key
 * @v subkey_name	Registry subkey name, or NULL
 * @v value_name	Registry value name
 * @v dword		Dword
 * @ret err		Error status
 */
LONG reg_set_dword ( HKEY key, LPCWSTR subkey_name, LPCWSTR value_name,
		     DWORD dword ) {
	return reg_set_value ( key, subkey_name, value_name, REG_DWORD,
			       ( ( LPBYTE ) &dword ), sizeof ( dword ) );
}
