#ifndef _IBFT_H
#define _IBFT_H

/*
 * Copyright Fen Systems Ltd. 2007.  Portions of this code are derived
 * from IBM Corporation Sample Programs.  Copyright IBM Corporation
 * 2004, 2007.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/** @file
 *
 * iSCSI boot firmware table
 *
 * The information in this file is derived from the document "iSCSI
 * Boot Firmware Table (iBFT)" as published by IBM at
 *
 * ftp://ftp.software.ibm.com/systems/support/system_x_pdf/ibm_iscsi_boot_firmware_table_v1.02.pdf
 *
 */

#include "acpi.h"

/** iSCSI Boot Firmware Table signature */
#define IBFT_SIG "iBFT"

/** A string within the iBFT */
#pragma pack(1)
typedef struct _IBFT_STRING {
	/** Length of string */
	USHORT length;
	/** Offset to string */
	USHORT offset;
} IBFT_STRING, *PIBFT_STRING;
#pragma pack()

/** An IP address within the iBFT */
#pragma pack(1)
typedef struct _IBFT_IPADDR {
	/** Reserved; must be zero */
	USHORT zeroes[5];
	/** Must be 0xffff if IPv4 address is present, otherwise zero */
	USHORT ones;
	/** The IPv4 address, or zero if not present */
	ULONG in;
} IBFT_IPADDR, *PIBFT_IPADDR;
#pragma pack()

/**
 * iBFT structure header
 *
 * This structure is common to several sections within the iBFT.
 */
#pragma pack()
typedef struct _IBFT_HEADER {
	/** Structure ID
	 *
	 * This is an IBFT_STRUCTURE_ID_XXX constant
	 */
	UCHAR structure_id;
	/** Version (always 1) */
	UCHAR version;
	/** Length, including this header */
	USHORT length;
	/** Index 
	 *
	 * This is the number of the NIC or Target, when applicable.
	 */
	UCHAR index;
	/** Flags */
	UCHAR flags;
} IBFT_HEADER, *PIBFT_HEADER;
#pragma pack()

/**
 * iBFT Control structure
 *
 */
#pragma pack(1)
typedef struct _IBFT_CONTROL {
	/** Common header */
	IBFT_HEADER header;
	/** Extensions */
	USHORT extensions;
	/** Offset to Initiator structure */
	USHORT initiator;
	/** Offset to NIC structure for NIC 0 */
	USHORT nic_0;
	/** Offset to Target structure for target 0 */
	USHORT target_0;
	/** Offset to NIC structure for NIC 1 */
	USHORT nic_1;
	/** Offset to Target structure for target 1 */
	USHORT target_1;
} IBFT_CONTROL, *PIBFT_CONTROL;
#pragma pack()

/** Structure ID for Control section */
#define IBFT_STRUCTURE_ID_CONTROL 0x01

/** Attempt login only to specified target
 *
 * If this flag is not set, all targets will be logged in to.
 */
#define IBFT_FL_CONTROL_SINGLE_LOGIN_ONLY 0x01

/**
 * iBFT Initiator structure
 *
 */
#pragma pack(1)
typedef struct _IBFT_INITIATOR {
	/** Common header */
	IBFT_HEADER header;
	/** iSNS server */
	IBFT_IPADDR isns_server;
	/** SLP server */
	IBFT_IPADDR slp_server;
	/** Primary and secondary Radius servers */
	IBFT_IPADDR radius[2];
	/** Initiator name */
	IBFT_STRING initiator_name;
} IBFT_INITIATOR, *PIBFT_INITIATOR;
#pragma pack()

/** Structure ID for Initiator section */
#define IBFT_STRUCTURE_ID_INITIATOR 0x02

/** Initiator block valid */
#define IBFT_FL_INITIATOR_BLOCK_VALID 0x01

/** Initiator firmware boot selected */
#define IBFT_FL_INITIATOR_FIRMWARE_BOOT_SELECTED 0x02

/**
 * iBFT NIC structure
 *
 */
#pragma pack(1)
typedef struct _IBFT_NIC {
	/** Common header */
	IBFT_HEADER header;
	/** IP address */
	IBFT_IPADDR ip_address;
	/** Subnet mask
	 *
	 * This is the length of the subnet mask in bits (e.g. /24).
	 */
	UCHAR subnet_mask_prefix;
	/** Origin */
	UCHAR origin;
	/** Default gateway */
	IBFT_IPADDR gateway;
	/** Primary and secondary DNS servers */
	IBFT_IPADDR dns[2];
	/** DHCP server */
	IBFT_IPADDR dhcp;
	/** VLAN tag */
	USHORT vlan;
	/** MAC address */
	UCHAR mac_address[6];
	/** PCI bus:dev:fn */
	USHORT pci_bus_dev_func;
	/** Hostname */
	IBFT_STRING hostname;
} IBFT_NIC, *PIBFT_NIC;
#pragma pack()

/** Structure ID for NIC section */
#define IBFT_STRUCTURE_ID_NIC 0x03

/** NIC block valid */
#define IBFT_FL_NIC_BLOCK_VALID 0x01

/** NIC firmware boot selected */
#define IBFT_FL_NIC_FIRMWARE_BOOT_SELECTED 0x02

/** NIC global / link local */
#define IBFT_FL_NIC_GLOBAL 0x04

/**
 * iBFT Target structure
 *
 */
#pragma pack(1)
typedef struct _IBFT_TARGET {
	/** Common header */
	IBFT_HEADER header;
	/** IP address */
	IBFT_IPADDR ip_address;
	/** TCP port */
	USHORT socket;
	/** Boot LUN */
	ULONGLONG boot_lun;
	/** CHAP type
	 *
	 * This is an IBFT_CHAP_XXX constant.
	 */
	UCHAR chap_type;
	/** NIC association */
	UCHAR nic_association;
	/** Target name */
	IBFT_STRING target_name;
	/** CHAP name */
	IBFT_STRING chap_name;
	/** CHAP secret */
	IBFT_STRING chap_secret;
	/** Reverse CHAP name */
	IBFT_STRING reverse_chap_name;
	/** Reverse CHAP secret */
	IBFT_STRING reverse_chap_secret;
} IBFT_TARGET, *PIBFT_TARGET;
#pragma pack()

/** Structure ID for Target section */
#define IBFT_STRUCTURE_ID_TARGET 0x04

/** Target block valid */
#define IBFT_FL_TARGET_BLOCK_VALID 0x01

/** Target firmware boot selected */
#define IBFT_FL_TARGET_FIRMWARE_BOOT_SELECTED 0x02

/** Target use Radius CHAP */
#define IBFT_FL_TARGET_USE_CHAP 0x04

/** Target use Radius rCHAP */
#define IBFT_FL_TARGET_USE_RCHAP 0x08

/* Values for chap_type */
#define IBFT_CHAP_NONE		0	/**< No CHAP authentication */
#define IBFT_CHAP_ONE_WAY	1	/**< One-way CHAP */
#define IBFT_CHAP_MUTUAL	2	/**< Mutual CHAP */

/**
 * iSCSI Boot Firmware Table (iBFT)
 */
#pragma pack(1)
typedef struct _IBFT_TABLE {
	/** ACPI header */
	ACPI_DESCRIPTION_HEADER acpi;
	/** Reserved */
	UCHAR reserved[12];
	/** Control structure */
	IBFT_CONTROL control;
} IBFT_TABLE, *PIBFT_TABLE;
#pragma pack()

#endif /* _IBFT_H */
