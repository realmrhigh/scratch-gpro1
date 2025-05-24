#include <jni.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <cmath> // For std::fabs, fmodf, floor, std::cyl_bessel_i (potentially with C++17, but provide fallback)
#include <algorithm> // For std::clamp, std::min, std::transform, std::max
#include <android/log.h>

// Define M_PI if not already defined (common in cmath but not guaranteed by standard before C++20)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Sinc Interpolation Parameters
constexpr int NUM_TAPS = 16; // Number of points for interpolation
constexpr int SUBDIVISION_STEPS = 1024; // Number of fractional offsets to pre-calculate
constexpr double KAISER_BETA = 6.0;
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

    // Sinc table
    static std::vector<std::vector<float>> sincTable;
    static bool sincTableInitialized;
    static void precalculateSincTable();
    static double bessel_i0_approx(double x);
    static double kaiserWindow(double n_rel, double N_total_taps, double beta);


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

    // inline float catmullRomInterpolate(float p0, float p1, float p2, float p3, float t) const {
    //     float t2 = t * t; float t3 = t2 * t;
    //     return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
    // }
};
// Static member initialization
std::vector<std::vector<float>> AudioSample::sincTable;
bool AudioSample::sincTableInitialized = false;

// Bessel function I0 approximation - using a common polynomial approximation
// Valid for -3.75 <= x <= 3.75. For Kaiser, argument to I0 is beta * sqrt(1 - (term)^2), term is [-1,1]
// So argument is [0, beta]. If beta is e.g. 6, this range is fine.
// For x > 3.75, another approximation or asymptotic series would be needed, but for typical beta values, this is often sufficient.
// Let's use a more general one from Numerical Recipes (approximation for I0(x))
double AudioSample::bessel_i0_approx(double x) {
    double ax = std::abs(x);
    if (ax < 3.75) {
        double y = x / 3.75;
        y *= y;
        return 1.0 + y * (3.5156229 + y * (3.0899424 + y * (1.2067492 + y * (0.2659732 + y * (0.0360768 + y * 0.0045813)))));
    } else {
        double y = 3.75 / ax;
        return (std::exp(ax) / std::sqrt(ax)) * (0.39894228 + y * (0.01328592 + y * (0.00225319 + y * (-0.00157565 + y * (0.00916281 + y * (-0.02057706 + y * (0.02635537 + y * (-0.01647633 + y * 0.00392377))))))));
    }
}

// n_rel: current sample index relative to the window center: 0 for center, +/- (N/2 -1) for edges
// N_total_taps: total number of taps in the window
double AudioSample::kaiserWindow(double n_rel_to_center, double N_total_taps, double beta) {
    if (std::abs(n_rel_to_center) > (N_total_taps / 2.0 - 0.5) && N_total_taps > 1) { // check if n is outside the window span for N>1
         // Should not happen if sincPoint is correctly calculated relative to window span
        return 0.0; // Outside the window
    }
    // Argument for Kaiser window: (2.0 * n_abs_from_zero_indexed_start / (N_total_taps - 1)) - 1.0
    // where n_abs_from_zero_indexed_start goes from 0 to N_total_taps-1
    // If n_rel_to_center is - (N/2 - 1) ... 0 ... (N/2 - 1)
    // then n_zero_indexed = n_rel_to_center + (N/2 -1)
    // (2.0 * (n_rel_to_center + (N_total_taps/2.0 -1.0) ) / (N_total_taps -1.0) ) - 1.0
    // This term inside sqrt: ( (2.0*n_idx_from_start) / (N-1) ) - 1.0; where n_idx_from_start is 0 to N-1
    // Let's use a simpler formulation: term = n_rel_to_center / (N_total_taps/2.0)
    // This makes 'term' go from -1 to 1 across the main lobe (approx)
    double term_val_for_bessel_arg;
    if (N_total_taps <= 1) term_val_for_bessel_arg = 0.0; // Single tap window is always 1.0
    else term_val_for_bessel_arg = (2.0 * (n_rel_to_center + (N_total_taps/2.0 - 0.5)) / (N_total_taps - 1.0)) - 1.0;


    double val_inside_sqrt = 1.0 - term_val_for_bessel_arg * term_val_for_bessel_arg;
    if (val_inside_sqrt < 0) val_inside_sqrt = 0; // Clamp due to precision

    return bessel_i0_approx(beta * std::sqrt(val_inside_sqrt)) / bessel_i0_approx(beta);
}


void AudioSample::precalculateSincTable() {
    if (sincTableInitialized) return;

    sincTable.resize(SUBDIVISION_STEPS, std::vector<float>(NUM_TAPS));
    double I0_beta = bessel_i0_approx(KAISER_BETA); // Denominator for Kaiser window

    for (int j = 0; j < SUBDIVISION_STEPS; ++j) {
        double fractionalOffset = static_cast<double>(j) / SUBDIVISION_STEPS;

        float sumCoeffs = 0.0f; // For normalization

        for (int i = 0; i < NUM_TAPS; ++i) {
            // sincPoint: distance from the tap 'i' to the desired interpolation point (fractionalOffset)
            // The center of the NUM_TAPS samples is between tap NUM_TAPS/2 - 1 and NUM_TAPS/2.
            // We want the filter kernel to be centered such that when fractionalOffset is 0,
            // the interpolated point corresponds to the sample at index (NUM_TAPS/2 - 1) in the input kernel.
            // Or, if we consider the "ideal" sample to be at `fractionalOffset` relative to `input_kernel[NUM_TAPS/2 -1]`.
            // Let the current tap be `i`. Its position relative to the *start of the window* is `i`.
            // The point we are interpolating *to* is `(NUM_TAPS/2 - 1) + fractionalOffset`.
            // So, distance from tap `i` to this point is `i - ((NUM_TAPS/2 - 1) + fractionalOffset)`.
            // Or, `( (double)i - (NUM_TAPS/2.0 - 1.0) ) - fractionalOffset` seems common.
            // This makes `sincPoint` centered around `fractionalOffset`.
            // If `i` is `NUM_TAPS/2 -1`, then `sincPoint = -fractionalOffset`.
            // If `i` is `NUM_TAPS/2`, then `sincPoint = 1 - fractionalOffset`.
            double sincPoint = (static_cast<double>(i) - (NUM_TAPS / 2.0 - 1.0)) - fractionalOffset;

            double sincValue;
            if (std::abs(sincPoint) < 1e-9) { // Check for sincPoint == 0
                sincValue = 1.0;
            } else {
                sincValue = std::sin(M_PI * sincPoint) / (M_PI * sincPoint);
            }

            // For Kaiser window, 'n_rel' is distance from center of the window.
            // Window is indexed 0 to NUM_TAPS-1. Center is at (NUM_TAPS-1)/2.0.
            // So, n_rel = i - (NUM_TAPS-1)/2.0
            double kaiser_n_rel = static_cast<double>(i) - (NUM_TAPS - 1.0) / 2.0;
            double windowValue = kaiserWindow(kaiser_n_rel, NUM_TAPS, KAISER_BETA);
            // The original kaiserWindow helper used n_rel_to_center directly, this is fine.
            // double windowValue = kaiserWindow( (double)i - (NUM_TAPS/2.0 -1.0) , NUM_TAPS, KAISER_BETA); // This was less clear

            sincTable[j][i] = static_cast<float>(sincValue * windowValue);
            sumCoeffs += sincTable[j][i];
        }

        // Normalize coefficients to sum to 1.0 to ensure gain is preserved
        if (std::abs(sumCoeffs) > 1e-6) { // Avoid division by zero if all coeffs are zero
            for (int i = 0; i < NUM_TAPS; ++i) {
                sincTable[j][i] /= sumCoeffs;
            }
        }
    }
    sincTableInitialized = true;
    ALOGI("Sinc table precalculated: %d steps, %d taps. Beta: %f", SUBDIVISION_STEPS, NUM_TAPS, KAISER_BETA);
}


class AudioEngine : public oboe::AudioStreamCallback {
public:
    std::atomic<float> platterTargetPlaybackRate_{1.0f};
    std::atomic<float> scratchSensitivity_{0.17f};
    const float MOVEMENT_THRESHOLD = 0.001f;
    float degreesPerFrameForUnityRate_ = 2.5f; // Default, will be updated from Kotlin

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
    void setDegreesPerFrameForUnityRateInternal(float degrees) {
        if (degrees > 0.0f) { // Basic validation
            degreesPerFrameForUnityRate_ = degrees;
            ALOGI("AudioEngine: degreesPerFrameForUnityRate_ set to %.4f", degreesPerFrameForUnityRate_);
        } else {
            ALOGE("AudioEngine: Invalid degreesPerFrameForUnityRate_ value: %.4f", degrees);
        }
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* stream, void* audioData, int32_t numFrames) override;
    void onErrorBeforeClose(oboe::AudioStream *stream, oboe::Result error) override;
    void onErrorAfterClose(oboe::AudioStream *stream, oboe::Result error) override;

    // Getter for isFingerDownOnPlatter_
    bool isPlatterTouched() const { return isFingerDownOnPlatter_.load(); }

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
    if (!sincTableInitialized) { // Ensure table is calculated, typically once per app run or if params change
        precalculateSincTable();
    }
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
    bool doLog = false;
    bool isPlatterTouched_engine = false;
    if (audioEnginePtr != nullptr) {
        isPlatterTouched_engine = audioEnginePtr->isPlatterTouched();
        if (isPlatterTouched_engine) {
            doLog = true;
        }
    }

    float localPreciseCurrentFrame = preciseCurrentFrame.load();
    float playbackRateToUse = 1.0f;

    if (useEngineRateForPlayback_.load() && audioEnginePtr != nullptr) {
        playbackRateToUse = audioEnginePtr->platterTargetPlaybackRate_.load();
    }

    if (doLog) {
        // Variables for logging, matching the requested items
        const char* log_filePath = this->filePath.c_str(); // Item 1
        float log_initialFrame = localPreciseCurrentFrame;    // Item 2
        bool log_isPlaying = isPlaying.load();              // Item 3
        bool log_useEngineRate = useEngineRateForPlayback_.load(); // Item 4
        bool log_enginePtrValid = (audioEnginePtr != nullptr); // Item 5
        // Item 6a (isPlatterTouched_engine) is already available
        float log_enginePlatterRate = -1.0f; // Item 6b (placeholder if not applicable)
        if (log_useEngineRate && audioEnginePtr != nullptr) {
            log_enginePlatterRate = audioEnginePtr->platterTargetPlaybackRate_.load();
        }
        // Item 7 (playbackRateToUse) is already available
        int log_totalFrames = this->totalFrames; // For context

        ALOGV("AudioSample::getAudio[%s] FingerDown:%d - StartFrame:%.2f, isPlaying:%d, useEngineRate:%d, enginePtrValid:%d, enginePlatterRate:%.2f, finalPlaybackRate:%.2f, totalFrames:%d",
              log_filePath,
              isPlatterTouched_engine, // This is the direct result of audioEnginePtr->isPlatterTouched()
              log_initialFrame,
              log_isPlaying,
              log_useEngineRate,
              log_enginePtrValid,
              log_enginePlatterRate,
              playbackRateToUse,
              log_totalFrames);
    }

    // Standard checks for playability
    if (!isPlaying.load() || audioData.empty() || totalFrames == 0 || channels == 0) {
        if (doLog) { // Log if returning early during a finger-down scenario
            ALOGV("AudioSample::getAudio[%s] FingerDown:%d - RETURNING EARLY. isPlaying:%d, audioEmpty:%d, totalFrames:%d, channels:%d. Frame:%.2f",
                  this->filePath.c_str(), isPlatterTouched_engine, isPlaying.load(), audioData.empty(), totalFrames, channels, localPreciseCurrentFrame);
        }
        return;
    }

    // Main processing loop
    // localPreciseCurrentFrame will be modified within this loop
    for (int i = 0; i < numOutputFrames; ++i) {
        if (!isPlaying.load()) {
            if (doLog) ALOGV("AudioSample::getAudio[%s] FingerDown:%d - Loop iter %d: Breaking loop, isPlaying is false. Frame: %.2f", this->filePath.c_str(), isPlatterTouched_engine, i, localPreciseCurrentFrame);
            break;
        }

        // Boundary logic
        if (localPreciseCurrentFrame >= static_cast<float>(totalFrames) || localPreciseCurrentFrame < 0.0f) {
            if (playOnceThenLoopSilently && !playedOnce) {
                if (doLog) ALOGV("AudioSample::getAudio[%s] FingerDown:%d - Loop iter %d: playOnceThenLoopSilently path. Frame: %.2f", this->filePath.c_str(), isPlatterTouched_engine, i, localPreciseCurrentFrame);
                playedOnce = true; localPreciseCurrentFrame = 0.0f;
                if (!loop.load()) loop.store(true);
            } else if (loop.load()) {
                if (totalFrames > 0) {
                    if (doLog && (localPreciseCurrentFrame >= static_cast<float>(totalFrames) || localPreciseCurrentFrame < 0.0f)) {
                         ALOGV("AudioSample::getAudio[%s] FingerDown:%d - Loop iter %d: Looping frame. Before: %.2f", this->filePath.c_str(), isPlatterTouched_engine, i, localPreciseCurrentFrame);
                    }
                    localPreciseCurrentFrame = fmodf(localPreciseCurrentFrame, static_cast<float>(totalFrames));
                    if (localPreciseCurrentFrame < 0.0f) localPreciseCurrentFrame += static_cast<float>(totalFrames);
                    if (doLog && (localPreciseCurrentFrame >= static_cast<float>(totalFrames) || localPreciseCurrentFrame < 0.0f)) { // Should ideally not happen after correction
                         ALOGV("AudioSample::getAudio[%s] FingerDown:%d - Loop iter %d: Looping frame. After: %.2f", this->filePath.c_str(), isPlatterTouched_engine, i, localPreciseCurrentFrame);
                    }
                } else {
                     localPreciseCurrentFrame = 0.0f;
                }
            } else { // Not looping, and beyond boundaries
                if (doLog) ALOGV("AudioSample::getAudio[%s] FingerDown:%d - Loop iter %d: End of non-looping sample. Setting isPlaying=false. Frame: %.2f", this->filePath.c_str(), isPlatterTouched_engine, i, localPreciseCurrentFrame);
                isPlaying.store(false);
                break; 
            }
        }
        
        if (!isPlaying.load()) { // Re-check after boundary logic might have changed isPlaying
             if (doLog) ALOGV("AudioSample::getAudio[%s] FingerDown:%d - Loop iter %d: Breaking loop (post-boundary logic), isPlaying is false. Frame: %.2f", this->filePath.c_str(), isPlatterTouched_engine, i, localPreciseCurrentFrame);
             break;
        }

        float fractionalTime = localPreciseCurrentFrame - std::floor(localPreciseCurrentFrame);
        int32_t baseFrameIndex = static_cast<int32_t>(std::floor(localPreciseCurrentFrame));

        // Determine index for sincTable lookup
        int sincTableIndex = static_cast<int>(fractionalTime * SUBDIVISION_STEPS);
        sincTableIndex = std::min(sincTableIndex, SUBDIVISION_STEPS - 1); // Clamp to max index

        const std::vector<float>& coefficients = sincTable[sincTableIndex];

        // Calculate start index for fetching samples for the convolution kernel
        // The kernel is centered around a point "just before" baseFrameIndex if fractionalTime is 0.
        // More precisely, for fractionalTime = 0, we want the output to be as close as possible
        // to the sample at baseFrameIndex.
        // The coefficients are indexed 0 to NUM_TAPS-1.
        // If fractionalTime = 0, sincPoint for tap i is (i - (NUM_TAPS/2 - 1)).
        // The peak of the sinc function (sincPoint=0) is when i = NUM_TAPS/2 - 1.
        // So, coefficients[NUM_TAPS/2 - 1] should be multiplied by sample at baseFrameIndex.
        // Thus, the first sample in our local kernel (kernelSamples[0]) should correspond to
        // baseFrameIndex - (NUM_TAPS/2 - 1).
        int32_t kernelStartFrameIndex = baseFrameIndex - (NUM_TAPS / 2 - 1);

        for (int ch_out = 0; ch_out < outputStreamChannels; ++ch_out) {
            int srcChannel = ch_out % channels; // Handle mono-to-stereo, etc.
            float interpolatedSample = 0.0f;

            // Collect samples for the convolution
            // This loop can be optimized by fetching all NUM_TAPS samples first if beneficial
            // but direct use of getSampleAt handles boundaries per sample.
            for (int k = 0; k < NUM_TAPS; ++k) {
                float sampleValue = getSampleAt(kernelStartFrameIndex + k, srcChannel);
                interpolatedSample += sampleValue * coefficients[k];
            }
            outputBuffer[i * outputStreamChannels + ch_out] += interpolatedSample * effectiveVolume;
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
    // Log 1: Input parameters
    ALOGV("AudioEngine::scratchPlatterActiveInternal - Input: isActiveTouch:%d, angleDeltaOrRate:%.4f", isActiveTouch, angleDeltaOrRateFromViewModel);

    isFingerDownOnPlatter_.store(isActiveTouch);

    if (!platterAudioSample_ || platterAudioSample_->totalFrames == 0) {
        if(isActiveTouch) ALOGW("ScratchPlatterActive: Attempt on unloaded/invalid platter sample.");
        if(platterAudioSample_) { 
            platterAudioSample_->useEngineRateForPlayback_.store(false);
            // Log 3 & 4 for early exit path, using the specified format
            ALOGV("AudioEngine::scratchPlatterActiveInternal - PlatterSample State: useEngineRate:%d, isPlaying:%d", platterAudioSample_->useEngineRateForPlayback_.load(), platterAudioSample_->isPlaying.load());
        }
        return;
    }

    platterAudioSample_->useEngineRateForPlayback_.store(true);
    // Log 3 & 4 after setting useEngineRateForPlayback_, using the specified format
    ALOGV("AudioEngine::scratchPlatterActiveInternal - PlatterSample State: useEngineRate:%d, isPlaying:%d", platterAudioSample_->useEngineRateForPlayback_.load(), platterAudioSample_->isPlaying.load());
    
    float targetAudioRate;
    float currentSensitivity = scratchSensitivity_.load();

    // This ALOGE was for specific debugging by the user, kept as ALOGE.
    ALOGE("ScratchPlatterActive INPUT (Detail): isActiveTouch: %d, angleDeltaOrRateFromVM: %.4f, Sensitivity: %.4f",
          isActiveTouch, angleDeltaOrRateFromViewModel, currentSensitivity);

    if (isActiveTouch) { // Finger is actively interacting (touch down or drag)
        if (std::fabs(angleDeltaOrRateFromViewModel) > MOVEMENT_THRESHOLD) { // Finger is moving
            // targetAudioRate = angleDeltaOrRateFromViewModel * currentSensitivity; // Old logic
            float normalizedInputRate = 0.0f;
            if (std::fabs(degreesPerFrameForUnityRate_) > 0.00001f) { // Avoid division by zero
                normalizedInputRate = angleDeltaOrRateFromViewModel / degreesPerFrameForUnityRate_;
            } else if (std::fabs(angleDeltaOrRateFromViewModel) > 0.00001f) {
                 // If degreesPerFrameForUnityRate_ is zero but there's movement,
                 // this is an undefined state, but use sensitivity directly as a fallback to avoid silence.
                normalizedInputRate = angleDeltaOrRateFromViewModel;
            }
            targetAudioRate = normalizedInputRate * currentSensitivity;

            targetAudioRate = std::clamp(targetAudioRate, -4.0f, 4.0f);
            if (!platterAudioSample_->isPlaying.load()) {
                platterAudioSample_->isPlaying.store(true);
            }
        } else { // Finger is down, but not moving
            targetAudioRate = 0.0f;
            if (platterAudioSample_->isPlaying.load()) {
                platterAudioSample_->isPlaying.store(false);
            }
        }
        // Log 3 & 4 after potential modifications in isActiveTouch=true branch, using the specified format
        ALOGV("AudioEngine::scratchPlatterActiveInternal - PlatterSample State: useEngineRate:%d, isPlaying:%d", platterAudioSample_->useEngineRateForPlayback_.load(), platterAudioSample_->isPlaying.load());

    } else { // Finger is NOT on platter (isActiveTouch is false)
        targetAudioRate = angleDeltaOrRateFromViewModel; // This is the desired normalized audio rate
        
        if (std::fabs(targetAudioRate) > 0.00001f) { // If coasting rate is non-zero
            if(!platterAudioSample_->isPlaying.load()) {
                platterAudioSample_->isPlaying.store(true);
            }
        } else { // Coasting rate is effectively zero
            if(platterAudioSample_->isPlaying.load()) {
                platterAudioSample_->isPlaying.store(false);
            }
        }
        // Log 3 & 4 after potential modifications in isActiveTouch=false branch, using the specified format
        ALOGV("AudioEngine::scratchPlatterActiveInternal - PlatterSample State: useEngineRate:%d, isPlaying:%d", platterAudioSample_->useEngineRateForPlayback_.load(), platterAudioSample_->isPlaying.load());
    }

    // Log 2: Calculated targetAudioRate before storing
    ALOGV("AudioEngine::scratchPlatterActiveInternal - Calculated: targetAudioRate:%.4f", targetAudioRate);
    platterTargetPlaybackRate_.store(targetAudioRate);

    // Final state log (Log 3 & 4 again) for completeness after storing targetAudioRate, using the specified format
    ALOGV("AudioEngine::scratchPlatterActiveInternal - PlatterSample State: useEngineRate:%d, isPlaying:%d", 
          (platterAudioSample_ ? platterAudioSample_->useEngineRateForPlayback_.load() : -1), 
          (platterAudioSample_ ? platterAudioSample_->isPlaying.load() : -1));
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
    if (gAudioEngine && gAudioEngine.get() != nullptr) { // NOLINT(*-simplify-boolean-expr)
        hello += " - AudioEngine Initialized and valid.";
    } else {
        hello += " - AudioEngine IS NULL or NOT Initialized.";
    }
    return env->NewStringUTF(hello.c_str());
}

JNIEXPORT void JNICALL
Java_com_example_fromscratch_MainActivity_setAudioNormalizationFactor(JNIEnv *env, jobject /* this */, jfloat degreesPerFrame) {
    ALOGI("JNI: setAudioNormalizationFactor called with degreesPerFrame: %.4f", degreesPerFrame);
    if (gAudioEngine) {
        gAudioEngine->setDegreesPerFrameForUnityRateInternal(static_cast<float>(degreesPerFrame));
    } else {
        ALOGE("JNI: AudioEngine not initialized for setAudioNormalizationFactor.");
    }
}
} // extern "C"
