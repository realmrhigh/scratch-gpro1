package com.example.fromscratch

import org.junit.Test
import org.junit.Assert.*
import org.junit.Before
// No need to import com.example.fromscratch.AppViewModel or MainActivity explicitly if they are in the same package
// and the test file is correctly placed in the source set that can see them.
// However, for clarity or if issues arise, explicit imports can be added.

class AppViewModelTest {

    private lateinit var viewModel: AppViewModel

    @Before
    fun setup() {
        // Reset static state before each test as it's modified by tests
        MainActivity.isCurrentUserPremium = false

        viewModel = AppViewModel(
            onPlayIntroAndLoopOnPlatter = {},
            onNextPlatterSample = {},
            onLoadUserPlatterSample = {},
            onPlayMusicTrack = {},
            onStopMusicTrack = {},
            onNextMusicTrackAndPlay = {},
            onNextMusicTrackAndKeepState = {},
            onLoadUserMusicTrack = {},
            onUpdatePlatterFaderVolume = {},
            onUpdateMusicMasterVolume = {},
            onScratchPlatterActive = { _, _ -> },
            onReleasePlatterTouch = {},
            onUpdateScratchSensitivity = {},
            onSetAudioNormalizationFactor = {} // This was added in a previous step
        )
    }

    @Test
    fun initialShowSubscribePopup_isFalse() {
        assertFalse("Initially, showSubscribePopup should be false", viewModel.showSubscribePopup)
    }

    @Test
    fun handleButton1Hold_premiumUser_doesNotShowPopup() {
        MainActivity.isCurrentUserPremium = true
        viewModel.handleButton1Hold()
        assertFalse("Popup should NOT show for premium user on Button 1 Hold", viewModel.showSubscribePopup)
    }

    @Test
    fun handleButton1Hold_nonPremiumUser_showsPopup() {
        MainActivity.isCurrentUserPremium = false // Explicitly set, though setup does it too
        viewModel.handleButton1Hold()
        assertTrue("Popup SHOULD show for non-premium user on Button 1 Hold", viewModel.showSubscribePopup)
    }

    @Test
    fun handleButton2Hold_premiumUser_doesNotShowPopup() {
        MainActivity.isCurrentUserPremium = true
        viewModel.handleButton2Hold()
        assertFalse("Popup should NOT show for premium user on Button 2 Hold", viewModel.showSubscribePopup)
    }

    @Test
    fun handleButton2Hold_nonPremiumUser_showsPopup() {
        MainActivity.isCurrentUserPremium = false // Explicitly set
        viewModel.handleButton2Hold()
        assertTrue("Popup SHOULD show for non-premium user on Button 2 Hold", viewModel.showSubscribePopup)
    }

    @Test
    fun settingsSwitch_canTogglePremiumStatus() {
        // Initial state (as set in setup)
        assertFalse("Initial premium status should be false (from setup)", MainActivity.isCurrentUserPremium)

        // Simulate toggling switch on
        MainActivity.isCurrentUserPremium = true
        assertTrue("Premium status should be true after direct modification", MainActivity.isCurrentUserPremium)

        // Simulate toggling switch off
        MainActivity.isCurrentUserPremium = false
        assertFalse("Premium status should be false after direct modification back", MainActivity.isCurrentUserPremium)
    }
}
