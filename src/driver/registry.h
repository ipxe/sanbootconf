#ifndef _REGISTRY_H
#define _REGISTRY_H

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

extern NTSTATUS reg_open ( PHANDLE reg_key, ... );
extern VOID reg_close ( HANDLE reg_key );
extern NTSTATUS reg_fetch_kvi ( HANDLE reg_key, LPCWSTR value_name,
				PKEY_VALUE_PARTIAL_INFORMATION *kvi );
extern NTSTATUS reg_fetch_sz ( HANDLE reg_key, LPCWSTR value_name,
			       LPWSTR *value );
extern NTSTATUS reg_fetch_multi_sz ( HANDLE reg_key, LPCWSTR value_name,
				     LPWSTR **values );
extern NTSTATUS reg_store_sz ( HANDLE reg_key, LPCWSTR value_name,
			       LPWSTR value );
extern NTSTATUS reg_store_multi_sz ( HANDLE reg_key, LPCWSTR value_name, ... );
extern NTSTATUS reg_store_dword ( HANDLE reg_key, LPCWSTR value_name,
				  ULONG value );

#endif /* _REGISTRY_H */
