/*
 * This file is part of doom-ng-odroid-go.
 * Copyright (c) 2019 ducalex.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <rg_system.h>
#include <sys/unistd.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <errno.h>
#include <doomtype.h>
#include <doomstat.h>
#include <doomdef.h>
#include <d_main.h>
#include <g_game.h>
#include <i_system.h>
#include <i_video.h>
#include <i_sound.h>
#include <i_main.h>
#include <m_argv.h>
#include <m_fixed.h>
#include <m_misc.h>
#include <r_draw.h>
#include <r_fps.h>
#include <s_sound.h>
#include <st_stuff.h>
#include <mmus2mid.h>
#include <midifile.h>
#include <oplplayer.h>

#define SAMPLERATE 11025
#define SAMPLECOUNT (SAMPLERATE / TICRATE + 1)
#define NUM_MIX_CHANNELS 8

static rg_video_update_t update;
static rg_app_t *app;

// Expected variables by doom
int snd_card = 1, mus_card = 1;
int snd_samplerate = SAMPLERATE;
int realtic_clock_rate = 100;
int current_palette = 0;

static struct {
    uint8_t *data;   // Sample
    uint8_t *endptr; // End of data
    int start;       // Time/gametic that the channel started playing
    int sfxid;       // SFX id of the playing sound effect.
} channels[NUM_MIX_CHANNELS];
static struct {
    void *data;
    size_t length;
} sfx[NUMSFX];
static short mixbuffer[SAMPLECOUNT * 2];
static const music_player_t *music_player = &opl_synth_player;
static bool musicPlaying = false;

// TO DO: Detect when menu is open so we can send better keys.

static const struct {int mask; int *key;} keymap[] = {
    {RG_KEY_UP, &key_up},
    {RG_KEY_DOWN, &key_down},
    {RG_KEY_LEFT, &key_left},
    {RG_KEY_RIGHT, &key_right},
    {RG_KEY_A, &key_fire},
    {RG_KEY_A, &key_menu_enter},
    {RG_KEY_B, &key_speed},
    {RG_KEY_B, &key_strafe},
    {RG_KEY_B, &key_backspace},
    {RG_KEY_MENU, &key_escape},
    {RG_KEY_OPTION, &key_map},
    {RG_KEY_START, &key_use},
    {RG_KEY_SELECT, &key_weapontoggle},
};

static const char *SETTING_GAMMA = "Gamma";


static dialog_return_t gamma_update_cb(dialog_option_t *option, dialog_event_t event)
{
    int gamma = usegamma;
    int max = 9;

    if (event == RG_DIALOG_PREV)
        gamma = gamma > 0 ? gamma - 1 : max;

    if (event == RG_DIALOG_NEXT)
        gamma = gamma < max ? gamma + 1 : 0;

    if (gamma != usegamma)
    {
        usegamma = gamma;
        I_SetPalette(current_palette);
        rg_display_queue_update(&update, NULL);
        rg_settings_set_app_int32(SETTING_GAMMA, gamma);
        usleep(50000);
    }

    sprintf(option->value, "%d/%d", gamma, max);

    return RG_DIALOG_IGNORE;
}


void I_StartFrame(void)
{
    //
}

void I_UpdateNoBlit(void)
{
    //
}

void I_FinishUpdate(void)
{
    rg_display_queue_update(&update, NULL);
}

bool I_StartDisplay(void)
{
    return true;
}

void I_EndDisplay(void)
{
    //
}

void I_SetPalette(int pal)
{
    uint16_t *palette = V_BuildPalette(pal, 16);
    for (int i = 0; i < 256; i++)
        update.palette[i] = palette[i] << 8 | palette[i] >> 8;
    Z_Free(palette);
    current_palette = pal;
}

void I_InitGraphics(void)
{
    // set first three to standard values
    for (int i = 0; i < 3; i++)
    {
        screens[i].width = SCREENWIDTH;
        screens[i].height = SCREENHEIGHT;
        screens[i].byte_pitch = SCREENWIDTH;
    }

    // Main screen uses internal ram for speed
    screens[0].data = update.buffer;
    screens[0].not_on_heap = true;

    // statusbar
    screens[4].width = SCREENWIDTH;
    screens[4].height = (ST_SCALED_HEIGHT + 1);
    screens[4].byte_pitch = SCREENWIDTH;

    rg_display_set_source_format(SCREENWIDTH, SCREENHEIGHT, 0, 0, SCREENWIDTH, RG_PIXEL_PAL565_BE);
}

int I_GetTimeMS(void)
{
    return esp_timer_get_time() / 1000;
}

int I_GetTime(void)
{
    return ((esp_timer_get_time() * TICRATE) / 1000000);
}

void I_uSleep(unsigned long usecs)
{
    usleep(usecs);
}

const char *I_DoomExeDir(void)
{
    return RG_BASE_PATH_ROMS "/doom";
}

void I_UpdateSoundParams(int handle, int volume, int seperation, int pitch)
{
}

int I_StartSound(int sfxid, int channel, int vol, int sep, int pitch, int priority)
{
    int oldest = gametic;
    int slot = 0;

    // These sound are played only once at a time. Stop any running ones.
    if (sfxid == sfx_sawup || sfxid == sfx_sawidl || sfxid == sfx_sawful
        || sfxid == sfx_sawhit || sfxid == sfx_stnmov || sfxid == sfx_pistol)
    {
        for (int i = 0; i < NUM_MIX_CHANNELS; i++)
        {
            if (channels[i].sfxid == sfxid)
                channels[i].data = NULL;
        }
    }

    // Find available channel or steal the oldest
    for (int i = 0; i < NUM_MIX_CHANNELS; i++)
    {
        if (channels[i].data == NULL)
        {
            slot = i;
            break;
        }
        else if (channels[i].start < oldest)
        {
            slot = i;
            oldest = channels[i].start;
        }
    }

    // Use empty channel if available, otherwise reuse the oldest one
    channels[slot].data = sfx[sfxid].data;
    channels[slot].endptr = channels[slot].data + sfx[sfxid].length;
    channels[slot].sfxid = sfxid;

    return 1;
}

void I_StopSound(int handle)
{
}

bool I_SoundIsPlaying(int handle)
{
    return gametic < handle;
}

bool I_AnySoundStillPlaying(void)
{
    return false;
}

static void soundTask(void *arg)
{
    while (1)
    {
        short *audioBuffer = mixbuffer;
        short stream[2];

        for (int i = 0; i < SAMPLECOUNT; ++i)
        {
            int totalSample = 0;
            int totalSources = 0;
            int sample;

            if (snd_SfxVolume > 0)
            {
                for (int chan = 0; chan < NUM_MIX_CHANNELS; chan++)
                {
                    if (!channels[chan].data)
                        continue;

                    sample = *channels[chan].data++;

                    if (sample != 0)
                    {
                        totalSample += sample;
                        totalSources++;
                    }

                    if (channels[chan].data >= channels[chan].endptr)
                    {
                        channels[chan].data = NULL;
                    }
                }

                totalSample <<= 6;
                totalSample /= (16 - snd_SfxVolume);
            }

            if (musicPlaying && snd_MusicVolume > 0)
            {
                music_player->render(&stream, 1); // It returns 2 (stereo) 16bits values per sample
                sample = (stream[0] + stream[1]) >> 1;
                if (sample > 0)
                {
                    totalSample += sample / (16 - snd_MusicVolume);
                    if (totalSources == 0)
                        totalSources = 1;
                }
            }

            if (totalSources == 0)
            {
                *(audioBuffer++) = 0;
                *(audioBuffer++) = 0;
            }
            else
            {
                *(audioBuffer++) = (short)(totalSample / totalSources);
                *(audioBuffer++) = (short)(totalSample / totalSources);
            }
        }
        rg_audio_submit(mixbuffer, SAMPLECOUNT);
    }
}

void I_InitSound(void)
{
    for (int i = 1; i < NUMSFX; i++)
    {
        if (S_sfx[i].lumpnum == -1)
            sfx[i] = sfx[1]; // Map unknown sounds to pistol
        else
        {
            size_t size = W_LumpLength(S_sfx[i].lumpnum);
            sfx[i].length = ((size + (SAMPLECOUNT - 1)) / SAMPLECOUNT) * SAMPLECOUNT;
            sfx[i].data = Z_Malloc(sfx[i].length, PU_SOUND, 0);
            W_ReadLump(sfx[i].data, S_sfx[i].lumpnum);
            memset(sfx[i].data + size, 0x80, sfx[i].length - size);
        }
    }
    RG_LOGI("all sound effects loaded.\n");

    music_player->init(snd_samplerate);
    music_player->setvolume(snd_MusicVolume);

    xTaskCreatePinnedToCore(&soundTask, "soundTask", 1024, NULL, 5, NULL, 1);
}

void I_ShutdownSound(void)
{
    RG_LOGI("called\n");
    music_player->shutdown();
}

void I_PlaySong(int handle, int looping)
{
    RG_LOGI("%d %d\n", handle, looping);
    music_player->play((void *)handle, looping);
    musicPlaying = true;
}

void I_PauseSong(int handle)
{
    RG_LOGI("handle: %d.\n", handle);
    music_player->pause();
    musicPlaying = false;
}

void I_ResumeSong(int handle)
{
    RG_LOGI("handle: %d.\n", handle);
    music_player->resume();
    musicPlaying = true;
}

void I_StopSong(int handle)
{
    RG_LOGI("handle: %d.\n", handle);
    music_player->stop();
    musicPlaying = false;
}

void I_UnRegisterSong(int handle)
{
    RG_LOGI("handle: %d\n", handle);
    music_player->unregistersong((void *)handle);
}

int I_RegisterSong(const void *data, size_t len)
{
    const uint16_t *header = data;
    MIDI mididata;
    uint8_t *mid;
    int handle = 0;
    int midlen;

    RG_LOGI("Length: %d, Start: %d, Channels: %d, SecChannels: %d, Instruments: %d.\n",
            header[2], header[3], header[4], header[5], header[6]);

    if (mmus2mid(data, &mididata, 64, 1) != 0)
    {
        return 0;
    }

    if (MIDIToMidi(&mididata, &mid, &midlen) == 0)
    {
        free_mididata(&mididata);
        handle = (intptr_t)music_player->registersong(mid, midlen);
        free(mid);
    }

    return handle;
}

void I_SetMusicVolume(int volume)
{
    music_player->setvolume(volume);
}

void I_StartTic(void)
{
    static uint64_t last_time = 0;
    static uint32_t prev_joystick = 0x0000;
    static uint32_t rg_menu_delay = 0;
    uint32_t joystick = rg_input_read_gamepad();
    uint32_t changed = prev_joystick ^ joystick;
    event_t event = {0};

    // Long press on menu will open retro-go's menu if needed, instead of DOOM's.
    // This is still needed to quit (DOOM 2) and for the debug menu. We'll unify that mess soon...
    if (joystick & RG_KEY_MENU)
    {
        if (rg_menu_delay++ == TICRATE / 2)
            rg_gui_game_menu();
    }
    else
    {
        rg_menu_delay = 0;
    }

    if (joystick & RG_KEY_OPTION)
    {
        rg_gui_game_settings_menu();
        // realtic_clock_rate = (app->speedupEnabled + 1) * 100;
    }
    else if (changed)
    {
        for (int i = 0; i < RG_COUNT(keymap); i++)
        {
            if (changed & keymap[i].mask)
            {
                event.type = (joystick & keymap[i].mask) ? ev_keydown : ev_keyup;
                event.data1 = *keymap[i].key;
                D_PostEvent(&event);
            }
        }
    }

    rg_system_tick(get_elapsed_time_since(last_time));
    last_time = get_elapsed_time();
    prev_joystick = joystick;
}

void I_Init(void)
{
    snd_channels = NUM_MIX_CHANNELS;
    snd_samplerate = SAMPLERATE;
    snd_MusicVolume = 15;
    snd_SfxVolume = 15;
    usegamma = rg_settings_get_app_int32(SETTING_GAMMA, 0);
}

static bool screenshot_handler(const char *filename, int width, int height)
{
    Z_FreeTags(PU_CACHE, PU_CACHE); // At this point the heap is usually full. Let's reclaim some!
	return rg_display_save_frame(filename, &update, width, height);
}

static bool save_state_handler(const char *filename)
{
    return false;
}

static bool load_state_handler(const char *filename)
{
    return false;
}

static bool reset_handler(bool hard)
{
    return false;
}

static void settings_handler(void)
{
    dialog_option_t options[] = {
        {100, "Gamma Boost", "0/5", 1, &gamma_update_cb},
        RG_DIALOG_CHOICE_LAST
    };
    rg_gui_dialog("Advanced", options, 0);
}

static void event_handler(int event, void *arg)
{
    if (event == RG_EVENT_SHUTDOWN)
    {
        // DOOM fully fills the internal heap and this causes some shutdown
        // steps to fail so we try to free everything!
        Z_FreeTags(0, PU_MAX);
        rg_audio_set_mute(true);
    }
    return;
}

void app_main()
{
    const rg_emu_proc_t handlers = {
        // .loadState = &load_state_handler,
        // .saveState = &save_state_handler,
        .reset = &reset_handler,
        .screenshot = &screenshot_handler,
        .settings = &settings_handler,
        .event = &event_handler,
    };

    app = rg_system_init(SAMPLERATE, &handlers);
    app->refreshRate = TICRATE;

    update.buffer = rg_alloc(SCREENHEIGHT*SCREENWIDTH, MEM_FAST);
    update.synchronous = true;

    const char *romtype = "-iwad";
    FILE *fp;

    // TO DO: We should probably make prboom detect what we're passing instead
    // and choose which default IWAD to use, if any.
    if ((fp = fopen(app->romPath, "rb")))
    {
        if (fgetc(fp) == 'P')
            romtype = "-file";
        fclose(fp);
    }

    myargv = (const char *[]){"doom", "-save", RG_BASE_PATH_SAVES "/doom", romtype, app->romPath};
    myargc = 5;

    Z_Init();
    D_DoomMain();
}
