//========= Copyright Nozzle Software, All rights reserved. ============//
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// See the GNU General Public License for more details.
//
//======================================================================//


//
// nonintel.c: code for non-Intel processors only
//

#include "waspdef.h"
#include "r_local.h"
#include "d_local.h"

#if	!id386

/*
================
R_Surf8Patch
================
*/
void R_Surf8Patch ()
{
	// we only patch code on Intel
}


/*
================
R_Surf16Patch
================
*/
void R_Surf16Patch ()
{
	// we only patch code on Intel
}


/*
================
R_SurfacePatch
================
*/
void R_SurfacePatch (void)
{
	// we only patch code on Intel
}


#endif	// !id386

