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
#include <stdarg.h>
#include "sanbootconf.h"
#include "boottext.h"

/* Maximum length of string to print */
#define BOOT_TEXT_MAX_LEN 128

/* Boot text colour */
#define BOOT_TEXT_COLOUR 15 /* White */

/* Boot text area left-hand edge */
#define BOOT_TEXT_AREA_LEFT 8

/* Boot text area right-hand edge */
#define BOOT_TEXT_AREA_RIGHT 631

/* Boot text area top edge */
#define BOOT_TEXT_AREA_TOP 14

/* Boot text area bottom edge */
#define BOOT_TEXT_AREA_BOTTOM 111

/* Definitions taken from ReactOS' inbvfuncs.h */
typedef BOOLEAN ( NTAPI * INBV_RESET_DISPLAY_PARAMETERS ) ( ULONG Cols,
							    ULONG Rows );
typedef VOID ( NTAPI * INBV_DISPLAY_STRING_FILTER ) ( PCHAR *Str );
VOID NTAPI InbvAcquireDisplayOwnership ( VOID );
BOOLEAN NTAPI InbvCheckDisplayOwnership ( VOID );
VOID NTAPI InbvNotifyDisplayOwnershipLost ( IN INBV_RESET_DISPLAY_PARAMETERS
					    Callback );
VOID NTAPI InbvEnableBootDriver ( IN BOOLEAN Enable );
VOID NTAPI InbvInstallDisplayStringFilter ( IN INBV_DISPLAY_STRING_FILTER
					    DisplayFilter );
BOOLEAN NTAPI InbvIsBootDriverInstalled ( VOID );
BOOLEAN NTAPI InbvDisplayString ( IN PCHAR String );
BOOLEAN NTAPI InbvEnableDisplayString ( IN BOOLEAN Enable );
BOOLEAN NTAPI InbvResetDisplay ( VOID );
VOID NTAPI InbvSetScrollRegion ( IN ULONG Left, IN ULONG Top, IN ULONG Width,
				 IN ULONG Height );
VOID NTAPI InbvSetTextColor ( IN ULONG Color );
VOID NTAPI InbvSolidColorFill ( IN ULONG Left, IN ULONG Top, IN ULONG Width,
				IN ULONG Height, IN ULONG Color );
VOID NTAPI InbvSetProgressBarSubset ( IN ULONG Floor, IN ULONG Ceiling );

/** Graphical boot is enabled (i.e. NOGUIBOOT switch is not present) */
BOOLEAN guiboot_enabled = TRUE;

/** Boot text is enabled */
BOOLEAN boottext_enabled = TRUE;

/**
 * Print text to boot screen
 *
 * @v fmt		Format string
 * @v ...		Arguments
 */
VOID BootPrint ( const char *fmt, ... ) {
	static BOOLEAN initialised = FALSE;
	char buf[BOOT_TEXT_MAX_LEN];
	va_list args;

	/* Generate string to print */
	va_start ( args, fmt );
	RtlStringCbVPrintfA ( buf, sizeof ( buf ), fmt, args );
	va_end ( args );

	/* Log to debugger, if attached */
	DbgPrint ( "%s", buf );

	/* Do nothing more unless graphical boot is enabled */
	if ( ! guiboot_enabled )
		return;

	/* Do nothing more unless boot text is enabled */
	if ( ! boottext_enabled )
		return;

	/* Configure display */
	if ( ! InbvCheckDisplayOwnership() )
		InbvAcquireDisplayOwnership();
	if ( ! initialised ) {
		InbvSetScrollRegion ( BOOT_TEXT_AREA_LEFT,
				      BOOT_TEXT_AREA_TOP,
				      BOOT_TEXT_AREA_RIGHT,
				      BOOT_TEXT_AREA_BOTTOM );
		initialised = TRUE;
	}
	InbvSetTextColor ( BOOT_TEXT_COLOUR );
	InbvEnableDisplayString ( TRUE );
	/* Avoid switching to the "chkdsk" screen */
	InbvInstallDisplayStringFilter ( NULL );

	/* Print the string */
	InbvDisplayString ( buf );
}
