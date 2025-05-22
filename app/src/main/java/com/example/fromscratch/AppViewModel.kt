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

    private var vinylSpeed by mutableFloatStateOf(0f)
    private val visualPlatterRPM = 25.0f
    private val degreesPerFrameAtPlatterRPM = (visualPlatterRPM / 60.0f) * 360.0f / 60.0f


    // --- Tunable Parameters (with defaults) ---
    var slipmatDampingFactor by mutableFloatStateOf(0.05f)
        private set
    var scratchSensitivitySetting by mutableFloatStateOf(0.05f)
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
        onUpdateScratchSensitivity(scratchSensitivitySetting)

        viewModelScope.launch {
            Log.d("AppViewModel", "ViewModel init coroutine launched.")
            val initialSample = platterSamplePaths.getOrElse(currentPlatterSampleIndex) { "" }
            if (initialSample.isNotEmpty()) {
                onPlayIntroAndLoopOnPlatter(initialSample)
            } else {
                Log.e("AppViewModel", "Cannot play intro: initial sample path is empty.")
            }
            delay(1500)
            currentScreen = AppScreen.Main

            launch {
                Log.d("AppViewModel", "Animation/Physics loop started. degreesPerFrameAtPlatterRPM: $degreesPerFrameAtPlatterRPM")
                while (true) {
                    visualPlatterAngle = (visualPlatterAngle + degreesPerFrameAtPlatterRPM.toFloat()) % 360f

                    if (!isPlatterTouched) {
                        val targetSpeed = degreesPerFrameAtPlatterRPM.toFloat()
                        val speedDifference = targetSpeed - vinylSpeed
                        vinylSpeed += speedDifference * slipmatDampingFactor
                        if (abs(speedDifference) < 0.01f && (sign(targetSpeed) == sign(vinylSpeed) || abs(targetSpeed) < 0.01f) ) {
                            // vinylSpeed = targetSpeed
                        }
                    }
                    vinylAngle = (vinylAngle + vinylSpeed) % 360f
                    if (vinylAngle < 0) vinylAngle += 360f

                    delay(16)
                }
            }
        }
    }

    fun handleButton1Press() {
        Log.i("AppViewModel_Button1", "handleButton1Press() CALLED")
        onNextPlatterSample()
        currentPlatterSampleIndex = (currentPlatterSampleIndex + 1) % platterSamplePaths.size
        Log.d("AppViewModel_Button1", "New platter sample index (UI): $currentPlatterSampleIndex")
    }

    fun handleButton1Hold() {
        Log.i("AppViewModel_Button1", "handleButton1Hold() CALLED")
        Log.d("AppViewModel_Button1", "Button 1 Hold: Request User Platter Sample Upload (Placeholder)")
    }

    fun handleButton2Press() {
        Log.i("AppViewModel_Button2", "handleButton2Press() CALLED. isMusicPlaying: $isMusicPlaying")
        if (isMusicPlaying) {
            Log.d("AppViewModel_Button2", "Button 2 Press (Music Playing): Next Music Track & Keep Playing")
            onNextMusicTrackAndKeepState()
            currentMusicTrackIndex = (currentMusicTrackIndex + 1) % musicTrackPaths.size
        } else {
            Log.d("AppViewModel_Button2", "Button 2 Press (Music Stopped): Play Music Track")
            onPlayMusicTrack()
            isMusicPlaying = true
        }
        Log.d("AppViewModel_Button2", "Music playing: $isMusicPlaying, Current track index (UI): $currentMusicTrackIndex")
    }

    fun handleButton2DoublePress() {
        Log.i("AppViewModel_Button2", "handleButton2DoublePress() CALLED. isMusicPlaying: $isMusicPlaying")
        if (isMusicPlaying) {
            Log.d("AppViewModel_Button2", "Button 2 Double Press (Music Playing): Stop Music Track")
            onStopMusicTrack()
            isMusicPlaying = false
        } else {
            Log.d("AppViewModel_Button2", "Button 2 Double Press (Music Stopped): Next Music Track & Play")
            onNextMusicTrackAndPlay()
            currentMusicTrackIndex = (currentMusicTrackIndex + 1) % musicTrackPaths.size
            isMusicPlaying = true
        }
        Log.d("AppViewModel_Button2", "Music playing: $isMusicPlaying, Current track index (UI): $currentMusicTrackIndex")
    }

    fun handleButton2Hold() {
        Log.i("AppViewModel_Button2", "handleButton2Hold() CALLED")
        Log.d("AppViewModel_Button2", "Button 2 Hold: Request User Music Track Upload (Placeholder)")
    }

    fun handleHoldBothButtons() {
        Log.i("AppViewModel_Buttons", "handleHoldBothButtons() CALLED")
        showSettingsDialog = true
    }

    fun onPlatterFaderVolumeChange(newVolume: Float) {
        platterFaderVolume = newVolume.coerceIn(0f, 1f)
        Log.d("AppViewModel_Fader", "Platter Fader Volume changed to: $platterFaderVolume")
        onUpdatePlatterFaderVolume(platterFaderVolume)
    }

    fun onMusicMasterVolumeChange(newVolume: Float) {
        musicMasterVolume = newVolume.coerceIn(0f, 1f)
        Log.d("AppViewModel_Settings", "Music Master Volume changed to: $musicMasterVolume")
        onUpdateMusicMasterVolume(musicMasterVolume)
    }

    fun onSlipmatDampingChange(newDamping: Float) {
        slipmatDampingFactor = newDamping.coerceIn(0.005f, 0.5f)
        Log.i("AppViewModel_Settings", "Slipmat Damping Factor changed to: $slipmatDampingFactor")
    }

    fun onScratchSensitivityChange(newSensitivity: Float) {
        scratchSensitivitySetting = newSensitivity.coerceIn(0.005f, 0.2f)
        Log.i("AppViewModel_Settings", "Scratch Sensitivity changed to: $scratchSensitivitySetting")
        onUpdateScratchSensitivity(scratchSensitivitySetting)
    }

    fun closeSettingsDialog() {
        Log.d("AppViewModel_Settings", "closeSettingsDialog() called.")
        showSettingsDialog = false
    }

    fun onPlatterTouchDown() {
        Log.d("AppViewModel_Platter", "onPlatterTouchDown called.")
        isPlatterTouched = true
        vinylSpeed = 0f
        onScratchPlatterActive(true, 0f)
    }

    fun onPlatterDrag(angleDelta: Float) {
        if (isPlatterTouched) {
            vinylAngle = (vinylAngle + angleDelta) % 360f
            if (vinylAngle < 0) vinylAngle += 360f
            vinylSpeed = angleDelta
            onScratchPlatterActive(true, angleDelta)
        }
    }

    fun onPlatterTouchUp() {
        Log.d("AppViewModel_Platter", "onPlatterTouchUp called. Current vinylSpeed before release: $vinylSpeed")
        isPlatterTouched = false
        onReleasePlatterTouch()
    }
}
