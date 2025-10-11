//========= Copyright Nozzle Software, All rights reserved. ============//
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// See the GNU General Public License for more details.
//
//======================================================================//


#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <linux/soundcard.h>
#include <alsa/asoundlib.h>
#include <stdio.h>
#include "waspdef.h" // Assuming this defines shm, sn, Con_Printf, COM_CheckParm, etc.

// --- MODERN ALSA GLOBALS ---
// The old 'audio_fd' (an integer file descriptor) is replaced by the ALSA PCM handle.
snd_pcm_t *pcm_handle = NULL;
// Use "default" to allow ALSA/PulseAudio/PipeWire to handle device selection.
static const char *ALSA_DEVICE = "default"; 
// Define default buffer and period sizes in frames (ALSA's preferred unit)
static snd_pcm_uframes_t alsa_buffer_size = 4096; // Total buffer size in frames (read head limit)
static snd_pcm_uframes_t alsa_period_size = 1024; // Chunk size for submission (write chunk size)
// This tracks the current write position in the user buffer (in frames)
static snd_pcm_uframes_t alsa_write_ptr_frames = 0; 
// --- END MODERN ALSA GLOBALS ---

// Original Globals (kept for archival/compatibility but not actively used by new ALSA logic)
int audio_fd; 
int snd_inited;

static int tryrates[] = { 11025, 22050, 44100, 8000 }; 

// shm is assumed to be a global or defined in waspdef.h (e.g., shm = &sn)

qboolean SNDDMA_Init(void)
{
	int i;
    int rc;
    char *s;
    
    // ALSA specific variables
    snd_pcm_hw_params_t *params;
    unsigned int rate = 0;
    snd_pcm_format_t format;
    unsigned int channels = 0;
    int dir = 0; 

    // Original OSS variables (commented out)
    /*
    int fmt;
    int tmp;
    struct audio_buf_info info;
    int caps;
    */

	snd_inited = 0;
    shm = &sn; // Assume shm initialization is required here
 
    // --- START: ORIGINAL OSS CODE (Archived) ---
    /*
	// open /dev/dsp, confirm capability to mmap, and get size of dma buffer
 
    audio_fd = open("/dev/dsp", O_RDWR);
    if (audio_fd < 0)
	{
		perror("/dev/dsp");
        Con_Printf("Could not open /dev/dsp\n");
		return 0;
	}
 
    rc = ioctl(audio_fd, SNDCTL_DSP_RESET, 0);
    if (rc < 0)
	{
		perror("/dev/dsp");
		Con_Printf("Could not reset /dev/dsp\n");
		close(audio_fd);
		return 0;
	}
 
	if (ioctl(audio_fd, SNDCTL_DSP_GETCAPS, &caps)==-1)
	{
		perror("/dev/dsp");
        Con_Printf("Sound driver too old\n");
		close(audio_fd);
		return 0;
	}
 
	if (!(caps & DSP_CAP_TRIGGER) || !(caps & DSP_CAP_MMAP))
	{
		Con_Printf("Sorry but your soundcard can't do this\n");
		close(audio_fd);
		return 0;
	}
 
    if (ioctl(audio_fd, SNDCTL_DSP_GETOSPACE, &info)==-1)
    {   
        perror("GETOSPACE");
		Con_Printf("Um, can't do GETOSPACE?\n");
		close(audio_fd);
		return 0;
    }
    
    shm->splitbuffer = 0;
    */
    // --- END: ORIGINAL OSS CODE (Archived) ---

    // --- START: MODERN ALSA IMPLEMENTATION ---

    // 1. Open ALSA Device
    if ((rc = snd_pcm_open(&pcm_handle, ALSA_DEVICE, SND_PCM_STREAM_PLAYBACK, 0)) < 0)
    {
        Con_Printf("ALSA: Could not open PCM device %s: %s\n", ALSA_DEVICE, snd_strerror(rc));
        return 0;
    }

    // Allocate memory for hardware configuration parameters
    snd_pcm_hw_params_alloca(&params);

    // Initialize the configuration space to "full"
    snd_pcm_hw_params_any(pcm_handle, params);
    
    // Set access type: Read/Write Interleaved (frames are R-L-R-L...)
    if ((rc = snd_pcm_hw_params_set_access(pcm_handle, params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) 
    {
        Con_Printf("ALSA: Error setting access type: %s\n", snd_strerror(rc));
        goto alsa_error;
    }

    // --- 2. Determine and Set Format (Sample Bits) ---
 
    s = getenv("QUAKE_SOUND_SAMPLEBITS");
    if (s) shm->samplebits = atoi(s);
	else if ((i = COM_CheckParm("-sndbits")) != 0)
		shm->samplebits = atoi(com_argv[i+1]);
    
    if (shm->samplebits == 16 || shm->samplebits == 0) {
        format = SND_PCM_FORMAT_S16_LE; 
        shm->samplebits = 16;
    } else if (shm->samplebits == 8) {
        format = SND_PCM_FORMAT_U8; 
    } else {
        Con_Printf("ALSA: %d-bit sound not supported by this driver.", shm->samplebits);
        goto alsa_error;
    }

    if ((rc = snd_pcm_hw_params_set_format(pcm_handle, params, format)) < 0)
    {
        // Fallback to 8-bit if 16-bit fails
        if (format == SND_PCM_FORMAT_S16_LE) {
            format = SND_PCM_FORMAT_U8;
            shm->samplebits = 8;
            if ((rc = snd_pcm_hw_params_set_format(pcm_handle, params, format)) < 0) {
                 Con_Printf("ALSA: Could not set 8-bit format: %s\n", snd_strerror(rc));
                 goto alsa_error;
            }
        } else {
             Con_Printf("ALSA: Could not set 8-bit format: %s\n", snd_strerror(rc));
             goto alsa_error;
        }
    }

    // --- 3. Determine and Set Channels (Mono/Stereo) ---
    s = getenv("QUAKE_SOUND_CHANNELS");
    if (s) channels = atoi(s);
	else if ((i = COM_CheckParm("-sndmono")) != 0)
		channels = 1;
	else if ((i = COM_CheckParm("-sndstereo")) != 0)
		channels = 2;
    else channels = 2; 

    if ((rc = snd_pcm_hw_params_set_channels(pcm_handle, params, channels)) < 0)
    {
        Con_Printf("ALSA: Could not set channels to %d: %s\n", channels, snd_strerror(rc));
        goto alsa_error;
    }
    shm->channels = channels; 

    // --- 4. Determine and Set Sample Rate (Speed) ---
    rate = 0;
    s = getenv("QUAKE_SOUND_SPEED");
    if (s) rate = atoi(s);
	else if ((i = COM_CheckParm("-sndspeed")) != 0)
		rate = atoi(com_argv[i+1]);

    if (rate == 0) {
        for (i=0 ; i<sizeof(tryrates)/sizeof(tryrates[0]) ; i++) {
             unsigned int temp_rate = tryrates[i];
             if (snd_pcm_hw_params_set_rate_near(pcm_handle, params, &temp_rate, &dir) == 0) {
                 rate = temp_rate;
                 break;
             }
        }
    } else {
        unsigned int temp_rate = rate;
        snd_pcm_hw_params_set_rate_near(pcm_handle, params, &temp_rate, &dir);
        rate = temp_rate; 
    }

    if (rate == 0) {
        Con_Printf("ALSA: Could not find a suitable speed/rate.\n");
        goto alsa_error;
    }
    shm->speed = rate;

    // --- 5. Set Buffer and Period Sizes ---
    if ((rc = snd_pcm_hw_params_set_buffer_size_near(pcm_handle, params, &alsa_buffer_size)) < 0) {
         Con_Printf("ALSA: Could not set buffer size: %s\n", snd_strerror(rc));
         goto alsa_error;
    }
    if ((rc = snd_pcm_hw_params_set_period_size_near(pcm_handle, params, &alsa_period_size, &dir)) < 0) {
         Con_Printf("ALSA: Could not set period size: %s\n", snd_strerror(rc));
         goto alsa_error;
    }

    // --- 6. Apply Parameters ---
    if ((rc = snd_pcm_hw_params(pcm_handle, params)) < 0)
    {
        Con_Printf("ALSA: Could not apply hardware parameters: %s\n", snd_strerror(rc));
        goto alsa_error;
    }

    // --- 7. Final Buffer Setup (Replacing mmap) ---
    // shm->samples is the TOTAL number of samples (not frames) in the buffer
    shm->samples = alsa_buffer_size * shm->channels; 
    
    // CRITICAL FIX: Set submission_chunk in SAMPLES, not FRAMES, for engine compatibility.
    shm->submission_chunk = alsa_period_size * shm->channels; 
    
    // Allocate buffer in user space (Total samples * bytes per sample)
    shm->buffer = (unsigned char *) malloc(shm->samples * (shm->samplebits / 8));
    if (!shm->buffer)
    {
        Con_Printf("ALSA: Could not allocate memory for sound buffer.\n");
        goto alsa_error;
    }
    
    // --- 8. Final Setup ---
    shm->samplepos = 0; // The read pointer starts at 0
    alsa_write_ptr_frames = 0; // The write pointer also starts at 0
	snd_inited = 1;
    
	return 1;


alsa_error:
    // Unified error cleanup for ALSA
    if (pcm_handle) {
        snd_pcm_close(pcm_handle);
        pcm_handle = NULL;
    }
    if (shm->buffer) {
        free(shm->buffer);
        shm->buffer = NULL;
    }
    return 0;

}


int SNDDMA_GetDMAPos(void)
{
    // Original OSS variables (commented out)
	/*
	struct count_info count;
	*/
    
    // ALSA specific variables
    snd_pcm_sframes_t delay;
    int rc;

	if (!snd_inited) return 0;

    // --- START: ORIGINAL OSS CODE (Archived) ---
    /*
	if (ioctl(audio_fd, SNDCTL_DSP_GETOPTR, &count)==-1)
	{
		perror("/dev/dsp");
		Con_Printf("Uh, sound dead.\n");
		close(audio_fd);
		snd_inited = 0;
		return 0;
	}
	shm->samplepos = count.ptr / (shm->samplebits / 8);
    */
    // --- END: ORIGINAL OSS CODE (Archived) ---

    // --- START: MODERN ALSA IMPLEMENTATION ---

    // Get the number of frames currently remaining (delay) in the hardware buffer.
    if ((rc = snd_pcm_delay(pcm_handle, &delay)) < 0)
    {
        // Check for common errors
        if (rc == -EPIPE) {
            // Underrun occurred, recover by preparing the stream
            Con_Printf("ALSA: Underrun detected in GetDMAPos. Preparing...\n");
            snd_pcm_prepare(pcm_handle);
            delay = alsa_buffer_size; // Assume the buffer is now empty (max delay)
        } else {
            // Critical error
            Con_Printf("ALSA: Status error in GetDMAPos: %s\n", snd_strerror(rc));
            SNDDMA_Shutdown();
            return 0;
        }
    }

    // Total frames in buffer:
    snd_pcm_uframes_t total_frames = alsa_buffer_size;

    // Current READ pointer position (in frames) = (Total Frames - Frames remaining)
    // The current sample position is the end of the data already read/played.
    snd_pcm_uframes_t read_frames = (total_frames - delay);

    // This position must wrap around the buffer size
    read_frames %= total_frames;

    // shm->samplepos stores the position in total SAMPLES
    shm->samplepos = read_frames * shm->channels;
    
    // --- END: MODERN ALSA IMPLEMENTATION ---

	return shm->samplepos;
}

void SNDDMA_Shutdown(void)
{
	if (snd_inited)
	{
        // --- START: ORIGINAL OSS CODE (Archived) ---
        /*
		close(audio_fd);
        */
        // --- END: ORIGINAL OSS CODE (Archived) ---

        // --- START: MODERN ALSA IMPLEMENTATION ---
        if (pcm_handle) {
            // Stop and close the ALSA device
            snd_pcm_close(pcm_handle);
            pcm_handle = NULL;
        }
        if (shm->buffer) {
            // Free the user-space buffer
            free(shm->buffer);
            shm->buffer = NULL;
        }
        // --- END: MODERN ALSA IMPLEMENTATION ---

		snd_inited = 0;
	}
}

/*
==============
SNDDMA_Submit

Send sound to device if buffer isn't really the dma buffer
===============
*/
void SNDDMA_Submit(void)
{
    // In ALSA write mode, this function MUST explicitly push the data to the device.

    if (!snd_inited || !pcm_handle || !shm->buffer) return;

    // CRITICAL FIX: Convert the engine's sample-based chunk size to ALSA's frame-based size.
    snd_pcm_sframes_t frames_to_write = shm->submission_chunk / shm->channels;

    // Calculate the start position in the user buffer (shm->buffer)
    size_t bytes_per_sample = shm->samplebits / 8;
    // Offset in bytes = current write position (frames) * channels * bytes_per_sample
    size_t offset_bytes = alsa_write_ptr_frames * shm->channels * bytes_per_sample;
    
    // Pointer to the start of the chunk to submit
    void *submit_ptr = shm->buffer + offset_bytes;

    // snd_pcm_writei performs an interleaved write of frames_to_write frames
    snd_pcm_sframes_t frames_written = snd_pcm_writei(
        pcm_handle, 
        submit_ptr, // The location in the user buffer to read from
        frames_to_write
    );

    if (frames_written > 0) {
        // SUCCESS: Advance the global write pointer
        alsa_write_ptr_frames += frames_written;
        // Wrap around the buffer size (in frames)
        alsa_write_ptr_frames %= alsa_buffer_size;
    } else {
        // Handle stream errors (frames_written < 0)
        if (frames_written == -EPIPE) {
            // EPIPE means an underrun, recover by preparing the stream
            Con_Printf("ALSA: Underrun detected in SNDDMA_Submit. Preparing...\n");
            snd_pcm_prepare(pcm_handle);
        } else if (frames_written == -ESTRPIPE) {
             // Suspend occurred (e.g., device hot-plug or state change), try to resume
             Con_Printf("ALSA: Suspend detected in SNDDMA_Submit. Attempting resume...\n");
             while ((frames_written = snd_pcm_resume(pcm_handle)) == -EAGAIN);
             if (frames_written < 0) {
                snd_pcm_prepare(pcm_handle);
             }
        } else {
             // Other fatal errors
             Con_Printf("ALSA: Write error in SNDDMA_Submit: %s\n", snd_strerror(frames_written));
        }
    }
}
