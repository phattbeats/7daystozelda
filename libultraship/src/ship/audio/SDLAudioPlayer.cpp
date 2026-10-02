#include "ship/audio/SDLAudioPlayer.h"
#include <spdlog/spdlog.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// Phones and tablets: slower main threads, so they get the deeper buffer.
EM_JS(int, lus_web_is_mobile, (), {
    return (/Android|iPhone|iPad|iPod|Mobile/i.test(navigator.userAgent) ||
            (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1)) ? 1 : 0;
});
#endif

namespace Ship {

SDLAudioPlayer::~SDLAudioPlayer() {
    SPDLOG_TRACE("destruct SDL audio player");
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool SDLAudioPlayer::DoInit() {
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        SPDLOG_ERROR("SDL init error: %s\n", SDL_GetError());
        return false;
    }
    mNumChannels = this->GetAudioChannels() == AudioChannelsSetting::audioSurround51 ? 6 : 2;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = this->GetSampleRate();
    want.format = AUDIO_S16SYS;
    want.channels = mNumChannels;
#ifdef __EMSCRIPTEN__
    // In the browser, SDL feeds Web Audio from a ScriptProcessorNode whose
    // callback runs on the main thread, once per `samples` frames. At 1024
    // frames (~21 ms) any game frame that runs long starves it and the audio
    // crackles. 2048 (~43 ms) on desktop, 4096 (~85 ms) on phones.
    const bool mobile = lus_web_is_mobile() != 0;
    want.samples = mobile ? 4096 : 2048;
    // Keep ~100 ms (phones ~130 ms) of mixed audio queued so a slow frame or a
    // GC pause drains the queue instead of the speakers.
    this->SetDesiredBuffered(this->GetSampleRate() * (mobile ? 130 : 100) / 1000);
#else
    want.samples = this->GetSampleLength();
#endif
    want.callback = NULL;
    mDevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (mDevice == 0) {
        SPDLOG_ERROR("SDL_OpenAudio error: {}", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(mDevice, 0);
    return true;
}

int SDLAudioPlayer::Buffered() {
    return SDL_GetQueuedAudioSize(mDevice) / (sizeof(int16_t) * mNumChannels);
}

void SDLAudioPlayer::Play(const uint8_t* buf, size_t len) {
#ifdef __EMSCRIPTEN__
    // The browser target queue is deeper (see DoInit), and after a hitch the
    // game catches up several ticks at once; a 6000-sample cap would drop whole
    // chunks of that catch-up audio, which is its own audible skip.
    const int cap = this->GetDesiredBuffered() * 3;
#else
    const int cap = 6000;
#endif
    if (Buffered() < cap) {
        // Don't fill the audio buffer too much in case this happens
        SDL_QueueAudio(mDevice, buf, len);
    }
}
} // namespace Ship
