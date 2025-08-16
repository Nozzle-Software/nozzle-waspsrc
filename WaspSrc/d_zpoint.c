//========= Copyright Nozzle Software, All rights reserved. ============//
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// See the GNU General Public License for more details.
//
//======================================================================//


// d_zpoint.c: software driver module for drawing z-buffered points

#include "waspdef.h"
#include "d_local.h"


/*
=====================
D_DrawZPoint
=====================
*/
void D_DrawZPoint (void)
{
	byte	*pdest;
	short	*pz;
	int		izi;
	
	pz = d_pzbuffer + (d_zwidth * r_zpointdesc.v) + r_zpointdesc.u;
	pdest = d_viewbuffer + d_scantable[r_zpointdesc.v] + r_zpointdesc.u;
	izi = (int)(r_zpointdesc.zi * 0x8000);

	if (*pz <= izi)
	{
		*pz = izi;
		*pdest = r_zpointdesc.color;
	}
}

