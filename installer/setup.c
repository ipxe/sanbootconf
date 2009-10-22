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
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include "setupdi.h"
#include "registry.h"

#define eprintf(...) fprintf ( stderr, __VA_ARGS__ )
#define array_size(x) ( sizeof ( (x) ) / sizeof ( (x)[0] ) )

#pragma warning(disable: 4702) /* Unreachable code */

/**
 * Find service group
 *
 * @v sgo		Service group order array
 * @v group_name	Service group name
 * @ret entry		Entry within service group order array, or NULL
 */
static LPWSTR * find_service_group ( LPWSTR *sgo, LPCWSTR group_name ) {
	LPWSTR *tmp;

	for ( tmp = sgo ; *tmp ; tmp++ ) {
		if ( _wcsicmp ( *tmp, group_name ) == 0 )
			return tmp;
	}
	eprintf ( "Cannot find service group \"%S\"\n", group_name );
	return NULL;
}

/**
 * Move service group
 *
 * @v sgo		Service group order array
 * @v group_name	Service group name to move
 * @v before_name	Service group name to move before
 * @ret err		Error status
 */
static LONG move_service_group ( LPWSTR *sgo, LPCWSTR group_name,
				 LPCWSTR before_name ) {
	LPWSTR *group;
	LPWSTR *before;
	LPWSTR tmp;

	group = find_service_group ( sgo, group_name );
	if ( ! group )
		return ERROR_FILE_NOT_FOUND;
	before = find_service_group ( sgo, before_name );
	if ( ! before )
		return ERROR_FILE_NOT_FOUND;

	while ( group > before ) {
		tmp = *group;
		*group = *( group - 1 );
		group--;
		*group = tmp;
	}

	return ERROR_SUCCESS;
}

/**
 * Fix service group order
 *
 * @ret err		Error status
 */
static LONG fix_service_group_order ( void ) {
	WCHAR sgo_key_name[] =
		L"SYSTEM\\CurrentControlSet\\Control\\ServiceGroupOrder";
	WCHAR sgo_value_name[] = L"List";
	WCHAR sgo_backup_value_name[] = L"List.pre-sanbootconf";
	LPWSTR *sgo;
	LONG err;

	/* Read existing service group order */
	err = reg_query_multi_sz ( HKEY_LOCAL_MACHINE, sgo_key_name,
				   sgo_value_name, &sgo );
	if ( err != ERROR_SUCCESS ) {
		eprintf ( "Cannot read service group order: %x\n", err );
		goto err_query_sgo;
	}

	/* Back up key (if no backup already exists) */
	err = reg_value_exists ( HKEY_LOCAL_MACHINE, sgo_key_name,
				 sgo_backup_value_name );
	if ( err == ERROR_FILE_NOT_FOUND ) {
		err = reg_set_multi_sz ( HKEY_LOCAL_MACHINE, sgo_key_name,
					 sgo_backup_value_name, sgo );
		if ( err != ERROR_SUCCESS ) {
			eprintf ( "Could not back up service group order: "
				  "%x\n", err );
			goto err_set_sgo_backup;
		}
	}
	if ( err != ERROR_SUCCESS ) {
		eprintf ( "Cannot check service group order backup: %x\n",
			  err );
		goto err_exists_sgo_backup;
	}

	/* Move service groups to required places */
	err = move_service_group ( sgo, L"PNP_TDI", L"SCSI miniport" );
	if ( err != ERROR_SUCCESS )
		return err;
	err = move_service_group ( sgo, L"Base", L"PNP_TDI" );
	if ( err != ERROR_SUCCESS )
		return err;
	err = move_service_group ( sgo, L"NDIS", L"Base" );
	if ( err != ERROR_SUCCESS )
		return err;
	err = move_service_group ( sgo, L"NDIS Wrapper", L"NDIS" );
	if ( err != ERROR_SUCCESS )
		return err;

	/* Write out modified service group order */
	err = reg_set_multi_sz ( HKEY_LOCAL_MACHINE, sgo_key_name,
				 sgo_value_name, sgo );
	if ( err != ERROR_SUCCESS ) {
		eprintf ( "Could not update service group order: %x\n", err );
		goto err_set_sgo;
	}

	/* Success */
	err = ERROR_SUCCESS;

 err_set_sgo:
 err_exists_sgo_backup:
 err_set_sgo_backup:
	free ( sgo );
 err_query_sgo:
	return err;
}

/**
 * Check to see if a service exists
 *
 * @v service_name	Service name
 * @ret err		Error status
 */
static LONG service_exists ( LPWSTR service_name ) {
	WCHAR services_key_name[] =
		L"SYSTEM\\CurrentControlSet\\Services";
	HKEY services;
	LONG err;

	/* Open Services key */
	err = reg_open ( HKEY_LOCAL_MACHINE, services_key_name, &services );
	if ( err != ERROR_SUCCESS )
		goto err_reg_open;

	/* Check service key existence */
	err = reg_key_exists ( services, service_name );

	reg_close ( services );
 err_reg_open:
	return err;
}

/**
 * Set service to start at boot time
 *
 * @v service_name	Service name
 * @ret err		Error status
 */
static LONG set_boot_start ( LPWSTR service_name ) {
	WCHAR services_key_name[] =
		L"SYSTEM\\CurrentControlSet\\Services";
	HKEY services;
	LPWSTR *depend_on;
	LPWSTR *tmp;
	LONG err;

	printf ( "Marking \"%S\" service as boot-start\n", service_name );

	/* Open Services key */
	err = reg_open ( HKEY_LOCAL_MACHINE, services_key_name, &services );
	if ( err != ERROR_SUCCESS )
		goto err_reg_open;

	/* Look up service dependencies, if any */
	err = reg_query_multi_sz ( services, service_name, L"DependOnService",
				   &depend_on );
	if ( err == ERROR_SUCCESS ) {
		/* Recurse into services that we depend on */
		for ( tmp = depend_on ; *tmp ; tmp++ ) {
			printf ( "...\"%S\" depends on \"%S\"\n",
				 service_name, *tmp );
			err = set_boot_start ( *tmp );
			if ( err != ERROR_SUCCESS ) {
				free ( depend_on );
				return err;
			}
		}
		free ( depend_on );
	}

	/* Set Start=0 for the service in question */
	err = reg_set_dword ( services, service_name, L"Start", 0 );
	if ( err != ERROR_SUCCESS ) {
		eprintf ( "Could not mark \"%S\" service as boot-start: %x\n",
			  service_name, err );
		goto err_set_dword;
	}

	/* Success */
	err = ERROR_SUCCESS;

 err_set_dword:
 err_reg_open:
	reg_close ( services );
	return err;
}

/**
 * Set all NIC services to start at boot time
 *
 * @ret err		Error status
 */
static LONG set_boot_start_nics ( void ) {
	HDEVINFO dev_info_list;
	DWORD dev_index;
	SP_DEVINFO_DATA dev_info;
	WCHAR name[256];
	WCHAR service[256];
	ULONG status;
	ULONG problem;
	LONG err;

	/* Get NIC list */
	dev_info_list = SetupDiGetClassDevs ( &GUID_DEVCLASS_NET, NULL,
					      NULL, DIGCF_PRESENT );
	if ( dev_info_list == INVALID_HANDLE_VALUE ) {
		err = GetLastError();
		eprintf ( "Could not get NIC list: %x\n", err );
		goto err_setupdigetclassdevs;
	}

	/* Enumerate NICs */
	for ( dev_index = 0 ; ; dev_index++ ) {
		dev_info.cbSize = sizeof ( dev_info );
		if ( ! SetupDiEnumDeviceInfo ( dev_info_list, dev_index,
					       &dev_info ) ) {
			err = GetLastError();
			if ( err == ERROR_NO_MORE_ITEMS )
				break;
			eprintf ( "Could not enumerate devices: %x\n", err );
			goto err_setupdienumdeviceinfo;
		}

		/* Fetch a name for the device, if available */
		if ( ! ( SetupDiGetDeviceRegistryPropertyW( dev_info_list,
							    &dev_info,
							    SPDRP_FRIENDLYNAME,
							    NULL,
							    ( (PBYTE) name ),
							    sizeof ( name ),
							    NULL ) ||
			 SetupDiGetDeviceRegistryPropertyW( dev_info_list,
							    &dev_info,
							    SPDRP_DEVICEDESC,
							    NULL,
							    ( (PBYTE) name ),
							    sizeof ( name ),
							    NULL ) ) ) {
			err = GetLastError();
			eprintf ( "Could not get device name: %x\n", err );
			goto err_setupdigetdeviceregistryproperty;
		}

		/* Fetch service name */
		if ( ! SetupDiGetDeviceRegistryPropertyW ( dev_info_list,
							   &dev_info,
							   SPDRP_SERVICE,
							   NULL,
							   ( (PBYTE) service ),
							   sizeof ( service ),
							   NULL ) ) {
			err = GetLastError();
			eprintf ( "Could not get service name: %x\n", err );
			goto err_setupdigetdeviceregistryproperty;
		}

		/* See if this is a hidden device */
		err = CM_Get_DevNode_Status ( &status, &problem,
					      dev_info.DevInst, 0 );
		if ( err != CR_SUCCESS ) {
			eprintf ( "Could not get device status: %x\n", err );
			goto err_cm_get_devnode_status;
		}

		/* Skip if this is a hidden device.  This is something
		 * of a heuristic.  It's a fairly safe bet that
		 * anything showing up as non-hidden is a real NIC;
		 * the remainder tend to be "WAN Miniport" devices et
		 * al., which will crash if set as boot-start.
		 */
		if ( status & DN_NO_SHOW_IN_DM )
			continue;

		printf ( "Found NIC \"%S\"\n", name );

		/* Mark NIC service as boot-start */
		err = set_boot_start ( service );
		if ( err != ERROR_SUCCESS )
			goto err_set_boot_start;
	}

	/* Success */
	err = 0;

 err_set_boot_start:
 err_cm_get_devnode_status:
 err_setupdigetdeviceregistryproperty:
 err_setupdienumdeviceinfo:
	SetupDiDestroyDeviceInfoList ( dev_info_list );
 err_setupdigetclassdevs:
	return err;
}

/**
 * Fix up various registry settings
 *
 * @ret err		Error status
 */
static LONG fix_registry ( void ) {
	WCHAR ddms_key_name[] =
		L"SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters";
	WCHAR dpe_key_name[] =
		L"SYSTEM\\CurrentControlSet\\Control\\Session Manager"
		L"\\Memory Management";
	LONG err;

	/* Disable DHCP media sensing */
	err = reg_set_dword ( HKEY_LOCAL_MACHINE, ddms_key_name,
			      L"DisableDhcpMediaSense", 1 );
	if ( err != ERROR_SUCCESS ) {
		eprintf ( "Could not disable DHCP media sensing: %x\n", err );
		return err;
	}

	/* Disable kernel paging */
	err = reg_set_dword ( HKEY_LOCAL_MACHINE, dpe_key_name,
			      L"DisablePagingExecutive", 1 );
	if ( err != ERROR_SUCCESS ) {
		eprintf ( "Could not disable kernel paging: %x\n", err );
		return err;
	}

	/* Update ServiceGroupOrder */
	if ( ( err = fix_service_group_order() ) != ERROR_SUCCESS )
		return err;

	/* Set relevant services to be boot-start */
	if ( ( err = set_boot_start ( L"NDIS" ) ) != ERROR_SUCCESS )
		return err;
	if ( ( err = set_boot_start ( L"Tcpip" ) ) != ERROR_SUCCESS )
		return err;
	if ( ( err = set_boot_start ( L"iScsiPrt" ) ) != ERROR_SUCCESS )
		return err;
	/* PSched service is not present on Win2k3 */
	if ( ( ( err = service_exists ( L"PSched" ) ) == ERROR_SUCCESS ) &&
	     ( ( err = set_boot_start ( L"PSched" ) ) != ERROR_SUCCESS ) )
		return err;
	if ( ( err = set_boot_start_nics() ) != ERROR_SUCCESS )
		return err;

	return ERROR_SUCCESS;
}

/**
 * Main entry point
 *
 * @v argc		Number of arguments
 * @v argv		Argument list
 * @ret exit		Exit status
 */
int __cdecl main ( int argc, char **argv ) {
	CHAR bin_path[MAX_PATH];
	CHAR inf_rel_path[MAX_PATH];
	CHAR inf_path[MAX_PATH];
	CHAR *file_part;
	WCHAR inf_path_w[MAX_PATH];
	WCHAR hw_id[] = L"ROOT\\sanbootconf";
	DWORD len;
	CHAR key;

	printf ( "SAN Boot Configuration Driver Installation\n" );
	printf ( "==========================================\n\n" );

	/* Check for iSCSI initiator existence */
	if ( service_exists ( L"iScsiPrt" ) != 0 ) {
		eprintf ( "\nYou must install the Microsoft iSCSI initiator "
			  "before installing this\nsoftware!\n" );
		goto fail;
	}

	/* Fix up registry before installing driver */
	if ( fix_registry() != 0 )
		goto fail;

	/* Locate .inf file */
	len = GetFullPathName ( argv[0], array_size ( bin_path ), bin_path,
				&file_part );
	if ( ( len == 0 ) || ( len >= array_size ( bin_path ) ) )
		goto fail;
	if ( file_part )
		*file_part = 0;
	_snprintf ( inf_rel_path, sizeof ( inf_rel_path ),
		    "%s\\..\\sanbootconf.inf", bin_path );
	len = GetFullPathName ( inf_rel_path, array_size ( inf_path ),
				inf_path, NULL );
	if ( ( len == 0 ) || ( len >= array_size ( inf_path ) ) )
		goto fail;
	printf ( "Installing from \"%s\"\n", inf_path );

	/* Install/update driver */
	_snwprintf ( inf_path_w, array_size ( inf_path_w ), L"%S", inf_path );
	if ( install_or_update_driver ( inf_path_w, hw_id ) != 0 )
		goto fail;

	/* Success */
	printf ( "SAN Boot Configuration Driver installed successfully\n" );

	( VOID ) argc;
	exit ( EXIT_SUCCESS );

 fail:
	eprintf ( "\n*** INSTALLATION FAILED ***\n" );
	eprintf ( "Press Enter to continue\n" );
	scanf ( "%c", &key );
	exit ( EXIT_FAILURE );
}
