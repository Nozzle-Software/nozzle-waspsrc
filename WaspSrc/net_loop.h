//========= Copyright Nozzle Software, All rights reserved. ============//
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// See the GNU General Public License for more details.
//
//======================================================================//


// net_loop.h

int			Loop_Init (void);
void		Loop_Listen (qboolean state);
void		Loop_SearchForHosts (qboolean xmit);
qsocket_t 	*Loop_Connect (char *host);
qsocket_t 	*Loop_CheckNewConnections (void);
int			Loop_GetMessage (qsocket_t *sock);
int			Loop_SendMessage (qsocket_t *sock, sizebuf_t *data);
int			Loop_SendUnreliableMessage (qsocket_t *sock, sizebuf_t *data);
qboolean	Loop_CanSendMessage (qsocket_t *sock);
qboolean	Loop_CanSendUnreliableMessage (qsocket_t *sock);
void		Loop_Close (qsocket_t *sock);
void		Loop_Shutdown (void);
