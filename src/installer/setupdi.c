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
#include <setupapi.h>
#include <cfgmgr32.h>
#include <objbase.h>
#include <newdev.h>
#include "setupdi.h"

#define eprintf(...) fprintf ( stderr, __VA_ARGS__ )
#define array_size(x) ( sizeof ( (x) ) / sizeof ( (x)[0] ) )

/*****************************************************************************
 *
 * Generic routines for installing non-PnP drivers
 *
 *****************************************************************************
 */

/**
 * Count device nodes
 *
 * @v inf_path		Path to .INF file
 * @v hw_id		Hardware ID
 * @v count		Number of device nodes
 * @ret rc		Return status code
 */
static int count_device_nodes ( LPWSTR inf_path, LPWSTR hw_id, DWORD *count ) {
	GUID class_guid;
	WCHAR class_name[MAX_CLASS_NAME_LEN];
	HDEVINFO dev_info_list;
	DWORD dev_index;
	SP_DEVINFO_DATA dev_info;
	WCHAR hw_ids[256];
	DWORD hw_ids_len;
	DWORD rc;

	/* Initialise count */
	*count = 0;

	/* Get class GUID and name */
	if ( ! SetupDiGetINFClassW ( inf_path, &class_guid, class_name,
				     array_size ( class_name ), NULL ) ) {
		rc = GetLastError();
		eprintf ( "Could not fetch class GUID from %S: %x\n",
			  inf_path, rc );
		goto err_setupdigetinfclass;
	}

	/* Get device list */
	dev_info_list = SetupDiGetClassDevsW ( &class_guid, NULL, NULL, 0 );
	if ( dev_info_list == INVALID_HANDLE_VALUE ) {
		rc = GetLastError();
		eprintf ( "Could not get device list: %x\n", rc );
		goto err_setupdigetclassdevs;
	}

	/* Enumerate devices */
	for ( dev_index = 0 ; ; dev_index++ ) {
		dev_info.cbSize = sizeof ( dev_info );
		if ( ! SetupDiEnumDeviceInfo ( dev_info_list, dev_index,
					       &dev_info ) ) {
			rc = GetLastError();
			if ( rc == ERROR_NO_MORE_ITEMS )
				break;
			eprintf ( "Could not enumerate devices: %x\n", rc );
			goto err_setupdienumdeviceinfo;
		}

		/* Fetch hardare IDs */
		if ( ! SetupDiGetDeviceRegistryPropertyW ( dev_info_list,
							   &dev_info,
							   SPDRP_HARDWAREID,
							   NULL,
							   ( (PBYTE) hw_ids ),
							   sizeof ( hw_ids ),
							   &hw_ids_len ) ){
			rc = GetLastError();
			eprintf ( "Could not get hardware ID: %x\n", rc );
			goto err_setupdigetdeviceregistryproperty;
		}

		/* Compare hardware IDs */
		if ( _wcsicmp ( hw_id, hw_ids ) == 0 ) {
			/* Increment count of matching devices */
			(*count)++;
		}
	}

	/* Success */
	rc = 0;

 err_setupdigetdeviceregistryproperty:
 err_setupdienumdeviceinfo:
	SetupDiDestroyDeviceInfoList ( dev_info_list );
 err_setupdigetclassdevs:
 err_setupdigetinfclass:
	return rc;
}

/**
 * Install a device node
 *
 * @v inf_path		Path to .INF file
 * @v hw_id		Hardware ID
 * @ret rc		Return status code
 */
static int install_device_node ( LPWSTR inf_path, LPWSTR hw_id ) {
	GUID class_guid;
	WCHAR class_name[MAX_CLASS_NAME_LEN];
	HDEVINFO dev_info_list;
	SP_DEVINFO_DATA dev_info;
	WCHAR dev_instance[256];
	WCHAR hw_ids[256];
	DWORD hw_ids_len;
	DWORD rc;

	printf ( "Installing device node for hardware ID \"%S\"\n", hw_id );

	/* Get class GUID and name */
	if ( ! SetupDiGetINFClassW ( inf_path, &class_guid, class_name,
				     array_size ( class_name ), NULL ) ) {
		rc = GetLastError();
		eprintf ( "Could not fetch class GUID from %S: %x\n",
			  inf_path, rc );
		goto err_setupdigetinfclass;
	}

	/* Create empty device information list */
	dev_info_list = SetupDiCreateDeviceInfoList ( &class_guid, 0 );
	if ( dev_info_list == INVALID_HANDLE_VALUE ) {
		rc = GetLastError();
		eprintf ( "Could not create device info list: %x\n", rc );
		goto err_setupdicreatedeviceinfolist;
	}

	/* Create device information element */
	dev_info.cbSize = sizeof ( dev_info );
	if ( ! SetupDiCreateDeviceInfoW ( dev_info_list, class_name,
					  &class_guid, NULL, 0,
					  DICD_GENERATE_ID, &dev_info ) ) {
		rc = GetLastError();
		eprintf ( "Could not create device info: %x\n", rc );
		goto err_setupdicreatedeviceinfo;
	}

	/* Fetch device instance ID */
	if ( ! SetupDiGetDeviceInstanceIdW ( dev_info_list, &dev_info,
					     dev_instance,
					     array_size ( dev_instance ),
					     NULL ) ) {
		rc = GetLastError();
		eprintf ( "Could not get device instance ID: %x\n", rc );
		goto err_setupdigetdeviceinstanceid;
	}
	printf ( "Device instance ID is \"%S\"\n", dev_instance );

	/* Add the hardware ID */
	hw_ids_len = _snwprintf_s ( hw_ids, array_size ( hw_ids ), _TRUNCATE,
				    L"%s%c", hw_id, 0 );
	if ( ! SetupDiSetDeviceRegistryPropertyW ( dev_info_list, &dev_info,
						   SPDRP_HARDWAREID,
						   ( ( LPBYTE ) hw_ids ),
						   ( ( hw_ids_len + 1 ) *
						     sizeof ( hw_ids[0] ) ) )){
		rc = GetLastError();
		eprintf ( "Could not set hardware ID: %x\n", rc );
		goto err_setupdisetdeviceregistryproperty;
	}

	/* Install the device node */
	if ( ! SetupDiCallClassInstaller ( DIF_REGISTERDEVICE, dev_info_list,
					   &dev_info ) ) {
		rc = GetLastError();
		eprintf ( "Could not install device node: %x\n", rc );
		goto err_setupdicallclassinstaller;
	}

	/* Success */
	rc = 0;

 err_setupdicallclassinstaller:
 err_setupdisetdeviceregistryproperty:
 err_setupdigetdeviceinstanceid:
 err_setupdicreatedeviceinfo:
	SetupDiDestroyDeviceInfoList ( dev_info_list );
 err_setupdicreatedeviceinfolist:
 err_setupdigetinfclass:
	return rc;
}

/**
 * Update driver
 *
 * @v inf_path		Path to .INF file
 * @v hw_id		Hardware ID
 * @ret rc		Return status code
 */
static int update_driver ( LPWSTR inf_path, LPWSTR hw_id ) {
	BOOL reboot_required;
	int rc;

	printf ( "Updating driver for hardware ID \"%S\"\n", hw_id );

	/* Update driver */
	if ( ! UpdateDriverForPlugAndPlayDevicesW ( NULL, hw_id, inf_path,
						    0, &reboot_required ) ) {
		rc = GetLastError();
		eprintf ( "Could not update driver: %x\n", rc );
		goto err_updatedriverforplugandplaydevices;
	}

	/* Success */
	rc = 0;

 err_updatedriverforplugandplaydevices:
	return rc;
}

/**
 * Install (or update) driver
 *
 * @v inf_path		Path to .INF file
 * @v hw_id		Hardware ID
 * @ret rc		Return status code
 */
int install_or_update_driver ( LPWSTR inf_path, LPWSTR hw_id ) {
	DWORD existing_devices;
	int rc;

	/* See if device node exists */
	if ( ( rc = count_device_nodes ( inf_path, hw_id,
					 &existing_devices ) ) != 0 )
		return rc;

	/* Install device node (if necessary) */
	if ( ( existing_devices == 0 ) &&
	     ( ( rc = install_device_node ( inf_path, hw_id ) ) != 0 ) )
		return rc;
	     
	/* Update driver */
	if ( ( rc = update_driver ( inf_path, hw_id ) ) != 0 )
		return rc;

	return 0;
}
