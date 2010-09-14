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

#include <ntddk.h>
#include <ntstrsafe.h>
#include "sanbootconf.h"
#include "sbft.h"

/**
 * Parse sBFT SCSI subtable
 *
 * @v sbft		sBFT
 * @v scsi		SCSI subtable
 */
static VOID parse_sbft_scsi ( PSBFT_TABLE sbft, PSBFT_SCSI_SUBTABLE scsi ) {

	DbgPrint ( "Found sBFT SCSI subtable:\n" );
	DbgPrint ( "  LUN = %02x%02x-%02x%02x-%02x%02x-%02x%02x\n",
		   scsi->lun[0], scsi->lun[1], scsi->lun[2], scsi->lun[3],
		   scsi->lun[4], scsi->lun[5], scsi->lun[6], scsi->lun[7] );
	( VOID ) sbft;
}

/**
 * Parse sBFT SRP subtable
 *
 * @v sbft		sBFT
 * @v srp		SRP subtable
 */
static VOID parse_sbft_srp ( PSBFT_TABLE sbft, PSBFT_SRP_SUBTABLE srp ) {

	DbgPrint ( "Found sBFT SRP subtable:\n" );
	DbgPrint ( "  Initiator port ID = 0x%08x%08x%08x%08x\n",
		   RtlUlongByteSwap ( srp->initiator_port_id.u.dwords[0] ),
		   RtlUlongByteSwap ( srp->initiator_port_id.u.dwords[1] ),
		   RtlUlongByteSwap ( srp->initiator_port_id.u.dwords[2] ),
		   RtlUlongByteSwap ( srp->initiator_port_id.u.dwords[3] ) );
	DbgPrint ( "  Target port ID = 0x%08x%08x%08x%08x\n",
		   RtlUlongByteSwap ( srp->target_port_id.u.dwords[0] ),
		   RtlUlongByteSwap ( srp->target_port_id.u.dwords[1] ),
		   RtlUlongByteSwap ( srp->target_port_id.u.dwords[2] ),
		   RtlUlongByteSwap ( srp->target_port_id.u.dwords[3] ) );
	( VOID ) sbft;
}

/**
 * Parse sBFT IB subtable
 *
 * @v sbft		sBFT
 * @v ib		IB subtable
 */
static VOID parse_sbft_ib ( PSBFT_TABLE sbft, PSBFT_IB_SUBTABLE ib ) {

	DbgPrint ( "Found sBFT IB subtable:\n" );
	DbgPrint ( "  Source GID = 0x%08x%08x%08x%08x\n",
		   RtlUlongByteSwap ( ib->sgid.u.dwords[0] ),
		   RtlUlongByteSwap ( ib->sgid.u.dwords[1] ),
		   RtlUlongByteSwap ( ib->sgid.u.dwords[2] ),
		   RtlUlongByteSwap ( ib->sgid.u.dwords[3] ) );
	DbgPrint ( "  Destination GID = 0x%08x%08x%08x%08x\n",
		   RtlUlongByteSwap ( ib->dgid.u.dwords[0] ),
		   RtlUlongByteSwap ( ib->dgid.u.dwords[1] ),
		   RtlUlongByteSwap ( ib->dgid.u.dwords[2] ),
		   RtlUlongByteSwap ( ib->dgid.u.dwords[3] ) );
	DbgPrint ( "  Service ID = 0x%08x%08x\n",
		   RtlUlongByteSwap ( ib->service_id.u.dwords[0] ),
		   RtlUlongByteSwap ( ib->service_id.u.dwords[1] ) );
	DbgPrint ( "  Partition key = 0x%04x\n", ib->pkey );
	( VOID ) sbft;
}

/**
 * Parse sBFT
 *
 * @v acpi		ACPI description header
 */
VOID parse_sbft ( PACPI_DESCRIPTION_HEADER acpi ) {
	PSBFT_TABLE sbft = ( PSBFT_TABLE ) acpi;
	PSBFT_SCSI_SUBTABLE scsi;
	PSBFT_SRP_SUBTABLE srp;
	PSBFT_IB_SUBTABLE ib;

	if ( sbft->scsi_offset ) {
		scsi = ( ( PSBFT_SCSI_SUBTABLE )
			 ( ( PUCHAR ) sbft + sbft->scsi_offset ) );
		parse_sbft_scsi ( sbft, scsi );
	}
	if ( sbft->srp_offset ) {
		srp = ( ( PSBFT_SRP_SUBTABLE )
			( ( PUCHAR ) sbft + sbft->srp_offset ) );
		parse_sbft_srp ( sbft, srp );
	}
	if ( sbft->ib_offset ) {
		ib = ( ( PSBFT_IB_SUBTABLE )
		       ( ( PUCHAR ) sbft + sbft->ib_offset ) );
		parse_sbft_ib ( sbft, ib );
	}
}
