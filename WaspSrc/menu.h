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
// the net drivers should just set the apropriate bits in m_activenet,
// instead of having the menu code look through their internal tables
//
#define	MNET_IPX		1
#define	MNET_TCP		2

extern	int	m_activenet;

//
// menus
//
void M_Init (void);
void M_Keydown (int key);
void M_Draw (void);
void M_ToggleMenu_f (void);


