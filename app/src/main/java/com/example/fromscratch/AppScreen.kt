// File: com/example/fromscratch/AppScreen.kt
package com.example.fromscratch

/**
 * Defines the different screens/states of the application.
 * This helps in managing navigation and UI display.
 */
sealed class AppScreen {
    object Loading : AppScreen()
    object Main : AppScreen()
    // Settings could be a dialog on Main screen or a separate screen.
    // For simplicity, we'll treat it as a state that triggers a dialog.
}