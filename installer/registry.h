#ifndef REGISTRY_H
#define REGSITRY_H

#include <windows.h>

extern LONG reg_open ( HKEY key, LPCWSTR subkey_name, PHKEY subkey );
extern VOID reg_close ( HKEY key );
extern LONG reg_key_exists ( HKEY key, LPCWSTR subkey_name );
extern LONG reg_query_value ( HKEY key, LPCWSTR subkey_name,
			      LPCWSTR value_name, LPBYTE *buffer,
			      LPDWORD len );
extern LONG reg_set_value ( HKEY key, LPCWSTR subkey_name, LPCWSTR value_name,
			    DWORD type, LPBYTE data, DWORD len );
extern LONG reg_value_exists ( HKEY key, LPCWSTR subkey_name,
			       LPCWSTR value_name );
extern LONG reg_query_sz ( HKEY key, LPCWSTR subkey_name, LPCWSTR value_name,
			   LPWSTR *sz );
extern LONG reg_set_sz ( HKEY key, LPCWSTR subkey_name, LPCWSTR value_name,
			 LPWSTR sz );
extern LONG reg_query_multi_sz ( HKEY key, LPCWSTR subkey_name,
				 LPCWSTR value_name, LPWSTR **multi_sz );
extern LONG reg_set_multi_sz ( HKEY key, LPCWSTR subkey_name,
			       LPCWSTR value_name, LPWSTR *multi_sz );
extern LONG reg_query_dword ( HKEY key, LPCWSTR subkey_name,
			      LPCWSTR value_name, PDWORD dword );
extern LONG reg_set_dword ( HKEY key, LPCWSTR subkey_name, LPCWSTR value_name,
			    DWORD dword );

#endif /* REGISTRY_H */
