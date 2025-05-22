// File: com/example/fromscratch/AppUI.kt
package com.example.fromscratch

import android.util.Log
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.GestureCancellationException
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.PressInteraction
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.TransformOrigin
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import kotlin.math.PI
import kotlin.math.atan2
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sqrt

import androidx.constraintlayout.compose.ConstraintLayout
import androidx.constraintlayout.compose.Dimension

@Composable
fun DjApp(viewModel: AppViewModel) {
    val currentScreen = viewModel.currentScreen
    MaterialTheme {
        Surface(
            modifier = Modifier.fillMaxSize(),
            color = MaterialTheme.colorScheme.background
        ) {
            when (currentScreen) {
                is AppScreen.Loading -> LoadingScreen(viewModel)
                is AppScreen.Main -> MainScreen(viewModel)
            }
            if (viewModel.showSettingsDialog) {
                SettingsDialog(
                    viewModel = viewModel,
                    onDismiss = { viewModel.closeSettingsDialog() }
                )
            }
        }
    }
}

@Composable
fun LoadingScreen(viewModel: AppViewModel?) {
    Log.d("LoadingScreen", "LoadingScreen composable executing")
    val configuration = LocalConfiguration.current
    val screenWidth = configuration.screenWidthDp.dp

    Box(modifier = Modifier.fillMaxSize()) {
        Image(
            painter = painterResource(id = R.drawable.loadbg),
            contentDescription = "Loading Background",
            modifier = Modifier.fillMaxSize(),
            contentScale = ContentScale.Crop
        )
        Column(
            modifier = Modifier
                .align(Alignment.Center)
                .padding(bottom = 50.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Image(
                painter = painterResource(id = R.drawable.ayodjlogo),
                contentDescription = "AYO DJ Logo",
                modifier = Modifier.width(screenWidth * 0.8f)
            )
        }
        Text(
            text = "Hold both buttons for Settings",
            color = Color.LightGray.copy(alpha = 0.7f),
            fontSize = 12.sp,
            textAlign = TextAlign.Center,
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .padding(horizontal = 32.dp)
                .padding(bottom = 24.dp)
        )
    }
}

@Composable
fun MainScreen(viewModel: AppViewModel) {
    val button1InteractionSource = remember { MutableInteractionSource() }
    val isButton1Pressed by button1InteractionSource.collectIsPressedAsState()
    val button2InteractionSource = remember { MutableInteractionSource() }
    val isButton2Pressed by button2InteractionSource.collectIsPressedAsState()

    var isB1PhysicallyHeld by remember { mutableStateOf(false) }
    var isB2PhysicallyHeld by remember { mutableStateOf(false) }

    val configuration = LocalConfiguration.current
    val screenWidthDp = configuration.screenWidthDp.dp
    val controlsBuffer = 10.dp
    val baseFaderHeight = 60.dp
    val faderHeight = baseFaderHeight * 1.3f
    val originalButtonHeight = screenWidthDp * 0.072f * 2.0f
    val originalButtonWidth = originalButtonHeight * 0.72f
    val newButtonHeight = originalButtonHeight * 1.5f
    val newButtonWidth = originalButtonWidth * 1.5f

    LaunchedEffect(isB1PhysicallyHeld, isB2PhysicallyHeld) {
        if (isB1PhysicallyHeld && isB2PhysicallyHeld) {
            Log.i("AppUI_Buttons", "Both buttons HELD. Calling handleHoldBothButtons.")
            viewModel.handleHoldBothButtons()
            isB1PhysicallyHeld = false
            isB2PhysicallyHeld = false
        }
    }

    Box(modifier = Modifier.fillMaxSize()) { // Root Box for MainScreen
        Image( // Background Image
            painter = painterResource(id = R.drawable.bg),
            contentDescription = "Background",
            modifier = Modifier.fillMaxSize(),
            contentScale = ContentScale.Crop
        )

        // Layer 1: PlatterView
        PlatterView(
            vinylAngle = viewModel.vinylAngle,
            visualPlatterAngle = viewModel.visualPlatterAngle,
            isTouched = viewModel.isPlatterTouched,
            onPress = viewModel::onPlatterTouchDown,
            onRelease = viewModel::onPlatterTouchUp,
            onDrag = viewModel::onPlatterDrag,
            contentScaleFactor = 4.0f,
            screenWidthDp = screenWidthDp,
            modifier = Modifier
                .fillMaxSize()
                .padding(bottom = newButtonHeight * 2 + faderHeight + controlsBuffer * 3 + controlsBuffer) // Extra padding at bottom
        )

        // Layer 2: Controls (Buttons & Fader)
        ConstraintLayout(modifier = Modifier.fillMaxSize()) {
            val (fader, button1, button2) = createRefs()

            CustomFader(
                volume = viewModel.platterFaderVolume,
                onVolumeChange = viewModel::onPlatterFaderVolumeChange,
                faderAreaHeight = faderHeight,
                modifier = Modifier
                    .constrainAs(fader) {
                        bottom.linkTo(parent.bottom, margin = controlsBuffer)
                        start.linkTo(parent.start, margin = controlsBuffer)
                        end.linkTo(parent.end, margin = controlsBuffer)
                        width = Dimension.fillToConstraints
                    }
                    .height(faderHeight)
            )

            Box(
                modifier = Modifier
                    .constrainAs(button1) {
                        bottom.linkTo(button2.top, margin = controlsBuffer / 2)
                        end.linkTo(fader.end)
                    }
                    .size(width = newButtonWidth, height = newButtonHeight)
                    .pointerInput(Unit) {
                        detectTapGestures(
                            onPress = { offset ->
                                isB1PhysicallyHeld = true
                                val press = PressInteraction.Press(offset)
                                button1InteractionSource.emit(press)
                                try {
                                    if (tryAwaitRelease()) button1InteractionSource.emit(PressInteraction.Release(press))
                                    else button1InteractionSource.emit(PressInteraction.Cancel(press))
                                } catch (e: GestureCancellationException) {
                                    button1InteractionSource.emit(PressInteraction.Cancel(press))
                                } finally { isB1PhysicallyHeld = false }
                            },
                            onTap = {
                                Log.e("AppUI_Button1_BOX_DEBUG", "****** BUTTON 1 BOX TAP ******")
                                viewModel.handleButton1Press()
                            },
                            onLongPress = {
                                Log.e("AppUI_Button1_BOX_DEBUG", "****** BUTTON 1 BOX LONG PRESS ******")
                                viewModel.handleButton1Hold()
                            }
                        )
                    }
            ) {
                Image(
                    painter = painterResource(id = if (isButton1Pressed) R.drawable.r1b1p else R.drawable.r1b1),
                    contentDescription = "Button 1",
                    contentScale = ContentScale.Fit,
                    modifier = Modifier.fillMaxSize()
                )
            }

            Box(
                modifier = Modifier
                    .constrainAs(button2) {
                        bottom.linkTo(fader.top, margin = controlsBuffer)
                        end.linkTo(fader.end)
                    }
                    .size(width = newButtonWidth, height = newButtonHeight)
                    .pointerInput(Unit) {
                        detectTapGestures(
                            onPress = { offset ->
                                isB2PhysicallyHeld = true
                                val press = PressInteraction.Press(offset)
                                button2InteractionSource.emit(press)
                                try {
                                    if (tryAwaitRelease()) button2InteractionSource.emit(PressInteraction.Release(press))
                                    else button2InteractionSource.emit(PressInteraction.Cancel(press))
                                } catch (e: GestureCancellationException) {
                                    button2InteractionSource.emit(PressInteraction.Cancel(press))
                                } finally { isB2PhysicallyHeld = false }
                            },
                            onTap = {
                                Log.i("AppUI_Button2_BOX", "****** BUTTON 2 BOX TAP ******")
                                viewModel.handleButton2Press()
                            },
                            onDoubleTap = {
                                Log.i("AppUI_Button2_BOX", "****** BUTTON 2 BOX DOUBLE TAP ******")
                                viewModel.handleButton2DoublePress()
                            },
                            onLongPress = {
                                Log.i("AppUI_Button2_BOX", "****** BUTTON 2 BOX LONG PRESS ******")
                                viewModel.handleButton2Hold()
                            }
                        )
                    }
            ) {
                Image(
                    painter = painterResource(id = if (isButton2Pressed) R.drawable.r1b2p else R.drawable.r1b2),
                    contentDescription = "Button 2",
                    contentScale = ContentScale.Fit,
                    modifier = Modifier.fillMaxSize()
                )
            }
        }
    }
}

@Composable
fun PlatterView(
    vinylAngle: Float,
    visualPlatterAngle: Float,
    isTouched: Boolean,
    onPress: () -> Unit,
    onRelease: () -> Unit,
    onDrag: (angleDelta: Float) -> Unit,
    contentScaleFactor: Float,
    screenWidthDp: Dp,
    modifier: Modifier = Modifier
) {
    var previousAngle by remember { mutableFloatStateOf(0f) }
    var localCenterForDragCalc by remember { mutableStateOf(Offset.Zero) }
    val density = LocalDensity.current
    var platterActualRenderSizePx by remember { mutableStateOf(IntSize.Zero) }

    Box( // This Box is the one that receives the modifier from MainScreen
        modifier = modifier // This will be .fillMaxSize().padding(bottom=...)
            .aspectRatio(1f) // Makes this Box square based on its height (after padding)
            .onSizeChanged { platterActualRenderSizePx = it } // Store the actual size of this touchable, square Box
            .graphicsLayer { // Apply scaling and translation to THIS Box
                scaleX = contentScaleFactor
                scaleY = contentScaleFactor
                transformOrigin = TransformOrigin.Center

                val currentBoxWidthPx = size.width // Width of this Box after padding & aspect ratio
                val screenWidthPx = with(density) { screenWidthDp.toPx() }

                // Horizontal positioning:
                // Target X on screen for the center of the scaled platter visual
                val targetVisualCenterXOnScreenPx = (screenWidthPx / 2f) - (screenWidthPx * 1.1f)
                // Translate this Box so its center (after scaling) aligns with targetVisualCenterXOnScreenPx
                translationX = targetVisualCenterXOnScreenPx - (currentBoxWidthPx / 2f)

                // Vertical positioning:
                // The PlatterView Box is already pushed up by the padding in MainScreen.
                // Apply an *additional* upward shift for the visual effect.
                val visualUpwardShiftPx = with(density) { (screenWidthDp * contentScaleFactor * 0.18f).toPx() }
                translationY = -visualUpwardShiftPx // Shift the already positioned box up by this amount
            }
            .pointerInput(Unit) {
                detectTapGestures(
                    onPress = { touchOffset ->
                        // Hit detection is based on the platterActualRenderSizePx (unscaled size of this Box)
                        val centerX = platterActualRenderSizePx.width / 2f
                        val centerY = platterActualRenderSizePx.height / 2f
                        val distance = sqrt((touchOffset.x - centerX).pow(2) + (touchOffset.y - centerY).pow(2))
                        val radius = platterActualRenderSizePx.width / 2f // Using width as it's square

                        if (distance <= radius) {
                            Log.d("PlatterView", "onPress (within radius). Offset: $touchOffset")
                            localCenterForDragCalc = Offset(centerX, centerY)
                            onPress()
                        } else {
                            Log.d("PlatterView", "onPress IGNORED (outside radius). Offset: $touchOffset")
                        }
                    }
                )
            }
            .pointerInput(Unit) {
                detectDragGestures(
                    onDragStart = { touchOffset ->
                        if (localCenterForDragCalc != Offset.Zero) { // Drag only if press was valid
                            Log.d("PlatterView", "onDragStart (drag initiated after valid press). Offset: $touchOffset")
                            previousAngle = atan2(
                                touchOffset.y - localCenterForDragCalc.y, // Use localCenterForDragCalc
                                touchOffset.x - localCenterForDragCalc.x
                            ) * (180f / PI.toFloat())
                        } else {
                            Log.d("PlatterView", "onDragStart IGNORED (localCenterForDragCalc is Zero).")
                        }
                    },
                    onDrag = { change, dragAmount ->
                        if (localCenterForDragCalc != Offset.Zero) {
                            val currentAngle = atan2(
                                change.position.y - localCenterForDragCalc.y,
                                change.position.x - localCenterForDragCalc.x
                            ) * (180f / PI.toFloat())
                            var angleDelta = currentAngle - previousAngle
                            if (angleDelta > 180) angleDelta -= 360
                            if (angleDelta < -180) angleDelta += 360
                            onDrag(angleDelta)
                            previousAngle = currentAngle
                            change.consume()
                        }
                    },
                    onDragEnd = {
                        if (localCenterForDragCalc != Offset.Zero) {
                            Log.d("PlatterView", "onDragEnd triggered.")
                            onRelease()
                        }
                        localCenterForDragCalc = Offset.Zero
                    },
                    onDragCancel = {
                        if (localCenterForDragCalc != Offset.Zero) {
                            Log.d("PlatterView", "onDragCancel triggered.")
                            onRelease()
                        }
                        localCenterForDragCalc = Offset.Zero
                    }
                )
            }
    ) {
        // These Images are children of the Box that is scaled and translated.
        // Their fillMaxSize will make them fill this transformed (scaled and translated) Box.
        Image(
            painter = painterResource(id = R.drawable.platter),
            contentDescription = "Turntable Platter Base",
            modifier = Modifier
                .fillMaxSize()
                .graphicsLayer { rotationZ = visualPlatterAngle }, // Only rotation
            contentScale = ContentScale.Fit
        )
        Image(
            painter = painterResource(id = R.drawable.vinyl),
            contentDescription = "Vinyl Record",
            modifier = Modifier
                .fillMaxSize()
                .graphicsLayer { rotationZ = vinylAngle }, // Only rotation
            contentScale = ContentScale.Fit
        )
    }
}

@Composable
fun SettingsDialog(
    viewModel: AppViewModel,
    onDismiss: () -> Unit
) {
    Dialog(onDismissRequest = onDismiss) {
        Card(
            modifier = Modifier
                .fillMaxWidth(0.9f)
                .padding(16.dp),
            colors = CardDefaults.cardColors(containerColor = Color(0xFF333333))
        ) {
            Column(
                modifier = Modifier
                    .padding(16.dp)
                    .verticalScroll(rememberScrollState()),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Text("Settings", style = MaterialTheme.typography.headlineSmall, color = Color.White)
                Divider(color = Color.Gray, thickness = 1.dp, modifier = Modifier.padding(vertical = 8.dp))

                Text("Music Track Volume: ${(viewModel.musicMasterVolume * 100).toInt()}%", color = Color.White, fontSize = 14.sp)
                Slider(
                    value = viewModel.musicMasterVolume,
                    onValueChange = viewModel::onMusicMasterVolumeChange,
                    valueRange = 0f..1f,
                    modifier = Modifier.fillMaxWidth(0.9f),
                    colors = SliderDefaults.colors(
                        thumbColor = MaterialTheme.colorScheme.primary,
                        activeTrackColor = MaterialTheme.colorScheme.primary,
                        inactiveTrackColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.24f)
                    )
                )
                Spacer(modifier = Modifier.height(8.dp))

                Text("Slipmat Damping: ${(viewModel.slipmatDampingFactor * 1000).roundToInt() / 10f}%", color = Color.White, fontSize = 14.sp)
                Slider(
                    value = viewModel.slipmatDampingFactor,
                    onValueChange = viewModel::onSlipmatDampingChange,
                    valueRange = 0.005f..0.5f,
                    steps = 98,
                    modifier = Modifier.fillMaxWidth(0.9f),
                    colors = SliderDefaults.colors(
                        thumbColor = MaterialTheme.colorScheme.secondary,
                        activeTrackColor = MaterialTheme.colorScheme.secondary,
                        inactiveTrackColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.24f)
                    )
                )
                Text("Lower: Slower catch-up | Higher: Faster catch-up", color = Color.Gray, fontSize = 10.sp)
                Spacer(modifier = Modifier.height(8.dp))

                Text("Scratch Sensitivity: ${(viewModel.scratchSensitivitySetting * 1000).roundToInt() / 10f}", color = Color.White, fontSize = 14.sp)
                Slider(
                    value = viewModel.scratchSensitivitySetting,
                    onValueChange = viewModel::onScratchSensitivityChange,
                    valueRange = 0.005f..0.2f,
                    steps = 38,
                    modifier = Modifier.fillMaxWidth(0.9f),
                    colors = SliderDefaults.colors(
                        thumbColor = MaterialTheme.colorScheme.tertiary,
                        activeTrackColor = MaterialTheme.colorScheme.tertiary,
                        inactiveTrackColor = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.24f)
                    )
                )
                Text("Lower: Less sensitive | Higher: More sensitive", color = Color.Gray, fontSize = 10.sp)

                Spacer(modifier = Modifier.height(16.dp))
                Button(
                    onClick = onDismiss,
                    colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF555555))
                ) {
                    Text("Close", color = Color.White)
                }
            }
        }
    }
}

// Previews ... (Ensure they are complete and correct from previous versions)
@Preview(showBackground = false)
@Composable
fun SettingsDialogPreview() {
    MaterialTheme {
        val dummyAppViewModel = AppViewModel(
            onPlayIntroAndLoopOnPlatter = {}, onNextPlatterSample = {}, onLoadUserPlatterSample = {},
            onPlayMusicTrack = {}, onStopMusicTrack = {}, onNextMusicTrackAndPlay = {},
            onNextMusicTrackAndKeepState = {}, onLoadUserMusicTrack = {},
            onUpdatePlatterFaderVolume = {}, onUpdateMusicMasterVolume = {},
            onScratchPlatterActive = { _, _ -> }, onReleasePlatterTouch = {},
            onUpdateScratchSensitivity = {}
        )
        Box(Modifier.fillMaxSize().background(Color.Gray.copy(alpha = 0.5f)), contentAlignment = Alignment.Center) {
            SettingsDialog(
                viewModel = dummyAppViewModel,
                onDismiss = {}
            )
        }
    }
}

@Preview(showBackground = true, widthDp = 360, heightDp = 740)
@Composable
fun MainScreenPreview() {
    MaterialTheme {
        val dummyAppViewModel = AppViewModel(
            onPlayIntroAndLoopOnPlatter = {},
            onNextPlatterSample = {}, onLoadUserPlatterSample = {},
            onPlayMusicTrack = {}, onStopMusicTrack = {}, onNextMusicTrackAndPlay = {}, onNextMusicTrackAndKeepState = {}, onLoadUserMusicTrack = {},
            onUpdatePlatterFaderVolume = {}, onUpdateMusicMasterVolume = {},
            onScratchPlatterActive = { _, _ -> }, onReleasePlatterTouch = {},
            onUpdateScratchSensitivity = {}
        )
        MainScreen(viewModel = dummyAppViewModel)
    }
}

@Preview(showBackground = true, backgroundColor = 0xFF333333)
@Composable
fun PlatterViewPreview() {
    MaterialTheme {
        PlatterView(
            vinylAngle = 0f,
            visualPlatterAngle = 30f,
            isTouched = false,
            onPress = { Log.d("PlatterViewPreview", "onPress called") },
            onRelease = { Log.d("PlatterViewPreview", "onRelease called") },
            onDrag = { Log.d("PlatterViewPreview", "onDrag called with $it") },
            contentScaleFactor = 1.0f,
            screenWidthDp = 360.dp,
            modifier = Modifier.size(200.dp).background(Color.DarkGray)
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CustomFader(
    volume: Float,
    onVolumeChange: (Float) -> Unit,
    faderAreaHeight: Dp,
    modifier: Modifier = Modifier
) {
    val knobHeight = faderAreaHeight * 0.7f
    val knobImageIntrinsicAspectRatio = 1.5f
    val knobWidth = knobHeight * knobImageIntrinsicAspectRatio

    Box(
        modifier = modifier
    ) {
        Image(
            painter = painterResource(id = R.drawable.fplate),
            contentDescription = "Fader Plate",
            modifier = Modifier.fillMaxSize(),
            contentScale = ContentScale.FillWidth
        )
        Slider(
            value = volume,
            onValueChange = onVolumeChange,
            valueRange = 0f..1f,
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 85.dp),
            thumb = {
                Image(
                    painter = painterResource(id = R.drawable.faderknob),
                    contentDescription = "Fader Knob",
                    modifier = Modifier.size(width = knobWidth, height = knobHeight),
                    contentScale = ContentScale.Fit
                )
            },
            colors = SliderDefaults.colors(
                thumbColor = Color.Transparent,
                activeTrackColor = Color.Transparent,
                inactiveTrackColor = Color.Transparent
            )
        )
    }
}

@Preview(showBackground = true, backgroundColor = 0xFF222222, widthDp = 300, heightDp = 65)
@Composable
fun CustomFaderPreview() {
    MaterialTheme {
        CustomFader(
            volume = 0.5f,
            onVolumeChange = {},
            faderAreaHeight = 65.dp,
            modifier = Modifier.height(65.dp).fillMaxWidth()
        )
    }
}
