#ifndef _SBFT_H
#define _SBFT_H

/*
 * Copyright (C) 2009 Fen Systems Ltd <mbrown@fensystems.co.uk>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @file
 *
 * SRP boot firmware table
 *
 * The working draft specification for the SRP boot firmware table can
 * be found at
 *
 *   http://etherboot.org/wiki/srp/sbft
 *
 */

#include "acpi.h"

/** SRP Boot Firmware Table signature */
#define SBFT_SIG "sBFT"

/**
 * SRP Boot Firmware Table
 */
#pragma pack(1)
typedef struct _SBFT_TABLE {
	/** ACPI header */
	ACPI_DESCRIPTION_HEADER acpi;
	/** Offset to SCSI subtable */
	USHORT scsi_offset;
	/** Offset to SRP subtable */
	USHORT srp_offset;
	/** Offset to IB subtable, if present */
	USHORT ib_offset;
	/** Reserved */
	UCHAR reserved[6];
} SBFT_TABLE, *PSBFT_TABLE;
#pragma pack()

/**
 * sBFT SCSI subtable
 */
#pragma pack(1)
typedef struct _SBFT_SCSI_SUBTABLE {
	/** LUN */
	ULONGLONG lun;
} SBFT_SCSI_SUBTABLE, *PSBFT_SCSI_SUBTABLE;
#pragma pack()

/** An SRP port ID */
#pragma pack(1)
typedef struct _SRP_PORT_ID {
	union {
		UCHAR bytes[16];
		USHORT words[8];
		ULONG dwords[4];
	} u;
} SRP_PORT_ID, *PSRP_PORT_ID;
#pragma pack()

/**
 * sBFT SRP subtable
 */
#pragma pack(1)
typedef struct _SBFT_SRP_SUBTABLE {
	/** Initiator port identifier */
	SRP_PORT_ID initiator_port_id;
	/** Target port identifier */
	SRP_PORT_ID target_port_id;
} SBFT_SRP_SUBTABLE, *PSBFT_SRP_SUBTABLE;
#pragma pack()

/** An Infiniband GUID */
#pragma pack(1)
typedef struct _IB_GUID {
	union {
		UCHAR bytes[8];
		USHORT words[4];
		ULONG dwords[2];
	} u;
} IB_GUID, *PIB_GUID;
#pragma pack()

/** An Infiniband GID */
#pragma pack(1)
typedef struct _IB_GID {
	union {
	        UCHAR bytes[16];
		USHORT words[8];
		ULONG dwords[4];
		IB_GUID guid[2];
	} u;
} IB_GID, *PIB_GID;
#pragma pack()

/**
 * sBFT IB subtable
 */
#pragma pack(1)
typedef struct _SBFT_IB_SUBTABLE {
	/** Source GID */
	IB_GID sgid;
	/** Destination GID */
	IB_GID dgid;
	/** Service ID */
	IB_GUID service_id;
	/** Partition key */
	USHORT pkey;
	/** Reserved */
	UCHAR reserved[6];
} SBFT_IB_SUBTABLE, *PSBFT_IB_SUBTABLE;
#pragma pack()

extern VOID parse_sbft ( PACPI_DESCRIPTION_HEADER acpi );

#endif /* _SBFT_H */
