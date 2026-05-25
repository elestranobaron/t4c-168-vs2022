#include "audio/T4CGameMusic.h"

#include "audio/T4CGameMusicZone.h"
#include "game/TncDataPaths.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <string>

namespace {

struct MusicStreamState {
    SDL_AudioStream *stream{nullptr};
    Uint8 *wavData{nullptr};
    Uint32 wavLen{0};
    size_t readPos{0};
    SDL_AudioSpec spec{};
};

std::mutex g_musicMutex;
MusicStreamState g_music{};
int g_currentTrackId = -1;
int g_oldTrackId = -1;
float g_volumeGain = 0.75f;
bool g_audioReady = false;

void FreeWav(MusicStreamState &st) {
    if (st.wavData) {
        SDL_free(st.wavData);
        st.wavData = nullptr;
    }
    st.wavLen = 0;
    st.readPos = 0;
}

void CloseStream(MusicStreamState &st) {
    if (st.stream) {
        SDL_DestroyAudioStream(st.stream);
        st.stream = nullptr;
    }
}

void SDLCALL MusicStreamCallback(void *userdata, SDL_AudioStream *stream, int additional_amount,
                                 int /*total_amount*/) {
    auto *st = static_cast<MusicStreamState *>(userdata);
    if (!st || !st->wavData || additional_amount <= 0) {
        return;
    }

    int remaining = additional_amount;
    while (remaining > 0) {
        if (st->readPos >= st->wavLen) {
            st->readPos = 0;
        }
        const int avail = static_cast<int>(st->wavLen - st->readPos);
        if (avail <= 0) {
            break;
        }
        const int chunk = std::min(remaining, avail);
        if (!SDL_PutAudioStreamData(stream, st->wavData + st->readPos, chunk)) {
            SDL_Log("[GameMusic] SDL_PutAudioStreamData: %s", SDL_GetError());
            break;
        }
        st->readPos += static_cast<size_t>(chunk);
        remaining -= chunk;
    }
}

std::string SonsWavPath(const char *baseName) {
    if (!baseName || !baseName[0]) {
        return {};
    }
    std::string path = T4CDataPath("sons");
    if (path.empty()) {
        return {};
    }
    path += '/';
    path += baseName;
    path += ".wav";
    return path;
}

bool LoadWavFile(const std::string &path, MusicStreamState &st) {
    FreeWav(st);
    if (path.empty()) {
        return false;
    }
    if (!SDL_LoadWAV(path.c_str(), &st.spec, &st.wavData, &st.wavLen)) {
        SDL_Log("[GameMusic] SDL_LoadWAV echoue: %s (%s)", path.c_str(), SDL_GetError());
        return false;
    }
    st.readPos = 0;
    return true;
}

bool OpenMusicStream(MusicStreamState &st) {
    if (!st.wavData || st.wavLen == 0) {
        return false;
    }
    st.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &st.spec,
                                          MusicStreamCallback, &st);
    if (!st.stream) {
        SDL_Log("[GameMusic] SDL_OpenAudioDeviceStream: %s", SDL_GetError());
        return false;
    }
    SDL_SetAudioStreamGain(st.stream, g_volumeGain);
    if (!SDL_ResumeAudioStreamDevice(st.stream)) {
        SDL_Log("[GameMusic] SDL_ResumeAudioStreamDevice: %s", SDL_GetError());
        CloseStream(st);
        return false;
    }
    return true;
}

void PlayTrackId(int trackId) {
    CloseStream(g_music);

    if (trackId == T4C_MUSIC_SILENCE) {
        FreeWav(g_music);
        g_currentTrackId = T4C_MUSIC_SILENCE;
        SDL_Log("[GameMusic] silence");
        return;
    }

    const char *baseName = T4CGameMusicTrackBaseName(trackId);
    if (!baseName) {
        SDL_Log("[GameMusic] piste inconnue id=%d", trackId);
        return;
    }

    const std::string path = SonsWavPath(baseName);
    if (!LoadWavFile(path, g_music)) {
        return;
    }
    if (!OpenMusicStream(g_music)) {
        FreeWav(g_music);
        return;
    }

    g_currentTrackId = trackId;
    SDL_Log("[GameMusic] lecture %s", path.c_str());
}

}  // namespace

bool T4CGameMusic::Init() {
    std::lock_guard<std::mutex> lock(g_musicMutex);
    if (g_audioReady) {
        return true;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("[GameMusic] SDL_INIT_AUDIO: %s", SDL_GetError());
        return false;
    }
    g_audioReady = true;
    g_currentTrackId = -1;
    g_oldTrackId = -1;
    SDL_Log("[GameMusic] init OK");
    return true;
}

void T4CGameMusic::Shutdown() {
    std::lock_guard<std::mutex> lock(g_musicMutex);
    CloseStream(g_music);
    FreeWav(g_music);
    g_currentTrackId = -1;
    g_oldTrackId = -1;
    if (g_audioReady) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        g_audioReady = false;
    }
}

void T4CGameMusic::StartCharacterSelect() {
    std::lock_guard<std::mutex> lock(g_musicMutex);
    if (!g_audioReady) {
        return;
    }
    g_oldTrackId = -1;
    PlayTrackId(T4C_MUSIC_SADNESS);
    g_oldTrackId = g_currentTrackId;
}

void T4CGameMusic::LoadNewSound(const std::uint16_t world, const std::uint32_t x, const std::uint32_t y,
                                const std::uint32_t playerLevel) {
    std::lock_guard<std::mutex> lock(g_musicMutex);
    if (!g_audioReady) {
        return;
    }

    const int track = T4CGameMusicPickTrack(world, x, y, static_cast<int>(playerLevel));
    if (track == g_oldTrackId) {
        return;
    }

    g_oldTrackId = track;
    PlayTrackId(track);
}

void T4CGameMusic::Stop() {
    std::lock_guard<std::mutex> lock(g_musicMutex);
    CloseStream(g_music);
    FreeWav(g_music);
    g_currentTrackId = -1;
    g_oldTrackId = -1;
}

void T4CGameMusic::Reset() {
    std::lock_guard<std::mutex> lock(g_musicMutex);
    g_oldTrackId = -1;
}

void T4CGameMusic::SetVolume(const float gain0to1) {
    std::lock_guard<std::mutex> lock(g_musicMutex);
    g_volumeGain = std::clamp(gain0to1, 0.0f, 1.0f);
    if (g_music.stream) {
        SDL_SetAudioStreamGain(g_music.stream, g_volumeGain);
    }
}
