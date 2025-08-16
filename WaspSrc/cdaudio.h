//========= Copyright Nozzle Software, All rights reserved. ============//
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// See the GNU General Public License for more details.
//
//======================================================================//



int CDAudio_Init(void);
void CDAudio_Play(byte track, qboolean looping);
void CDAudio_Stop(void);
void CDAudio_Pause(void);
void CDAudio_Resume(void);
void CDAudio_Shutdown(void);
void CDAudio_Update(void);
