package com.example.fromscratch

import android.content.pm.ActivityInfo
import android.content.res.AssetManager
import android.os.Bundle
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.OnBackPressedCallback
import androidx.activity.compose.setContent
import androidx.activity.viewModels


class MainActivity : ComponentActivity() {

    companion object {
        var isCurrentUserPremium: Boolean = false
        const val PAYMENT_URL: String = "https://www.example.com/subscribe"

        init {
            try {
                System.loadLibrary("scratch-emulator-lib")
                Log.d("ScratchEmulator", "Native library loaded successfully.")
            } catch (e: UnsatisfiedLinkError) {
                Log.e("ScratchEmulator", "Failed to load native library", e)
            }
        }
    }

    // JNI Function Declarations
    private external fun initAudioEngine(assetManager: android.content.res.AssetManager)
    private external fun releaseAudioEngine()
    private external fun startPlayback()
    private external fun stopPlayback()
    external fun stringFromJNI(): String
    private external fun playIntroAndLoopOnPlatter(assetManager: android.content.res.AssetManager, filePath: String)
    private external fun nextPlatterSample()
    // private external fun loadUserPlatterSample(filePath: String) // Placeholder
    private external fun playMusicTrack()
    private external fun stopMusicTrack()
    private external fun nextMusicTrackAndPlay()
    private external fun nextMusicTrackAndKeepState()
    // private external fun loadUserMusicTrack(filePath: String) // Placeholder
    private external fun setPlatterFaderVolume(volume: Float)
    private external fun setMusicMasterVolume(volume: Float)
    private external fun scratchPlatterActive(isActive: Boolean, angleDeltaOrRate: Float)
    private external fun releasePlatterTouch()
    private external fun setScratchSensitivity(sensitivity: Float) 
    private external fun setAudioNormalizationFactor(degreesPerFrame: Float) // New JNI declaration

    private val appViewModel: AppViewModel by viewModels {
        AppViewModelFactory(this)
    }

    class AppViewModelFactory(private val activity: MainActivity) : androidx.lifecycle.ViewModelProvider.Factory {
        override fun <T : androidx.lifecycle.ViewModel> create(modelClass: Class<T>): T {
            if (modelClass.isAssignableFrom(AppViewModel::class.java)) {
                @Suppress("UNCHECKED_CAST")
                return AppViewModel(
                    onPlayIntroAndLoopOnPlatter = { filePath ->
                        activity.playIntroAndLoopOnPlatter(activity.assets, filePath)
                    },
                    onNextPlatterSample = { activity.nextPlatterSample() },
                    onLoadUserPlatterSample = { filePath ->
                        Log.d("MainActivity", "VM -> JNI: loadUserPlatterSample with $filePath (Not fully implemented)")
                        // activity.loadUserPlatterSample(filePath)
                    },
                    onPlayMusicTrack = { activity.playMusicTrack() },
                    onStopMusicTrack = { activity.stopMusicTrack() },
                    onNextMusicTrackAndPlay = { activity.nextMusicTrackAndPlay() },
                    onNextMusicTrackAndKeepState = { activity.nextMusicTrackAndKeepState() },
                    onLoadUserMusicTrack = { filePath ->
                        Log.d("MainActivity", "VM -> JNI: loadUserMusicTrack with $filePath (Not fully implemented)")
                        // activity.loadUserMusicTrack(filePath)
                    },
                    onUpdatePlatterFaderVolume = { volume -> activity.setPlatterFaderVolume(volume) },
                    onUpdateMusicMasterVolume = { volume -> activity.setMusicMasterVolume(volume) },
                    onScratchPlatterActive = { isActive, angleDeltaOrRate ->
                        activity.scratchPlatterActive(isActive, angleDeltaOrRate)
                    },
                    onReleasePlatterTouch = { activity.releasePlatterTouch() },
                    onUpdateScratchSensitivity = { sensitivity ->
                        Log.d("MainActivity", "VM -> JNI: setScratchSensitivity($sensitivity)")
                        activity.setScratchSensitivity(sensitivity)
                    },
                    onSetAudioNormalizationFactor = { degrees -> // New lambda for ViewModelFactory
                        Log.d("MainActivity", "VM -> JNI: setAudioNormalizationFactor($degrees)")
                        activity.setAudioNormalizationFactor(degrees)
                    }
                ) as T
            }
            throw IllegalArgumentException("Unknown ViewModel class")
        }
    }


    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.d("ScratchEmulator", "MainActivity onCreate called.")

        // Lock orientation (AndroidManifest.xml is the primary way, this is an alternative)
        // requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT;

        val onBackPressedCallback = object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                Log.i("MainActivity", "Back press intercepted and ignored.")
            }
        }
        onBackPressedDispatcher.addCallback(this, onBackPressedCallback)

        val assetManager = this.assets
        Log.d("ScratchEmulator", "Got AssetManager.")

        initAudioEngine(assetManager) // Initializes gAudioEngine
        Log.d("ScratchEmulator", "AudioEngine object potentially initialized via JNI.")

        // ViewModel init will call onUpdateScratchSensitivity, which calls JNI setScratchSensitivity.
        // This ensures sensitivity is set in C++ before any scratching might occur.

        startPlayback() // Starts the Oboe stream
        Log.d("ScratchEmulator", "Playback stream requested start via JNI.")

        setContent {
            DjApp(viewModel = appViewModel)
        }
        Log.d("ScratchEmulator", "Compose UI content set.")
    }

    override fun onDestroy() {
        super.onDestroy()
        Log.d("ScratchEmulator", "MainActivity onDestroy called. Releasing AudioEngine.")
        stopPlayback()
        releaseAudioEngine()
        Log.d("ScratchEmulator", "AudioEngine released via JNI.")
    }
}
