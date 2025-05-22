package com.example.fromscratch

import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.math.abs
import kotlin.math.sign

class AppViewModel( // This should be the only declaration of this class in this package
    // Lambdas to communicate with MainActivity
    private val onPlayIntroAndLoopOnPlatter: (filePath: String) -> Unit,
    private val onNextPlatterSample: () -> Unit,
    private val onLoadUserPlatterSample: (filePath: String) -> Unit, // Placeholder
    private val onPlayMusicTrack: () -> Unit,
    private val onStopMusicTrack: () -> Unit,
    private val onNextMusicTrackAndPlay: () -> Unit,
    private val onNextMusicTrackAndKeepState: () -> Unit,
    private val onLoadUserMusicTrack: (filePath: String) -> Unit, // Placeholder
    private val onUpdatePlatterFaderVolume: (volume: Float) -> Unit,
    private val onUpdateMusicMasterVolume: (volume: Float) -> Unit,
    private val onScratchPlatterActive: (isActive: Boolean, angleDeltaOrRate: Float) -> Unit,
    private val onReleasePlatterTouch: () -> Unit,
    private val onUpdateScratchSensitivity: (sensitivity: Float) -> Unit
) : ViewModel() {

    var currentScreen by mutableStateOf<AppScreen>(AppScreen.Loading)
        private set
    var showSettingsDialog by mutableStateOf(false)
        private set

    // --- Platter and Vinyl Mechanics ---
    var visualPlatterAngle by mutableFloatStateOf(0f)
    var vinylAngle by mutableFloatStateOf(0f)
    var isPlatterTouched by mutableStateOf(false)

    private var vinylSpeed by mutableFloatStateOf(0f) // Current speed of the vinyl in degrees per animation frame
    private val visualPlatterRPM = 25.0f // Target RPM for the visual platter base when spinning freely
    // Degrees per animation frame for the visual platter at its target RPM.
    // This is also assumed to be the visual speed at which the audio should play at 1.0x rate.
    private val degreesPerFrameAtPlatterRPM = (visualPlatterRPM / 60.0f) * 360.0f / 60.0f // Approx. 60 FPS for animation loop


    // --- Tunable Parameters (with defaults) ---
    var slipmatDampingFactor by mutableFloatStateOf(0.05f)
        private set
    var scratchSensitivitySetting by mutableFloatStateOf(0.05f) // User-facing setting
        private set


    // --- Other States ---
    var platterFaderVolume by mutableFloatStateOf(0.0f)
        private set
    var isMusicPlaying by mutableStateOf(false)
        private set
    var platterSamplePaths by mutableStateOf(listOf("sounds/haahhh", "sounds/sample1", "sounds/sample2"))
        private set
    var currentPlatterSampleIndex by mutableIntStateOf(0)
        private set
    var musicTrackPaths by mutableStateOf(listOf("tracks/trackA", "tracks/trackB"))
        private set
    var currentMusicTrackIndex by mutableIntStateOf(0)
        private set
    var musicMasterVolume by mutableFloatStateOf(0.9f)
        private set


    init {
        Log.d("AppViewModel", "ViewModel init block started.")
        Log.d("AppViewModel", "Initial Scratch Sensitivity (Kotlin): $scratchSensitivitySetting, Damping: $slipmatDampingFactor")
        // Send initial sensitivity to native layer
        onUpdateScratchSensitivity(scratchSensitivitySetting)

        viewModelScope.launch {
            Log.d("AppViewModel", "ViewModel init coroutine launched.")
            val initialSample = platterSamplePaths.getOrElse(currentPlatterSampleIndex) { "" }
            if (initialSample.isNotEmpty()) {
                onPlayIntroAndLoopOnPlatter(initialSample)
            } else {
                Log.e("AppViewModel", "Cannot play intro: initial sample path is empty.")
            }
            delay(1500) // Simulate loading time
            currentScreen = AppScreen.Main

            // Main animation and physics loop
            launch {
                Log.d("AppViewModel", "Animation/Physics loop started. degreesPerFrameAtPlatterRPM: $degreesPerFrameAtPlatterRPM")
                while (true) {
                    // Update visual angle of the platter base (always spins)
                    visualPlatterAngle = (visualPlatterAngle + degreesPerFrameAtPlatterRPM.toFloat()) % 360f

                    // Update vinyl speed and angle
                    if (!isPlatterTouched) {
                        // Platter is not being touched, apply slipmat physics to coast or spin up to normal speed
                        val targetSpeed = degreesPerFrameAtPlatterRPM.toFloat() // Target speed is normal playback speed
                        val speedDifference = targetSpeed - vinylSpeed
                        vinylSpeed += speedDifference * slipmatDampingFactor

                        // Optional: Snap to targetSpeed if very close to avoid tiny oscillations
                        // if (abs(speedDifference) < 0.01f && (sign(targetSpeed) == sign(vinylSpeed) || abs(targetSpeed) < 0.01f) ) {
                        //     vinylSpeed = targetSpeed
                        // }

                        // **** KEY CHANGE: Continuously update native layer with coasting/normal playback rate ****
                        // Calculate normalized audio rate based on current visual vinylSpeed.
                        // If degreesPerFrameAtPlatterRPM is the visual speed for 1.0x audio,
                        // then vinylSpeed / degreesPerFrameAtPlatterRPM gives the desired audio rate multiplier.
                        val normalizedAudioRate = if (degreesPerFrameAtPlatterRPM.toFloat() != 0f) {
                            vinylSpeed / degreesPerFrameAtPlatterRPM.toFloat()
                        } else {
                            if (vinylSpeed == 0f) 0f else 1f // Avoid division by zero; default to 1.0 if platterRPM is 0 but vinyl moves
                        }
                        onScratchPlatterActive(false, normalizedAudioRate)
                        // Log.v("AppViewModel_Loop", "Coasting/Normal. vinylSpeed: $vinylSpeed, normalizedAudioRate: $normalizedAudioRate")

                    }
                    // else: Platter is touched. vinylSpeed is set directly by onPlatterDrag,
                    // and onScratchPlatterActive(true, angleDelta) is called from there.

                    // Update vinyl's visual angle
                    vinylAngle = (vinylAngle + vinylSpeed) % 360f
                    if (vinylAngle < 0) vinylAngle += 360f // Ensure positive angle

                    delay(16) // Aim for ~60 FPS
                }
            }
        }
    }

    fun handleButton1Press() {
        Log.i("AppViewModel_Button1", "handleButton1Press() CALLED")
        onNextPlatterSample() // Inform C++ to load and prepare the next sample
        currentPlatterSampleIndex = (currentPlatterSampleIndex + 1) % platterSamplePaths.size
        // Reset vinyl state for the new sample
        vinylAngle = 0f
        vinylSpeed = 0f // It will spin up due to the animation loop
        Log.d("AppViewModel_Button1", "New platter sample index (UI): $currentPlatterSampleIndex. Vinyl angle/speed reset.")
    }

    fun handleButton1Hold() {
        Log.i("AppViewModel_Button1", "handleButton1Hold() CALLED")
        Log.d("AppViewModel_Button1", "Button 1 Hold: Request User Platter Sample Upload (Placeholder)")
        // onLoadUserPlatterSample("path/to/user/sample") // Example
    }

    fun handleButton2Press() {
        Log.i("AppViewModel_Button2", "handleButton2Press() CALLED. isMusicPlaying: $isMusicPlaying")
        if (isMusicPlaying) {
            Log.d("AppViewModel_Button2", "Button 2 Press (Music Playing): Next Music Track & Keep Playing")
            onNextMusicTrackAndKeepState() // C++ loads next track, keeps playing if it was
            currentMusicTrackIndex = (currentMusicTrackIndex + 1) % musicTrackPaths.size
        } else {
            Log.d("AppViewModel_Button2", "Button 2 Press (Music Stopped): Play Music Track")
            onPlayMusicTrack() // C++ starts playing current track
            isMusicPlaying = true
        }
        Log.d("AppViewModel_Button2", "Music playing: $isMusicPlaying, Current track index (UI): $currentMusicTrackIndex")
    }

    fun handleButton2DoublePress() {
        Log.i("AppViewModel_Button2", "handleButton2DoublePress() CALLED. isMusicPlaying: $isMusicPlaying")
        if (isMusicPlaying) {
            Log.d("AppViewModel_Button2", "Button 2 Double Press (Music Playing): Stop Music Track")
            onStopMusicTrack() // C++ stops music
            isMusicPlaying = false
        } else {
            Log.d("AppViewModel_Button2", "Button 2 Double Press (Music Stopped): Next Music Track & Play")
            onNextMusicTrackAndPlay() // C++ loads next track and starts playing it
            currentMusicTrackIndex = (currentMusicTrackIndex + 1) % musicTrackPaths.size
            isMusicPlaying = true
        }
        Log.d("AppViewModel_Button2", "Music playing: $isMusicPlaying, Current track index (UI): $currentMusicTrackIndex")
    }

    fun handleButton2Hold() {
        Log.i("AppViewModel_Button2", "handleButton2Hold() CALLED")
        Log.d("AppViewModel_Button2", "Button 2 Hold: Request User Music Track Upload (Placeholder)")
        // onLoadUserMusicTrack("path/to/user/track") // Example
    }

    fun handleHoldBothButtons() {
        Log.i("AppViewModel_Buttons", "handleHoldBothButtons() CALLED")
        showSettingsDialog = true
    }

    fun onPlatterFaderVolumeChange(newVolume: Float) {
        platterFaderVolume = newVolume.coerceIn(0f, 1f)
        Log.d("AppViewModel_Fader", "Platter Fader Volume changed to: $platterFaderVolume")
        onUpdatePlatterFaderVolume(platterFaderVolume) // Inform C++
    }

    fun onMusicMasterVolumeChange(newVolume: Float) {
        musicMasterVolume = newVolume.coerceIn(0f, 1f)
        Log.d("AppViewModel_Settings", "Music Master Volume changed to: $musicMasterVolume")
        onUpdateMusicMasterVolume(musicMasterVolume) // Inform C++
    }

    fun onSlipmatDampingChange(newDamping: Float) {
        slipmatDampingFactor = newDamping.coerceIn(0.005f, 0.5f)
        Log.i("AppViewModel_Settings", "Slipmat Damping Factor changed to: $slipmatDampingFactor")
    }

    fun onScratchSensitivityChange(newSensitivity: Float) {
        scratchSensitivitySetting = newSensitivity.coerceIn(0.005f, 0.2f)
        Log.i("AppViewModel_Settings", "Scratch Sensitivity changed to: $scratchSensitivitySetting")
        onUpdateScratchSensitivity(scratchSensitivitySetting) // Inform C++
    }

    fun closeSettingsDialog() {
        Log.d("AppViewModel_Settings", "closeSettingsDialog() called.")
        showSettingsDialog = false
    }

    // Called from PlatterView when touch starts
    fun onPlatterTouchDown() {
        Log.d("AppViewModel_Platter", "onPlatterTouchDown called.")
        isPlatterTouched = true
        vinylSpeed = 0f // Stop vinyl visually immediately
        // Inform C++ that touch is active and current movement (or lack thereof) is 0.
        // C++ will set its internal playback rate to 0 (or very low) if sensitivity is applied.
        onScratchPlatterActive(true, 0f)
    }

    // Called from PlatterView during drag
    fun onPlatterDrag(angleDelta: Float) {
        if (isPlatterTouched) {
            // Update visual angle of the vinyl
            vinylAngle = (vinylAngle + angleDelta) % 360f
            if (vinylAngle < 0) vinylAngle += 360f

            // Set visual speed directly from drag
            vinylSpeed = angleDelta

            // Inform C++ about the active scratch and the angle delta.
            // C++ will use this angleDelta (scaled by sensitivity) to set its playback rate.
            onScratchPlatterActive(true, angleDelta)
            // Log.v("AppViewModel_Platter", "Dragging. angleDelta: $angleDelta, new vinylAngle: $vinylAngle")
        }
    }

    // Called from PlatterView when touch ends
    fun onPlatterTouchUp() {
        Log.d("AppViewModel_Platter", "onPlatterTouchUp called. Current visual vinylSpeed before release: $vinylSpeed")
        isPlatterTouched = false
        // Inform C++ that touch has been released.
        // The C++ side's releasePlatterTouchInternal() sets useEngineRateForPlayback_ to true.
        // The animation loop will then take over sending coasting rates via onScratchPlatterActive(false, normalizedRate).
        onReleasePlatterTouch()
    }
}
