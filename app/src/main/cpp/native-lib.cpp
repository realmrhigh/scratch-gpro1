#include <jni.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <cmath> // For std::fabs, fmodf, floor
#include <algorithm> // For std::clamp, std::min, std::transform, std::max
#include <android/log.h>
#include <android/asset_manager_jni.h> // For AAssetManager_fromJava
#include <oboe/Oboe.h>
#include <oboe/Utilities.h> // For oboe::convertToText

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#define APP_TAG "ScratchEmulator"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, APP_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, APP_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, APP_TAG, __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, APP_TAG, __VA_ARGS__)

class AudioEngine;

struct AudioSample {
    std::string filePath;
    std::vector<float> audioData;
    int32_t totalFrames = 0;
    int32_t channels = 0;
    uint32_t sampleRate = 0;
    std::atomic<bool> isPlaying{false};
    std::atomic<bool> loop{false};
    bool playOnceThenLoopSilently = false;
    bool playedOnce = false;
    std::atomic<float> preciseCurrentFrame{0.0f};
    AudioEngine* audioEnginePtr = nullptr;
    std::atomic<bool> useEngineRateForPlayback_{false};

    bool hasExtension(const std::string& path, const std::string& extension);
    bool tryLoadPath(AAssetManager* assetManager, const std::string& currentPathToTry);
    void load(AAssetManager* assetManager, const std::string& basePath, AudioEngine* engine);
    void getAudio(float* outputBuffer, int32_t numOutputFrames, int32_t outputStreamChannels, float effectiveVolume);

    inline float getSampleAt(int32_t frameIndex, int channelIndex) const {
        if (audioData.empty() || totalFrames == 0) return 0.0f;

        int32_t effectiveFrameIndex = frameIndex;
        if (loop.load()) {
            if (totalFrames > 0) {
                effectiveFrameIndex = frameIndex % totalFrames;
                if (effectiveFrameIndex < 0) {
                    effectiveFrameIndex += totalFrames;
                }
            } else {
                effectiveFrameIndex = 0;
            }
        } else {
            effectiveFrameIndex = std::max(0, std::min(frameIndex, totalFrames - 1));
        }

        size_t actualIndex = static_cast<size_t>(effectiveFrameIndex) * channels + (channelIndex % channels);
        if (actualIndex < audioData.size()) {
            return audioData[actualIndex];
        }
        return 0.0f;
    }

    inline float catmullRomInterpolate(float p0, float p1, float p2, float p3, float t) const {
        float t2 = t * t; float t3 = t2 * t;
        return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    }
};

class AudioEngine : public oboe::AudioStreamCallback {
public:
    std::atomic<float> platterTargetPlaybackRate_{1.0f};
    std::atomic<float> scratchSensitivity_{0.17f};
    const float MOVEMENT_THRESHOLD = 0.001f;

    AudioEngine() : appAssetManager_(nullptr), streamSampleRate_(0) {
        ALOGI("AudioEngine default constructor.");
        currentPlatterSampleIndex_.store(0);
        currentMusicTrackIndex_.store(0);
        platterFaderVolume_.store(0.0f);
        generalMusicVolume_.store(0.9f);
        isFingerDownOnPlatter_.store(false);
        platterTargetPlaybackRate_.store(1.0f);
        scratchSensitivity_.store(0.17f);

        platterSamplePaths_ = {"sounds/haahhh", "sounds/sample1", "sounds/sample2"};
        musicTrackPaths_    = {"tracks/trackA", "tracks/trackB"};
        ALOGI("AudioEngine Constructor: Initial scratchSensitivity_ set to %.4f", scratchSensitivity_.load());
    }

    ~AudioEngine() { ALOGI("AudioEngine destructor."); release(); }
    bool init(AAssetManager* mgr);
    void release();
    oboe::Result startStream();
    oboe::Result stopStream();
    void playIntroAndLoopOnPlatterInternal(const std::string& initialBasePath);
    void nextPlatterSampleInternal();
    void playMusicTrackInternal();
    void stopMusicTrackInternal();
    void nextMusicTrackAndPlayInternal();
    void nextMusicTrackAndKeepStateInternal();
    void setPlatterFaderVolumeInternal(float volume);
    void setMusicMasterVolumeInternal(float volume);
    void scratchPlatterActiveInternal(bool isActiveTouch, float angleDeltaOrRateFromViewModel);
    void releasePlatterTouchInternal();
    void setScratchSensitivityInternal(float sensitivity) {
        ALOGI("AudioEngine: Setting scratch sensitivity from JNI to %.4f", sensitivity);
        scratchSensitivity_.store(sensitivity);
        ALOGE("AudioEngine: CONFIRMED scratchSensitivity_ (member) is now %.4f after store", scratchSensitivity_.load());
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) override;
    void onErrorBeforeClose(oboe::AudioStream *stream, oboe::Result error) override;
    void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override;

private:
    std::shared_ptr<oboe::AudioStream> audioStream_;
    AAssetManager* appAssetManager_ = nullptr;
    uint32_t streamSampleRate_ = 0;
    std::unique_ptr<AudioSample> platterAudioSample_;
    std::unique_ptr<AudioSample> musicAudioSample_;
    std::vector<std::string> platterSamplePaths_;
    std::atomic<int> currentPlatterSampleIndex_;
    std::vector<std::string> musicTrackPaths_;
    std::atomic<int> currentMusicTrackIndex_;
    std::atomic<float> platterFaderVolume_;
    std::atomic<float> generalMusicVolume_;
    std::atomic<bool> isFingerDownOnPlatter_;
};

// ... (AudioSample methods: hasExtension, tryLoadPath, load, getAudio - Catmull-Rom version) ...
bool AudioSample::hasExtension(const std::string& path, const std::string& extension) {
    if (path.length() >= extension.length()) {
        std::string lowerFilePath = path;
        std::transform(lowerFilePath.begin(), lowerFilePath.end(), lowerFilePath.begin(),
                       [](unsigned char c){ return std::tolower(c); });
        return (0 == lowerFilePath.compare(lowerFilePath.length() - extension.length(), extension.length(), extension));
    }
    return false;
}

bool AudioSample::tryLoadPath(AAssetManager* assetManager, const std::string& currentPathToTry) {
    audioData.clear(); totalFrames = 0; channels = 0; sampleRate = 0; bool success = false;
    AAsset* asset = AAssetManager_open(assetManager, currentPathToTry.c_str(), AASSET_MODE_BUFFER);
    if (!asset) return false;
    const void* assetBuffer = AAsset_getBuffer(asset);
    size_t assetLength = AAsset_getLength(asset);
    if (!assetBuffer) { AAsset_close(asset); return false; }
    if (hasExtension(currentPathToTry, ".wav")) {
        drwav wav;
        if (drwav_init_memory(&wav, assetBuffer, assetLength, nullptr)) {
            channels = wav.channels; totalFrames = (int32_t)wav.totalPCMFrameCount; sampleRate = wav.sampleRate;
            audioData.resize(totalFrames * channels);
            success = (drwav_read_pcm_frames_f32(&wav, totalFrames, audioData.data()) == totalFrames);
            drwav_uninit(&wav);
        }
    } else if (hasExtension(currentPathToTry, ".mp3")) {
        drmp3_config config; drmp3_uint64 pcmFrameCount;
        float* pPcmFrames = drmp3_open_memory_and_read_pcm_frames_f32(assetBuffer, assetLength, &config, &pcmFrameCount, nullptr);
        if (pPcmFrames) {
            channels = config.channels; sampleRate = config.sampleRate; totalFrames = (int32_t)pcmFrameCount;
            audioData.assign(pPcmFrames, pPcmFrames + (pcmFrameCount * channels));
            drmp3_free(pPcmFrames, nullptr); success = true;
        }
    }
    AAsset_close(asset); return success;
}

void AudioSample::load(AAssetManager* assetManager, const std::string& basePath, AudioEngine* engine) {
    this->audioEnginePtr = engine; ALOGI("AudioSample: Attempting to load base path: %s", basePath.c_str());
    isPlaying.store(false); preciseCurrentFrame.store(0.0f); useEngineRateForPlayback_.store(false);
    playedOnce = false; loop.store(false); playOnceThenLoopSilently = false;
    if (!assetManager) { ALOGE("AudioSample: AssetManager is null for %s!", basePath.c_str()); return; }
    bool loadedSuccessfully = false; std::string successfulPath;
    if (hasExtension(basePath, ".wav") || hasExtension(basePath, ".mp3")) {
        if (tryLoadPath(assetManager, basePath)) { loadedSuccessfully = true; successfulPath = basePath; }
    }
    if (!loadedSuccessfully) {
        std::string pathWithMp3 = basePath + ".mp3";
        if (tryLoadPath(assetManager, pathWithMp3)) { loadedSuccessfully = true; successfulPath = pathWithMp3; }
    }
    if (!loadedSuccessfully) {
        std::string pathWithWav = basePath + ".wav";
        if (tryLoadPath(assetManager, pathWithWav)) { loadedSuccessfully = true; successfulPath = pathWithWav; }
    }
    if (loadedSuccessfully) {
        this->filePath = successfulPath;
        ALOGI("AudioSample: Successfully loaded '%s' (Frames: %d, Ch: %d, SR: %u Hz)", filePath.c_str(), totalFrames, channels, sampleRate);
    } else {
        this->filePath = basePath; ALOGE("AudioSample: Failed to load audio for base '%s'", basePath.c_str());
        audioData.clear(); totalFrames = 0; channels = 0; sampleRate = 0;
    }
}

void AudioSample::getAudio(float* outputBuffer, int32_t numOutputFrames, int32_t outputStreamChannels,
                           float effectiveVolume) {
    if (!isPlaying.load() || audioData.empty() || totalFrames == 0 || channels == 0) {
        return;
    }

    float localPreciseCurrentFrame = preciseCurrentFrame.load();
    float playbackRateToUse = 1.0f;

    if (useEngineRateForPlayback_.load() && audioEnginePtr != nullptr) {
        playbackRateToUse = audioEnginePtr->platterTargetPlaybackRate_.load();
        if (std::fabs(playbackRateToUse) < 0.00001f) {
            return;
        }
    }

    float frameAtLoopStart = localPreciseCurrentFrame;

    for (int i = 0; i < numOutputFrames; ++i) {
        if (localPreciseCurrentFrame >= static_cast<float>(totalFrames) || localPreciseCurrentFrame < 0.0f) {
            if (playOnceThenLoopSilently && !playedOnce) {
                playedOnce = true; localPreciseCurrentFrame = 0.0f;
                if (!loop.load()) loop.store(true);
            } else if (loop.load()) {
                if (totalFrames > 0) {
                    localPreciseCurrentFrame = fmodf(localPreciseCurrentFrame, static_cast<float>(totalFrames));
                    if (localPreciseCurrentFrame < 0.0f) localPreciseCurrentFrame += static_cast<float>(totalFrames);
                } else localPreciseCurrentFrame = 0.0f;
            } else { isPlaying.store(false); break; }
        }
        if (!isPlaying.load()) break;

        float t = localPreciseCurrentFrame - std::floor(localPreciseCurrentFrame);
        int32_t p1_idx = static_cast<int32_t>(std::floor(localPreciseCurrentFrame));

        for (int ch_out = 0; ch_out < outputStreamChannels; ++ch_out) {
            int srcChannel = ch_out % channels;
            float p0 = getSampleAt(p1_idx - 1, srcChannel); float p1 = getSampleAt(p1_idx, srcChannel);
            float p2 = getSampleAt(p1_idx + 1, srcChannel); float p3 = getSampleAt(p1_idx + 2, srcChannel);
            outputBuffer[i * outputStreamChannels + ch_out] += catmullRomInterpolate(p0, p1, p2, p3, t) * effectiveVolume;
        }
        localPreciseCurrentFrame += playbackRateToUse;
    }
    preciseCurrentFrame.store(localPreciseCurrentFrame);
}


// Implementations for AudioEngine methods
bool AudioEngine::init(AAssetManager* mgr) {
    ALOGI("AudioEngine init. this: %p", this);
    appAssetManager_ = mgr;
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Output)
            ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
            ->setSharingMode(oboe::SharingMode::Exclusive)
            ->setFormat(oboe::AudioFormat::Float)
            ->setChannelCount(oboe::ChannelCount::Stereo)
            ->setCallback(this);
    ALOGI("AudioEngine init: Attempting to open stream...");
    oboe::Result result = builder.openStream(audioStream_);
    ALOGI("AudioEngine init: openStream result: %s. audioStream_.get() after open: %p",
          oboe::convertToText(result), (audioStream_ ? audioStream_.get() : nullptr) );
    if (result == oboe::Result::OK && audioStream_ && audioStream_.get() != nullptr) {
        oboe::StreamState testState = audioStream_->getState();
        ALOGI("AudioEngine init: Stream object seems valid. Initial testState: %s", oboe::convertToText(testState));
        streamSampleRate_ = audioStream_->getSampleRate();
        ALOGI("Stream opened successfully: SR=%u, Channels=%d, Format=%s, Current State: %s",
              streamSampleRate_, audioStream_->getChannelCount(),
              oboe::convertToText(audioStream_->getFormat()),
              oboe::convertToText(audioStream_->getState()));
        platterAudioSample_ = std::make_unique<AudioSample>();
        musicAudioSample_ = std::make_unique<AudioSample>();
        ALOGI("AudioEngine init: Platter and Music AudioSample unique_ptrs created.");
        return true;
    } else {
        ALOGE("Failed to open stream OR stream object is invalid. Oboe Result: %s. audioStream_.get(): %p",
              oboe::convertToText(result),
              (audioStream_ ? audioStream_.get() : nullptr) );
        if (audioStream_ && audioStream_.get() != nullptr) {
            ALOGI("Attempting to close potentially problematic stream in init failure path.");
            audioStream_->close();
        }
        audioStream_.reset();
        return false;
    }
}

void AudioEngine::release() {
    ALOGI("AudioEngine release.");
    if (audioStream_) {
        stopStream();
        audioStream_->close();
        audioStream_.reset();
    }
    platterAudioSample_.reset();
    musicAudioSample_.reset();
    ALOGI("AudioEngine release: Platter and Music AudioSample unique_ptrs reset.");
    appAssetManager_ = nullptr;
}

oboe::Result AudioEngine::startStream() {
    ALOGI("AudioEngine::startStream() ENTRY. this: %p, audioStream_.get(): %p", this, (audioStream_ ? audioStream_.get() : nullptr) );
    ALOGI("AudioEngine: Requesting stream start.");
    if (!audioStream_ || !audioStream_.get()) {
        ALOGE("Stream not initialized or invalid for startStream! audioStream_.get(): %p",
              (audioStream_ ? audioStream_.get() : nullptr) );
        return oboe::Result::ErrorNull;
    }
    ALOGI("AudioEngine::startStream() - About to call audioStream_->getState(). audioStream_.get(): %p", audioStream_.get());
    oboe::StreamState currentState = audioStream_->getState();
    ALOGI("AudioEngine: Current stream state before requestStart: %s", oboe::convertToText(currentState));
    if (currentState == oboe::StreamState::Started || currentState == oboe::StreamState::Starting) {
        ALOGW("Stream already started or starting.");
        return oboe::Result::OK;
    }
    if (currentState == oboe::StreamState::Closed || currentState == oboe::StreamState::Disconnected) {
        ALOGE("Stream is already closed or disconnected (%s). Cannot start.", oboe::convertToText(currentState));
        return oboe::Result::ErrorClosed;
    }
    ALOGI("AudioEngine: Calling audioStream_->requestStart()...");
    oboe::Result result = audioStream_->requestStart();
    if (result != oboe::Result::OK) {
        ALOGE("AudioEngine: audioStream_->requestStart() FAILED with result: %s. Current state after attempt: %s",
              oboe::convertToText(result),
              oboe::convertToText(audioStream_->getState()));
    } else {
        ALOGI("AudioEngine: audioStream_->requestStart() SUCCEEDED. Current state after attempt: %s",
              oboe::convertToText(audioStream_->getState()));
    }
    return result;
}

oboe::Result AudioEngine::stopStream() {
    ALOGI("AudioEngine::stopStream() ENTRY. this: %p, audioStream_.get(): %p", this, (audioStream_ ? audioStream_.get() : nullptr) );
    ALOGI("AudioEngine: Requesting stream stop.");
    if (!audioStream_ || !audioStream_.get()) {
        ALOGE("Stream not initialized or invalid for stopStream! audioStream_.get(): %p",
              (audioStream_ ? audioStream_.get() : nullptr) );
        return oboe::Result::ErrorNull;
    }
    ALOGI("AudioEngine::stopStream() - About to call audioStream_->getState(). audioStream_.get(): %p", audioStream_.get());
    oboe::StreamState currentState = audioStream_->getState();
    ALOGI("AudioEngine: Current stream state before requestStop: %s", oboe::convertToText(currentState));
    if (currentState == oboe::StreamState::Stopped || currentState == oboe::StreamState::Stopping) {
        ALOGW("Stream already stopped or stopping.");
        return oboe::Result::OK;
    }
    if (currentState == oboe::StreamState::Closed || currentState == oboe::StreamState::Disconnected) {
        ALOGE("Stream is already closed or disconnected (%s). Cannot stop.", oboe::convertToText(currentState));
        return oboe::Result::ErrorClosed;
    }
    ALOGI("AudioEngine: Calling audioStream_->requestStop()...");
    oboe::Result result = audioStream_->requestStop();
    if (result != oboe::Result::OK) {
        ALOGE("AudioEngine: audioStream_->requestStop() FAILED with result: %s. Current state after attempt: %s",
              oboe::convertToText(result),
              oboe::convertToText(audioStream_->getState()));
    } else {
        ALOGI("AudioEngine: audioStream_->requestStop() SUCCEEDED. Current state after attempt: %s",
              oboe::convertToText(audioStream_->getState()));
    }
    return result;
}

void AudioEngine::playIntroAndLoopOnPlatterInternal(const std::string& initialBasePath) {
    ALOGI("AudioEngine: playIntroAndLoopOnPlatterInternal with base path: %s", initialBasePath.c_str());
    if (!appAssetManager_) { ALOGE("playIntro: appAssetManager_ is null!"); return; }
    if (!platterAudioSample_) { ALOGE("playIntro: platterAudioSample_ is null!"); return; }
    int initialIndex = 0;
    if (!platterSamplePaths_.empty()) {
        auto it = std::find(platterSamplePaths_.begin(), platterSamplePaths_.end(), initialBasePath);
        if (it != platterSamplePaths_.end()) {
            initialIndex = std::distance(platterSamplePaths_.begin(), it);
        } else {
            ALOGW("Initial base path '%s' not in pre-defined platter paths. Using index 0 or adding.", initialBasePath.c_str());
            if (platterSamplePaths_.empty()) {
                platterSamplePaths_.push_back(initialBasePath);
            } else {
                initialIndex = 0;
            }
        }
    } else {
        ALOGI("No platter samples pre-defined. Using '%s' as the first.", initialBasePath.c_str());
        platterSamplePaths_.push_back(initialBasePath);
    }
    currentPlatterSampleIndex_.store(initialIndex);
    std::string basePathToLoad = platterSamplePaths_[currentPlatterSampleIndex_.load()];
    platterAudioSample_->load(appAssetManager_, basePathToLoad, this);
    if (platterAudioSample_->totalFrames > 0) {
        platterAudioSample_->playOnceThenLoopSilently = true;
        platterAudioSample_->playedOnce = false;
        platterAudioSample_->loop.store(false);
        platterAudioSample_->preciseCurrentFrame.store(0.0f);
        platterAudioSample_->isPlaying.store(true);
        platterAudioSample_->useEngineRateForPlayback_.store(false);
        platterTargetPlaybackRate_.store(1.0f);
        setPlatterFaderVolumeInternal(0.0f);
        ALOGI("Intro sample from base '%s' loaded as '%s'. Will play once then loop.", basePathToLoad.c_str(), platterAudioSample_->filePath.c_str());
    } else ALOGE("Failed to load intro sample from base path: %s", basePathToLoad.c_str());
}

void AudioEngine::nextPlatterSampleInternal() {
    ALOGI("AudioEngine: nextPlatterSampleInternal");
    if (!appAssetManager_ || !platterAudioSample_ || platterSamplePaths_.empty()) {
        ALOGE("nextPlatterSample: Readiness check failed (AssetManager: %d, SamplePtr: %d, PathsEmpty: %d)",
              (appAssetManager_ != nullptr), (platterAudioSample_ != nullptr), platterSamplePaths_.empty());
        return;
    }
    int currentIndex = currentPlatterSampleIndex_.load();
    currentIndex = (currentIndex + 1) % platterSamplePaths_.size();
    currentPlatterSampleIndex_.store(currentIndex);
    std::string nextBasePath = platterSamplePaths_[currentIndex];
    ALOGI("Loading next platter sample from base path: %s (index %d)", nextBasePath.c_str(), currentIndex);
    platterAudioSample_->load(appAssetManager_, nextBasePath, this);
    if (platterAudioSample_->totalFrames > 0) {
        platterAudioSample_->loop.store(true);
        platterAudioSample_->playOnceThenLoopSilently = false;
        platterAudioSample_->preciseCurrentFrame.store(0.0f);
        platterAudioSample_->isPlaying.store(true);
        platterAudioSample_->useEngineRateForPlayback_.store(false);
        platterTargetPlaybackRate_.store(1.0f);
        ALOGI("Next platter sample loaded as '%s'", platterAudioSample_->filePath.c_str());
    } else {
        ALOGE("Failed to load next platter sample from base: %s", nextBasePath.c_str());
        if(platterAudioSample_) platterAudioSample_->isPlaying.store(false);
    }
}

void AudioEngine::playMusicTrackInternal() {
    ALOGI("AudioEngine: playMusicTrackInternal called.");
    if (!appAssetManager_) { ALOGE("playMusicTrackInternal: appAssetManager_ is NULL."); return; }
    if (!musicAudioSample_) { ALOGE("playMusicTrackInternal: musicAudioSample_ unique_ptr is NULL."); return; }
    if (musicTrackPaths_.empty()) { ALOGE("playMusicTrackInternal: musicTrackPaths_ vector is EMPTY. Count: %zu", musicTrackPaths_.size()); return; }
    if (currentMusicTrackIndex_.load() < 0 || currentMusicTrackIndex_.load() >= musicTrackPaths_.size()) {
        ALOGE("playMusicTrackInternal: currentMusicTrackIndex_ (%d) out of bounds. Resetting.", currentMusicTrackIndex_.load());
        currentMusicTrackIndex_.store(0);
        if (musicTrackPaths_.empty()) { ALOGE("playMusicTrackInternal: Paths STILL empty."); return; }
    }
    std::string basePathToPlay = musicTrackPaths_[currentMusicTrackIndex_.load()];
    ALOGI("Attempting to play music track from base: %s (index %d)", basePathToPlay.c_str(), currentMusicTrackIndex_.load());
    if (musicAudioSample_->isPlaying.load() &&
        (musicAudioSample_->filePath == basePathToPlay + ".mp3" || musicAudioSample_->filePath == basePathToPlay + ".wav" || musicAudioSample_->filePath == basePathToPlay) ) {
        ALOGI("Music track from base '%s' (resolved to '%s') is already playing. Restarting.", basePathToPlay.c_str(), musicAudioSample_->filePath.c_str());
        musicAudioSample_->preciseCurrentFrame.store(0.0f);
        return;
    }
    musicAudioSample_->load(appAssetManager_, basePathToPlay, this);
    if (musicAudioSample_->totalFrames > 0) {
        musicAudioSample_->loop.store(false);
        musicAudioSample_->playOnceThenLoopSilently = false;
        musicAudioSample_->preciseCurrentFrame.store(0.0f);
        musicAudioSample_->isPlaying.store(true);
        ALOGI("Playing music track loaded as '%s'", musicAudioSample_->filePath.c_str());
    } else {
        ALOGE("Failed to load music track for playback from base: %s", basePathToPlay.c_str());
        if(musicAudioSample_) musicAudioSample_->isPlaying.store(false);
    }
}

void AudioEngine::stopMusicTrackInternal() {
    ALOGI("AudioEngine: stopMusicTrackInternal");
    if (musicAudioSample_) {
        musicAudioSample_->isPlaying.store(false);
        ALOGI("Stopped music track: %s", musicAudioSample_->filePath.c_str());
    } else {
        ALOGW("stopMusicTrackInternal: musicAudioSample_ is null.");
    }
}

void AudioEngine::nextMusicTrackAndPlayInternal() {
    ALOGI("AudioEngine: nextMusicTrackAndPlayInternal");
    if (musicTrackPaths_.empty()) { ALOGW("No music tracks in list. Count: %zu", musicTrackPaths_.size()); return; }
    int currentIndex = currentMusicTrackIndex_.load();
    currentIndex = (currentIndex + 1) % musicTrackPaths_.size();
    currentMusicTrackIndex_.store(currentIndex);
    ALOGI("Advanced to next music track (and play): index %d", currentIndex);
    playMusicTrackInternal();
}

void AudioEngine::nextMusicTrackAndKeepStateInternal() {
    ALOGI("AudioEngine: nextMusicTrackAndKeepStateInternal");
    if (musicTrackPaths_.empty()) { ALOGW("No music tracks in list. Count: %zu", musicTrackPaths_.size()); return; }
    bool wasPlaying = musicAudioSample_ && musicAudioSample_->isPlaying.load();
    int currentIndex = currentMusicTrackIndex_.load();
    currentIndex = (currentIndex + 1) % musicTrackPaths_.size();
    currentMusicTrackIndex_.store(currentIndex);
    std::string nextTrackBasePath = musicTrackPaths_[currentIndex];
    ALOGI("Advanced to next music track (keep state), base: %s (index %d). Was playing: %d", nextTrackBasePath.c_str(), currentIndex, wasPlaying);
    if (!musicAudioSample_) {
        ALOGE("nextMusicTrackAndKeepStateInternal: musicAudioSample_ is null!");
        musicAudioSample_ = std::make_unique<AudioSample>();
    }
    musicAudioSample_->load(appAssetManager_, nextTrackBasePath, this);
    if (musicAudioSample_->totalFrames > 0) {
        if (wasPlaying) {
            musicAudioSample_->preciseCurrentFrame.store(0.0f);
            musicAudioSample_->isPlaying.store(true);
            ALOGI("Resuming playback with new track loaded as '%s'", musicAudioSample_->filePath.c_str());
        } else {
            musicAudioSample_->isPlaying.store(false);
            ALOGI("New track loaded as '%s', was not playing.", musicAudioSample_->filePath.c_str());
        }
    } else {
        ALOGE("Failed to load track from base '%s'.", nextTrackBasePath.c_str());
        if(musicAudioSample_) musicAudioSample_->isPlaying.store(false);
    }
}

void AudioEngine::setPlatterFaderVolumeInternal(float volume) {
    float clampedVolume = std::clamp(volume, 0.0f, 1.0f);
    platterFaderVolume_.store(clampedVolume);
    ALOGI("AudioEngine: Platter Fader Volume set to %f", clampedVolume);
}

void AudioEngine::setMusicMasterVolumeInternal(float volume) {
    float clampedVolume = std::clamp(volume, 0.0f, 1.0f);
    generalMusicVolume_.store(clampedVolume);
    ALOGI("AudioEngine: Music Master Volume set to %f", clampedVolume);
}

// MODIFIED: Logic to handle coasting rates and isPlaying state
void AudioEngine::scratchPlatterActiveInternal(bool isActiveTouch, float angleDeltaOrRateFromViewModel) {
    isFingerDownOnPlatter_.store(isActiveTouch);

    if (!platterAudioSample_ || platterAudioSample_->totalFrames == 0) {
        if(isActiveTouch) ALOGW("ScratchPlatterActive: Attempt on unloaded/invalid platter sample.");
        if(platterAudioSample_) platterAudioSample_->useEngineRateForPlayback_.store(false);
        return;
    }

    platterAudioSample_->useEngineRateForPlayback_.store(true);
    float targetAudioRate;
    float currentSensitivity = scratchSensitivity_.load();

    ALOGE("ScratchPlatterActive INPUT: isActiveTouch: %d, angleDeltaOrRateFromVM: %.4f, ACTUAL currentSensitivity_ LOADED: %.4f",
          isActiveTouch, angleDeltaOrRateFromViewModel, currentSensitivity);

    if (isActiveTouch) { // Finger is actively interacting (touch down or drag)
        if (std::fabs(angleDeltaOrRateFromViewModel) > MOVEMENT_THRESHOLD) { // Finger is moving
            targetAudioRate = angleDeltaOrRateFromViewModel * currentSensitivity;
            targetAudioRate = std::clamp(targetAudioRate, -4.0f, 4.0f);
            if (!platterAudioSample_->isPlaying.load()) {
                ALOGV("Scratch Move: angleDelta: %.2f, currentSensitivity: %.4f, target rate: %.4f. Setting isPlaying=true.",
                      angleDeltaOrRateFromViewModel, currentSensitivity, targetAudioRate);
                platterAudioSample_->isPlaying.store(true);
            } else {
                ALOGV("Scratch Move: angleDelta: %.2f, currentSensitivity: %.4f, target rate: %.4f. isPlaying already true.",
                      angleDeltaOrRateFromViewModel, currentSensitivity, targetAudioRate);
            }
        } else { // Finger is down, but not moving (angleDeltaOrRateFromViewModel is 0 from onPlatterTouchDown)
            targetAudioRate = 0.0f;
            if (platterAudioSample_->isPlaying.load()) {
                ALOGV("Scratch Hold: No significant movement. Setting isPlaying=false. Target rate: %.4f", targetAudioRate);
                platterAudioSample_->isPlaying.store(false);
            } else {
                ALOGV("Scratch Hold: Already silent or becoming silent. Target rate: %.4f", targetAudioRate);
            }
        }
    } else { // Finger is NOT on platter (isActiveTouch is false), ViewModel is sending a coasting/damped rate
        targetAudioRate = angleDeltaOrRateFromViewModel; // This is the desired normalized audio rate
        ALOGV("Coasting/NotTouched: Received rate from ViewModel: %.4f", targetAudioRate);

        // If coasting rate is non-zero, audio should be playing.
        // If coasting rate becomes zero (vinyl has stopped), audio should stop.
        if (std::fabs(targetAudioRate) > 0.00001f) {
            if(!platterAudioSample_->isPlaying.load()) {
                ALOGV("Coasting: Rate: %.4f. Setting isPlaying=true.", targetAudioRate);
                platterAudioSample_->isPlaying.store(true);
            }
        } else {
            if(platterAudioSample_->isPlaying.load()) {
                ALOGV("Coasting: Rate is effectively zero (%.4f). Setting isPlaying=false.", targetAudioRate);
                platterAudioSample_->isPlaying.store(false);
            }
        }
    }
    platterTargetPlaybackRate_.store(targetAudioRate);
    ALOGV("scratchPlatterActiveInternal END: Final platterTargetPlaybackRate_ set to: %.4f, isPlaying: %d",
          targetAudioRate, (platterAudioSample_ ? platterAudioSample_->isPlaying.load() : -1));
}

void AudioEngine::releasePlatterTouchInternal() {
    ALOGI("AudioEngine: releasePlatterTouchInternal");
    isFingerDownOnPlatter_.store(false);
    if (platterAudioSample_) {
        // ViewModel's animation loop will now continuously call scratchPlatterActiveInternal
        // with isActiveTouch = false and the current coasting rate.
        // The isPlaying state will be managed by those calls.
        // We ensure useEngineRateForPlayback_ is true so AudioSample uses the rates from platterTargetPlaybackRate_.
        platterAudioSample_->useEngineRateForPlayback_.store(true);
        ALOGI("AudioEngine: Finger up. ViewModel controls coasting rate. Sample will use engine rate. Current platterTargetPlaybackRate_: %.4f", platterTargetPlaybackRate_.load());
    }
}

oboe::DataCallbackResult AudioEngine::onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) {
    auto* outputBuffer = static_cast<float*>(audioData);
    const int32_t channelCount = stream->getChannelCount();
    memset(outputBuffer, 0, numFrames * channelCount * sizeof(float));

    if (platterAudioSample_) {
        float platterVol = platterFaderVolume_.load();
        // Only apply generalMusicVolume for intro if not actively being touched AND not under engine rate control (i.e., initial normal playback of intro)
        if (platterAudioSample_->playOnceThenLoopSilently &&
            !platterAudioSample_->playedOnce &&
            !isFingerDownOnPlatter_.load() &&
            !platterAudioSample_->useEngineRateForPlayback_.load()
                ) {
            platterVol = generalMusicVolume_.load();
        }
        platterAudioSample_->getAudio(outputBuffer, numFrames, channelCount, platterVol);
    }

    if (musicAudioSample_ && musicAudioSample_->isPlaying.load()) {
        musicAudioSample_->getAudio(outputBuffer, numFrames, channelCount, generalMusicVolume_.load());
    }
    return oboe::DataCallbackResult::Continue;
}

void AudioEngine::onErrorBeforeClose(oboe::AudioStream *stream, oboe::Result error) { ALOGE("Oboe error before close: %s", oboe::convertToText(error)); }
void AudioEngine::onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) { ALOGE("Oboe error after close: %s", oboe::convertToText(error)); }

std::unique_ptr<AudioEngine> gAudioEngine = nullptr;

extern "C" {

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_initAudioEngine(JNIEnv* env, jobject /* this */, jobject assetManager) {
    ALOGI("JNI: initAudioEngine called");
    if (!gAudioEngine) {
        gAudioEngine = std::make_unique<AudioEngine>();
    } else {
        ALOGW("JNI: AudioEngine already exists during init. Re-initializing.");
        gAudioEngine->release();
        gAudioEngine = std::make_unique<AudioEngine>();
    }
    AAssetManager* nativeAssetManager = AAssetManager_fromJava(env, assetManager);
    if (!nativeAssetManager) { ALOGE("JNI: Failed to get native AssetManager!"); return; }

    if (!gAudioEngine->init(nativeAssetManager)) {
        ALOGE("JNI: Failed to initialize AudioEngine. gAudioEngine will be reset.");
        gAudioEngine.reset();
    } else {
        ALOGI("JNI: AudioEngine initialized successfully.");
    }
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_releaseAudioEngine(JNIEnv* env, jobject /* this */) {
    ALOGI("JNI: releaseAudioEngine called");
    if (gAudioEngine) {
        gAudioEngine->release();
        gAudioEngine.reset();
        ALOGI("JNI: AudioEngine released and reset.");
    } else {
        ALOGW("JNI: releaseAudioEngine called but gAudioEngine was already null.");
    }
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_startPlayback(JNIEnv* env, jobject /* this */) {
    ALOGI("JNI: startPlayback called (starts Oboe stream)");
    if (gAudioEngine) {
        oboe::Result result = gAudioEngine->startStream();
        if (result != oboe::Result::OK) {
            ALOGE("JNI: gAudioEngine->startStream() FAILED with result: %s", oboe::convertToText(result));
        } else {
            ALOGI("JNI: gAudioEngine->startStream() SUCCEEDED.");
        }
    } else {
        ALOGE("JNI: AudioEngine not initialized for startPlayback.");
    }
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_stopPlayback(JNIEnv* env, jobject /* this */) {
    ALOGI("JNI: stopPlayback called (stops Oboe stream)");
    if (gAudioEngine) {
        oboe::Result result = gAudioEngine->stopStream();
        if (result != oboe::Result::OK) ALOGE("JNI: AudioEngine stopStream failed: %s", oboe::convertToText(result));
    } else ALOGW("JNI: AudioEngine not initialized for stopPlayback (or already released).");
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_playIntroAndLoopOnPlatter(JNIEnv *env, jobject /* this */, jobject assetManager, jstring filePathJ) {
    ALOGI("JNI: playIntroAndLoopOnPlatter called");
    if (!gAudioEngine) { ALOGE("JNI: AudioEngine not initialized for playIntroAndLoopOnPlatter."); return; }
    const char *filePathNative = env->GetStringUTFChars(filePathJ, nullptr);
    if (!filePathNative) { ALOGE("JNI: Failed to get filePath string for intro."); return; }
    std::string filePathStr(filePathNative);
    env->ReleaseStringUTFChars(filePathJ, filePathNative);
    ALOGI("JNI: playIntroAndLoopOnPlatter with path: %s", filePathStr.c_str());
    gAudioEngine->playIntroAndLoopOnPlatterInternal(filePathStr);
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_nextPlatterSample(JNIEnv* env, jobject /* this */) {
    ALOGI("JNI: nextPlatterSample called");
    if (gAudioEngine) gAudioEngine->nextPlatterSampleInternal();
    else ALOGE("JNI: AudioEngine not initialized for nextPlatterSample.");
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_loadUserPlatterSample(JNIEnv *env, jobject, jstring filePathJ) {
    ALOGI("JNI: loadUserPlatterSample (Placeholder/Not Implemented)");
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_playMusicTrack(JNIEnv* env, jobject /* this */) {
    ALOGI("JNI: playMusicTrack called");
    if (gAudioEngine) gAudioEngine->playMusicTrackInternal();
    else ALOGE("JNI: AudioEngine not initialized for playMusicTrack.");
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_stopMusicTrack(JNIEnv* env, jobject /* this */) {
    ALOGI("JNI: stopMusicTrack called");
    if (gAudioEngine) gAudioEngine->stopMusicTrackInternal();
    else ALOGE("JNI: AudioEngine not initialized for stopMusicTrack.");
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_nextMusicTrackAndPlay(JNIEnv* env, jobject /* this */) {
    ALOGI("JNI: nextMusicTrackAndPlay called");
    if (gAudioEngine) gAudioEngine->nextMusicTrackAndPlayInternal();
    else ALOGE("JNI: AudioEngine not initialized for nextMusicTrackAndPlay.");
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_nextMusicTrackAndKeepState(JNIEnv* env, jobject /* this */) {
    ALOGI("JNI: nextMusicTrackAndKeepState called");
    if (gAudioEngine) gAudioEngine->nextMusicTrackAndKeepStateInternal();
    else ALOGE("JNI: AudioEngine not initialized for nextMusicTrackAndKeepState.");
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_loadUserMusicTrack(JNIEnv *env, jobject, jstring filePathJ) {
    ALOGI("JNI: loadUserMusicTrack (Placeholder/Not Implemented)");
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_setPlatterFaderVolume(JNIEnv *env, jobject /* this */, jfloat volume) {
    ALOGI("JNI: setPlatterFaderVolume called with volume: %.2f", volume);
    if (gAudioEngine) gAudioEngine->setPlatterFaderVolumeInternal(static_cast<float>(volume));
    else ALOGW("JNI: AudioEngine not initialized for setPlatterFaderVolume.");
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_setMusicMasterVolume(JNIEnv *env, jobject /* this */, jfloat volume) {
    ALOGI("JNI: setMusicMasterVolume called with volume: %.2f", volume);
    if (gAudioEngine) gAudioEngine->setMusicMasterVolumeInternal(static_cast<float>(volume));
    else ALOGW("JNI: AudioEngine not initialized for setMusicMasterVolume.");
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_scratchPlatterActive(JNIEnv *env, jobject /* this */, jboolean isActive, jfloat angleDeltaOrRate) {
    ALOGI("JNI: scratchPlatterActive called - isActive: %d, angleDeltaOrRate: %.4f", isActive, angleDeltaOrRate);
    if (gAudioEngine) {
        gAudioEngine->scratchPlatterActiveInternal(static_cast<bool>(isActive), static_cast<float>(angleDeltaOrRate));
    } else {
        ALOGE("JNI: AudioEngine not initialized for scratchPlatterActive.");
    }
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_releasePlatterTouch(JNIEnv *env, jobject /* this */) {
    ALOGI("JNI: releasePlatterTouch called");
    if (gAudioEngine) {
        gAudioEngine->releasePlatterTouchInternal();
    } else {
        ALOGW("JNI: AudioEngine not initialized for releasePlatterTouch (or already released).");
    }
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_setScratchSensitivity(JNIEnv *env, jobject /* this */, jfloat sensitivity) {
    ALOGI("JNI: setScratchSensitivity called with sensitivity: %.4f", sensitivity);
    if (gAudioEngine) {
        gAudioEngine->setScratchSensitivityInternal(static_cast<float>(sensitivity));
    } else {
        ALOGE("JNI: AudioEngine not initialized for setScratchSensitivity.");
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_fromscratch_MainActivity_stringFromJNI(JNIEnv* env, jobject /* this */) {
    ALOGI("JNI: stringFromJNI called!");
    std::string hello = "Hello from C++ (Consolidated native-lib.cpp)";
    if (gAudioEngine && gAudioEngine.get() != nullptr) {
        hello += " - AudioEngine Initialized and valid.";
    } else {
        hello += " - AudioEngine IS NULL or NOT Initialized.";
    }
    return env->NewStringUTF(hello.c_str());
}
} // extern "C"
