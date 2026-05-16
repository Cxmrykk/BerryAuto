# Relevant Source Code For `headunit-revived` GitHub Repo

- Full Source: [https://github.com/andreknieriem/headunit-revived](https://github.com/andreknieriem/headunit-revived)

`app/src/main/java/com/andrerinas/headunitrevived/utils/SetupWizard.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.content.Context
import androidx.appcompat.app.AlertDialog
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.R
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.util.concurrent.atomic.AtomicBoolean

class SetupWizard(private val context: Context, private val onFinished: () -> Unit) {

    private val settings = App.provide(context).settings
    private val optimizer = SystemOptimizer(context)

    private var selectedSize: SystemOptimizer.DisplaySizePreset = SystemOptimizer.DisplaySizePreset.STANDARD_9_10
    private var selectedPortrait: Boolean = false

    fun start() {
        showWelcome()
    }

    private fun showWelcome() {
        MaterialAlertDialogBuilder(context, R.style.DarkAlertDialog)
            .setTitle(R.string.setup_welcome_title)
            .setMessage(R.string.setup_welcome_msg)
            .setCancelable(false)
            .setPositiveButton(R.string.setup_start) { _, _ ->
                showSizeSelection()
            }
            .setNegativeButton(R.string.cancel) { _, _ ->
                settings.hasCompletedSetupWizard = true // Don't show again even if cancelled
                onFinished()
            }
            .show()
    }

    private fun showSizeSelection() {
        val options = arrayOf(
            context.getString(R.string.setup_size_phone),     // 4-6"
            context.getString(R.string.setup_size_small),     // 7-8"
            context.getString(R.string.setup_size_standard),  // 9-10"
            context.getString(R.string.setup_size_large)      // 11"+
        )

        MaterialAlertDialogBuilder(context, R.style.DarkAlertDialog)
            .setTitle(R.string.setup_size_title)
            .setCancelable(false)
            .setItems(options) { _, which ->
                selectedSize = when (which) {
                    0 -> SystemOptimizer.DisplaySizePreset.PHONE_4_6
                    1 -> SystemOptimizer.DisplaySizePreset.SMALL_7_8
                    2 -> SystemOptimizer.DisplaySizePreset.STANDARD_9_10
                    else -> SystemOptimizer.DisplaySizePreset.LARGE_11_PLUS
                }
                showOrientationSelection()
            }
            .setNeutralButton(R.string.back) { _, _ -> showWelcome() }
            .setNegativeButton(R.string.cancel) { _, _ ->
                settings.hasCompletedSetupWizard = true
                onFinished()
            }
            .show()
    }

    private fun showOrientationSelection() {
        val options = arrayOf(
            context.getString(R.string.setup_orientation_landscape),
            context.getString(R.string.setup_orientation_portrait)
        )

        MaterialAlertDialogBuilder(context, R.style.DarkAlertDialog)
            .setTitle(R.string.setup_orientation_title)
            .setCancelable(false)
            .setItems(options) { _, which ->
                selectedPortrait = (which == 1)
                runOptimization()
            }
            .setNeutralButton(R.string.back) { _, _ -> showSizeSelection() }
            .setNegativeButton(R.string.cancel) { _, _ ->
                settings.hasCompletedSetupWizard = true
                onFinished()
            }
            .show()
    }

    private fun runOptimization() {
        val result = optimizer.calculateOptimalSettings(selectedSize, selectedPortrait)

        val summary = StringBuilder()
        summary.append("${context.getString(R.string.resolution)}: ${Settings.Resolution.fromId(result.recommendedResolutionId)?.resName}\n")
        summary.append("${context.getString(R.string.video_codec)}: ${result.recommendedVideoCodec}\n")
        summary.append("${context.getString(R.string.view_mode)}: ${result.recommendedViewMode.name}\n")
        summary.append("${context.getString(R.string.dpi)}: ${result.recommendedDpi}\n")

        if (result.isWidescreen) {
            summary.append("${context.getString(R.string.setup_widescreen_detected)}\n")
        }

        MaterialAlertDialogBuilder(context, R.style.DarkAlertDialog)
            .setTitle(R.string.setup_result_title)
            .setMessage(summary.toString())
            .setCancelable(false)
            .setPositiveButton(R.string.setup_apply) { _, _ ->
                applySettings(result)
            }
            .setNeutralButton(R.string.back) { _, _ -> showOrientationSelection() }
            .setNegativeButton(R.string.cancel) { _, _ ->
                settings.hasCompletedSetupWizard = true
                onFinished()
            }
            .show()
    }

    private fun applySettings(result: SystemOptimizer.OptimizationResult) {
        settings.resolutionId = result.recommendedResolutionId
        settings.videoCodec = result.recommendedVideoCodec
        settings.viewMode = result.recommendedViewMode
        settings.dpiPixelDensity = result.recommendedDpi
        settings.screenOrientation = result.suggestedOrientation
        settings.hasCompletedSetupWizard = true

        onFinished()
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/utils/SystemOptimizer.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.content.Context
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.os.Build
import android.util.DisplayMetrics
import android.view.WindowManager
import kotlin.math.sqrt

class SystemOptimizer(private val context: Context) {

    data class OptimizationResult(
        val recommendedResolutionId: Int,
        val recommendedDpi: Int,
        val recommendedVideoCodec: String,
        val recommendedViewMode: Settings.ViewMode,
        val isWidescreen: Boolean,
        val suggestedOrientation: Settings.ScreenOrientation,
        val h265Support: Boolean
    )

    enum class DisplaySizePreset(val diagonalInch: Float) {
        PHONE_4_6(6.0f),
        SMALL_7_8(7.5f),
        STANDARD_9_10(10.0f),
        LARGE_11_PLUS(12.5f)
    }

    private fun isReliableHevcChipset(): Boolean {
        val hw = Build.HARDWARE.lowercase()
        val soc = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            Build.SOC_MANUFACTURER.lowercase()
        } else ""

        // Only allow known-reliable chipsets for high-bitrate HEVC Auto-Discovery.
        // This avoids issues with low-end devices that 'lie' about stable support.
        return hw.startsWith("qcom") || hw.startsWith("msm") || // Qualcomm
               hw.startsWith("exynos") || // Samsung
               hw.startsWith("gs") || hw.contains("google") || // Google Tensor
               soc.contains("qualcomm") || soc.contains("samsung") || soc.contains("google") ||
               // High-end MediaTek (Dimensity 700/800/900/1000/9000+ series)
               hw.startsWith("mt68") || hw.startsWith("mt69")
    }

    fun checkH265HardwareSupport(): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) return false

        // Hardening: even if a codec exists, we skip it if the chipset is known to be unreliable
        if (!isReliableHevcChipset()) return false

        val codecList = MediaCodecList(MediaCodecList.ALL_CODECS)
        for (info in codecList.codecInfos) {
            if (info.isEncoder) continue
            for (type in info.supportedTypes) {
                if (type.equals("video/hevc", ignoreCase = true)) {
                    val name = info.name.lowercase()
                    // Filter out known software codecs
                    val isSoftware = name.startsWith("omx.google.") ||
                                   name.startsWith("c2.android.") ||
                                   name.startsWith("omx.ffmpeg.") ||
                                   name.contains(".sw.") ||
                                   name.contains("software")

                    if (!isSoftware) return true
                }
            }
        }
        return false
    }

    fun calculateOptimalSettings(
        sizePreset: DisplaySizePreset,
        isPortraitTarget: Boolean
    ): OptimizationResult {
        val windowManager = context.getSystemService(Context.WINDOW_SERVICE) as WindowManager
        val metrics = DisplayMetrics()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            context.display?.getRealMetrics(metrics)
        } else {
            @Suppress("DEPRECATION")
            windowManager.defaultDisplay.getRealMetrics(metrics)
        }

        val width = metrics.widthPixels.toFloat()
        val height = metrics.heightPixels.toFloat()
        val densityDpi = metrics.densityDpi
        val pixelDiagonal = sqrt(width * width + height * height)
        val aspectRatio = if (width > height) width / height else height / width

        val hasH265 = checkH265HardwareSupport()

        // 1. Resolution Recommendation
        val recResId = when {
            width >= 2560 && hasH265 -> 4
            width >= 1080 || densityDpi >= 320 || aspectRatio > 2.0f -> 3
            else -> 2
        }

        // 2. DPI Strategy
        val calculatedDpi = (pixelDiagonal / sizePreset.diagonalInch) * 1.2f
        var recDpi = calculatedDpi.toInt()

        // 3. View Mode Recommendation
        val recViewMode = when {
            // Very old devices (Android 4.x) often have distortion issues with SurfaceView,
            // so we recommend TextureView instead.
            Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP -> Settings.ViewMode.TEXTURE

            // Middle-aged devices (5.0 - 8.1) often perform best with GLES20
            Build.VERSION.SDK_INT <= Build.VERSION_CODES.O_MR1 -> Settings.ViewMode.GLES

            // Modern devices are usually fine with the default TextureView
            else -> Settings.ViewMode.TEXTURE
        }

        // 4. Apply orientation-based caps
        if (isPortraitTarget) {
            recDpi = recDpi.coerceAtMost(190)
        } else {
            recDpi = recDpi.coerceAtMost(240)
        }

        recDpi = recDpi.coerceAtLeast(110)

        return OptimizationResult(
            recommendedResolutionId = recResId,
            recommendedDpi = recDpi,
            recommendedVideoCodec = if (hasH265) "H.265" else "H.264",
            recommendedViewMode = recViewMode,
            isWidescreen = aspectRatio > 1.7f,
            suggestedOrientation = if (isPortraitTarget) Settings.ScreenOrientation.PORTRAIT else Settings.ScreenOrientation.LANDSCAPE,
            h265Support = hasH265
        )
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/utils/LocaleHelper.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.content.Context
import android.content.res.Configuration
import android.os.Build
import com.andrerinas.headunitrevived.BuildConfig
import java.util.Locale

object LocaleHelper {

    const val SYSTEM_DEFAULT = ""

    /**
     * Gets available locales that the app has translations for.
     *
     * The list is determined at build time by scanning the res/values-* directories
     * for those containing strings.xml files. This is stored in BuildConfig.AVAILABLE_LOCALES
     * so new translations are automatically included when contributors add values-XX folders.
     *
     * English is always included as it's the default language in the base "values/" folder.
     */
    fun getAvailableLocales(context: Context): List<Locale> {
        return try {
            val localesFromBuildConfig = BuildConfig.AVAILABLE_LOCALES
                .takeIf { it.isNotBlank() }
                ?.split(",")
                ?.mapNotNull { parseLocale(it.trim()) }
                ?: emptyList()

            // Always include English as it's the default language (in values/ folder)
            val allLocales = localesFromBuildConfig + Locale.ENGLISH

            allLocales
                .distinctBy { it.language + "_" + it.country }
                .sortedBy { it.getDisplayName(it).lowercase() }
        } catch (e: Exception) {
            // Fallback: at minimum return English
            listOf(Locale.ENGLISH)
        }
    }

    /**
     * Parse a locale string like "es", "pt-rBR", "zh-rTW" into a Locale object.
     * Handles Android resource qualifier format where region is prefixed with 'r'.
     */
    private fun parseLocale(localeString: String): Locale? {
        if (localeString.isBlank()) return null

        // Android resource locales use format like "pt-rBR" for regional variants
        // Convert to standard format: "pt-rBR" -> "pt-BR"
        val normalized = localeString.replace("-r", "-")
        val parts = normalized.split("-", "_")

        return when (parts.size) {
            1 -> Locale(parts[0])
            2 -> Locale(parts[0], parts[1])
            3 -> Locale(parts[0], parts[1], parts[2])
            else -> null
        }
    }

    /**
     * Converts a Locale to a storage string format.
     */
    fun localeToString(locale: Locale?): String {
        if (locale == null) return SYSTEM_DEFAULT
        return if (locale.country.isNotEmpty()) {
            "${locale.language}-${locale.country}"
        } else {
            locale.language
        }
    }

    /**
     * Converts a stored string back to a Locale.
     */
    fun stringToLocale(localeString: String): Locale? {
        if (localeString.isEmpty()) return null
        return parseLocale(localeString)
    }

    /**
     * Gets the display name for a locale in its own language.
     */
    fun getDisplayName(locale: Locale): String {
        val displayName = locale.getDisplayName(locale)
        // Capitalize first letter
        return displayName.replaceFirstChar {
            if (it.isLowerCase()) it.titlecase(locale) else it.toString()
        }
    }

    /**
     * Applies the selected locale to a context.
     * Returns a new Context with the applied locale.
     */
    fun applyLocale(context: Context, settings: Settings): Context {
        val localeString = settings.appLanguage
        if (localeString.isEmpty()) {
            // Use system default
            return context
        }

        val locale = stringToLocale(localeString) ?: return context
        Locale.setDefault(locale)

        val config = Configuration(context.resources.configuration)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            config.setLocale(locale)
            config.setLocales(android.os.LocaleList(locale))
        } else {
            @Suppress("DEPRECATION")
            config.locale = locale
        }

        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1) {
            context.createConfigurationContext(config)
        } else {
            @Suppress("DEPRECATION")
            context.resources.updateConfiguration(config, context.resources.displayMetrics)
            context
        }
    }

    /**
     * Updates the configuration for the given context.
     * Use this in attachBaseContext of Activities.
     */
    fun wrapContext(context: Context): Context {
        val settings = Settings(context)
        return applyLocale(context, settings)
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/utils/HeadUnitScreenConfig.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.content.Context
import android.os.Build
import android.util.DisplayMetrics
import com.andrerinas.headunitrevived.aap.protocol.proto.Control
import kotlin.math.roundToInt

object HeadUnitScreenConfig {

    private var screenWidthPx: Int = 0
    private var screenHeightPx: Int = 0
    private var density: Float = 1.0f
    private var densityDpi: Int = 240
    private var scaleFactor: Float = 1.0f
    private var isSmallScreen: Boolean = true
    private var isPortraitScaled: Boolean = false
    private var isInitialized: Boolean = false

    // Flag to determine if the projection should stretch and ignore aspect ratio
    private var stretchToFill: Boolean = false

    // Forced scale for older devices (Legacy fix)
    var forcedScale: Boolean = false
        private set

    var negotiatedResolutionType: Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType = Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._800x480
    var isResolutionLocked: Boolean = false
        private set

    private lateinit var currentSettings: Settings // Store settings instance

    // System Insets (Bars/Cutouts)
    var systemInsetLeft: Int = 0
        private set
    var systemInsetTop: Int = 0
        private set
    var systemInsetRight: Int = 0
        private set
    var systemInsetBottom: Int = 0
        private set

    // Raw Screen Dimensions (Full Display)
    private var realScreenWidthPx: Int = 0
    private var realScreenHeightPx: Int = 0


    fun init(context: Context, displayMetrics: DisplayMetrics, settings: Settings) {
        stretchToFill = settings.stretchToFill
        forcedScale = settings.forcedScale && settings.viewMode == Settings.ViewMode.SURFACE

        val realW: Int
        val realH: Int
        val usableW: Int
        val usableH: Int

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) { // API 30+
            val windowManager = context.getSystemService(android.view.WindowManager::class.java)
            val bounds = windowManager.currentWindowMetrics.bounds
            // On API 30+, bounds on an Activity context often return the usable area.
            // We use the displayMetrics as a fallback for the physical area.
            realW = displayMetrics.widthPixels
            realH = displayMetrics.heightPixels
            usableW = bounds.width()
            usableH = bounds.height()
        } else { // Older APIs
            @Suppress("DEPRECATION")
            val windowManager = context.getSystemService(Context.WINDOW_SERVICE) as android.view.WindowManager
            val display = windowManager.defaultDisplay
            val size = android.graphics.Point()
            @Suppress("DEPRECATION")
            display.getRealSize(size)
            realW = size.x
            realH = size.y

            @Suppress("DEPRECATION")
            display.getSize(size)
            usableW = size.x
            usableH = size.y
        }

        // Only update if dimensions or settings changed
        if (isInitialized && realScreenWidthPx == realW && realScreenHeightPx == realH && this::currentSettings.isInitialized && currentSettings == settings) {
            return
        }

        isInitialized = true
        currentSettings = settings

        // Determine if we are planning to hide the bars (Immersive)
        val immersive = settings.fullscreenMode == Settings.FullscreenMode.IMMERSIVE ||
                        settings.fullscreenMode == Settings.FullscreenMode.IMMERSIVE_WITH_NOTCH

        // THE ANCHOR:
        // If we are immersive, our "World" is the physical screen.
        // If we are NOT, our "World" is limited to the usable window area (no lying to AA).
        val defaultAnchorW = if (immersive) realW else usableW
        val defaultAnchorH = if (immersive) realH else usableH

        density = displayMetrics.density
        densityDpi = displayMetrics.densityDpi

        // Initial Insets: For non-immersive, the bars are already baked into the anchor (realSize = 736),
        // so we start with 0 system insets and just add manual settings.
        systemInsetLeft = settings.insetLeft
        systemInsetTop = settings.insetTop
        systemInsetRight = settings.insetRight
        systemInsetBottom = settings.insetBottom

        // Check if we have cached surface dimensions from a previous session.
        // If the settings haven't changed (same hash), use the cached values
        // to avoid a mid-session UpdateUiConfigRequest and potential flicker.
        val currentHash = computeSettingsHash(settings)
        val cachedW = settings.cachedSurfaceWidth
        val cachedH = settings.cachedSurfaceHeight
        val cachedHash = settings.cachedSurfaceSettingsHash

        if (cachedW > 0 && cachedH > 0 && cachedHash == currentHash) {
            // Cached surface dimensions are the usable area. The anchor includes insets.
            realScreenWidthPx = cachedW + systemInsetLeft + systemInsetRight
            realScreenHeightPx = cachedH + systemInsetTop + systemInsetBottom
            AppLog.i("[UI_DEBUG_FIX] HeadUnitScreenConfig: Using cached surface dimensions: ${cachedW}x${cachedH} (anchor: ${realScreenWidthPx}x${realScreenHeightPx})")
        } else {
            realScreenWidthPx = defaultAnchorW
            realScreenHeightPx = defaultAnchorH
            if (cachedW > 0) {
                AppLog.i("[UI_DEBUG_FIX] HeadUnitScreenConfig: Cache invalidated (hash mismatch: stored=$cachedHash, current=$currentHash)")
            }
        }

        AppLog.i("[UI_DEBUG] HeadUnitScreenConfig: Honest Init | Mode: ${settings.fullscreenMode} | Anchor: ${realScreenWidthPx}x${realScreenHeightPx} | Seeded Insets: L$systemInsetLeft T$systemInsetTop R$systemInsetRight B$systemInsetBottom")

        recalculate()
    }

    fun updateInsets(left: Int, top: Int, right: Int, bottom: Int) {
        if (systemInsetLeft == left && systemInsetTop == top && systemInsetRight == right && systemInsetBottom == bottom) {
            return
        }

        systemInsetLeft = left
        systemInsetTop = top
        systemInsetRight = right
        systemInsetBottom = bottom

        if (isInitialized) {
            recalculate()
        }
    }

    private fun recalculate() {
        // Calculate USABLE area
        screenWidthPx = realScreenWidthPx - systemInsetLeft - systemInsetRight
        screenHeightPx = realScreenHeightPx - systemInsetTop - systemInsetBottom

        if (screenWidthPx <= 0 || screenHeightPx <= 0) {
            screenWidthPx = realScreenWidthPx
            screenHeightPx = realScreenHeightPx
        }

        val selectedResolution = Settings.Resolution.fromId(currentSettings.resolutionId)
        val isPortraitDisplay = screenHeightPx > screenWidthPx

        // 1. Determine base negotiated resolution
        if (isResolutionLocked) {
            AppLog.i("[UI_DEBUG] CarScreen: RESOLUTION LOCKED to $negotiatedResolutionType. Skipping re-negotiation.")
        } else if (selectedResolution == Settings.Resolution.AUTO) {
            if (isPortraitDisplay) {
                negotiatedResolutionType = if (screenWidthPx > 720 || screenHeightPx > 1280) {
                    Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._1080x1920
                } else {
                    Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._720x1280
                }
            } else {
                negotiatedResolutionType = when {
                    screenWidthPx <= 800 && screenHeightPx <= 480 -> Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._800x480
                    (screenWidthPx >= 3840 || screenHeightPx >= 2160) && com.andrerinas.headunitrevived.decoder.VideoDecoder.isHevcSupported() && Build.VERSION.SDK_INT >= 24 ->
                        Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._3840x2160
                    (screenWidthPx >= 2560 || screenHeightPx >= 1440) && com.andrerinas.headunitrevived.decoder.VideoDecoder.isHevcSupported() && Build.VERSION.SDK_INT >= 24 ->
                        Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._2560x1440
                    screenWidthPx > 1280 || screenHeightPx > 720 -> Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._1920x1080
                    else -> Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._1280x720
                }
            }
        } else {
            // Manual selection: Map to correct orientation
            val codec = selectedResolution?.codec ?: Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._800x480
            negotiatedResolutionType = if (isPortraitDisplay) {
                when (selectedResolution) {
                    Settings.Resolution._800x480 -> Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._720x1280
                    Settings.Resolution._1280x720 -> Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._720x1280
                    Settings.Resolution._1920x1080 -> Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._1080x1920
                    Settings.Resolution._2560x1440 -> Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._1440x2560
                    Settings.Resolution._3840x2160 -> Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._2160x3840
                    else -> codec
                }
            } else {
                codec
            }
        }

        // 2. Perform scaling calculations (now safe because negotiatedResolutionType is set)
        AppLog.i("[UI_DEBUG] CarScreen: usable area ${screenWidthPx}x${screenHeightPx}, using $negotiatedResolutionType")

        if (screenHeightPx > screenWidthPx) {
            isSmallScreen = screenWidthPx <= 1080 && screenHeightPx <= 1920
        } else {
            isSmallScreen = screenWidthPx <= 1920 && screenHeightPx <= 1080
        }

        scaleFactor = 1.0f
        if (!isSmallScreen) {
            val sWidth = screenWidthPx.toFloat()
            val sHeight = screenHeightPx.toFloat()
            if (getNegotiatedWidth() > 0 && getNegotiatedHeight() > 0) {
                 if (sWidth / sHeight < getAspectRatio()) {
                    isPortraitScaled = true
                    scaleFactor = sHeight / getNegotiatedHeight().toFloat()
                } else {
                    isPortraitScaled = false
                    scaleFactor = sWidth / getNegotiatedWidth().toFloat()
                }
            }
        }

        AppLog.i("[UI_DEBUG] CarScreen isSmallScreen: $isSmallScreen, scaleFactor: $scaleFactor, margins: w=${getWidthMargin()}, h=${getHeightMargin()}")
    }

    fun getAdjustedHeight(): Int {
        return (getNegotiatedHeight() * scaleFactor).roundToInt()
    }

    fun getAdjustedWidth(): Int {
        return (getNegotiatedWidth() * scaleFactor).roundToInt()
    }

    private fun getAspectRatio(): Float {
        return getNegotiatedWidth().toFloat() / getNegotiatedHeight().toFloat()
    }

    fun getNegotiatedHeight(): Int {
        val resString = negotiatedResolutionType.toString().replace("_", "")
        return try {
            resString.split("x")[1].toInt()
        } catch (e: Exception) {
            480
        }
    }

    fun getNegotiatedWidth(): Int {
        val resString = negotiatedResolutionType.toString().replace("_", "")
        return try {
            resString.split("x")[0].toInt()
        } catch (e: Exception) {
            800
        }
    }

    fun getHeightMargin(): Int {
        val margin = ((getAdjustedHeight() - screenHeightPx) / scaleFactor).roundToInt()
        return margin.coerceAtLeast(0)
    }

    fun getWidthMargin(): Int {
        val margin = ((getAdjustedWidth() - screenWidthPx) / scaleFactor).roundToInt()
        return margin.coerceAtLeast(0)
    }

    private fun divideOrOne(numerator: Float, denominator: Float): Float {
        return if (denominator == 0.0f) 1.0f else numerator / denominator
    }

    fun getScaleX(): Float {
        if (forcedScale) {
            return 1.0f
        }

        if (getNegotiatedWidth() > screenWidthPx) {
            return divideOrOne(getNegotiatedWidth().toFloat(), screenWidthPx.toFloat())
        }
        if (isPortraitScaled) {
            return divideOrOne(getAspectRatio(), (screenWidthPx.toFloat() / screenHeightPx.toFloat()))
        }
        return 1.0f
    }
        // Stretch option PR #259
    fun getScaleY(): Float {
        if (forcedScale) {
            return 1.0f
        }

        if (getNegotiatedHeight() > screenHeightPx) {
            return if (stretchToFill) {
                // Before PR #233 Fix scaler Y
                divideOrOne(getNegotiatedHeight().toFloat(), screenHeightPx.toFloat())
            } else {
                // After PR #233 Fix scaler Y
                divideOrOne((screenWidthPx.toFloat() / screenHeightPx.toFloat()), getAspectRatio())
            }
        }

        if (isPortraitScaled) {
            return 1.0f
        }

        return divideOrOne((screenWidthPx.toFloat() / screenHeightPx.toFloat()), getAspectRatio())
    }

    fun getDensityDpi(): Int {
        return if (this::currentSettings.isInitialized && currentSettings.dpiPixelDensity != 0) {
            currentSettings.dpiPixelDensity
        } else {
            densityDpi
        }
    }

    fun getUsableWidth(): Int = screenWidthPx
    fun getUsableHeight(): Int = screenHeightPx

    // --- Per-side margins for UpdateUiConfigRequest (matching HUR's pattern) ---
    // These are half the total margin, distributed symmetrically.
    fun getLeftMargin(): Int = getWidthMargin() / 2
    fun getRightMargin(): Int = getWidthMargin() - getLeftMargin()
    fun getTopMargin(): Int = getHeightMargin() / 2
    fun getBottomMargin(): Int = getHeightMargin() - getTopMargin()

    /**
     * Called when the actual rendering surface dimensions become known (from onSurfaceChanged).
     * Compares with the current usable area and updates the anchor if they differ.
     * @return true if the dimensions changed and margins need to be re-sent to AA.
     */
    fun updateSurfaceDimensions(surfaceW: Int, surfaceH: Int): Boolean {
        val diffW = kotlin.math.abs(surfaceW - screenWidthPx)
        val diffH = kotlin.math.abs(surfaceH - screenHeightPx)

        if (diffW <= SURFACE_MISMATCH_TOLERANCE && diffH <= SURFACE_MISMATCH_TOLERANCE) {
            return false
        }

        AppLog.i("[UI_DEBUG_FIX] Surface mismatch detected! Usable: ${screenWidthPx}x${screenHeightPx}, Actual surface: ${surfaceW}x${surfaceH} (diff: ${diffW}x${diffH})")

        // Update anchor: the surface dimensions ARE the real usable area,
        // so the anchor is the usable area plus insets.
        realScreenWidthPx = surfaceW + systemInsetLeft + systemInsetRight
        realScreenHeightPx = surfaceH + systemInsetTop + systemInsetBottom

        recalculate()

        AppLog.i("[UI_DEBUG_FIX] Recalculated: usable=${screenWidthPx}x${screenHeightPx}, margins: w=${getWidthMargin()}, h=${getHeightMargin()}, per-side: L=${getLeftMargin()} T=${getTopMargin()} R=${getRightMargin()} B=${getBottomMargin()}")
        return true
    }

    /**
     * Computes a hash of all settings that affect screen dimensions.
     * Used to invalidate the cached surface dimensions when settings change.
     */
    fun computeSettingsHash(settings: Settings): Int {
        var hash = 17
        hash = 31 * hash + settings.resolutionId
        hash = 31 * hash + settings.dpiPixelDensity
        hash = 31 * hash + settings.insetLeft
        hash = 31 * hash + settings.insetTop
        hash = 31 * hash + settings.insetRight
        hash = 31 * hash + settings.insetBottom
        hash = 31 * hash + settings.viewMode.ordinal
        hash = 31 * hash + settings.screenOrientation.ordinal
        hash = 31 * hash + settings.fullscreenMode.value
        hash = 31 * hash + (if (settings.stretchToFill) 1 else 0)
        hash = 31 * hash + (if (settings.forcedScale) 1 else 0)
        return hash
    }

    fun lockResolution() {
        AppLog.i("[UI_DEBUG] HeadUnitScreenConfig: Locking resolution.")
        isResolutionLocked = true
    }

    fun unlockResolution() {
        AppLog.i("[UI_DEBUG] HeadUnitScreenConfig: Unlocking resolution.")
        isResolutionLocked = false
    }

    private const val SURFACE_MISMATCH_TOLERANCE = 4
}

```

`app/src/main/java/com/andrerinas/headunitrevived/utils/DeviceIntent.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.content.Intent
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager

class DeviceIntent(private val intent: Intent?) {
    val device: UsbDevice?
        get() = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
            intent?.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent?.getParcelableExtra(UsbManager.EXTRA_DEVICE)
        }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/utils/LogExporter.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.content.Context
import android.content.Intent
import android.util.Log
import androidx.core.content.FileProvider
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object LogExporter {

    enum class LogLevel(val filter: String, val logLevel: Int) {
        VERBOSE("*:V", Log.VERBOSE),
        DEBUG("*:D", Log.DEBUG),
        INFO("*:I", Log.INFO),
        WARNING("*:W", Log.WARN),
        ERROR("*:E", Log.ERROR)
    }

    private const val MAX_LOG_FILES = 10
    private const val MAX_TOTAL_SIZE = 50L * 1024 * 1024 // 50 MB

    private var captureProcess: Process? = null
    private var captureThread: Thread? = null
    private var captureFile: File? = null
    private var captureVerbosity: LogLevel = LogLevel.DEBUG
    private var captureRestarts = 0
    private const val MAX_RESTARTS = 5

    val isCapturing: Boolean get() = captureProcess != null

    /**
     * Deletes the oldest HUR_Log_* files until the count is below [MAX_LOG_FILES]
     * and the total size is below [MAX_TOTAL_SIZE], preserving the most recent files.
     */
    private fun rotateLogs(logDir: File) {
        val files = logDir.listFiles { _, name -> name.startsWith("HUR_Log_") }
            ?.sortedBy { it.lastModified() }
            ?.toMutableList() ?: return

        while (files.size >= MAX_LOG_FILES) {
            files.removeAt(0).delete()
        }

        var totalSize = files.sumOf { it.length() }
        while (totalSize > MAX_TOTAL_SIZE && files.isNotEmpty()) {
            val oldest = files.removeAt(0)
            totalSize -= oldest.length()
            oldest.delete()
        }
    }

    /**
     * Starts a continuous logcat process writing to a timestamped file.
     * Unlike [saveLogToPublicFile], this captures everything from the moment it is called,
     * bypassing the small shared ring buffer.
     */
    fun startCapture(context: Context, verbosity: LogLevel) {
        stopCapture()
        val logDir = context.getExternalFilesDir(null) ?: return
        logDir.mkdirs()
        rotateLogs(logDir)

        val timeStamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val file = File(logDir, "HUR_Log_$timeStamp.txt")
        captureFile = file
        captureVerbosity = verbosity
        captureRestarts = 0

        launchLogcatPipe(file, verbosity)
    }

    /**
     * Spawns a logcat process piping stdout into [file] (append mode).
     * When the process exits unexpectedly, restarts automatically up to [MAX_RESTARTS] times
     * so a system-killed logcat doesn't silently stop the capture.
     */
    private fun launchLogcatPipe(file: File, verbosity: LogLevel) {
        try {
            val process = Runtime.getRuntime().exec(
                arrayOf("logcat", "-v", "threadtime", verbosity.filter)
            )
            captureProcess = process
            captureThread = Thread {
                try {
                    FileOutputStream(file, true).use { out ->
                        process.inputStream.copyTo(out)
                    }
                } catch (_: IOException) { }
                // copyTo returned — logcat process died or was intentionally stopped
                if (captureProcess === process && captureRestarts < MAX_RESTARTS) {
                    captureRestarts++
                    AppLog.w("Log capture process exited, restarting (attempt $captureRestarts/$MAX_RESTARTS)")
                    try { Thread.sleep(2000) } catch (_: InterruptedException) { return@Thread }
                    launchLogcatPipe(file, verbosity)
                }
            }.also { it.isDaemon = true; it.start() }
        } catch (e: IOException) {
            AppLog.e("Failed to start log capture", e)
            captureFile = null
        }
    }

    /** Stops the continuous capture process. */
    fun stopCapture() {
        captureProcess?.destroy()
        captureProcess = null
        captureThread?.join(2000)
        captureThread = null
    }

    /**
     * Writes logs to a timestamped file and returns it.
     * - If a capture file is available (capture was started, active or already stopped):
     *   copies its content into a fresh export file so the original capture file is preserved.
     * - Otherwise: dumps the current logcat ring buffer.
     */
    fun saveLogToPublicFile(context: Context, verbosity: LogLevel): File? {
        val logDir = context.getExternalFilesDir(null) ?: return null
        if (!logDir.exists()) logDir.mkdirs()

        val source = captureFile
        if (source != null && source.exists() && source.length() > 0) {
            return source
        }

        return try {
            rotateLogs(logDir)
            val timeStamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
            val logFile = File(logDir, "HUR_Log_$timeStamp.txt")
            // Use stdout piping instead of -f flag; -f is unreliable on Android 4.4.
            val process = Runtime.getRuntime().exec(
                arrayOf("logcat", "-d", "-v", "threadtime", verbosity.filter)
            )
            FileOutputStream(logFile).use { out ->
                process.inputStream.copyTo(out)
            }
            process.waitFor()
            logFile
        } catch (e: Exception) {
            AppLog.e("Failed to save logs", e)
            null
        }
    }

    fun shareLogFile(context: Context, file: File) {
        val uri = FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", file)

        val shareIntent = Intent(Intent.ACTION_SEND).apply {
            type = "text/plain"
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }

        val chooser = Intent.createChooser(shareIntent, "Share Log File")
        chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        context.startActivity(chooser)
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/utils/TwilightCalculator.java`:

```java
/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.andrerinas.headunitrevived.utils;
import android.text.format.DateUtils;

public class TwilightCalculator {
    /** Value of {@link #mState} if it is currently day */
    public static final int DAY = 0;
    /** Value of {@link #mState} if it is currently night */
    public static final int NIGHT = 1;
    private static final float DEGREES_TO_RADIANS = (float) (Math.PI / 180.0f);
    // element for calculating solar transit.
    private static final float J0 = 0.0009f;
    // correction for "early" sunrise/sunset to prevent glare.
    // +1.0 degree in radians (approx 4 minutes before actual sunset)
    private static final float ALTIDUTE_CORRECTION_EARLY_NIGHT = 0.017453293f;
    // coefficients for calculating Equation of Center.
    private static final float C1 = 0.0334196f;
    private static final float C2 = 0.000349066f;
    private static final float C3 = 0.000005236f;
    private static final float OBLIQUITY = 0.40927971f;
    // Java time on Jan 1, 2000 12:00 UTC.
    private static final long UTC_2000 = 946728000000L;
    /**
     * Time of sunset in milliseconds or -1 in the case the day
     * or night never ends.
     */
    public long mSunset;
    /**
     * Time of sunrise in milliseconds or -1 in the case the
     * day or night never ends.
     */
    public long mSunrise;
    /** Current state */
    public int mState;
    /**
     * calculates the sunrise/sunset bases on time and geo-coordinates.
     *
     * @param time time in milliseconds.
     * @param latitude latitude in degrees.
     * @param longitude latitude in degrees.
     */
    public void calculateTwilight(long time, double latitude, double longitude) {
        final float daysSince2000 = (float) (time - UTC_2000) / DateUtils.DAY_IN_MILLIS;
        // mean anomaly
        final float meanAnomaly = 6.240059968f + daysSince2000 * 0.01720197f;
        // true anomaly
        final double trueAnomaly = meanAnomaly + C1 * Math.sin(meanAnomaly) + C2
                * Math.sin(2 * meanAnomaly) + C3 * Math.sin(3 * meanAnomaly);
        // ecliptic longitude
        final double solarLng = trueAnomaly + 1.796593063d + Math.PI;
        // solar transit in days since 2000
        final double arcLongitude = -longitude / 360;
        float n = Math.round(daysSince2000 - J0 - arcLongitude);
        double solarTransitJ2000 = n + J0 + arcLongitude + 0.0053d * Math.sin(meanAnomaly)
                + -0.0069d * Math.sin(2 * solarLng);
        // declination of sun
        double solarDec = Math.asin(Math.sin(solarLng) * Math.sin(OBLIQUITY));
        final double latRad = latitude * DEGREES_TO_RADIANS;
        double cosHourAngle = (Math.sin(ALTIDUTE_CORRECTION_EARLY_NIGHT) - Math.sin(latRad)
                * Math.sin(solarDec)) / (Math.cos(latRad) * Math.cos(solarDec));
        // The day or night never ends for the given date and location, if this value is out of
        // range.
        if (cosHourAngle >= 1) {
            mState = NIGHT;
            mSunset = -1;
            mSunrise = -1;
            return;
        } else if (cosHourAngle <= -1) {
            mState = DAY;
            mSunset = -1;
            mSunrise = -1;
            return;
        }
        float hourAngle = (float) (Math.acos(cosHourAngle) / (2 * Math.PI));
        mSunset = Math.round((solarTransitJ2000 + hourAngle) * DateUtils.DAY_IN_MILLIS) + UTC_2000;
        mSunrise = Math.round((solarTransitJ2000 - hourAngle) * DateUtils.DAY_IN_MILLIS) + UTC_2000;
        if (mSunrise < time && mSunset > time) {
            mState = DAY;
        } else {
            mState = NIGHT;
        }
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/utils/LegacyOptimizer.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.os.Build
import android.os.Process
import java.util.concurrent.ConcurrentLinkedQueue

/**
 * Performance optimizations for legacy devices (Android 4.x - 5.x).
 * Focuses on Thread Priority and Memory Management (Buffer Recycling).
 */
object LegacyOptimizer {

    private const val TAG = "LegacyOptimizer"
    private const val MAX_POOL_SIZE = 5
    private const val BUFFER_SIZE = 1024 * 1024 // 1MB buffer for video frames

    private val bufferPool = ConcurrentLinkedQueue<ByteArray>()

    /**
     * Boosts thread priority for critical streaming threads on old devices.
     */
    fun setHighPriority() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
            try {
                Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)
                AppLog.d("LegacyOptimizer: Thread priority boosted to URGENT_AUDIO")
            } catch (e: Exception) {
                AppLog.e("LegacyOptimizer: Failed to set thread priority", e)
            }
        }
    }

    /**
     * Retrieves a buffer from the pool or creates a new one if empty.
     */
    fun acquireBuffer(): ByteArray {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            return ByteArray(BUFFER_SIZE)
        }

        return bufferPool.poll() ?: ByteArray(BUFFER_SIZE)
    }

    /**
     * Returns a buffer to the pool for reuse.
     */
    fun releaseBuffer(buffer: ByteArray) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
            if (bufferPool.size < MAX_POOL_SIZE) {
                bufferPool.offer(buffer)
            }
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/utils/AppThemeManager.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.database.ContentObserver
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.Settings as SystemSettings
import androidx.appcompat.app.AppCompatDelegate
import androidx.core.content.ContextCompat
import androidx.lifecycle.MutableLiveData
import com.andrerinas.headunitrevived.contract.LocationUpdateIntent
import java.util.Calendar

class AppThemeManager(
    private val context: Context,
    private val settings: Settings
) : SensorEventListener {

    private val sensorManager = context.getSystemService(Context.SENSOR_SERVICE) as SensorManager
    private val lightSensor = sensorManager.getDefaultSensor(Sensor.TYPE_LIGHT)
    private var nightModeCalculator = NightMode(settings, false)

    private var lastEmittedNight: Boolean? = null
    private var currentLux: Float = -1f
    private var currentBrightness: Int = -1
    private var isFirstSensorReading = true
    private var isSensorRegistered = false
    private var isObserverRegistered = false

    private val handler = Handler(Looper.getMainLooper())

    private var pendingValue: Boolean? = null
    private val debounceRunnable = Runnable {
        pendingValue?.let { isNight ->
            if (lastEmittedNight != isNight) {
                lastEmittedNight = isNight
                applyNightMode(isNight)
            }
        }
    }

    private val brightnessObserver = object : ContentObserver(handler) {
        override fun onChange(selfChange: Boolean) {
            update()
        }
    }

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == LocationUpdateIntent.action) {
                val oldLux = currentLux
                val oldBright = currentBrightness
                nightModeCalculator = NightMode(settings, true)
                nightModeCalculator.currentLux = oldLux
                nightModeCalculator.currentBrightness = oldBright
            }
            update()
        }
    }

    fun start() {
        AppLog.d("AppThemeManager: Starting with mode=${settings.appTheme}, luxThreshold=${settings.appThemeThresholdLux}, brightnessThreshold=${settings.appThemeThresholdBrightness}")
        val filter = IntentFilter().apply {
            addAction(Intent.ACTION_TIME_TICK)
            addAction(LocationUpdateIntent.action)
        }

        ContextCompat.registerReceiver(context, receiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)

        refreshListeners()

        lastEmittedNight = null
        update(debounce = false)
    }

    fun stop() {
        try { context.unregisterReceiver(receiver) } catch (e: Exception) {}
        if (isSensorRegistered) {
            sensorManager.unregisterListener(this)
            isSensorRegistered = false
        }
        if (isObserverRegistered) {
            context.contentResolver.unregisterContentObserver(brightnessObserver)
            isObserverRegistered = false
        }
        handler.removeCallbacks(debounceRunnable)
    }

    private fun refreshListeners() {
        val theme = settings.appTheme

        if (theme == Settings.AppTheme.LIGHT_SENSOR) {
            if (!isSensorRegistered && lightSensor != null) {
                sensorManager.registerListener(this, lightSensor, SensorManager.SENSOR_DELAY_NORMAL)
                isSensorRegistered = true
            }
        } else {
            if (isSensorRegistered) {
                sensorManager.unregisterListener(this)
                isSensorRegistered = false
            }
        }

        if (theme == Settings.AppTheme.SCREEN_BRIGHTNESS) {
            if (!isObserverRegistered) {
                context.contentResolver.registerContentObserver(
                    SystemSettings.System.getUriFor(SystemSettings.System.SCREEN_BRIGHTNESS),
                    false, brightnessObserver
                )
                if (Build.VERSION.SDK_INT >= 28) {
                    context.contentResolver.registerContentObserver(
                        SystemSettings.System.getUriFor("screen_brightness_float"),
                        false, brightnessObserver
                    )
                }
                isObserverRegistered = true
            }
        } else {
            if (isObserverRegistered) {
                context.contentResolver.unregisterContentObserver(brightnessObserver)
                isObserverRegistered = false
            }
        }
    }

    private fun update(debounce: Boolean = true) {
        var isNight = false
        val threshold = settings.appThemeThresholdLux
        val thresholdBrightness = settings.appThemeThresholdBrightness

        when (settings.appTheme) {
            Settings.AppTheme.LIGHT_SENSOR -> {
                if (currentLux >= 0) {
                    val hyst = 10.0f
                    val currentIsNight = lastEmittedNight ?: false
                    isNight = if (currentIsNight) {
                        currentLux < (threshold + hyst)
                    } else {
                        currentLux < threshold
                    }
                    AppLog.d("AppThemeManager: LIGHT_SENSOR lux=$currentLux threshold=$threshold isNight=$isNight")
                }
            }
            Settings.AppTheme.SCREEN_BRIGHTNESS -> {
                currentBrightness = readBrightness()
                if (currentBrightness >= 0) {
                    val hyst = 10
                    val currentIsNight = lastEmittedNight ?: false
                    isNight = if (currentIsNight) {
                        currentBrightness < (thresholdBrightness + hyst)
                    } else {
                        currentBrightness < thresholdBrightness
                    }
                    AppLog.d("AppThemeManager: SCREEN_BRIGHTNESS brightness=$currentBrightness threshold=$thresholdBrightness isNight=$isNight")
                }
            }
            Settings.AppTheme.AUTO_SUNRISE -> {
                nightModeCalculator = NightMode(settings, true)
                isNight = nightModeCalculator.current
                AppLog.d("AppThemeManager: AUTO_SUNRISE isNight=$isNight")
            }
            Settings.AppTheme.MANUAL_TIME -> {
                val now = Calendar.getInstance()
                val currentMinutes = now.get(Calendar.HOUR_OF_DAY) * 60 + now.get(Calendar.MINUTE)
                val start = settings.appThemeManualStart
                val end = settings.appThemeManualEnd
                isNight = if (start <= end) {
                    currentMinutes in start..end
                } else {
                    currentMinutes >= start || currentMinutes <= end
                }
                AppLog.d("AppThemeManager: MANUAL_TIME currentMinutes=$currentMinutes start=$start end=$end isNight=$isNight")
            }
            else -> return
        }

        if (debounce) {
            if (pendingValue != isNight) {
                pendingValue = isNight
                handler.removeCallbacks(debounceRunnable)
                handler.postDelayed(debounceRunnable, 2000)
            }
        } else {
            handler.removeCallbacks(debounceRunnable)
            if (lastEmittedNight != isNight) {
                lastEmittedNight = isNight
                applyNightMode(isNight)
            }
        }
    }

    private fun applyNightMode(isNight: Boolean) {
        val mode = if (isNight) AppCompatDelegate.MODE_NIGHT_YES else AppCompatDelegate.MODE_NIGHT_NO
        AppLog.d("AppThemeManager: Setting night mode to ${if (isNight) "NIGHT" else "DAY"}")
        AppCompatDelegate.setDefaultNightMode(mode)
        signalThemeChange()
    }

    override fun onSensorChanged(event: SensorEvent) {
        if (event.sensor.type == Sensor.TYPE_LIGHT) {
            val newLux = event.values[0]
            if (kotlin.math.abs(newLux - currentLux) >= 1.0f || isFirstSensorReading) {
                currentLux = newLux
                nightModeCalculator.currentLux = currentLux
                if (isFirstSensorReading) {
                    isFirstSensorReading = false
                    update(debounce = false)
                } else {
                    update()
                }
            }
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}

    private fun readBrightness(): Int {
        return try {
            SystemSettings.System.getInt(
                context.contentResolver, SystemSettings.System.SCREEN_BRIGHTNESS
            ).coerceIn(0, 255)
        } catch (_: Exception) { -1 }
    }

    fun getCurrentLux(): Float = currentLux
    fun getCurrentBrightness(): Int = currentBrightness

    companion object {
        val themeVersion = MutableLiveData<Int>()
        private var versionCounter = 0

        private fun signalThemeChange() {
            versionCounter++
            themeVersion.value = versionCounter
        }

        fun signalVisualChange() {
            signalThemeChange()
        }

        fun applyStaticTheme(settings: Settings) {
            val mode = when (settings.appTheme) {
                Settings.AppTheme.AUTOMATIC -> AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM
                Settings.AppTheme.CLEAR -> AppCompatDelegate.MODE_NIGHT_NO
                Settings.AppTheme.DARK, Settings.AppTheme.EXTREME_DARK -> AppCompatDelegate.MODE_NIGHT_YES
                else -> return
            }
            AppCompatDelegate.setDefaultNightMode(mode)
            signalThemeChange()
        }

        fun isStaticMode(theme: Settings.AppTheme): Boolean {
            return theme == Settings.AppTheme.AUTOMATIC ||
                    theme == Settings.AppTheme.CLEAR ||
                    theme == Settings.AppTheme.DARK ||
                    theme == Settings.AppTheme.EXTREME_DARK
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/utils/NightModeManager.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.database.ContentObserver
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.Settings as SystemSettings
import androidx.core.content.ContextCompat
import com.andrerinas.headunitrevived.contract.LocationUpdateIntent
import java.util.Calendar

class NightModeManager(
    private val context: Context,
    private val settings: Settings,
    private val onUpdate: (Boolean) -> Unit
) : SensorEventListener {

    private val sensorManager = context.getSystemService(Context.SENSOR_SERVICE) as SensorManager
    private val lightSensor = sensorManager.getDefaultSensor(Sensor.TYPE_LIGHT)
    private var nightModeCalculator = NightMode(settings, false)

    // State
    private var lastEmittedValue: Boolean? = null
    private var currentLux: Float = -1f
    private var currentBrightness: Int = -1
    private var isFirstSensorReading = true
    private var isSensorRegistered = false
    private var isObserverRegistered = false

    private val handler = Handler(Looper.getMainLooper())

    // Debouncing
    private var pendingValue: Boolean? = null
    private val debounceRunnable = Runnable {
        pendingValue?.let { newValue ->
            if (lastEmittedValue != newValue) {
                lastEmittedValue = newValue
                onUpdate(newValue)
            }
        }
    }

    private val brightnessObserver = object : ContentObserver(handler) {
        override fun onChange(selfChange: Boolean) {
            update()
        }
    }

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == LocationUpdateIntent.action) {
                // Keep the sensor values when recreating the calculator
                val oldLux = currentLux
                val oldBright = currentBrightness
                nightModeCalculator = NightMode(settings, true)
                nightModeCalculator.currentLux = oldLux
                nightModeCalculator.currentBrightness = oldBright
            }
            update()
        }
    }

    fun start() {
        val filter = IntentFilter().apply {
            addAction(Intent.ACTION_TIME_TICK)
            addAction(LocationUpdateIntent.action)
        }

        ContextCompat.registerReceiver(context, receiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)

        // Initial setup of sensors/observers based on current settings
        refreshListeners()

        // Initial update immediately
        // Reset lastEmittedValue to ensure initial state is sent
        lastEmittedValue = null
        update(debounce = false)
    }

    fun stop() {
        try { context.unregisterReceiver(receiver) } catch (e: Exception) {}
        if (isSensorRegistered) {
            sensorManager.unregisterListener(this)
            isSensorRegistered = false
        }
        if (isObserverRegistered) {
            context.contentResolver.unregisterContentObserver(brightnessObserver)
            isObserverRegistered = false
        }
        handler.removeCallbacks(debounceRunnable)
    }

    fun resendCurrentState() {
        // Force a fresh check and SEND even if value hasn't changed (e.g. new connection)
        nightModeCalculator = NightMode(settings, true)
        nightModeCalculator.currentLux = currentLux
        nightModeCalculator.currentBrightness = currentBrightness

        refreshListeners()
        lastEmittedValue = null // Invalidate cache to force emission
        update(debounce = false)
    }

    private fun refreshListeners() {
        // 1. Light Sensor
        if (settings.nightMode == Settings.NightMode.LIGHT_SENSOR) {
            if (!isSensorRegistered && lightSensor != null) {
                sensorManager.registerListener(this, lightSensor, SensorManager.SENSOR_DELAY_NORMAL)
                isSensorRegistered = true
            }
        } else {
            if (isSensorRegistered) {
                sensorManager.unregisterListener(this)
                isSensorRegistered = false
            }
        }

        // 2. Brightness Observer
        if (settings.nightMode == Settings.NightMode.SCREEN_BRIGHTNESS) {
            if (!isObserverRegistered) {
                context.contentResolver.registerContentObserver(
                    SystemSettings.System.getUriFor(SystemSettings.System.SCREEN_BRIGHTNESS),
                    false,
                    brightnessObserver
                )
                if (Build.VERSION.SDK_INT >= 28) {
                    context.contentResolver.registerContentObserver(
                        SystemSettings.System.getUriFor("screen_brightness_float"),
                        false,
                        brightnessObserver
                    )
                }
                isObserverRegistered = true
            }
        } else {
            if (isObserverRegistered) {
                context.contentResolver.unregisterContentObserver(brightnessObserver)
                isObserverRegistered = false
            }
        }
    }

    // Made public so Service can force an update
    fun update(debounce: Boolean = true) {
        var isNight = false
        val threshold = settings.nightModeThresholdLux
        val thresholdBrightness = settings.nightModeThresholdBrightness

        when (settings.nightMode) {
            Settings.NightMode.LIGHT_SENSOR -> {
                if (currentLux >= 0) {
                    // Hysteresis Logic
                    val hyst = 10.0f // 5 Lux buffer
                    val currentIsNight = lastEmittedValue ?: false

                    isNight = if (currentIsNight) {
                        currentLux < (threshold + hyst)
                    } else {
                        currentLux < threshold
                    }
                }
            }
            Settings.NightMode.SCREEN_BRIGHTNESS -> {
                currentBrightness = readBrightness()
                if (currentBrightness >= 0) {
                    val hyst = 10
                    val currentIsNight = lastEmittedValue ?: false

                    isNight = if (currentIsNight) {
                        currentBrightness < (thresholdBrightness + hyst)
                    } else {
                        currentBrightness < thresholdBrightness
                    }
                }
            }
            // Delegate to standard calculator for other modes (Auto, Day, Night, Manual)
            else -> {
                // Ensure calculator has latest settings reference/values
                isNight = nightModeCalculator.current
            }
        }

        // Apply
        if (debounce) {
            if (pendingValue != isNight) {
                pendingValue = isNight
                handler.removeCallbacks(debounceRunnable)
                handler.postDelayed(debounceRunnable, 2000)
            }
        } else {
            // Immediate update
            handler.removeCallbacks(debounceRunnable) // Cancel any pending debounce
            if (lastEmittedValue != isNight) {
                lastEmittedValue = isNight
                onUpdate(isNight)
            }
        }
    }

    override fun onSensorChanged(event: SensorEvent) {
        if (event.sensor.type == Sensor.TYPE_LIGHT) {
            val newLux = event.values[0]

            // Only update if value changed significantly or it's the first reading
            if (kotlin.math.abs(newLux - currentLux) >= 1.0f || isFirstSensorReading) {
                currentLux = newLux
                nightModeCalculator.currentLux = currentLux

                if (isFirstSensorReading) {
                    isFirstSensorReading = false
                    update(debounce = false)
                } else {
                    update()
                }
            }
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}

    private fun readBrightness(): Int {
        return try {
            SystemSettings.System.getInt(
                context.contentResolver, SystemSettings.System.SCREEN_BRIGHTNESS
            ).coerceIn(0, 255)
        } catch (_: Exception) { -1 }
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/utils/AppLog.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.content.Intent
import android.util.Log

import java.util.IllegalFormatException
import java.util.Locale

object AppLog {

    interface Logger {
        fun println(priority: Int, tag: String, msg: String)

        class Android : Logger {
            override fun println(priority: Int, tag: String, msg: String) {
                Log.println(priority, TAG, msg)
            }
        }

        class StdOut : Logger {
            override fun println(priority: Int, tag: String, msg: String) {
                println("[$tag:$priority] $msg")
            }
        }
    }

    private var settings: Settings? = null

    fun init(settings: Settings) {
        this.settings = settings
    }

    var LOGGER: Logger = Logger.Android()
    private val LOG_LEVEL get() = settings?.logLevel ?: Log.INFO

    const val TAG = "HUREV"
    // LOG_LEVEL constants should not longer be needed because we check the setting directly.
    val LOG_VERBOSE get() = LOG_LEVEL <= Log.VERBOSE
    val LOG_DEBUG get() = LOG_LEVEL <= Log.DEBUG

    fun i(msg: String) {
        log(Log.INFO, format(msg))
    }

    fun i(msg: String, vararg params: Any) {
        log(Log.INFO, format(msg, *params))
    }

    fun e(msg: String?) {
        loge(format(msg ?: "Unknown error"), null)
    }

    fun e(msg: String, tr: Throwable) {
        loge(format(msg), tr)
    }

    fun e(tr: Throwable) {
        loge(tr.message ?: "Unknown error", tr)
    }


    fun e(msg: String?, vararg params: Any) {
        loge(format(msg ?: "Unknown error", *params), null)
    }

    fun v(msg: String, vararg params: Any) {
        log(Log.VERBOSE, format(msg, *params))
    }

    fun d(msg: String, vararg params: Any) {
        log(Log.DEBUG, format(msg, *params))
    }

    fun d(msg: String) {
        log(Log.DEBUG, format(msg))
    }

    fun w(msg: String) {
        log(Log.WARN, format(msg))
    }

    fun w(msg: String, vararg params: Any) {
        log(Log.WARN, format(msg, *params))
    }

    private fun log(priority: Int, msg: String) {
        if (priority >= LOG_LEVEL) {
            LOGGER.println(priority, TAG, msg)
        }
    }

    private fun loge(message: String, tr: Throwable?) {
        val trace = if (LOGGER is Logger.Android) Log.getStackTraceString(tr) else ""
        LOGGER.println(Log.ERROR, TAG, message + '\n' + trace)
    }


    private fun format(msg: String, vararg array: Any): String {
        var formatted: String
        if (array.isEmpty()) {
            formatted = msg
        } else try {
            formatted = String.format(Locale.US, msg, *array)
        } catch (ex: IllegalFormatException) {
            e("IllegalFormatException: formatString='%s' numArgs=%d", msg, array.size)
            formatted = "$msg (An error occurred while formatting the message.)"
        }
        val stackTrace = Throwable().fillInStackTrace().stackTrace
        var string = "<unknown>"
        for (i in 2 until stackTrace.size) {
            val className = stackTrace[i].className
            if (className != AppLog::class.java.name) {
                val substring = className.substring(1 + className.indexOfLast { a -> a == 46.toChar() })
                string = substring.substring(1 + substring.indexOfLast { a -> a == 36.toChar() }) + "." + stackTrace[i].methodName
                break
            }
        }
        return String.format(Locale.US, "[%d] %s | %s", Thread.currentThread().id, string, formatted)
    }

    fun i(intent: Intent) {
        i(intent.toString())
        val ex = intent.extras
        if (ex != null) {
            i(ex.toString())
        }
    }
}


```

`app/src/main/java/com/andrerinas/headunitrevived/utils/IntentFilters.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.content.IntentFilter
import com.andrerinas.headunitrevived.contract.KeyIntent

object IntentFilters {
    val keyEvent = IntentFilter(KeyIntent.action)
}
```

`app/src/main/java/com/andrerinas/headunitrevived/utils/Settings.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.annotation.SuppressLint
import android.content.ComponentName
import android.content.Context
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.location.Location
import android.os.Build
import com.andrerinas.headunitrevived.aap.protocol.proto.Control
import com.andrerinas.headunitrevived.app.UsbAttachedActivity
import com.andrerinas.headunitrevived.connection.UsbDeviceCompat

class Settings(context: Context) {

    private val prefs: SharedPreferences = context.getSharedPreferences("settings", Context.MODE_PRIVATE)

    fun isConnectingDevice(deviceCompat: UsbDeviceCompat): Boolean {
        val allowDevices = prefs.getStringSet("allow-devices", null) ?: return false
        return allowDevices.contains(deviceCompat.uniqueName)
    }

    var allowedDevices: Set<String>
        get() = prefs.getStringSet("allow-devices", HashSet<String>())!!
        set(devices) {
            prefs.edit().putStringSet("allow-devices", devices).apply()
        }

    var networkAddresses: Set<String>
        get() = prefs.getStringSet("network-addresses", HashSet<String>())!!
        set(addrs) {
            prefs.edit().putStringSet("network-addresses", addrs).apply()
        }

    var bluetoothAddress: String
        get() = prefs.getString("bt-address", "")!!
        set(value) = prefs.edit().putString("bt-address", value).apply()

    var lastKnownLocation: Location
        get() {
            val latitude = prefs.getLong("last-loc-latitude", (32.0864169).toLong())
            val longitude = prefs.getLong("last-loc-longitude", (34.7557871).toLong())

            val location = Location("")
            location.latitude = latitude.toDouble()
            location.longitude = longitude.toDouble()
            return location
        }
        set(location) {
            prefs.edit()
                .putLong("last-loc-latitude", location.latitude.toLong())
                .putLong("last-loc-longitude", location.longitude.toLong())
                .apply()
        }

    var resolutionId: Int
        get() = prefs.getInt("resolutionId", 0)
        set(value) = prefs.edit().putInt("resolutionId", value).apply()

    // Flag to determine if the projection should stretch and ignore aspect ratio to fill the screen
    var stretchToFill: Boolean
        get() = prefs.getBoolean("stretch_to_fill", true)
        set(value) { prefs.edit().putBoolean("stretch_to_fill", value).apply() }

    // Forced scale for older devices (SurfaceView fix)
    var forcedScale: Boolean
        get() = prefs.getBoolean("forced_scale", false)
        set(value) { prefs.edit().putBoolean("forced_scale", value).apply() }

    var micSampleRate: Int
        get() = prefs.getInt("mic-sample-rate", 16000)
        set(sampleRate) {
            prefs.edit().putInt("mic-sample-rate", sampleRate).apply()
        }

    var useGpsForNavigation: Boolean
        get() = prefs.getBoolean("gps-navigation", true)
        set(value) {
            prefs.edit().putBoolean("gps-navigation", value).apply()
        }

    var showNavigationNotifications: Boolean
        get() = prefs.getBoolean("show-navigation-notifications", false)
        set(value) {
            prefs.edit().putBoolean("show-navigation-notifications", value).apply()
        }

    /** Mirror phone now-playing (title, artist, duration, art) in the system media session. */
    var syncMediaSessionWithAaMetadata: Boolean
        get() = prefs.getBoolean(KEY_SYNC_MEDIA_SESSION_AA_METADATA, false)
        set(value) {
            prefs.edit().putBoolean(KEY_SYNC_MEDIA_SESSION_AA_METADATA, value).apply()
        }

    var nightMode: NightMode
        get() {
            val value = prefs.getInt("night-mode", 0)
            val mode = NightMode.fromInt(value)
            return mode!!
        }
        set(nightMode) {
            prefs.edit().putInt("night-mode", nightMode.value).apply()
        }

    var nightModeThresholdLux: Int
        get() = prefs.getInt("night-mode-threshold-lux", 100)
        set(value) {
            prefs.edit().putInt("night-mode-threshold-lux", value).apply()
        }

    var nightModeThresholdBrightness: Int
        get() = prefs.getInt("night-mode-threshold-brightness", 100)
        set(value) {
            prefs.edit().putInt("night-mode-threshold-brightness", value).apply()
        }

    var keyCodes: MutableMap<Int, Int>
        get() {
            val set = prefs.getStringSet("key-codes", mutableSetOf())!!
            val map = mutableMapOf<Int, Int>()
            set.forEach {
                val codes = it.split("-")
                map[codes[0].toInt()] = codes[1].toInt()
            }
            return map
        }
        set(codesMap) {
            val list: List<String> = codesMap.map { "${it.key}-${it.value}" }
            prefs.edit().putStringSet("key-codes", list.toSet()).apply()
        }

    var exporterLogLevel: LogExporter.LogLevel
        get() = LogExporter.LogLevel.entries.getOrElse(prefs.getInt("log-level", LogExporter.LogLevel.INFO.ordinal)) { LogExporter.LogLevel.INFO }
        set(value) { prefs.edit().putInt("log-level", value.ordinal).apply() }

    val logLevel: Int get() = exporterLogLevel.logLevel

    var viewMode: ViewMode
        get() {
            val value = prefs.getInt("view-mode", 1)
            return ViewMode.fromInt(value)!!
        }
        set(viewMode) {
            prefs.edit().putInt("view-mode", viewMode.value).apply()
        }

    var screenOrientation: ScreenOrientation
        get() {
            val value = prefs.getInt("screen-orientation", 0)
            return ScreenOrientation.fromInt(value) ?: ScreenOrientation.SYSTEM
        }
        set(orientation) {
            prefs.edit().putInt("screen-orientation", orientation.value).apply()
        }

    var dpiPixelDensity: Int
        get() = prefs.getInt("dpi-pixel-density", 0) // Default 0 for Auto
        set(value) {
            prefs.edit().putInt("dpi-pixel-density", value).apply()
        }

    var fakeSpeed: Boolean
        get() = prefs.getBoolean("fake_speed", true)
        set(value) {
            prefs.edit().putBoolean("fake_speed", value).apply()
        }

    var gestureHintShown: Boolean
        get() = prefs.getBoolean("gesture_hint_shown", false)
        set(value) {
            prefs.edit().putBoolean("gesture_hint_shown", value).apply()
        }

    // Custom Insets (Screen Margins)
    var insetLeft: Int
        get() = prefs.getInt("inset-left", 0)
        set(value) { prefs.edit().putInt("inset-left", value).apply() }

    var insetTop: Int
        get() = prefs.getInt("inset-top", 0)
        set(value) { prefs.edit().putInt("inset-top", value).apply() }

    var insetRight: Int
        get() = prefs.getInt("inset-right", 0)
        set(value) { prefs.edit().putInt("inset-right", value).apply() }

    var insetBottom: Int
        get() = prefs.getInt("inset-bottom", 0)
        set(value) { prefs.edit().putInt("inset-bottom", value).apply() }

    // Cached Surface Dimensions (used to avoid mismatch flicker on next start)
    var cachedSurfaceWidth: Int
        get() = prefs.getInt("cached-surface-width", 0)
        set(value) { prefs.edit().putInt("cached-surface-width", value).apply() }

    var cachedSurfaceHeight: Int
        get() = prefs.getInt("cached-surface-height", 0)
        set(value) { prefs.edit().putInt("cached-surface-height", value).apply() }

    var cachedSurfaceSettingsHash: Int
        get() = prefs.getInt("cached-surface-settings-hash", 0)
        set(value) { prefs.edit().putInt("cached-surface-settings-hash", value).apply() }

    // Legacy Margins (can be removed later if unused)
    var marginLeft: Int
        get() = prefs.getInt("margin-left", 0)
        set(value) { prefs.edit().putInt("margin-left", value).apply() }

    var marginTop: Int
        get() = prefs.getInt("margin-top", 0)
        set(value) { prefs.edit().putInt("margin-top", value).apply() }

    var marginRight: Int
        get() = prefs.getInt("margin-right", 0)
        set(value) { prefs.edit().putInt("margin-right", value).apply() }

    var marginBottom: Int
        get() = prefs.getInt("margin-bottom", 0)
        set(value) { prefs.edit().putInt("margin-bottom", value).apply() }

    var fullscreenMode: FullscreenMode
        get() {
            // Migration logic
            if (!prefs.contains("fullscreen-mode") && prefs.contains("start-in-fullscreen-mode")) {
                val old = prefs.getBoolean("start-in-fullscreen-mode", true)
                val migrated = if (old) FullscreenMode.IMMERSIVE else FullscreenMode.NONE
                prefs.edit().putInt("fullscreen-mode", migrated.value).apply()
                return migrated
            }
            val value = prefs.getInt("fullscreen-mode", FullscreenMode.IMMERSIVE.value)
            return FullscreenMode.fromInt(value) ?: FullscreenMode.IMMERSIVE
        }
        set(value) { prefs.edit().putInt("fullscreen-mode", value.value).apply() }

    @Deprecated("Use fullscreenMode instead")
    var startInFullscreenMode: Boolean
        get() = fullscreenMode != FullscreenMode.NONE
        set(value) { fullscreenMode = if (value) FullscreenMode.IMMERSIVE else FullscreenMode.NONE }

    var forceSoftwareDecoding: Boolean
        get() = prefs.getBoolean("force-software-decoding", false)
        set(value) { prefs.edit().putBoolean("force-software-decoding", value).apply() }

    var rightHandDrive: Boolean
        get() = prefs.getBoolean("right-hand-drive", false)
        set(value) { prefs.edit().putBoolean("right-hand-drive", value).apply() }

    // Vehicle info settings (sent to phone during Android Auto handshake)
    var vehicleDisplayName: String
        get() = prefs.getString("vehicle-display-name", "Headunit Revived")!!
        set(value) { prefs.edit().putString("vehicle-display-name", value).apply() }

    var vehicleMake: String
        get() = prefs.getString("vehicle-make", "Google")!!
        set(value) { prefs.edit().putString("vehicle-make", value).apply() }

    var vehicleModel: String
        get() = prefs.getString("vehicle-model", "Desktop Head Unit")!!
        set(value) { prefs.edit().putString("vehicle-model", value).apply() }

    var vehicleYear: String
        get() = prefs.getString("vehicle-year", "2025")!!
        set(value) { prefs.edit().putString("vehicle-year", value).apply() }

    var vehicleId: String
        get() = prefs.getString("vehicle-id", "headlessunit-001")!!
        set(value) { prefs.edit().putString("vehicle-id", value).apply() }

    var headUnitMake: String
        get() = prefs.getString("head-unit-make", "Google")!!
        set(value) { prefs.edit().putString("head-unit-make", value).apply() }

    var headUnitModel: String
        get() = prefs.getString("head-unit-model", "Desktop Head Unit")!!
        set(value) { prefs.edit().putString("head-unit-model", value).apply() }

    // 0 = Manual, 1 = Auto (Headunit Server), 2 = Helper (Wifi Launcher), 3 = Native AA
    var wifiConnectionMode: Int
        get() {
            // Migration: Check if old helper boolean exists
            if (prefs.contains("wifi-launcher-mode")) {
                val old = prefs.getBoolean("wifi-launcher-mode", false)
                val newMode = if (old) 2 else 1
                prefs.edit().putInt("wifi-connection-mode", newMode).remove("wifi-launcher-mode").apply()
                return newMode
            }
            // Migration: Check if native-aa-wireless was true
            if (prefs.getBoolean("native-aa-wireless", false)) {
                prefs.edit().putInt("wifi-connection-mode", 3).remove("native-aa-wireless").apply()
                return 3
            }
            return prefs.getInt("wifi-connection-mode", 2) // Default 2 (Wireless Helper)
        }
        set(value) { prefs.edit().putInt("wifi-connection-mode", value).apply() }

    var videoCodec: String
        get() = prefs.getString("video-codec", "Auto")!!
        set(value) { prefs.edit().putString("video-codec", value).apply() }

    var fpsLimit: Int
        get() = prefs.getInt("fps-limit", 60)
        set(value) { prefs.edit().putInt("fps-limit", value).apply() }

    var hasAcceptedDisclaimer: Boolean
        get() = prefs.getBoolean("has-accepted-disclaimer", false)
        set(value) { prefs.edit().putBoolean("has-accepted-disclaimer", value).apply() }

    var hasCompletedSetupWizard: Boolean
        get() = prefs.getBoolean("has-completed-setup-wizard", false)
        set(value) { prefs.edit().putBoolean("has-completed-setup-wizard", value).apply() }

    var autoConnectLastSession: Boolean
        get() = prefs.getBoolean("auto-connect-last-session", false)
        set(value) { prefs.edit().putBoolean("auto-connect-last-session", value).apply() }

    var autoConnectSingleUsbDevice: Boolean
        get() = prefs.getBoolean("auto-connect-single-usb", false)
        set(value) { prefs.edit().putBoolean("auto-connect-single-usb", value).apply() }

    var lastConnectionType: String
        get() = prefs.getString("last-connection-type", "")!!
        set(value) { prefs.edit().putString("last-connection-type", value).apply() }

    var lastConnectionIp: String
        get() = prefs.getString("last-connection-ip", "")!!
        set(value) { prefs.edit().putString("last-connection-ip", value).apply() }

    var lastConnectionUsbDevice: String
        get() = prefs.getString("last-connection-usb-device", "")!!
        set(value) { prefs.edit().putString("last-connection-usb-device", value).apply() }

    fun saveLastConnection(type: String, ip: String = "", usbDevice: String = "") {
        lastConnectionType = type
        lastConnectionIp = ip
        lastConnectionUsbDevice = usbDevice
    }

    fun clearLastConnection() {
        lastConnectionType = ""
        lastConnectionIp = ""
        lastConnectionUsbDevice = ""
    }

    var enableAudioSink: Boolean
        get() = prefs.getBoolean("enable-audio-sink", true)
        set(value) { prefs.edit().putBoolean("enable-audio-sink", value).apply() }

    var micInputSource: Int
        get() = prefs.getInt("mic-input-source", 0) // Default: DEFAULT
        set(value) { prefs.edit().putInt("mic-input-source", value).apply() }

    var audioLatencyMultiplier: Int
        get() = prefs.getInt("audio-latency-multiplier", 8)
        set(value) { prefs.edit().putInt("audio-latency-multiplier", value).apply() }

    var audioQueueCapacity: Int
        get() = prefs.getInt("audio-queue-capacity", 0)
        set(value) { prefs.edit().putInt("audio-queue-capacity", value).apply() }

    var useAacAudio: Boolean
        get() = prefs.getBoolean("use-aac-audio", false)
        set(value) { prefs.edit().putBoolean("use-aac-audio", value).apply() }

    var useNativeSsl: Boolean
        get() = prefs.getBoolean("use-native-ssl", false)
        set(value) { prefs.edit().putBoolean("use-native-ssl", value).apply() }

    var autoStartSelfMode: Boolean
        get() = prefs.getBoolean("auto-start-self-mode", false)
        set(value) { prefs.edit().putBoolean("auto-start-self-mode", value).apply() }

    var autoStartOnUsb: Boolean
        get() = prefs.getBoolean("auto-start-on-usb", false)
        set(value) { prefs.edit().putBoolean("auto-start-on-usb", value).apply() }

    var autoStartOnBoot: Boolean
        get() = prefs.getBoolean("auto-start-on-boot", false)
        set(value) { prefs.edit().putBoolean("auto-start-on-boot", value).apply() }

    var autoStartOnScreenOn: Boolean
        get() = prefs.getBoolean("auto-start-on-screen-on", false)
        set(value) { prefs.edit().putBoolean("auto-start-on-screen-on", value).apply() }

    var listenForUsbDevices: Boolean
        get() = prefs.getBoolean("listen-for-usb-devices", true)
        set(value) { prefs.edit().putBoolean("listen-for-usb-devices", value).apply() }

    var reopenOnReconnection: Boolean
        get() = prefs.getBoolean("reopen-on-reconnection", true)
        set(value) { prefs.edit().putBoolean("reopen-on-reconnection", value).apply() }

    var autoConnectPriorityOrder: List<String>
        get() {
            val stored = prefs.getString("auto-connect-priority-order", null)
            val order = if (stored.isNullOrEmpty()) {
                DEFAULT_AUTO_CONNECT_ORDER.toMutableList()
            } else {
                stored.split(",").toMutableList()
            }
            // Migration safety: append any missing methods at end
            for (method in DEFAULT_AUTO_CONNECT_ORDER) {
                if (method !in order) {
                    order.add(method)
                }
            }
            // Remove unknown methods
            order.retainAll(DEFAULT_AUTO_CONNECT_ORDER)
            return order
        }
        set(value) {
            prefs.edit().putString("auto-connect-priority-order", value.joinToString(",")).apply()
        }

    var autoStartBluetoothDeviceName: String
        get() = prefs.getString("auto-start-bt-name", "")!!
        set(value) { prefs.edit().putString("auto-start-bt-name", value).apply() }

    var autoStartBluetoothDeviceMac: String
        get() = prefs.getString("auto-start-bt-mac", "")!!
        set(value) = prefs.edit().putString("auto-start-bt-mac", value).apply()

    var appLanguage: String
        get() = prefs.getString("app-language", "")!!
        set(value) { prefs.edit().putString("app-language", value).apply() }

    var mediaVolumeOffset: Int
        get() = prefs.getInt("media-volume-offset", 0)
        set(value) { prefs.edit().putInt("media-volume-offset", value).apply() }

    var assistantVolumeOffset: Int
        get() = prefs.getInt("assistant-volume-offset", 0)
        set(value) { prefs.edit().putInt("assistant-volume-offset", value).apply() }

    var navigationVolumeOffset: Int
        get() = prefs.getInt("navigation-volume-offset", 0)
        set(value) { prefs.edit().putInt("navigation-volume-offset", value).apply() }

    @SuppressLint("ApplySharedPref")
    fun commit() {
        prefs.edit().commit()
    }

    enum class Resolution(val id: Int, val resName: String, val width: Int, val height: Int, val codec: Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType?) {
        AUTO(0, "Auto",0, 0, null),
        _800x480(1, "480p", 800, 480, Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._800x480),
        _1280x720(2, "720p", 1280, 720, Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._1280x720),
        _1920x1080(3, "1080p", 1920, 1080, Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._1920x1080),
        _2560x1440(4, "1440p (2K)", 2560, 1440, Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._2560x1440),
        _3840x2160(5, "2160p (4K)", 3840, 2160, Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._3840x2160);

        companion object {
            private val map = values().associateBy(Resolution::id)
            fun fromId(id: Int) = map[id]
            val allRes: Array<String>
                get() = values().map { it.resName }.toTypedArray()
            val allResolutions: Array<Resolution>
                get() = values()
        }
    }

    enum class NightMode(val value: Int) {
        AUTO(0),
        DAY(1),
        NIGHT(2),
        MANUAL_TIME(3),
        LIGHT_SENSOR(4),
        SCREEN_BRIGHTNESS(5);

        companion object {
            private val map = NightMode.values().associateBy(NightMode::value)
            fun fromInt(value: Int) = map[value]
        }
    }

    var nightModeManualStart: Int
        get() = prefs.getInt("night-mode-manual-start", 1140) // Default 19:00 (19 * 60)
        set(value) {
            prefs.edit().putInt("night-mode-manual-start", value).apply()
        }

    var nightModeManualEnd: Int
        get() = prefs.getInt("night-mode-manual-end", 420) // Default 07:00 (7 * 60)
        set(value) {
            prefs.edit().putInt("night-mode-manual-end", value).apply()
        }

    // App Theme independent threshold/time settings (separate from Night Mode)
    var appThemeThresholdLux: Int
        get() = prefs.getInt("app-theme-threshold-lux", 100)
        set(value) { prefs.edit().putInt("app-theme-threshold-lux", value).apply() }

    var appThemeThresholdBrightness: Int
        get() = prefs.getInt("app-theme-threshold-brightness", 100)
        set(value) { prefs.edit().putInt("app-theme-threshold-brightness", value).apply() }

    var appThemeManualStart: Int
        get() = prefs.getInt("app-theme-manual-start", 1140)
        set(value) { prefs.edit().putInt("app-theme-manual-start", value).apply() }

    var appThemeManualEnd: Int
        get() = prefs.getInt("app-theme-manual-end", 420)
        set(value) { prefs.edit().putInt("app-theme-manual-end", value).apply() }
    var showFpsCounter: Boolean
        get() = prefs.getBoolean("show-fps-counter", false)
        set(value) {
            prefs.edit().putBoolean("show-fps-counter", value).apply()
        }

    companion object {
        const val CONNECTION_TYPE_WIFI = "wifi"
        const val CONNECTION_TYPE_USB = "usb"
        const val CONNECTION_TYPE_NEARBY = "nearby"

        /** SharedPreferences key; also used by [AapService] for change listener. */
        const val KEY_SYNC_MEDIA_SESSION_AA_METADATA = "sync-media-session-aa-metadata"

        const val AUTO_CONNECT_LAST_SESSION = "last-session"
        const val AUTO_CONNECT_SELF_MODE = "self-mode"
        const val AUTO_CONNECT_SINGLE_USB = "single-usb"

        val DEFAULT_AUTO_CONNECT_ORDER = listOf(
            AUTO_CONNECT_LAST_SESSION,
            AUTO_CONNECT_SELF_MODE,
            AUTO_CONNECT_SINGLE_USB
        )

        private const val DEVICE_PREFS_NAME = "settings_device_protected"
        private const val KEY_AUTO_START_ON_BOOT = "auto-start-on-boot"

        /**
         * Reads auto-start-on-boot from device-protected storage (API 24+),
         * falling back to regular prefs on older devices.
         * Safe to call during locked boot when credential storage is unavailable.
         */
        fun isAutoStartOnBootEnabled(context: Context): Boolean {
            val prefs = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                val deviceContext = context.createDeviceProtectedStorageContext()
                deviceContext.getSharedPreferences(DEVICE_PREFS_NAME, Context.MODE_PRIVATE)
            } else {
                context.getSharedPreferences("settings", Context.MODE_PRIVATE)
            }
            return prefs.getBoolean(KEY_AUTO_START_ON_BOOT, false)
        }

        /**
         * Syncs the auto-start-on-boot value to device-protected storage.
         * Call this whenever the user saves settings.
         */
        fun syncAutoStartOnBootToDeviceStorage(context: Context, enabled: Boolean) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                val deviceContext = context.createDeviceProtectedStorageContext()
                deviceContext.getSharedPreferences(DEVICE_PREFS_NAME, Context.MODE_PRIVATE)
                    .edit()
                    .putBoolean(KEY_AUTO_START_ON_BOOT, enabled)
                    .apply()
            }
        }

        private const val KEY_AUTO_START_ON_SCREEN_ON = "auto-start-on-screen-on"

        /**
         * Reads auto-start-on-screen-on from device-protected storage (API 24+),
         * falling back to regular prefs on older devices.
         * Safe to call during locked boot when credential storage is unavailable.
         */
        fun isAutoStartOnScreenOnEnabled(context: Context): Boolean {
            val prefs = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                val deviceContext = context.createDeviceProtectedStorageContext()
                deviceContext.getSharedPreferences(DEVICE_PREFS_NAME, Context.MODE_PRIVATE)
            } else {
                context.getSharedPreferences("settings", Context.MODE_PRIVATE)
            }
            return prefs.getBoolean(KEY_AUTO_START_ON_SCREEN_ON, false)
        }

        fun syncAutoStartOnScreenOnToDeviceStorage(context: Context, enabled: Boolean) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                val deviceContext = context.createDeviceProtectedStorageContext()
                deviceContext.getSharedPreferences(DEVICE_PREFS_NAME, Context.MODE_PRIVATE)
                    .edit()
                    .putBoolean(KEY_AUTO_START_ON_SCREEN_ON, enabled)
                    .apply()
            }
        }

        private const val KEY_AUTO_START_ON_USB = "auto-start-on-usb"
        private const val KEY_LISTEN_FOR_USB_DEVICES = "listen-for-usb-devices"
        private const val KEY_AUTO_START_BT_MAC = "auto-start-bt-mac"

        /**
         * Reads auto-start-on-usb from device-protected storage (API 24+),
         * falling back to regular prefs on older devices.
         * Safe to call during locked boot when credential storage is unavailable.
         */
        fun isAutoStartOnUsbEnabled(context: Context): Boolean {
            val prefs = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                val deviceContext = context.createDeviceProtectedStorageContext()
                deviceContext.getSharedPreferences(DEVICE_PREFS_NAME, Context.MODE_PRIVATE)
            } else {
                context.getSharedPreferences("settings", Context.MODE_PRIVATE)
            }
            return prefs.getBoolean(KEY_AUTO_START_ON_USB, false)
        }

        fun syncAutoStartOnUsbToDeviceStorage(context: Context, enabled: Boolean) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                val deviceContext = context.createDeviceProtectedStorageContext()
                deviceContext.getSharedPreferences(DEVICE_PREFS_NAME, Context.MODE_PRIVATE)
                    .edit()
                    .putBoolean(KEY_AUTO_START_ON_USB, enabled)
                    .apply()
            }
        }

        fun isListenForUsbDevicesEnabled(context: Context): Boolean {
            val prefs = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                val deviceContext = context.createDeviceProtectedStorageContext()
                deviceContext.getSharedPreferences(DEVICE_PREFS_NAME, Context.MODE_PRIVATE)
            } else {
                context.getSharedPreferences("settings", Context.MODE_PRIVATE)
            }
            return prefs.getBoolean(KEY_LISTEN_FOR_USB_DEVICES, true) // Default is TRUE
        }

        fun syncListenForUsbDevicesToDeviceStorage(context: Context, enabled: Boolean) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                val deviceContext = context.createDeviceProtectedStorageContext()
                deviceContext.getSharedPreferences(DEVICE_PREFS_NAME, Context.MODE_PRIVATE)
                    .edit()
                    .putBoolean(KEY_LISTEN_FOR_USB_DEVICES, enabled)
                    .apply()
            }
        }


        fun setUsbAttachedActivityEnabled(context: Context, enabled: Boolean) {
            val component = ComponentName(context, UsbAttachedActivity::class.java)
            val newState = if (enabled)
                PackageManager.COMPONENT_ENABLED_STATE_ENABLED
            else
                PackageManager.COMPONENT_ENABLED_STATE_DISABLED
            if (context.packageManager.getComponentEnabledSetting(component) != newState) {
                context.packageManager.setComponentEnabledSetting(
                    component, newState, PackageManager.DONT_KILL_APP
                )
            }
        }

        /**
         * Reads the Bluetooth auto-start MAC from device-protected storage (API 24+),
         * falling back to regular prefs on older devices.
         */
        fun getAutoStartBtMac(context: Context): String {
            val prefs = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                val deviceContext = context.createDeviceProtectedStorageContext()
                deviceContext.getSharedPreferences(DEVICE_PREFS_NAME, Context.MODE_PRIVATE)
            } else {
                context.getSharedPreferences("settings", Context.MODE_PRIVATE)
            }
            return prefs.getString(KEY_AUTO_START_BT_MAC, "") ?: ""
        }

        fun syncAutoStartBtMacToDeviceStorage(context: Context, mac: String) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                val deviceContext = context.createDeviceProtectedStorageContext()
                deviceContext.getSharedPreferences(DEVICE_PREFS_NAME, Context.MODE_PRIVATE)
                    .edit()
                    .putString(KEY_AUTO_START_BT_MAC, mac)
                    .apply()
            }
        }

        val MicSampleRates = listOf(8000, 16000, 24000, 32000, 44100, 48000) // Changed to List

        fun getNextMicSampleRate(currentRate: Int): Int {
            val currentIndex = MicSampleRates.indexOf(currentRate)
            return if (currentIndex != -1 && currentIndex < MicSampleRates.size - 1) {
                MicSampleRates[currentIndex + 1]
            } else {
                MicSampleRates.first() // Loop back to first if at end or not found
            }
        }

        // NightMode is now an enum, so we can iterate its values directly
    }

    enum class ViewMode(val value: Int) {
        SURFACE(0),
        TEXTURE(1),
        GLES(2);

        companion object {
            private val map = values().associateBy(ViewMode::value)
            fun fromInt(value: Int) = map[value]
        }
    }

    enum class ScreenOrientation(val value: Int, val androidOrientation: Int) {
        SYSTEM(0, android.content.pm.ActivityInfo.SCREEN_ORIENTATION_USER),
        AUTO(1, android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR),
        LANDSCAPE(2, android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE),
        LANDSCAPE_REVERSE(3, android.content.pm.ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE),
        PORTRAIT(4, android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT),
        PORTRAIT_REVERSE(5, android.content.pm.ActivityInfo.SCREEN_ORIENTATION_REVERSE_PORTRAIT);

        companion object {
            private val map = values().associateBy(ScreenOrientation::value)
            fun fromInt(value: Int) = map[value]
        }
    }

    enum class FullscreenMode(val value: Int) {
        NONE(0),
        IMMERSIVE(1),
        STATUS_ONLY(2),
        IMMERSIVE_WITH_NOTCH(3);

        companion object {
            private val map = values().associateBy(FullscreenMode::value)
            fun fromInt(value: Int) = map[value]
        }
    }

    enum class AppTheme(val value: Int) {
        AUTOMATIC(0),
        CLEAR(1),
        DARK(2),
        EXTREME_DARK(3),
        AUTO_SUNRISE(4),
        MANUAL_TIME(5),
        LIGHT_SENSOR(6),
        SCREEN_BRIGHTNESS(7);

        companion object {
            private val map = values().associateBy(AppTheme::value)
            fun fromInt(value: Int) = map[value] ?: AUTOMATIC
        }
    }

    var monochromeIcons: Boolean
        get() = prefs.getBoolean("monochrome-icons", false)
        set(value) { prefs.edit().putBoolean("monochrome-icons", value).apply() }

    var useExtremeDarkMode: Boolean
        get() = prefs.getBoolean("use-extreme-dark-mode", false)
        set(value) { prefs.edit().putBoolean("use-extreme-dark-mode", value).apply() }

    var useGradientBackground: Boolean
        get() = prefs.getBoolean("use-gradient-background", false)
        set(value) { prefs.edit().putBoolean("use-gradient-background", value).apply() }

    var aaMonochromeEnabled: Boolean
        get() = prefs.getBoolean("aa-monochrome-enabled", false)
        set(value) { prefs.edit().putBoolean("aa-monochrome-enabled", value).apply() }

    var aaDesaturationLevel: Int
        get() = prefs.getInt("aa-desaturation-level", 100)
        set(value) { prefs.edit().putInt("aa-desaturation-level", value).apply() }

    var appTheme: AppTheme
        get() {
            val value = prefs.getInt("app-theme", 0)
            return AppTheme.fromInt(value) ?: AppTheme.AUTOMATIC
        }
        set(theme) {
            prefs.edit().putInt("app-theme", theme.value).apply()
        }

    var enableRotary: Boolean
        get() = prefs.getBoolean("enable-rotary", false)
        set(value) { prefs.edit().putBoolean("enable-rotary", value).apply() }

    var killOnDisconnect: Boolean
        get() = prefs.getBoolean("kill-on-disconnect", false)
        set(value) { prefs.edit().putBoolean("kill-on-disconnect", value).apply() }

    var autoEnableHotspot: Boolean
        get() = prefs.getBoolean("auto-enable-hotspot", false)
        set(value) { prefs.edit().putBoolean("auto-enable-hotspot", value).apply() }

    var waitForWifiBeforeWifiDirect: Boolean
        get() = prefs.getBoolean("wait-for-wifi-before-wifi-direct", false)
        set(value) { prefs.edit().putBoolean("wait-for-wifi-before-wifi-direct", value).apply() }

    var waitForWifiTimeout: Int
        get() = prefs.getInt("wait-for-wifi-timeout", 10)
        set(value) { prefs.edit().putInt("wait-for-wifi-timeout", value).apply() }

    // 0 = Common Wifi (NSD), 1 = Wifi Direct P2P, 2 = Nearby Devices, 3 = Phone Hotspot (Host), 4 = Headunit Hotspot (Passive)
    var helperConnectionStrategy: Int
        get() = prefs.getInt("helper-connection-strategy", 2) // Default to Nearby Devices (2)
        set(value) = prefs.edit().putInt("helper-connection-strategy", value).apply()

    var lastNearbyDeviceName: String
        get() = prefs.getString("last-nearby-device-name", "")!!
        set(value) = prefs.edit().putString("last-nearby-device-name", value).apply()

}

```

`app/src/main/java/com/andrerinas/headunitrevived/utils/ProtoUint32.kt`:

```kt
package com.andrerinas.headunitrevived.utils

/**
 * Protobuf `uint32` fields are represented as signed [Int] in generated Java code.
 * Values above 2³¹−1 appear negative; use this before arithmetic or comparisons with duration/position.
 */
@Suppress("NOTHING_TO_INLINE")
inline fun Int.protoUint32ToLong(): Long = toLong() and 0xFFFFFFFFL

```

`app/src/main/java/com/andrerinas/headunitrevived/utils/HotspotManager.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.content.Context
import android.net.ConnectivityManager
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.android.dx.DexMaker
import com.android.dx.TypeId
import java.lang.reflect.Method

/**
 * Manages WiFi Hotspot (tethering) using reflection + dexmaker.
 */
object HotspotManager {
    private const val TAG = "HUREV_WIFI"
    private const val CALLBACK_CLASS = "android.net.ConnectivityManager\$OnStartTetheringCallback"

    private var cachedCallbackClass: Class<*>? = null

    fun setHotspotEnabled(context: Context, enabled: Boolean): Boolean {
        AppLog.i("HotspotManager: Setting hotspot enabled=$enabled (API ${Build.VERSION.SDK_INT})")

        // On Android 8+, WiFi must be disabled before tethering can start
        if (enabled) {
            try {
                val wm = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
                if (wm.isWifiEnabled) {
                    AppLog.i("HotspotManager: Disabling WiFi before enabling hotspot...")
                    wm.isWifiEnabled = false
                    Thread.sleep(500) // Let the radio settle
                }
            } catch (e: Exception) {
                AppLog.w("HotspotManager: Failed to disable WiFi: ${e.message}")
            }
        }

        if (tryConnectivityManager(context, enabled)) return true
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (tryTetheringManager(context, enabled)) return true
        }
        if (tryLegacyWifiManager(context, enabled)) return true

        AppLog.w("HotspotManager: All hotspot attempts failed.")
        return false
    }

    private fun tryConnectivityManager(context: Context, enabled: Boolean): Boolean {
        try {
            val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
            if (!enabled) {
                val stopMethod = cm.javaClass.methods.find { it.name == "stopTethering" }
                if (stopMethod != null) {
                    stopMethod.isAccessible = true
                    stopMethod.invoke(cm, 0)
                    return true
                }
                return false
            }

            val startMethod = cm.javaClass.methods.find {
                it.name == "startTethering" && it.parameterTypes.size >= 4
            } ?: return false

            startMethod.isAccessible = true
            val callbackInst = createTetheringCallback(context)
            val handler = Handler(Looper.getMainLooper())

            return when (startMethod.parameterTypes.size) {
                4 -> {
                    startMethod.invoke(cm, 0, false, callbackInst, handler)
                    true
                }
                5 -> {
                    startMethod.invoke(cm, 0, false, callbackInst, handler, context.packageName)
                    true
                }
                else -> false
            }
        } catch (e: Exception) {
            AppLog.e("HotspotManager: CM path failed: ${e.message}")
            return false
        }
    }

    @Suppress("UNCHECKED_CAST")
    private fun createTetheringCallback(context: Context): Any? {
        try {
            cachedCallbackClass?.let { cls ->
                return cls.getDeclaredConstructor().newInstance()
            }

            val parentClass = Class.forName(CALLBACK_CLASS) ?: return null
            val dexMaker = DexMaker()
            val getByName: Method = TypeId::class.java.getDeclaredMethod("get", String::class.java)
            val getByClass: Method = TypeId::class.java.getDeclaredMethod("get", Class::class.java)

            val generatedType = getByName.invoke(null, "LTetheringCallback;") as TypeId<Any>
            val parentType = getByClass.invoke(null, parentClass) as TypeId<Any>

            dexMaker.declare(generatedType, "TetheringCallback.generated", java.lang.reflect.Modifier.PUBLIC, parentType)

            val constructor = generatedType.getConstructor() as com.android.dx.MethodId<Any, Void>
            val parentConstructor = parentType.getConstructor() as com.android.dx.MethodId<Any, Void>
            val code = dexMaker.declare(constructor, java.lang.reflect.Modifier.PUBLIC)
            val thisRef = code.getThis(generatedType)
            code.invokeDirect(parentConstructor, null, thisRef)
            code.returnVoid()

            val dexCache = context.codeCacheDir
            val classLoader = dexMaker.generateAndLoad(this.javaClass.classLoader, dexCache)
            val generatedClass = classLoader.loadClass("TetheringCallback")
            cachedCallbackClass = generatedClass

            return generatedClass.getDeclaredConstructor().newInstance()
        } catch (e: Exception) {
            AppLog.e("HotspotManager: Dexmaker failed: ${e.message}")
            return null
        }
    }

    private fun tryTetheringManager(context: Context, enabled: Boolean): Boolean {
        try {
            val tm = context.getSystemService("tethering") ?: return false
            if (enabled) {
                val startMethod = tm.javaClass.methods.find {
                    it.name == "startTethering" && it.parameterTypes.size == 3
                } ?: return false
                startMethod.invoke(tm, 0, context.mainExecutor, null)
                return true
            } else {
                val stopMethod = tm.javaClass.methods.find { it.name == "stopTethering" }
                stopMethod?.invoke(tm, 0)
                return true
            }
        } catch (e: Exception) { return false }
    }

    private fun tryLegacyWifiManager(context: Context, enabled: Boolean): Boolean {
        try {
            val wm = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
            val method = wm.javaClass.getMethod("setWifiApEnabled", android.net.wifi.WifiConfiguration::class.java, Boolean::class.javaPrimitiveType)
            return method.invoke(wm, null, enabled) as Boolean
        } catch (_: Exception) { return false }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/utils/SilentAudioPlayer.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.content.Context
import android.media.MediaPlayer
import com.andrerinas.headunitrevived.R

/**
 * Plays a silent audio loop to keep the media focus on the app.
 * This is a common trick used in Chinese headunits to ensure steering wheel
 * buttons are always routed to the active foreground app.
 */
class SilentAudioPlayer(private val context: Context) {

    private var mediaPlayer: MediaPlayer? = null

    fun start() {
        if (mediaPlayer != null) return

        try {
            // Note: R.raw.mute should be a very short silent wav/mp3 file
            mediaPlayer = MediaPlayer.create(context, R.raw.mute)
            mediaPlayer?.apply {
                isLooping = true
                setVolume(0f, 0f)
                start()
            }
            AppLog.i("SilentAudioPlayer: Started silent loop for media focus.")
        } catch (e: Exception) {
            AppLog.e("SilentAudioPlayer: Failed to start silent loop. Is R.raw.mute missing?", e)
        }
    }

    fun stop() {
        try {
            mediaPlayer?.stop()
            mediaPlayer?.release()
        } catch (e: Exception) {}
        mediaPlayer = null
        AppLog.i("SilentAudioPlayer: Stopped silent loop.")
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/utils/SystemUI.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import android.graphics.Color
import android.os.Build
import android.view.View
import android.view.Window
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.andrerinas.headunitrevived.utils.AppLog

object SystemUI {

    fun apply(window: Window, root: View, mode: Settings.FullscreenMode, onInsetsChanged: (() -> Unit)? = null) {
        // Always keep screen on for Headunit functionality
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            val params = window.attributes
            params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
            window.attributes = params
        }

        val controllerCompat = WindowInsetsControllerCompat(window, window.decorView)

        // Handle Immersive Mode for modern APIs (30+)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val controller = window.insetsController
            if (controller != null) {
                when (mode) {
                    Settings.FullscreenMode.IMMERSIVE, Settings.FullscreenMode.IMMERSIVE_WITH_NOTCH -> {
                        controller.hide(WindowInsets.Type.statusBars() or WindowInsets.Type.navigationBars())
                        controller.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                    }
                    Settings.FullscreenMode.STATUS_ONLY -> {
                        controller.hide(WindowInsets.Type.statusBars())
                        controller.show(WindowInsets.Type.navigationBars())
                        controller.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                    }
                    Settings.FullscreenMode.NONE -> {
                        controller.show(WindowInsets.Type.statusBars() or WindowInsets.Type.navigationBars())
                    }
                }
            }
        } else {
            // Legacy Flags (Jelly Bean API 16 and above)
            @Suppress("DEPRECATION")
            when (mode) {
                Settings.FullscreenMode.IMMERSIVE, Settings.FullscreenMode.IMMERSIVE_WITH_NOTCH -> {
                    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.KITKAT) {
                        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
                        window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_FULLSCREEN
                                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                or View.SYSTEM_UI_FLAG_LOW_PROFILE)
                    } else {
                        window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_FULLSCREEN
                                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                                or View.SYSTEM_UI_FLAG_LOW_PROFILE
                                or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY)
                    }
                }
                Settings.FullscreenMode.STATUS_ONLY -> {
                    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.KITKAT) {
                        window.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN)
                        window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_FULLSCREEN
                                or View.SYSTEM_UI_FLAG_LOW_PROFILE)
                    } else {
                        window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_FULLSCREEN
                                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                                or View.SYSTEM_UI_FLAG_LOW_PROFILE
                                or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY)
                    }
                }
                Settings.FullscreenMode.NONE -> {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                        window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN)
                    } else {
                        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    }
                }
            }
        }

        // Fix for Non-Immersive: Force black bars on older devices
        if (mode != Settings.FullscreenMode.IMMERSIVE && mode != Settings.FullscreenMode.IMMERSIVE_WITH_NOTCH) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS)
                if (Build.VERSION.SDK_INT < 35) {
                    window.statusBarColor = Color.BLACK
                    if (mode == Settings.FullscreenMode.NONE) {
                        window.navigationBarColor = Color.BLACK
                    }
                }
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
                window.clearFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS)
                if (mode == Settings.FullscreenMode.NONE) {
                    window.clearFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_NAVIGATION)
                }
            }
            controllerCompat.isAppearanceLightStatusBars = false
            controllerCompat.isAppearanceLightNavigationBars = false
        }

        val settings = Settings(root.context)

        // IMMEDIATE APPLICATION
        val manualL = settings.insetLeft
        val manualT = settings.insetTop
        val manualR = settings.insetRight
        val manualB = settings.insetBottom

        root.setPadding(manualL, manualT, manualR, manualB)
        HeadUnitScreenConfig.updateInsets(manualL, manualT, manualR, manualB)

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
            // Legacy Fallback for Android 4.x
            root.viewTreeObserver.addOnGlobalLayoutListener(object : android.view.ViewTreeObserver.OnGlobalLayoutListener {
                override fun onGlobalLayout() {
                    val rect = android.graphics.Rect()
                    window.decorView.getWindowVisibleDisplayFrame(rect)

                    @Suppress("DEPRECATION")
                    val display = window.windowManager.defaultDisplay
                    val size = android.graphics.Point()
                    display.getSize(size)

                    val insetT = rect.top
                    val insetB = size.y - rect.bottom
                    val insetL = rect.left
                    val insetR = size.x - rect.right

                    AppLog.d("[UI_DEBUG] Legacy SystemUI: Detected Insets L$insetL T$insetT R$insetR B$insetB")
                    HeadUnitScreenConfig.updateInsets(manualL + insetL, manualT + insetT, manualR + insetR, manualB + insetB)
                    onInsetsChanged?.invoke()
                    root.viewTreeObserver.removeGlobalOnLayoutListener(this)
                }
            })
        }

        // Set up listener for dynamic system bars (API 21+)
        ViewCompat.setOnApplyWindowInsetsListener(root) { v, insetsCompat ->
            var typeMask = 0

            when (mode) {
                Settings.FullscreenMode.IMMERSIVE -> {
                    typeMask = 0 // Standard Immersive: Overlay everything (Notch included)
                }
                Settings.FullscreenMode.IMMERSIVE_WITH_NOTCH -> {
                    // This is now the "Avoid Notch" mode (ID 3)
                    if (Build.VERSION.SDK_INT >= 28) {
                        typeMask = WindowInsetsCompat.Type.displayCutout()
                    }
                }
                Settings.FullscreenMode.STATUS_ONLY -> {
                    typeMask = WindowInsetsCompat.Type.navigationBars()
                    if (Build.VERSION.SDK_INT >= 28) {
                        typeMask = typeMask or WindowInsetsCompat.Type.displayCutout()
                    }
                }
                else -> {
                    typeMask = WindowInsetsCompat.Type.systemBars()
                    if (Build.VERSION.SDK_INT >= 28) {
                        typeMask = typeMask or WindowInsetsCompat.Type.displayCutout()
                    }
                }
            }

            val bars = if (typeMask != 0) {
                insetsCompat.getInsets(typeMask)
            } else {
                androidx.core.graphics.Insets.NONE
            }

            v.setPadding(bars.left + manualL, bars.top + manualT, bars.right + manualR, bars.bottom + manualB)
            HeadUnitScreenConfig.updateInsets(bars.left + manualL, bars.top + manualT, bars.right + manualR, bars.bottom + manualB)

            onInsetsChanged?.invoke()
            WindowInsetsCompat.CONSUMED
        }

        ViewCompat.requestApplyInsets(root)
        root.requestLayout()
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/utils/InetAddress.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import java.net.InetAddress
import java.net.UnknownHostException

/**
 * Convert a IPv4 address from an integer to an InetAddress.
 * @param hostAddress an int corresponding to the IPv4 address in network byte order
 */
fun Int.toInetAddress(): InetAddress {
    val hostAddress = this
    val addressBytes = byteArrayOf((0xff and hostAddress).toByte(),
            (0xff and (hostAddress shr 8)).toByte(),
            (0xff and (hostAddress shr 16)).toByte(),
            (0xff and (hostAddress shr 24)).toByte())
    return try {
        InetAddress.getByAddress(addressBytes)
    } catch (e: UnknownHostException) {
        AppLog.e(e)
        throw e
    }
}

fun InetAddress.changeLastBit(byte: Byte): InetAddress {
    return InetAddress.getByAddress(byteArrayOf(address[0], address[1], address[2], byte))
}
```

`app/src/main/java/com/andrerinas/headunitrevived/utils/NightMode.kt`:

```kt
package com.andrerinas.headunitrevived.utils

import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Date
import java.util.Locale

class NightMode(private val settings: Settings, val hasGPSLocation: Boolean) {
    private val calculator = NightModeCalculator(settings)
    var currentLux: Float = -1f
    var currentBrightness: Int = -1

    var current: Boolean = false
        get()  {
            return when (settings.nightMode){
                Settings.NightMode.AUTO -> calculator.current
                Settings.NightMode.DAY -> false
                Settings.NightMode.NIGHT -> true
                Settings.NightMode.MANUAL_TIME -> {
                    val now = Calendar.getInstance()
                    val currentMinutes = now.get(Calendar.HOUR_OF_DAY) * 60 + now.get(Calendar.MINUTE)
                    val start = settings.nightModeManualStart
                    val end = settings.nightModeManualEnd

                    val isNight = if (start <= end) {
                        currentMinutes in start..end
                    } else {
                        // Rollover (e.g. 22:00 to 06:00)
                        currentMinutes >= start || currentMinutes <= end
                    }

                    AppLog.d("NightMode Check: Now=$currentMinutes, Start=$start, End=$end, Result=$isNight")
                    isNight
                }
                Settings.NightMode.LIGHT_SENSOR -> {
                    if (currentLux >= 0) currentLux < settings.nightModeThresholdLux else false
                }
                Settings.NightMode.SCREEN_BRIGHTNESS -> {
                    if (currentBrightness >= 0) currentBrightness < settings.nightModeThresholdBrightness else false
                }
            }
        }

    override fun toString(): String {
        return when (settings.nightMode){
            Settings.NightMode.AUTO -> "NightMode: ${calculator.current}"
            Settings.NightMode.DAY -> "NightMode: DAY"
            Settings.NightMode.NIGHT -> "NightMode: NIGHT"
            Settings.NightMode.MANUAL_TIME -> {
                val startH = settings.nightModeManualStart / 60
                val startM = settings.nightModeManualStart % 60
                val endH = settings.nightModeManualEnd / 60
                val endM = settings.nightModeManualEnd % 60
                "NightMode: Manual (%02d:%02d - %02d:%02d)".format(startH, startM, endH, endM)
            }
            Settings.NightMode.LIGHT_SENSOR -> "NightMode: Sensor ($currentLux < ${settings.nightModeThresholdLux})"
            Settings.NightMode.SCREEN_BRIGHTNESS -> "NightMode: Brightness ($currentBrightness < ${settings.nightModeThresholdBrightness})"
        }
    }

    fun getCalculationInfo(): String {
        return calculator.getCalculationInfo()
    }
}

private class NightModeCalculator(private val settings: Settings) {
    private val twilightCalculator = TwilightCalculator()
    private val format = SimpleDateFormat("HH:mm", Locale.US)

    fun getCalculationInfo(): String {
        val time = Calendar.getInstance().time
        val location = settings.lastKnownLocation
        twilightCalculator.calculateTwilight(time.time, location.latitude, location.longitude)

        val sunrise = if (twilightCalculator.mSunrise > 0) format.format(Date(twilightCalculator.mSunrise)) else "--:--"
        val sunset = if (twilightCalculator.mSunset > 0) format.format(Date(twilightCalculator.mSunset)) else "--:--"
        return "$sunrise - $sunset"
    }

    var current: Boolean = false
        get()  {
            val time = Calendar.getInstance().time
            val location = settings.lastKnownLocation
            twilightCalculator.calculateTwilight(time.time, location.latitude, location.longitude)
            return twilightCalculator.mState == TwilightCalculator.NIGHT
        }

    override fun toString(): String {
        val sunrise = if (twilightCalculator.mSunrise > 0) format.format(Date(twilightCalculator.mSunrise)) else "-1"
        val sunset = if (twilightCalculator.mSunset > 0) format.format(Date(twilightCalculator.mSunset)) else "-1"
        val mode = if (twilightCalculator.mState == TwilightCalculator.NIGHT) "NIGHT" else "DAY"
        return String.format(Locale.US, "%s, (%s - %s)", mode, sunrise, sunset)
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/ssl/SslContextFactory.kt`:

```kt
package com.andrerinas.headunitrevived.ssl

import android.content.Context
import java.security.SecureRandom
import javax.net.ssl.SSLContext
import javax.net.ssl.TrustManager
import javax.net.ssl.X509TrustManager
import java.security.cert.X509Certificate

object SslContextFactory {

    fun create(context: Context): SSLContext {
        // Create a custom TrustManager that trusts all certificates
        val trustAllCerts = arrayOf<TrustManager>(object : X509TrustManager {
            override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?) {}
            override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?) {}
            override fun getAcceptedIssuers(): Array<X509Certificate> = arrayOf()
        })

        // Create a KeyManager using the existing SingleKeyKeyManager
        val keyManager = SingleKeyKeyManager(context)

        // Create an SSLContext that uses our KeyManager and the trust-all TrustManager
        val sslContext = SSLContext.getInstance("TLS")
        sslContext.init(arrayOf(keyManager), trustAllCerts, SecureRandom())

        return sslContext
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/ssl/NoCheckTrustManager.kt`:

```kt
package com.andrerinas.headunitrevived.ssl

import java.security.cert.X509Certificate
import javax.net.ssl.X509TrustManager

class NoCheckTrustManager: X509TrustManager {
    override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?) {
    }

    override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?) {
    }

    override fun getAcceptedIssuers(): Array<X509Certificate>? {
        return null
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/ssl/ConscryptInitializer.kt`:

```kt
package com.andrerinas.headunitrevived.ssl

import android.os.Build
import java.security.Security

object ConscryptInitializer {
    @Volatile private var initialized = false
    @Volatile private var conscryptAvailable = false

    @Synchronized
    fun initialize(): Boolean {
        if (initialized) return conscryptAvailable
        initialized = true

        try {
            val conscrypt = Class.forName("org.conscrypt.Conscrypt")
            val newProviderMethod = conscrypt.getMethod("newProvider")
            val provider = newProviderMethod.invoke(null) as java.security.Provider

            // Insert at position 1 (highest priority)
            val result = Security.insertProviderAt(provider, 1)

            // Check if installation succeeded or if already installed
            conscryptAvailable = result != -1 || Security.getProvider("Conscrypt") != null

            if (conscryptAvailable) {
                android.util.Log.i("ConscryptInit", "Conscrypt installed as security provider (position: $result)")
            }
        } catch (e: ClassNotFoundException) {
            android.util.Log.e("ConscryptInit", "Conscrypt library not found - TLS 1.2 may not work on Android < 21", e)
            conscryptAvailable = false
        } catch (e: Exception) {
            android.util.Log.e("ConscryptInit", "Failed to initialize Conscrypt", e)
            conscryptAvailable = false
        }

        return conscryptAvailable
    }

    fun isAvailable(): Boolean = conscryptAvailable

    fun isNeededForTls12(): Boolean = Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP

    fun getProviderName(): String? = if (conscryptAvailable) "Conscrypt" else null
}

```

`app/src/main/java/com/andrerinas/headunitrevived/ssl/SingleKeyKeyManager.kt`:

```kt
package com.andrerinas.headunitrevived.ssl

import android.content.Context
import android.util.Base64
import com.andrerinas.headunitrevived.R
import java.net.Socket
import java.security.KeyFactory
import java.security.KeyStore
import java.security.Principal
import java.security.PrivateKey
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import java.security.spec.PKCS8EncodedKeySpec
import javax.net.ssl.KeyManagerFactory
import javax.net.ssl.SSLEngine
import javax.net.ssl.X509ExtendedKeyManager
import javax.net.ssl.X509KeyManager

class SingleKeyKeyManager(certificate: X509Certificate, privateKey: PrivateKey): X509ExtendedKeyManager() {

    constructor(context: Context)
        : this(createCertificate(context), createPrivateKey(context))

    private val delegate: X509KeyManager

    init {
        val ks = KeyStore.getInstance(KeyStore.getDefaultType())
        ks.load(null)
        ks.setCertificateEntry(DEFAULT_ALIAS, certificate)
        ks.setKeyEntry(DEFAULT_ALIAS, privateKey, charArrayOf(), arrayOf(certificate))

        val kmf = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm())
        kmf.init(ks, charArrayOf())
        delegate = kmf.keyManagers[0] as X509KeyManager
    }

    override fun getClientAliases(keyType: String?, issuers: Array<out Principal>?): Array<String> {
        return delegate.getClientAliases(keyType, issuers)
    }

    override fun getServerAliases(keyType: String?, issuers: Array<out Principal>?): Array<String> {
        return delegate.getServerAliases(keyType, issuers)
    }

    override fun chooseServerAlias(keyType: String?, issuers: Array<out Principal>?, socket: Socket?): String {
        return delegate.chooseServerAlias(keyType, issuers, socket)
    }

    override fun getCertificateChain(alias: String?): Array<X509Certificate> {
        return delegate.getCertificateChain(alias)
    }

    override fun getPrivateKey(alias: String?): PrivateKey {
        return delegate.getPrivateKey(alias)
    }

    override fun chooseClientAlias(keyType: Array<out String>?, issuers: Array<out Principal>?, socket: Socket?): String {
        return DEFAULT_ALIAS
    }

    override fun chooseEngineClientAlias(keyType: Array<out String>?, issuers: Array<out Principal>?, engine: SSLEngine?): String {
        return DEFAULT_ALIAS
    }

    companion object {
        private const val DEFAULT_ALIAS = "defaultSingleKeyAlias"

        private fun createCertificate(context: Context): X509Certificate {
            val certStream = context.resources.openRawResource(R.raw.cert)
            val certificateFactory = CertificateFactory.getInstance("X.509")
            return certificateFactory.generateCertificate(certStream) as X509Certificate
        }

        private fun createPrivateKey(context: Context): PrivateKey {
            val privateKeyContent = context.resources
                    .openRawResource(R.raw.privkey)
                    .bufferedReader().use { it.readText() }
                    .replace("\n", "")
                    .replace("-----BEGIN PRIVATE KEY-----", "")
                    .replace("-----END PRIVATE KEY-----", "")
            val keySpecPKCS8 = PKCS8EncodedKeySpec(Base64.decode(privateKeyContent, Base64.DEFAULT))
            val kf = KeyFactory.getInstance("RSA")
            return kf.generatePrivate(keySpecPKCS8)
        }
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/view/IProjectionView.kt`:

```kt
package com.andrerinas.headunitrevived.view

import android.view.Surface

interface IProjectionView {
    interface Callbacks {
        fun onSurfaceCreated(surface: Surface)
        fun onSurfaceDestroyed(surface: Surface)
        fun onSurfaceChanged(surface: Surface, width: Int, height: Int)
    }

    fun addCallback(callback: Callbacks)
    fun removeCallback(callback: Callbacks)
    fun setVideoSize(width: Int, height: Int)
    fun setVideoScale(scaleX: Float, scaleY: Float)
}

```

`app/src/main/java/com/andrerinas/headunitrevived/view/ProjectionViewScaler.kt`:

```kt
package com.andrerinas.headunitrevived.view

import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import android.view.Gravity
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.HeadUnitScreenConfig

object ProjectionViewScaler {

    fun updateScale(view: View, videoWidth: Int, videoHeight: Int) {
        if (videoWidth == 0 || videoHeight == 0 || view.width == 0 || view.height == 0) {
            return
        }

        val settings = App.provide(view.context).settings
        HeadUnitScreenConfig.init(view.context, view.resources.displayMetrics, settings)

        val usableW = HeadUnitScreenConfig.getUsableWidth()
        val usableH = HeadUnitScreenConfig.getUsableHeight()

        if (HeadUnitScreenConfig.forcedScale && view is ProjectionView) {
            val lp = view.layoutParams
            var paramsChanged = false

            // NOTE: For legacy forcedScale (SurfaceView), the 'stretchToFill' setting logic
            // is historically inverted compared to its name.
            if (settings.stretchToFill) {
                // stretchToFill = TRUE results in Aspect Ratio preservation (Centered with bars)
                val targetW = HeadUnitScreenConfig.getAdjustedWidth()
                val targetH = HeadUnitScreenConfig.getAdjustedHeight()

                if (lp.width != targetW || lp.height != targetH) {
                    lp.width = targetW
                    lp.height = targetH
                    paramsChanged = true
                }

                // Center the view in the usable area
                if (lp is FrameLayout.LayoutParams) {
                    if (lp.gravity != Gravity.CENTER) {
                        lp.gravity = Gravity.CENTER
                        paramsChanged = true
                    }
                }

                if (paramsChanged) {
                    view.layoutParams = lp
                }

                view.scaleX = 1.0f
                view.scaleY = 1.0f
                view.translationX = 0f
                view.translationY = 0f

                AppLog.i("[UI_DEBUG] FORCED & STRETCH On: Resized view to ${targetW}x${targetH} (centered)")
            } else {
                // Mode B: Stretch to fill the usable area exactly (ignores aspect ratio)
                if (lp.width != usableW || lp.height != usableH) {
                    lp.width = usableW
                    lp.height = usableH
                    paramsChanged = true
                }

                if (lp is FrameLayout.LayoutParams) {
                    val targetGravity = Gravity.TOP or Gravity.START
                    if (lp.gravity != targetGravity) {
                        lp.gravity = targetGravity
                        paramsChanged = true
                    }
                }

                if (paramsChanged) {
                    view.layoutParams = lp
                }

                view.scaleX = 1.0f
                view.scaleY = 1.0f
                view.translationX = 0f
                view.translationY = 0f

                AppLog.i("[UI_DEBUG] FORCED & STRETCH Off: Resized view to match screen exactly: ${usableW}x${usableH}")
            }
        } else {
            // Modern way / TextureView: Use View scaling properties on a full-screen view
            val finalScaleX = HeadUnitScreenConfig.getScaleX()
            val finalScaleY = HeadUnitScreenConfig.getScaleY()

            val lp = view.layoutParams
            var paramsChanged = false

            if (lp.width != ViewGroup.LayoutParams.MATCH_PARENT ||
                lp.height != ViewGroup.LayoutParams.MATCH_PARENT) {
                lp.width = ViewGroup.LayoutParams.MATCH_PARENT
                lp.height = ViewGroup.LayoutParams.MATCH_PARENT
                paramsChanged = true
            }

            if (lp is FrameLayout.LayoutParams) {
                if (lp.gravity != Gravity.NO_GRAVITY) {
                    lp.gravity = Gravity.NO_GRAVITY
                    paramsChanged = true
                }
            }

            if (paramsChanged) {
                view.layoutParams = lp
            }

            // Normal centering for non-forced modes
            view.translationX = 0f
            view.translationY = 0f

            if (view is IProjectionView) {
                view.setVideoScale(finalScaleX, finalScaleY)
            } else {
                view.scaleX = finalScaleX
                view.scaleY = finalScaleY
            }
            AppLog.i("[UI_DEBUG] Normal Scale. scaleX: $finalScaleX, scaleY: $finalScaleY")
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/view/GlProjectionView.kt`:

```kt
package com.andrerinas.headunitrevived.view

import android.content.Context
import android.graphics.SurfaceTexture
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.opengl.Matrix
import android.os.Handler
import android.os.Looper
import android.view.Surface
import com.andrerinas.headunitrevived.utils.AppLog
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

class GlProjectionView(context: Context) : GLSurfaceView(context), IProjectionView {

    private val renderer: VideoRenderer
    private val callbacks = mutableListOf<IProjectionView.Callbacks>()

    init {
        setEGLContextClientVersion(2)
        renderer = VideoRenderer()
        setRenderer(renderer)
        renderMode = RENDERMODE_WHEN_DIRTY
        preserveEGLContextOnPause = true // Keep context alive if possible
    }

    override fun addCallback(callback: IProjectionView.Callbacks) {
        callbacks.add(callback)
        renderer.getSurface()?.let {
            if (it.isValid) {
                callback.onSurfaceCreated(it)
                callback.onSurfaceChanged(it, width, height)
            }
        }
    }

    override fun removeCallback(callback: IProjectionView.Callbacks) {
        callbacks.remove(callback)
    }

    fun getSurface(): Surface? = renderer.getSurface()
    fun isSurfaceValid(): Boolean = renderer.getSurface()?.isValid == true

    override fun setVideoSize(width: Int, height: Int) {
        AppLog.i("GlProjectionView setVideoSize: ${width}x$height")
        renderer.updateBufferSize(width, height)
        // ProjectionViewScaler removed, we use setVideoScale via Matrix
    }

    override fun setVideoScale(scaleX: Float, scaleY: Float) {
        renderer.setScale(scaleX, scaleY)
    }

    fun setDesaturation(value: Float) {
        renderer.setDesaturation(value)
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        renderer.release()
    }

    private inner class VideoRenderer : Renderer, SurfaceTexture.OnFrameAvailableListener {
        private var surfaceTexture: SurfaceTexture? = null
        private var surface: Surface? = null

        private var textureId: Int = 0
        private var program: Int = 0

        private var mVPMatrix = FloatArray(16)
        private var sSTMatrix = FloatArray(16)

        private var mScaleX = 1.0f
        private var mScaleY = 1.0f

        fun updateBufferSize(width: Int, height: Int) {
            surfaceTexture?.setDefaultBufferSize(width, height)
        }

        fun setScale(x: Float, y: Float) {
            mScaleX = x
            mScaleY = y
        }

        private val vertexShaderCode = """
            attribute vec4 aPosition;
            attribute vec4 aTextureCoord;
            varying vec2 vTextureCoord;
            uniform mat4 uMVPMatrix;
            uniform mat4 uSTMatrix;
            void main() {
                gl_Position = uMVPMatrix * aPosition;
                vTextureCoord = (uSTMatrix * aTextureCoord).xy;
            }
        """

        private val fragmentShaderCode = """
            #extension GL_OES_EGL_image_external : require
            precision mediump float;
            varying vec2 vTextureCoord;
            uniform samplerExternalOES sTexture;
            uniform float uDesaturation;
            void main() {
                vec4 color = texture2D(sTexture, vTextureCoord);
                float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
                gl_FragColor = vec4(mix(color.rgb, vec3(gray), uDesaturation), color.a);
            }
        """

        private var vertexBuffer: FloatBuffer? = null
        private val squareCoords = floatArrayOf(
            -1.0f, -1.0f, 0.0f, // bottom left
             1.0f, -1.0f, 0.0f, // bottom right
            -1.0f,  1.0f, 0.0f, // top left
             1.0f,  1.0f, 0.0f  // top right
        )

        private val textureCoords = floatArrayOf(
            0f, 0f,
            1f, 0f,
            0f, 1f,
            1f, 1f
        )
        private var textureBuffer: FloatBuffer? = null

        private var maPositionHandle = 0
        private var maTextureHandle = 0
        private var muMVPMatrixHandle = 0
        private var muSTMatrixHandle = 0
        private var muDesaturationHandle = 0

        @Volatile
        private var desaturation = 0.0f

        fun setDesaturation(value: Float) {
            desaturation = value.coerceIn(0f, 1f)
        }

        private var updateSurface = false

        fun getSurface(): Surface? = surface

        fun release() {
            surface?.let { s ->
                Handler(Looper.getMainLooper()).post {
                    callbacks.forEach { it.onSurfaceDestroyed(s) }
                }
            }
            surface?.release()
            surfaceTexture?.release()
        }

        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            AppLog.i("GlProjectionView: onSurfaceCreated (GL Context)")

            // Setup texture
            val textures = IntArray(1)
            GLES20.glGenTextures(1, textures, 0)
            textureId = textures[0]

            GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId)

            GLES20.glTexParameterf(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_NEAREST.toFloat())
            GLES20.glTexParameterf(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR.toFloat())
            GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
            GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)

            // Compile shaders
            val vertexShader = loadShader(GLES20.GL_VERTEX_SHADER, vertexShaderCode)
            val fragmentShader = loadShader(GLES20.GL_FRAGMENT_SHADER, fragmentShaderCode)

            program = GLES20.glCreateProgram()
            GLES20.glAttachShader(program, vertexShader)
            GLES20.glAttachShader(program, fragmentShader)
            GLES20.glLinkProgram(program)

            // Get handles
            maPositionHandle = GLES20.glGetAttribLocation(program, "aPosition")
            maTextureHandle = GLES20.glGetAttribLocation(program, "aTextureCoord")
            muMVPMatrixHandle = GLES20.glGetUniformLocation(program, "uMVPMatrix")
            muSTMatrixHandle = GLES20.glGetUniformLocation(program, "uSTMatrix")
            muDesaturationHandle = GLES20.glGetUniformLocation(program, "uDesaturation")

            // Buffers
            val bb = ByteBuffer.allocateDirect(squareCoords.size * 4)
            bb.order(ByteOrder.nativeOrder())
            vertexBuffer = bb.asFloatBuffer()
            vertexBuffer?.put(squareCoords)
            vertexBuffer?.position(0)

            val bbT = ByteBuffer.allocateDirect(textureCoords.size * 4)
            bbT.order(ByteOrder.nativeOrder())
            textureBuffer = bbT.asFloatBuffer()
            textureBuffer?.put(textureCoords)
            textureBuffer?.position(0)

            // Create Surface
            surfaceTexture = SurfaceTexture(textureId)
            surfaceTexture!!.setOnFrameAvailableListener(this)
            surface = Surface(surfaceTexture)

            // Notify Activity on Main Thread
            Handler(Looper.getMainLooper()).post {
                AppLog.i("GlProjectionView: Reporting Surface Created")
                callbacks.forEach { it.onSurfaceCreated(surface!!) }
            }
        }

        override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
            AppLog.i("GlProjectionView: onSurfaceChanged: ${width}x$height")
            GLES20.glViewport(0, 0, width, height)
            Handler(Looper.getMainLooper()).post {
                callbacks.forEach { it.onSurfaceChanged(surface!!, width, height) }
            }
        }

        override fun onDrawFrame(gl: GL10?) {
            synchronized(this) {
                if (updateSurface) {
                    surfaceTexture?.updateTexImage()
                    surfaceTexture?.getTransformMatrix(sSTMatrix)
                    updateSurface = false
                }
            }

            GLES20.glClearColor(0.0f, 0.0f, 0.0f, 1.0f)
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)

            GLES20.glUseProgram(program)

            GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
            GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, textureId)

            vertexBuffer?.position(0)
            GLES20.glVertexAttribPointer(maPositionHandle, 3, GLES20.GL_FLOAT, false, 3 * 4, vertexBuffer)
            GLES20.glEnableVertexAttribArray(maPositionHandle)

            textureBuffer?.position(0)
            GLES20.glVertexAttribPointer(maTextureHandle, 2, GLES20.GL_FLOAT, false, 2 * 4, textureBuffer)
            GLES20.glEnableVertexAttribArray(maTextureHandle)

            Matrix.setIdentityM(mVPMatrix, 0)
            Matrix.scaleM(mVPMatrix, 0, mScaleX, mScaleY, 1f)
            GLES20.glUniformMatrix4fv(muMVPMatrixHandle, 1, false, mVPMatrix, 0)
            GLES20.glUniformMatrix4fv(muSTMatrixHandle, 1, false, sSTMatrix, 0)
            GLES20.glUniform1f(muDesaturationHandle, desaturation)

            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        }

        override fun onFrameAvailable(surfaceTexture: SurfaceTexture?) {
            synchronized(this) {
                updateSurface = true
            }
            requestRender()
        }

        private fun loadShader(type: Int, shaderCode: String): Int {
            val shader = GLES20.glCreateShader(type)
            GLES20.glShaderSource(shader, shaderCode)
            GLES20.glCompileShader(shader)
            return shader
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/view/DebugOverlayView.kt`:

```kt
package com.andrerinas.headunitrevived.view

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.util.AttributeSet
import android.view.View

class DebugOverlayView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    private val paint = Paint().apply {
        style = Paint.Style.STROKE
        strokeWidth = 4f
        isAntiAlias = true
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        // View-Rand (ROT)
        paint.color = Color.RED
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), paint)

        // Mittelpunkt (BLAU)
        paint.color = Color.BLUE
        canvas.drawLine(
            width / 2f - 40, height / 2f,
            width / 2f + 40, height / 2f,
            paint
        )
        canvas.drawLine(
            width / 2f, height / 2f - 40,
            width / 2f, height / 2f + 40,
            paint
        )

        // Ursprung (GRÜN)
        paint.color = Color.GREEN
        canvas.drawCircle(0f, 0f, 12f, paint)
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/view/OverlayTouchView.kt`:

```kt
package com.andrerinas.headunitrevived.view

import android.content.Context
import android.graphics.Color
import android.util.AttributeSet

import android.view.View

class OverlayTouchView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : View(context, attrs) {

    init {
        setBackgroundColor(Color.TRANSPARENT)
        isClickable = true
        isFocusable = true
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/view/ProjectionView.kt`:

```kt
package com.andrerinas.headunitrevived.view

import android.content.Context
import android.util.AttributeSet
import android.view.SurfaceHolder
import android.view.SurfaceView
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.decoder.VideoDecoder
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.HeadUnitScreenConfig

class ProjectionView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null, defStyleAttr: Int = 0
) : SurfaceView(context, attrs, defStyleAttr), IProjectionView, SurfaceHolder.Callback {

    private val callbacks = mutableListOf<IProjectionView.Callbacks>()
    private var videoDecoder: VideoDecoder? = null
    private var videoWidth = 0
    private var videoHeight = 0

    init {
        videoDecoder = App.provide(context).videoDecoder
        holder.addCallback(this)
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        videoDecoder?.stop("onDetachedFromWindow")
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        ProjectionViewScaler.updateScale(this, videoWidth, videoHeight)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        AppLog.i("holder $holder")
        callbacks.forEach { it.onSurfaceCreated(holder.surface) }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        AppLog.i("holder $holder, format: $format, width: $width, height: $height")
        callbacks.forEach { it.onSurfaceChanged(holder.surface, width, height) }
        ProjectionViewScaler.updateScale(this, videoWidth, videoHeight)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        AppLog.i("holder $holder")
        videoDecoder?.stop("surfaceDestroyed")
        callbacks.forEach { it.onSurfaceDestroyed(holder.surface) }
    }

    override fun addCallback(callback: IProjectionView.Callbacks) {
        callbacks.add(callback)
        if (holder.surface.isValid) {
            callback.onSurfaceCreated(holder.surface)
            callback.onSurfaceChanged(holder.surface, width, height)
        }
    }

    override fun removeCallback(callback: IProjectionView.Callbacks) {
        callbacks.remove(callback)
    }

    override fun setVideoSize(width: Int, height: Int) {
        if (videoWidth == width && videoHeight == height) return
        AppLog.i("Video size set to ${width}x$height")
        videoWidth = width
        videoHeight = height

        if (HeadUnitScreenConfig.forcedScale) {
            val settings = App.provide(context).settings
            if (settings.stretchToFill) {
                holder.setSizeFromLayout()
            } else {
                AppLog.i("FORCED SCALE: Setting fixed size to ${width}x$height")
                holder.setFixedSize(width, height)
            }
        } else {
            holder.setSizeFromLayout()
        }

        ProjectionViewScaler.updateScale(this, videoWidth, videoHeight)
    }

    override fun setVideoScale(scaleX: Float, scaleY: Float) {
        this.scaleX = scaleX
        this.scaleY = scaleY
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/view/TextureProjectionView.kt`:

```kt
package com.andrerinas.headunitrevived.view

import android.content.Context
import android.graphics.SurfaceTexture
import android.util.AttributeSet
import android.view.Surface
import android.view.TextureView
import com.andrerinas.headunitrevived.utils.AppLog

class TextureProjectionView @JvmOverloads constructor(
    context: Context, attrs: AttributeSet? = null, defStyleAttr: Int = 0
) : TextureView(context, attrs, defStyleAttr), IProjectionView, TextureView.SurfaceTextureListener {

    private val callbacks = mutableListOf<IProjectionView.Callbacks>()
    private var surface: Surface? = null

    private var videoWidth = 0
    private var videoHeight = 0

    init {
        surfaceTextureListener = this
    }

    // ----------------------------------------------------------------
    // Public API
    // ----------------------------------------------------------------

    override fun setVideoSize(width: Int, height: Int) {
        if (videoWidth == width && videoHeight == height) return
        AppLog.i("TextureProjectionView", "Video size set to ${width}x$height")
        videoWidth = width
        videoHeight = height
        ProjectionViewScaler.updateScale(this, videoWidth, videoHeight)
    }

    override fun setVideoScale(scaleX: Float, scaleY: Float) {
        this.scaleX = scaleX
        this.scaleY = scaleY
        this.translationX = 0f
        this.translationY = 0f
    }

    // ----------------------------------------------------------------
    // Lifecycle & SurfaceTextureListener
    // ----------------------------------------------------------------

    override fun onSurfaceTextureAvailable(surfaceTexture: SurfaceTexture, width: Int, height: Int) {
        AppLog.i("TextureProjectionView: Surface available: ${width}x$height")
        surface = Surface(surfaceTexture)
        surface?.let {
            callbacks.forEach { cb -> cb.onSurfaceCreated(it) }
            // The width and height of the view are passed here, but the decoder should
            // use the actual video dimensions it parses from the SPS.
            callbacks.forEach { cb -> cb.onSurfaceChanged(it, width, height) }
        }
        ProjectionViewScaler.updateScale(this, videoWidth, videoHeight)
    }

    override fun onSurfaceTextureSizeChanged(surfaceTexture: SurfaceTexture, width: Int, height: Int) {
        AppLog.i("TextureProjectionView: Surface size changed: ${width}x$height")
        ProjectionViewScaler.updateScale(this, videoWidth, videoHeight)
    }

    override fun onSurfaceTextureDestroyed(surfaceTexture: SurfaceTexture): Boolean {
        AppLog.i("TextureProjectionView: Surface destroyed")
        surface?.let {
            callbacks.forEach { cb -> cb.onSurfaceDestroyed(it) }
        }
        surface?.release()
        surface = null
        return true
    }

    override fun onSurfaceTextureUpdated(surfaceTexture: SurfaceTexture) {
        // Not used
    }

    // ----------------------------------------------------------------
    // Callbacks
    // ----------------------------------------------------------------

    override fun addCallback(callback: IProjectionView.Callbacks) {
        callbacks.add(callback)
        // If surface is already available, notify immediately.
        surface?.let {
            callback.onSurfaceCreated(it)
            callback.onSurfaceChanged(it, width, height)
        }
    }

    override fun removeCallback(callback: IProjectionView.Callbacks) {
        callbacks.remove(callback)
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapSsl.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import com.andrerinas.headunitrevived.connection.AccessoryConnection

interface AapSsl {
    fun decrypt(start: Int, length: Int, buffer: ByteArray): ByteArrayWithLimit?
    fun encrypt(offset: Int, length: Int, buffer: ByteArray): ByteArrayWithLimit?
    fun postHandshakeReset()
    fun performHandshake(connection: AccessoryConnection): Boolean
    fun release()
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapMessage.kt`:

```kt
package com.andrerinas.headunitrevived.aap


import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.MsgType
import com.google.protobuf.CodedOutputStream
import com.google.protobuf.Message

open class AapMessage(
        internal val channel: Int,
        internal val flags: Byte,
        internal val type: Int,
        internal val dataOffset: Int,
        internal val size: Int,
        val data: ByteArray) {

    @JvmOverloads constructor(channel: Int, type: Int, proto: Message, buf: ByteArray = ByteArray(size(proto)))
            : this(channel, flags(channel, type), type, HEADER_SIZE + MsgType.SIZE, size(proto), buf) {

        val msgType = this.type
        this.data[0] = channel.toByte()
        this.data[1] = flags
        Utils.intToBytes(proto.serializedSize + MsgType.SIZE, 2, this.data)
        this.data[4] = (msgType shr 8).toByte()
        this.data[5] = (msgType and 0xFF).toByte()

        toByteArray(proto, this.data, HEADER_SIZE + MsgType.SIZE, proto.serializedSize)
    }

    val isAudio: Boolean
        get() = Channel.isAudio(this.channel)

    val isVideo: Boolean
        get() = this.channel == Channel.ID_VID

    override fun toString(): String {
        val sb = StringBuilder()
        sb.append(Channel.name(channel))
        sb.append(' ')
        sb.append(MsgType.name(type, channel))
        sb.append(" type: ")
        sb.append(type)
        sb.append(" flags: ")
        sb.append(flags)
        sb.append(" size: ")
        sb.append(size)
        sb.append(" dataOffset: ")
        sb.append(dataOffset)

//        sb.append('\n')
//        AapDump.logHex("", 0, data, this.size, sb)

        return sb.toString()
    }

    override fun equals(other: Any?): Boolean {
        val msg = other as AapMessage

        if (msg.channel != this.channel) {
            return false
        }
        if (msg.flags != this.flags) {
            return false
        }
        if (msg.type != this.type) {
            return false
        }
        if (msg.size != this.size || msg.data.size < this.size) {
            return false
        }
        if (msg.dataOffset != this.dataOffset) {
            return false
        }
        for (i in 0 until this.size) {
            if (msg.data[i] != this.data[i]) {
                return false
            }
        }
        return true
    }

    override fun hashCode(): Int{
        var result = channel
        result = 31 * result + flags
        result = 31 * result + type
        result = 31 * result + dataOffset
        result = 31 * result + size
 //       result = 31 * result + Arrays.hashCode(data)
        return result
    }


    internal fun <T : Message.Builder> parse(builder: T): T {
        builder.mergeFrom(this.data, this.dataOffset, this.size - this.dataOffset)
        return builder
    }

    private fun toByteArray(msg: Message, data: ByteArray, offset: Int, length: Int) {
        val output = CodedOutputStream.newInstance(data, offset, length)
        msg.writeTo(output)
        output.checkNoSpaceLeft()
    }

    companion object {
        const val HEADER_SIZE = 4

        private fun size(proto: Message): Int {
            return proto.serializedSize + MsgType.SIZE + HEADER_SIZE
        }

        private fun flags(channel: Int, type: Int): Byte {
            var flags: Byte = 0x0b
            if (channel != Channel.ID_CTR && MsgType.isControl(type)) {
                // Set Control Flag (On non-control channels, indicates generic/"control type" messages
                flags = 0x0f
            }
            return flags
        }
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapService.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import android.app.Notification
import android.app.PendingIntent
import android.app.Service
import android.app.UiModeManager
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.SharedPreferences
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.net.wifi.WifiManager
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Build
import android.os.IBinder
import android.os.Parcel
import android.os.Parcelable
import android.os.PowerManager
import android.widget.Toast
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import androidx.core.content.IntentCompat
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.app.BootCompleteReceiver
import com.andrerinas.headunitrevived.main.MainActivity
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.aap.protocol.messages.NightModeEvent
import com.andrerinas.headunitrevived.aap.protocol.proto.MediaPlayback
import com.andrerinas.headunitrevived.connection.CommManager
import com.andrerinas.headunitrevived.connection.NetworkDiscovery
import com.andrerinas.headunitrevived.connection.WifiDirectManager
import android.support.v4.media.session.MediaSessionCompat
import android.support.v4.media.MediaMetadataCompat
import android.support.v4.media.session.PlaybackStateCompat
import androidx.media.session.MediaButtonReceiver
import com.andrerinas.headunitrevived.connection.UsbAccessoryMode
import com.andrerinas.headunitrevived.connection.UsbDeviceCompat
import com.andrerinas.headunitrevived.connection.UsbReceiver
import com.andrerinas.headunitrevived.location.GpsLocationService
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.LocaleHelper
import com.andrerinas.headunitrevived.utils.LogExporter
import com.andrerinas.headunitrevived.utils.NightModeManager
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.SystemClock
import java.util.concurrent.atomic.AtomicBoolean
import android.app.NotificationManager
import android.content.pm.ServiceInfo
import android.graphics.PixelFormat
import android.provider.Settings as AndroidSettings
import android.view.View
import android.view.WindowManager
import android.media.AudioManager
import com.andrerinas.headunitrevived.utils.HotspotManager
import com.andrerinas.headunitrevived.utils.VpnControl
import com.andrerinas.headunitrevived.utils.SilentAudioPlayer
import com.andrerinas.headunitrevived.connection.CarKeyReceiver
import com.andrerinas.headunitrevived.connection.NativeAaHandshakeManager
import com.andrerinas.headunitrevived.connection.NearbyManager
import com.andrerinas.headunitrevived.main.BackgroundNotification
import com.andrerinas.headunitrevived.utils.Settings
import com.andrerinas.headunitrevived.utils.protoUint32ToLong
import java.net.ServerSocket

/**
 * Top-level foreground service that manages the Android Auto connection lifecycle.
 *
 * Responsibilities:
 * - Manages the [CommManager] connection state machine (USB and WiFi)
 * - Drives [AapProjectionActivity] via intents and connection state flow
 * - Runs a [WirelessServer] for the "server" WiFi mode and coordinates [NetworkDiscovery] scans
 * - Keeps a foreground notification updated to reflect the current connection state
 * - Manages car mode, night mode, media session, and GPS location service
 *
 * Connection types:
 * - **USB**: [UsbReceiver] detects attach → [checkAlreadyConnectedUsb] → [connectUsbWithRetry]
 * - **WiFi (client)**: [NetworkDiscovery] finds a Headunit Server → [CommManager.connect]
 * - **WiFi (server)**: [WirelessServer] accepts incoming sockets from AA Wireless / Self Mode
 * - **Self Mode**: starts [WirelessServer] and launches the AA Wireless Setup Activity on-device
 */
class AapService : Service(), UsbReceiver.Listener {

    // SupervisorJob prevents a child coroutine failure from cancelling the whole scope
    private val serviceScope = CoroutineScope(Dispatchers.Main + SupervisorJob())

    private lateinit var uiModeManager: UiModeManager
    private lateinit var usbReceiver: UsbReceiver
    private var nightModeManager: NightModeManager? = null
    private var wifiDirectManager: WifiDirectManager? = null
    private var nativeAaHandshakeManager: NativeAaHandshakeManager? = null
    private var nearbyManager: NearbyManager? = null
    private var carKeyReceiver: CarKeyReceiver? = null
    private var silentAudioPlayer: SilentAudioPlayer? = null
    private var wirelessServer: WirelessServer? = null
    private var networkDiscovery: NetworkDiscovery? = null
    private var mediaSession: MediaSessionCompat? = null
    private var permanentFocusRequest: android.media.AudioFocusRequest? = null
    private var lastMediaButtonClickTime = 0L

    private var lastAaMediaMetadata: MediaPlayback.MediaMetaData? = null
    private var lastAaPlaybackPositionMs: Long = 0L
    private var lastAaPlaybackIsPlaying: Boolean? = null
    private var mediaSessionIsPlaying = false
    private var mediaMetadataDecodeJob: Job? = null
    /** Decoded on a background thread in [scheduleApplyAaMediaMetadata]; reused for notification updates on position ticks. */
    private var cachedAaAlbumArtBitmap: Bitmap? = null
    private var settingsPrefs: SharedPreferences? = null
    private val mediaNotification by lazy { BackgroundNotification(this) }

    private val settingsPreferenceListener =
        SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
            if (key == Settings.KEY_SYNC_MEDIA_SESSION_AA_METADATA) {
                serviceScope.launch(Dispatchers.Main) {
                    refreshMediaSessionMetadataForPrefsChange()
                }
            }
        }

    /**
     * Set to `true` before calling [stopSelf] or entering [onDestroy] to suppress any
     * flow observers that would otherwise update the already-dismissed notification.
     */
    private var isDestroying = false
    private var hasEverConnected = false
    private var accessoryHandshakeFailures = 0
    private var networkCallback: ConnectivityManager.NetworkCallback? = null
    private var wifiLock: WifiManager.WifiLock? = null

    private var wifiReadyCallback: ConnectivityManager.NetworkCallback? = null

    private var wifiReadyTimeoutJob: Job? = null
    private var wifiModeInitialized = false

    private var activeWifiMode = -1
    private var activeHelperStrategy = -1

    /**
     * Partial wake lock acquired when the service starts from boot/screen-on.
     * Keeps the CPU active while the head unit runs without ACC, making the
     * service harder for MediaTek's background power saving to kill.
     */
    private var bootWakeLock: PowerManager.WakeLock? = null

    /**
     * Runtime-registered receiver for MEDIA_BUTTON intents.
     * Unlike manifest-registered receivers, runtime receivers are NOT affected by
     * Android 8+ implicit broadcast restrictions — this is a critical difference
     * that makes steering wheel controls work on China headunits.
     */
    private val mediaButtonReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (Intent.ACTION_MEDIA_BUTTON == intent.action) {
                AppLog.i("Runtime MEDIA_BUTTON receiver fired")
                mediaSession?.let {
                    MediaButtonReceiver.handleIntent(it, intent)
                }
            }
        }
    }

    /**
     * Guards against duplicate [UsbAccessoryMode.connectAndSwitch] calls AND duplicate
     * [connectUsbWithRetry] calls for devices already in accessory mode.
     *
     * Set to `true` synchronously on the main thread before launching any background
     * USB connect/switch coroutine. Checked in [checkAlreadyConnectedUsb] to prevent
     * multiple concurrent connection attempts on the same device.
     * Cleared in the coroutine's finally block, or on disconnect.
     */
    private val isSwitchingToAccessory = AtomicBoolean(false)

    /**
     * Set when the phone sends VIDEO_FOCUS_NATIVE (user tapped "Exit" in AA).
     * Suppresses [scheduleReconnectIfNeeded] so we don't try to reconnect to a
     * stale dongle that hasn't re-enumerated yet.
     * Cleared on USB detach (dongle reset complete) or on fresh USB attach.
     */
    @Volatile
    private var userExitedAA = false
    @Volatile private var userExitCooldownUntil = 0L

    private val commManager get() = App.provide(this).commManager

    fun updateMediaSessionState(isPlaying: Boolean) {
        mediaSessionIsPlaying = isPlaying
        var actions = PlaybackStateCompat.ACTION_STOP or
                PlaybackStateCompat.ACTION_SKIP_TO_NEXT or
                PlaybackStateCompat.ACTION_SKIP_TO_PREVIOUS or
                PlaybackStateCompat.ACTION_PLAY_PAUSE

        var state: Int

        if (isPlaying) {
            state = PlaybackStateCompat.STATE_PLAYING
            actions = actions or PlaybackStateCompat.ACTION_PAUSE
        } else {
            state = PlaybackStateCompat.STATE_STOPPED
            actions = actions or PlaybackStateCompat.ACTION_PLAY
        }

        mediaSession?.setPlaybackState(
            PlaybackStateCompat.Builder()
                .setState(state, lastAaPlaybackPositionMs, if (isPlaying) 1.0f else 0.0f)
                .setActions(actions)
                .build()
        )
        AppLog.d(
            "MediaSession: State updated to ${if (isPlaying) "PLAYING" else "STOPPED"}, positionMs=$lastAaPlaybackPositionMs"
        )
    }

    private fun applyPlaceholderMediaMetadata() {
        mediaSession?.setMetadata(
            MediaMetadataCompat.Builder()
                .putString(MediaMetadataCompat.METADATA_KEY_TITLE, getString(R.string.video))
                .putString(MediaMetadataCompat.METADATA_KEY_ARTIST, getString(R.string.media_session_aa_status_placeholder))
                .build()
        )
    }

    private fun refreshMediaSessionMetadataForPrefsChange() {
        if (isDestroying) return
        val sync = App.provide(this).settings.syncMediaSessionWithAaMetadata
        if (!sync) {
            applyPlaceholderMediaMetadata()
            cachedAaAlbumArtBitmap = null
            mediaNotification.cancel()
        } else {
            val last = lastAaMediaMetadata
            if (last != null) {
                scheduleApplyAaMediaMetadata(last)
            } else {
                applyPlaceholderMediaMetadata()
                cachedAaAlbumArtBitmap = null
                mediaNotification.cancel()
            }
        }
    }

    private fun onAaMediaMetadataFromPhone(meta: MediaPlayback.MediaMetaData) {
        if (isDestroying) return
        lastAaMediaMetadata = meta
        if (!App.provide(this).settings.syncMediaSessionWithAaMetadata) return
        // Avoid showing a previous track's art with new title/artist until decode finishes.
        cachedAaAlbumArtBitmap = null
        scheduleApplyAaMediaMetadata(meta)
    }

    private fun onAaPlaybackStatusFromPhone(status: MediaPlayback.MediaPlaybackStatus) {
        if (isDestroying) return
        if (status.hasPlaybackSeconds()) {
            lastAaPlaybackPositionMs = status.playbackSeconds.protoUint32ToLong() * 1000L
        }
        val isPlayingFromStatus = resolveIsPlayingFromStatus(status)
        lastAaPlaybackIsPlaying = isPlayingFromStatus
        mediaSessionIsPlaying = isPlayingFromStatus

        if (!App.provide(this).settings.syncMediaSessionWithAaMetadata) return
        updateMediaSessionState(isPlayingFromStatus)
        lastAaMediaMetadata?.let { updateMediaNotification(it) }
    }

    private fun resolveIsPlayingFromStatus(status: MediaPlayback.MediaPlaybackStatus): Boolean {
        if (!status.hasState()) return lastAaPlaybackIsPlaying ?: mediaSessionIsPlaying
        return when (val s = status.state) {
            MediaPlayback.MediaPlaybackStatus.State.PLAYING -> true
            MediaPlayback.MediaPlaybackStatus.State.STOPPED,
            MediaPlayback.MediaPlaybackStatus.State.PAUSED -> false
        }
    }

    private fun updateMediaNotification(meta: MediaPlayback.MediaMetaData) {
        if (!App.provide(this).settings.syncMediaSessionWithAaMetadata) return
        mediaNotification.notify(
            metadata = meta,
            playbackSeconds = lastAaPlaybackPositionMs / 1000L,
            isPlaying = lastAaPlaybackIsPlaying ?: mediaSessionIsPlaying,
            albumArtBitmap = cachedAaAlbumArtBitmap
        )
    }

    private fun scheduleApplyAaMediaMetadata(meta: MediaPlayback.MediaMetaData) {
        mediaMetadataDecodeJob?.cancel()
        mediaMetadataDecodeJob = serviceScope.launch(Dispatchers.Default) {
            val bytes = if (meta.hasAlbumArt() && !meta.albumArt.isEmpty) meta.albumArt.toByteArray() else null
            val bitmap = bytes?.let { decodeAlbumArt(it) }
            if (!isActive) return@launch
            withContext(Dispatchers.Main) {
                if (isDestroying) return@withContext
                if (!App.provide(this@AapService).settings.syncMediaSessionWithAaMetadata) return@withContext
                // Drop stale decode results if newer metadata arrived while we were decoding.
                if (lastAaMediaMetadata !== meta) return@withContext
                cachedAaAlbumArtBitmap = bitmap
                applyAaMediaMetadataToSession(meta, bitmap)
                updateMediaNotification(meta)
            }
        }
    }

    private fun decodeAlbumArt(bytes: ByteArray): Bitmap? {
        if (bytes.isEmpty()) return null
        return try {
            val opts = BitmapFactory.Options()
            opts.inJustDecodeBounds = true
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size, opts)
            if (opts.outWidth <= 0 || opts.outHeight <= 0) {
                opts.inJustDecodeBounds = false
                opts.inSampleSize = 1
                return BitmapFactory.decodeByteArray(bytes, 0, bytes.size, opts)
            }
            var sampleSize = 1
            val maxDim = 720
            while (opts.outWidth / sampleSize > maxDim || opts.outHeight / sampleSize > maxDim) {
                sampleSize *= 2
            }
            opts.inJustDecodeBounds = false
            opts.inSampleSize = sampleSize
            BitmapFactory.decodeByteArray(bytes, 0, bytes.size, opts)
        } catch (_: OutOfMemoryError) {
            null
        }
    }

    private fun applyAaMediaMetadataToSession(meta: MediaPlayback.MediaMetaData, albumArt: Bitmap?) {
        val session = mediaSession ?: return
        val title = when {
            meta.hasSong() && meta.song.isNotBlank() -> meta.song
            else -> getString(R.string.video)
        }
        val artist = when {
            meta.hasArtist() && meta.artist.isNotBlank() -> meta.artist
            else -> getString(R.string.media_session_aa_status_placeholder)
        }
        val b = MediaMetadataCompat.Builder()
            .putString(MediaMetadataCompat.METADATA_KEY_TITLE, title)
            .putString(MediaMetadataCompat.METADATA_KEY_ARTIST, artist)
        if (meta.hasAlbum() && meta.album.isNotBlank()) {
            b.putString(MediaMetadataCompat.METADATA_KEY_ALBUM, meta.album)
        }
        if (meta.hasDurationSeconds()) {
            val durationSec = meta.durationSeconds.protoUint32ToLong()
            if (durationSec > 0L) {
                b.putLong(MediaMetadataCompat.METADATA_KEY_DURATION, durationSec * 1000L)
            }
        }
        if (albumArt != null) {
            b.putBitmap(MediaMetadataCompat.METADATA_KEY_ALBUM_ART, albumArt)
        }
        session.setMetadata(b.build())
    }

    // Receives ACTION_REQUEST_NIGHT_MODE_UPDATE broadcasts sent by the key-binding handler
    // when the user presses the night-mode toggle key.
    private val nightModeUpdateReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == ACTION_REQUEST_NIGHT_MODE_UPDATE) {
                AppLog.i("Received request to resend night mode state")
                nightModeManager?.resendCurrentState()
            }
        }
    }

    // -------------------------------------------------------------------------
    // Wake detection for hibernate/quick boot head units
    // -------------------------------------------------------------------------

    /**
     * Timestamp (elapsedRealtime) when the screen last turned off.
     * Used to measure how long the device was asleep and distinguish a normal
     * screen timeout from a hibernate wake (car ACC off → on).
     */
    private var screenOffTimestamp = 0L

    /**
     * Debounce: last time [onHibernateWake] actually ran.
     * Prevents double-triggering when both BootCompleteReceiver and this dynamic
     * receiver fire for the same wake event.
     */
    private var lastWakeHandledTimestamp = 0L

    /**
     * Runtime-registered receiver for system wake/boot/power/screen events.
     *
     * On Chinese head units with Quick Boot (hibernate/resume), standard broadcasts
     * like BOOT_COMPLETED and USB_DEVICE_ATTACHED often don't fire after waking.
     * This receiver serves two purposes:
     *
     * 1. **Diagnostic logging:** Logs every received system event with the
     *    "WakeDetect:" prefix so users can export logs and we can see which
     *    broadcasts their specific head unit sends (or doesn't send) on wake.
     *
     * 2. **Universal wake detection:** Uses ACTION_SCREEN_ON (which fires on ALL
     *    devices after hibernate) combined with screen-off duration tracking to
     *    detect hibernate wakes and trigger auto-start — regardless of which OEM
     *    boot/ACC intents the device sends.
     *
     * ACTION_SCREEN_ON can only be received by dynamically registered receivers,
     * not manifest-declared ones — that's why the manifest-based BootCompleteReceiver
     * can't catch it and we need this service-based approach.
     */
    private val wakeDetectReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val action = intent.action ?: return

            when (action) {
                Intent.ACTION_SCREEN_OFF -> {
                    screenOffTimestamp = SystemClock.elapsedRealtime()
                    AppLog.i("WakeDetect: SCREEN_OFF")
                }
                Intent.ACTION_SCREEN_ON -> {
                    val now = SystemClock.elapsedRealtime()
                    val offDuration = if (screenOffTimestamp > 0) now - screenOffTimestamp else -1L
                    val offSec = if (offDuration >= 0) offDuration / 1000 else -1L
                    screenOffTimestamp = 0

                    AppLog.i("WakeDetect: SCREEN_ON (screen was off for ${offSec}s)")

                    val settings = App.provide(this@AapService).settings

                    // "Start on screen on" — triggers on every SCREEN_ON, designed for
                    // head units that never truly power off (quick boot / always-on).
                    if (settings.autoStartOnScreenOn) {
                        AppLog.i("WakeDetect: start-on-screen-on enabled, triggering auto-start")
                        onScreenOnAutoStart()
                    } else if (offDuration > HIBERNATE_WAKE_THRESHOLD_MS) {
                        // Hibernate wake detection — only for longer sleeps
                        AppLog.i("WakeDetect: hibernate wake detected (off for ${offSec}s > ${HIBERNATE_WAKE_THRESHOLD_MS / 1000}s threshold)")
                        onHibernateWake("SCREEN_ON after ${offSec}s sleep")
                    }
                }
                Intent.ACTION_USER_PRESENT -> {
                    AppLog.i("WakeDetect: USER_PRESENT")
                }
                Intent.ACTION_POWER_CONNECTED -> {
                    AppLog.i("WakeDetect: POWER_CONNECTED")
                    // On some head units, power connected = ACC on = car started.
                    // Only check USB (don't launch UI) since this could also be a
                    // charger being plugged in on a phone/tablet.
                    onPossibleWake("POWER_CONNECTED")
                }
                Intent.ACTION_POWER_DISCONNECTED -> {
                    AppLog.i("WakeDetect: POWER_DISCONNECTED")
                }
                Intent.ACTION_SHUTDOWN -> {
                    AppLog.i("WakeDetect: SHUTDOWN (system shutting down, not hibernating)")
                }
                else -> {
                    // OEM boot/ACC/wake intents — log with extras for diagnostics
                    AppLog.i("WakeDetect: $action")
                    val extras = intent.extras
                    if (extras != null && !extras.isEmpty) {
                        val extrasStr = extras.keySet().joinToString { "$it=${extras.get(it)}" }
                        AppLog.i("WakeDetect: extras: $extrasStr")
                    }
                    // Any OEM boot/ACC intent received dynamically = definite wake
                    onHibernateWake(action)
                }
            }
        }
    }

    /**
     * Called when we've confidently detected a hibernate wake (screen was off for
     * a long time, or an OEM boot/ACC intent was received by the dynamic receiver).
     */
    private fun onHibernateWake(trigger: String) {
        // Debounce: don't re-trigger within 10 seconds (covers BootCompleteReceiver + this)
        val now = SystemClock.elapsedRealtime()
        if (now - lastWakeHandledTimestamp < 10_000) {
            AppLog.i("WakeDetect: wake already handled ${(now - lastWakeHandledTimestamp) / 1000}s ago, skipping ($trigger)")
            return
        }
        lastWakeHandledTimestamp = now

        if (commManager.isConnected ||
            commManager.connectionState.value is CommManager.ConnectionState.Connecting ||
            isSwitchingToAccessory.get()) {
            AppLog.i("WakeDetect: already connected/connecting, skipping ($trigger)")
            return
        }

        val settings = App.provide(this).settings

        if (settings.autoStartOnBoot) {
            AppLog.i("WakeDetect: launching UI (trigger=$trigger)")
            launchMainActivityOnBoot()
        }

        if (settings.autoStartOnUsb) {
            AppLog.i("WakeDetect: checking USB devices (trigger=$trigger)")
            checkAlreadyConnectedUsb(force = true)
        }
    }

    /**
     * Called on events that MIGHT indicate a wake (e.g. POWER_CONNECTED) but aren't
     * conclusive alone. Only checks USB — does not launch the UI.
     */
    private fun onPossibleWake(trigger: String) {
        if (commManager.isConnected ||
            commManager.connectionState.value is CommManager.ConnectionState.Connecting ||
            isSwitchingToAccessory.get()) return

        val settings = App.provide(this).settings
        if (settings.autoStartOnUsb) {
            AppLog.i("WakeDetect: possible wake, checking USB (trigger=$trigger)")
            checkAlreadyConnectedUsb(force = true)
        }
    }

    /**
     * Called on every SCREEN_ON when "Start on screen on" is enabled.
     * Designed for head units that never truly power off — screen on = car turned on.
     *
     * If the connection is still active (e.g. brief screen toggle), returns to the
     * projection activity. Otherwise launches the main UI and checks USB.
     */
    private fun onScreenOnAutoStart() {
        // Debounce: don't re-trigger within 5 seconds
        val now = SystemClock.elapsedRealtime()
        if (now - lastWakeHandledTimestamp < 5_000) {
            AppLog.i("WakeDetect: screen-on auto-start already handled recently, skipping")
            return
        }
        lastWakeHandledTimestamp = now

        // Acquire wake lock to resist power saving cleanup on Quick Boot devices
        acquireBootWakeLock()

        if (commManager.isConnected) {
            // Connection still alive — return to projection screen
            AppLog.i("WakeDetect: connection active, returning to projection")
            try {
                val projectionIntent = AapProjectionActivity.intent(this).apply {
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
                }
                startActivity(projectionIntent)
            } catch (e: Exception) {
                AppLog.e("WakeDetect: failed to launch projection: ${e.message}")
            }
            return
        }

        if (commManager.connectionState.value is CommManager.ConnectionState.Connecting ||
            isSwitchingToAccessory.get()) {
            AppLog.i("WakeDetect: already connecting, skipping screen-on auto-start")
            return
        }

        // Not connected — launch UI (which triggers auto-connect via HomeFragment)
        AppLog.i("WakeDetect: launching UI on screen on")
        launchMainActivityOnBoot()

        val settings = App.provide(this).settings
        if (settings.autoStartOnUsb) {
            AppLog.i("WakeDetect: checking USB devices on screen on")
            checkAlreadyConnectedUsb(force = true)
        }
    }

    override fun onBind(intent: Intent): IBinder? = null

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    override fun onCreate() {
        super.onCreate()
        AppLog.i("AapService creating...")

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(1, createNotification(),
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE or ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
        } else {
            startForeground(1, createNotification())
        }
        setupCarMode()
        setupNightMode()
        observeConnectionState()
        registerReceivers()

        // Initialize MediaSession early and set it active immediately.
        // This ensures media button routing works even BEFORE an AA connection,
        // which is critical for keymap configuration and early button presses.
        if (mediaSession == null) {
            setupMediaSession()
        }
        mediaSession?.isActive = true
        updateMediaSessionState(false) // Set initial PlaybackState so system knows our actions

        commManager.onAaMediaMetadata = { meta -> onAaMediaMetadataFromPhone(meta) }
        commManager.onAaPlaybackStatus = { status -> onAaPlaybackStatusFromPhone(status) }
        settingsPrefs = getSharedPreferences("settings", MODE_PRIVATE).also { prefs ->
            prefs.registerOnSharedPreferenceChangeListener(settingsPreferenceListener)
        }

        LogExporter.startCapture(this, LogExporter.LogLevel.DEBUG)
        AppLog.i("Auto-started continuous log capture")

        startService(GpsLocationService.intent(this))

        nativeAaHandshakeManager = NativeAaHandshakeManager(this, serviceScope)
        wifiDirectManager = WifiDirectManager(this)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            try {
                nearbyManager = NearbyManager(this, serviceScope) { socket ->
                    val settings = App.provide(this).settings
                    settings.saveLastConnection(Settings.CONNECTION_TYPE_NEARBY)
                    serviceScope.launch(Dispatchers.IO) {
                        commManager.connect(socket)
                    }
                }
            } catch (e: Exception) {
                AppLog.e("AapService: Failed to init NearbyManager: ${e.message}")
            }
        }

        initWifiModeWithOptionalWait()
        wifiDirectManager?.setCredentialsListener { ssid, psk, ip, bssid ->
            val settings = App.provide(this).settings
            if (settings.wifiConnectionMode == 3) {
                AppLog.i("AapService: Received WiFi credentials from manager (SSID=$ssid, IP=$ip). Updating and Triggering Poke.")
                nativeAaHandshakeManager?.updateWifiCredentials(ssid, psk, ip, bssid)
                // [FIX] Only auto-poke if the user didn't explicitly exit.
                // If they did, they must click the "WiFi" button manually to poke.
                if (!userExitedAA) {
                    nativeAaHandshakeManager?.triggerPoke()
                } else {
                    AppLog.i("AapService: userExitedAA is true. Skipping auto-poke.")
                }
            } else {
                AppLog.d("AapService: WiFi credentials received, but not in Native AA mode. Skipping HandshakeManager update.")
            }
        }


        carKeyReceiver = CarKeyReceiver()
        silentAudioPlayer = SilentAudioPlayer(this)

        initWifiMode()
        checkAlreadyConnectedUsb()
        registerNetworkMonitor()

        // Grab permanent AUDIOFOCUS_GAIN at service start.
        // This ensures the headunit owns system audio focus before any AA connection,
        // preventing other apps from stealing it and causing AA to keep audio on the phone.
        val audioManager = getSystemService(Context.AUDIO_SERVICE) as AudioManager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val attrs = android.media.AudioAttributes.Builder()
                .setUsage(android.media.AudioAttributes.USAGE_MEDIA)
                .setContentType(android.media.AudioAttributes.CONTENT_TYPE_MUSIC)
                .build()
            permanentFocusRequest = android.media.AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(attrs)
                .setWillPauseWhenDucked(false)
                .setOnAudioFocusChangeListener(AudioManager.OnAudioFocusChangeListener { focusChange ->
                    AppLog.d("Permanent audio focus changed: $focusChange")
                })
                .build()
            audioManager.requestAudioFocus(permanentFocusRequest!!)
        } else {
            @Suppress("DEPRECATION")
            audioManager.requestAudioFocus(
                AudioManager.OnAudioFocusChangeListener { focusChange ->
                    AppLog.d("Permanent audio focus changed: $focusChange")
                },
                AudioManager.STREAM_MUSIC,
                AudioManager.AUDIOFOCUS_GAIN
            )
        }
        AppLog.i("Grabbed permanent AUDIOFOCUS_GAIN at service start")
    }

    /** Enables Android Automotive UI mode so the system uses car-optimised layouts. */
    private fun setupCarMode() {
        uiModeManager = getSystemService(UI_MODE_SERVICE) as UiModeManager
        uiModeManager.enableCarMode(0)
    }

    /** Initialises [NightModeManager] and forwards night-mode changes to Android Auto via AAP. */
    private fun setupNightMode() {
        nightModeManager = NightModeManager(this, App.provide(this).settings) { isNight ->
            AppLog.i("NightMode update: $isNight")
            commManager.send(NightModeEvent(isNight))
            // Also notify local components (for AA monochrome filter)
            val intent = Intent(ACTION_NIGHT_MODE_CHANGED).apply {
                setPackage(packageName)
                putExtra("isNight", isNight)
            }
            sendBroadcast(intent)
        }
        nightModeManager?.start()
    }

    /**
     * Single observer for all [CommManager.ConnectionState] transitions.
     *
     * Uses [hasEverConnected] to skip the initial [ConnectionState.Disconnected] emission
     * from StateFlow replay, avoiding a spurious disconnect on startup.
     */
    private fun observeConnectionState() {
        serviceScope.launch {
            commManager.connectionState.collect { state ->
                when (state) {
                    is CommManager.ConnectionState.Connected -> onConnected()
                    is CommManager.ConnectionState.HandshakeComplete -> {
                        launchAapProjectionActivity()
                    }
                    is CommManager.ConnectionState.TransportStarted -> {
                        hasEverConnected = true
                        accessoryHandshakeFailures = 0
                        sendBroadcast(Intent(ACTION_REQUEST_NIGHT_MODE_UPDATE).apply {
                            setPackage(packageName)
                        })
                    }
                    is CommManager.ConnectionState.Error -> {
                        if (state.message.contains("Handshake failed")) {
                            onHandshakeFailed()
                        }
                    }
                    is CommManager.ConnectionState.Disconnected -> {
                        if (hasEverConnected) onDisconnected(state)
                    }
                    else -> {}
                }
            }
        }
    }

    /**
     * Called by [CommManager.ConnectionState.Connected] observer:
     * 1. Refreshes the foreground notification.
     * 2. Activates a [MediaSessionCompat] so media keys are routed to Android Auto.
     * 3. Starts the SSL handshake ([CommManager.startHandshake]) **in parallel** with
     *    launching [AapProjectionActivity], hiding multi-second handshake latency behind
     *    activity-inflation time.
     *
     * The inbound message loop ([CommManager.startReading]) is intentionally NOT started
     * here. It is deferred until [AapProjectionActivity] confirms its render surface is
     * ready (via [CommManager.ConnectionState.HandshakeComplete] observer), guaranteeing
     * that [VideoDecoder.setSurface] is always called before the first video frame arrives.
     */
    private fun onConnected() {
        isSwitchingToAccessory.set(false)
        updateNotification()
        acquireWifiLock()

        // Start silent audio hack to keep media focus (helps with steering wheel buttons)
        silentAudioPlayer?.start()

        // Register the comprehensive steering wheel key receiver
        val filter = IntentFilter().apply {
            priority = 1000
            CarKeyReceiver.ACTIONS.forEach { addAction(it) }
        }
        try {
            ContextCompat.registerReceiver(
                this,
                carKeyReceiver,
                filter,
                ContextCompat.RECEIVER_EXPORTED
            )
        } catch (e: Exception) {
            AppLog.e("AapService: Failed to register CarKeyReceiver", e)
        }

        // Reactivate the existing MediaSession (created in onCreate, kept alive across disconnects)
        mediaSession?.isActive = true
        updateMediaSessionState(true)
        applyPlaceholderMediaMetadata()

        // Link audio focus state changes to our MediaSession state
        commManager.onAudioFocusStateChanged = { isPlaying ->
            updateMediaSessionState(isPlaying)
        }

        serviceScope.launch { commManager.startHandshake() }
    }

    private fun launchAapProjectionActivity() {
        startActivity(AapProjectionActivity.intent(this).apply {
            putExtra(AapProjectionActivity.EXTRA_FOCUS, true)
            addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
        })
    }

    private fun setupMediaSession() {
        val mbr = ComponentName(this, MediaButtonReceiver::class.java)
        mediaSession = MediaSessionCompat(this, "HeadunitRevived", mbr, null).apply {
            setCallback(object : MediaSessionCompat.Callback() {
                override fun onMediaButtonEvent(mediaButtonEvent: Intent?): Boolean {
                    val keyEvent = mediaButtonEvent?.let { IntentCompat.getParcelableExtra(it, Intent.EXTRA_KEY_EVENT, android.view.KeyEvent::class.java) }

                    if (keyEvent != null) {
                        val actionStr = if (keyEvent.action == android.view.KeyEvent.ACTION_DOWN) "DOWN" else "UP"
                        AppLog.d("MediaButtonEvent: Received key ${keyEvent.keyCode} ($actionStr)")

                        // Only handle ACTION_DOWN to prevent double triggers.
                        if (keyEvent.action == android.view.KeyEvent.ACTION_DOWN) {
                            val now = System.currentTimeMillis()
                            if (now - lastMediaButtonClickTime < 300) {
                                AppLog.i("MediaButtonEvent: Debouncing key ${keyEvent.keyCode} (too fast)")
                                return true
                            }
                            lastMediaButtonClickTime = now

                            AppLog.i("MediaButtonEvent: Processing key ${keyEvent.keyCode}")
                            // Send a complete click sequence (press + release) immediately
                            commManager.send(keyEvent.keyCode, true)
                            commManager.send(keyEvent.keyCode, false)
                            return true
                        }

                        // Consume ACTION_UP to prevent fallback
                        if (keyEvent.action == android.view.KeyEvent.ACTION_UP) {
                            return true
                        }
                    }

                    return super.onMediaButtonEvent(mediaButtonEvent)
                }

                override fun onPause() {
                    AppLog.i("MediaSession: Processing transport control action = KEYCODE_MEDIA_PAUSE")
                    commManager.send(android.view.KeyEvent.KEYCODE_MEDIA_PAUSE, true)
                    commManager.send(android.view.KeyEvent.KEYCODE_MEDIA_PAUSE, false)
                }

                override fun onPlay() {
                    AppLog.i("MediaSession: Processing transport control action = KEYCODE_MEDIA_PLAY")
                    commManager.send(android.view.KeyEvent.KEYCODE_MEDIA_PLAY, true)
                    commManager.send(android.view.KeyEvent.KEYCODE_MEDIA_PLAY, false)
                }

                override fun onSkipToNext() {
                    AppLog.i("MediaSession: Processing transport control action = KEYCODE_MEDIA_NEXT")
                    commManager.send(android.view.KeyEvent.KEYCODE_MEDIA_NEXT, true)
                    commManager.send(android.view.KeyEvent.KEYCODE_MEDIA_NEXT, false)
                }

                override fun onSkipToPrevious() {
                    AppLog.i("MediaSession: Processing transport control action = KEYCODE_MEDIA_PREVIOUS")
                    commManager.send(android.view.KeyEvent.KEYCODE_MEDIA_PREVIOUS, true)
                    commManager.send(android.view.KeyEvent.KEYCODE_MEDIA_PREVIOUS, false)
                }

                override fun onStop() {
                    AppLog.i("MediaSession: Processing transport control action = KEYCODE_MEDIA_STOP")
                    commManager.send(android.view.KeyEvent.KEYCODE_MEDIA_STOP, true)
                    commManager.send(android.view.KeyEvent.KEYCODE_MEDIA_STOP, false)
                }
            })
            setPlaybackToLocal(android.media.AudioManager.STREAM_MUSIC)
        }
        applyPlaceholderMediaMetadata()
    }

    /**
     * Called by [CommManager.ConnectionState.Disconnected] observer:
     * 1. Refreshing the notification (unless we are already tearing down)
     * 2. Releasing the [MediaSessionCompat]
     * 3. Stopping audio/video decoders on the IO thread
     * 4. Scheduling a reconnect attempt if applicable (see [scheduleReconnectIfNeeded])
     */
    private fun onDisconnected(state: CommManager.ConnectionState.Disconnected) {
        isSwitchingToAccessory.set(false)
        releaseWifiLock()

        // Cleanup steering wheel and audio focus hacks
        silentAudioPlayer?.stop()
        try {
            carKeyReceiver?.let { unregisterReceiver(it) }
        } catch (e: Exception) {}

        if (!isDestroying) updateNotification()
        mediaMetadataDecodeJob?.cancel()
        mediaMetadataDecodeJob = null
        lastAaMediaMetadata = null
        lastAaPlaybackPositionMs = 0L
        lastAaPlaybackIsPlaying = null
        cachedAaAlbumArtBitmap = null
        mediaNotification.cancel()
        applyPlaceholderMediaMetadata()
        // Keep MediaSession alive across disconnect/reconnect cycles.
        // Only deactivate it — do NOT release it. A released session can no longer
        // receive media button events, which means the keymap stops working until
        // the next connection. HURev keeps its session alive the entire service lifetime.
        mediaSession?.isActive = false
        updateMediaSessionState(false)
        serviceScope.launch(Dispatchers.IO) {
            nearbyManager?.stop() // Disconnect Nearby tunnel

            val settings = App.provide(this@AapService).settings
            if (settings.wifiConnectionMode == 3) {
                if (state.isUserExit) {
                    // [FIX] User voluntarily exited AA. Stop the BT handshake servers and
                    // tear down the WiFi Direct group so the phone can't auto-reconnect.
                    AppLog.i("AapService: Native AA user exit. Stopping handshake manager and WiFi Direct group.")
                    nativeAaHandshakeManager?.stop()
                } else {
                    // Unexpected disconnect — reset and re-initialize for auto-reconnect.
                    AppLog.i("AapService: Native AA Mode disconnected. Resetting manager and group in 1.5s...")
                    nativeAaHandshakeManager?.stop()
                    serviceScope.launch {
                        delay(1500) // Give hardware time to settle before re-initializing P2P
                        initWifiMode(force = true)
                    }
                }
            }
            App.provide(this@AapService).audioDecoder.stop()
            App.provide(this@AapService).videoDecoder.stop("AapService::onDisconnect")
        }

        // [FIX] Set cooldown flag for ALL user exits (not just USB).
        // The WirelessServer checks this flag to reject instant reconnections.
        if (state.isUserExit) {
            userExitedAA = true
            userExitCooldownUntil = android.os.SystemClock.elapsedRealtime() + USER_EXIT_COOLDOWN_MS
            AppLog.i("AapService: User exit cooldown active for ${USER_EXIT_COOLDOWN_MS}ms")
        }

        scheduleReconnectIfNeeded(state)
    }

    /**
     * Schedules a reconnect attempt 2 seconds after an unexpected disconnect:
     * - **Server mode** ([wirelessServer] != null): always restarts the discovery loop.
     * - **Auto WiFi mode** (mode == 1): triggers a one-shot scan on unclean disconnect only.
     *
     * [CommManager.ConnectionState.Disconnected.isClean] is `true` only when the phone
     * explicitly sends a `ByeByeRequest`. All other causes (USB detach, read error, explicit
     * disconnect) produce `isClean = false`.
     */
    private fun scheduleReconnectIfNeeded(state: CommManager.ConnectionState.Disconnected) {
        if (selfMode) {
            AppLog.i("AapService: Self Mode disconnected. Not restarting.")
            selfMode = false
            stopWirelessServer()
            return
        }

        val settings = App.provide(this).settings

        if (wirelessServer != null) {
            // Skip reconnect for user-initiated exits — the user explicitly wants to stop.
            if (state.isUserExit) {
                AppLog.i("AapService: User exit with wirelessServer active. Not restarting discovery.")
                return
            }
            AppLog.i("AapService: Disconnected. Restarting discovery loop in 2s...")
            serviceScope.launch {
                delay(2000)
                if (!commManager.isConnected) {
                    if (settings.wifiConnectionMode == 2 && settings.helperConnectionStrategy == 2) {
                        nearbyManager?.start()
                    } else if (settings.wifiConnectionMode == 2 && settings.helperConnectionStrategy == 1) {
                        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as android.net.wifi.WifiManager
                        if (wifiManager.isWifiEnabled) {
                            wifiDirectManager?.makeVisible()
                        }
                    } else {
                        startDiscovery()
                    }
                }
            }
            return
        }

        val lastType = settings.lastConnectionType

        // USB auto-reconnect: try again after a delay to give dongles time to re-enumerate.
        // Skip if the user voluntarily exited AA — the dongle is likely still connected with
        // stale data, and reconnecting immediately just causes handshake failures. The next
        // USB attach event will re-trigger the flow cleanly.
        if (lastType == Settings.CONNECTION_TYPE_USB &&
            (settings.autoConnectLastSession || settings.autoConnectSingleUsbDevice)) {
            if (state.isUserExit && !(settings.autoStartOnUsb && settings.reopenOnReconnection)) {
                AppLog.i("AapService: USB disconnect after user Exit. Skipping auto-reconnect (waiting for dongle re-enumeration).")
                userExitedAA = true
                return
            }
            if (state.isUserExit && settings.autoStartOnUsb && settings.reopenOnReconnection) {
                AppLog.i("AapService: USB disconnect after user Exit with reopenOnReconnection enabled. Will reconnect on next USB attach.")
                return
            }
            AppLog.i("AapService: USB disconnect. Scheduling reconnect check in ${USB_RECONNECT_DELAY_MS}ms...")
            serviceScope.launch {
                delay(USB_RECONNECT_DELAY_MS)
                if (!commManager.isConnected) checkAlreadyConnectedUsb(force = true)
            }
        }

        if (!state.isClean) {
            val mode = settings.wifiConnectionMode
            if (mode == 1 && lastType != Settings.CONNECTION_TYPE_USB) {
                AppLog.i("AapService: Unclean WiFi disconnect in Auto Mode. Retrying discovery in 2s...")
                serviceScope.launch {
                    delay(2000)
                    if (!commManager.isConnected) startDiscovery(oneShot = true)
                }
            }
        }
    }

    override fun attachBaseContext(newBase: Context) {
        super.attachBaseContext(LocaleHelper.wrapContext(newBase))
    }

    private fun registerReceivers() {
        usbReceiver = UsbReceiver(this)
        ContextCompat.registerReceiver(
            this, nightModeUpdateReceiver,
            IntentFilter(ACTION_REQUEST_NIGHT_MODE_UPDATE),
            ContextCompat.RECEIVER_NOT_EXPORTED
        )
        ContextCompat.registerReceiver(
            this, usbReceiver,
            UsbReceiver.createFilter(),
            ContextCompat.RECEIVER_NOT_EXPORTED
        )
        // Runtime-registered MEDIA_BUTTON receiver.
        // Unlike manifest-registered receivers, runtime receivers bypass the
        // Android 8+ implicit broadcast restriction. This is the primary mechanism
        // that makes steering wheel media buttons work on China headunits.
        ContextCompat.registerReceiver(
            this, mediaButtonReceiver,
            IntentFilter(Intent.ACTION_MEDIA_BUTTON),
            ContextCompat.RECEIVER_EXPORTED
        )
        AppLog.i("Registered runtime MEDIA_BUTTON receiver")

        // Wake detection receiver: catches SCREEN_ON, SCREEN_OFF, POWER_CONNECTED,
        // and all known OEM boot/ACC intents. Enables hibernate wake detection on
        // Quick Boot head units where BOOT_COMPLETED never fires.
        val wakeFilter = IntentFilter().apply {
            // Screen events (only receivable by dynamic receivers on Android 8+)
            addAction(Intent.ACTION_SCREEN_ON)
            addAction(Intent.ACTION_SCREEN_OFF)
            addAction(Intent.ACTION_USER_PRESENT)
            // Power events
            addAction(Intent.ACTION_POWER_CONNECTED)
            addAction(Intent.ACTION_POWER_DISCONNECTED)
            addAction(Intent.ACTION_SHUTDOWN)
            // Standard boot (dynamic duplicate — BootCompleteReceiver handles manifest side)
            addAction(Intent.ACTION_BOOT_COMPLETED)
            addAction(Intent.ACTION_LOCKED_BOOT_COMPLETED)
            // Quick boot variants
            addAction("android.intent.action.QUICKBOOT_POWERON")
            addAction("com.htc.intent.action.QUICKBOOT_POWERON")
            // MediaTek IPO (Instant Power On)
            addAction("com.mediatek.intent.action.QUICKBOOT_POWERON")
            addAction("com.mediatek.intent.action.BOOT_IPO")
            // FYT / GLSX head units (ACC ignition wake)
            addAction("com.fyt.boot.ACCON")
            addAction("com.glsx.boot.ACCON")
            addAction("android.intent.action.ACTION_MT_COMMAND_SLEEP_OUT")
            // Microntek / MTCD / PX3 head units (ACC wake)
            addAction("com.cayboy.action.ACC_ON")
            addAction("com.carboy.action.ACC_ON")
        }
        ContextCompat.registerReceiver(
            this, wakeDetectReceiver,
            wakeFilter,
            ContextCompat.RECEIVER_EXPORTED
        )
        AppLog.i("Registered wake detection receiver (${wakeFilter.countActions()} actions)")
    }

    private fun registerNetworkMonitor() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) return
        val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val callback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                AppLog.i("NetworkMonitor: Network available: $network")
            }
            override fun onLost(network: Network) {
                AppLog.w("NetworkMonitor: Network lost: $network")
            }
            override fun onCapabilitiesChanged(network: Network, caps: NetworkCapabilities) {
                AppLog.d("NetworkMonitor: Capabilities changed: $network → $caps")
            }
        }
        networkCallback = callback
        val request = NetworkRequest.Builder().build()
        cm.registerNetworkCallback(request, callback)
        AppLog.i("NetworkMonitor: Registered network change listener")
    }

    private fun unregisterNetworkMonitor() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) return
        networkCallback?.let {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
                try { cm.unregisterNetworkCallback(it) } catch (e: Exception) { }
            }
            networkCallback = null
        }
    }

    /**
     * Decides whether to call [initWifiMode] immediately or wait for WiFi connectivity.
     *
     * When "Wait for WiFi before WiFi Direct" is enabled AND WiFi connection mode is 2
     * (Wireless Helper), registers a [ConnectivityManager.NetworkCallback] filtered to
     * TRANSPORT_WIFI. [initWifiMode] fires as soon as WiFi connects, or after the
     * configured timeout — whichever comes first.
     *
     * When the setting is disabled, or the mode is not 2, [initWifiMode] runs immediately.
     */
    private fun initWifiModeWithOptionalWait() {
        val settings = App.provide(this).settings

        if (settings.wifiConnectionMode != 2 || !settings.waitForWifiBeforeWifiDirect) {
            initWifiMode()
            return
        }

        wifiModeInitialized = false

        val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val isWifiConnected = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            val activeNetwork = cm.activeNetwork
            val caps = if (activeNetwork != null) cm.getNetworkCapabilities(activeNetwork) else null
            caps != null && caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
        } else {
            @Suppress("DEPRECATION")
            val info = cm.activeNetworkInfo
            info != null && info.isConnected && info.type == ConnectivityManager.TYPE_WIFI
        }

        if (isWifiConnected || Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
            if (isWifiConnected) AppLog.i("WifiWait: WiFi already connected, initializing immediately")
            else AppLog.i("WifiWait: Legacy device (API < 21), skipping wait.")

            wifiModeInitialized = true
            initWifiMode()
            return
        }

        val timeoutSec = settings.waitForWifiTimeout.toLong()
        AppLog.i("WifiWait: Waiting up to ${timeoutSec}s for WiFi before initializing WiFi Direct...")

        val callback = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(network: Network) {
                    AppLog.i("WifiWait: WiFi connected (network=$network)")
                    serviceScope.launch {
                        completeWifiWait("WiFi connected")
                    }
                }
            }
        } else null

        wifiReadyCallback = callback

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP && callback != null) {
            val request = NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .build()
            cm.registerNetworkCallback(request, callback)
        }

        wifiReadyTimeoutJob = serviceScope.launch {
            delay(timeoutSec * 1000)
            completeWifiWait("timeout (${timeoutSec}s)")
        }
    }

    private fun completeWifiWait(reason: String) {
        if (wifiModeInitialized || isDestroying) return
        wifiModeInitialized = true

        AppLog.i("WifiWait: Completing (reason=$reason)")

        wifiReadyTimeoutJob?.cancel()
        wifiReadyTimeoutJob = null

        wifiReadyCallback?.let {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
                try { cm.unregisterNetworkCallback(it) } catch (_: Exception) {}
            }
            wifiReadyCallback = null
        }

        initWifiMode()
    }

    /** Starts [WirelessServer] if the user has configured server WiFi mode. */
    private fun initWifiMode(force: Boolean = false) {
        val settings = App.provide(this).settings
        val mode = settings.wifiConnectionMode
        val strategy = settings.helperConnectionStrategy

        if (!force && mode == activeWifiMode && strategy == activeHelperStrategy) {
            AppLog.d("AapService: WiFi Mode $mode (Strategy: $strategy) is already initialized.")
            return
        }

        AppLog.i("AapService: Initializing WiFi Mode: $mode (Strategy: $strategy)")

        // 0. Clean up existing wireless state before re-initializing
        stopWirelessServer()
        networkDiscovery?.stop()
        nearbyManager?.stop()
        nativeAaHandshakeManager?.stop()

        // Mode 1: Auto (Headunit Server), Mode 2: Helper (Wireless Launcher), Mode 3: Native AA
        if (mode == 1 || mode == 2 || mode == 3) {
            startWirelessServer()

            // Mode 1: Headunit Server Mode
            if (mode == 1) {
                // Auto discovery for standard server mode via NSD/mDNS
                startDiscovery(oneShot = false)
            }

            // Mode 2: Wireless Helper Mode
            if (mode == 2) {
                when (strategy) {
                    0 -> startDiscovery(oneShot = false) // Common Wifi (NSD)
                    1 -> { // WiFi Direct (P2P)
                        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as android.net.wifi.WifiManager
                        if (wifiManager.isWifiEnabled) {
                            wifiDirectManager?.makeVisible()
                        }
                    }
                    2 -> { // Google Nearby
                        nearbyManager?.start()
                    }
                    3, 4 -> { /* Host/Passive - just wait for connection on WirelessServer port */ }
                }

                // Hotspot logic for Helper mode if enabled
                if (settings.autoEnableHotspot) {
                    Thread {
                        AppLog.i("AapService: Auto-enabling hotspot for Helper mode...")
                        HotspotManager.setHotspotEnabled(this, true)
                    }.start()
                }
            }

            // Mode 3: Native AA Wireless
            if (mode == 3) {
                val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as android.net.wifi.WifiManager
                if (wifiManager.isWifiEnabled) {
                    // Start WiFi Direct as a "quiet host" (P2P Group for phone to join)
                    wifiDirectManager?.startNativeAaQuietHost()
                }
                // Start the official Bluetooth handshake servers
                nativeAaHandshakeManager?.start()
            }
        }

        activeWifiMode = mode
        activeHelperStrategy = strategy
    }

    private fun acquireWifiLock() {
        if (wifiLock == null) {
            val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
            wifiLock = wifiManager.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "HeadunitRevived:Connection")
        }
        if (wifiLock?.isHeld == false) {
            wifiLock?.acquire()
            AppLog.i("WifiLock acquired (HIGH_PERF)")
        }
    }

    private fun releaseWifiLock() {
        if (wifiLock?.isHeld == true) {
            wifiLock?.release()
            AppLog.i("WifiLock released")
        }
    }

    /**
     * Acquires a partial wake lock to resist MediaTek/Reglink background power
     * saving that force-stops third-party apps when ACC is off.
     * The wake lock has a 10-minute timeout as a safety net.
     */
    private fun acquireBootWakeLock() {
        if (bootWakeLock?.isHeld == true) return
        val pm = getSystemService(Context.POWER_SERVICE) as PowerManager
        bootWakeLock = pm.newWakeLock(
            PowerManager.PARTIAL_WAKE_LOCK,
            "HeadunitRevived::BootAutoStart"
        ).apply {
            acquire(10 * 60 * 1000L) // 10 minute timeout
        }
        AppLog.i("Boot WakeLock acquired (10min timeout)")

        // Log battery optimization status for diagnostics
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            val exempt = pm.isIgnoringBatteryOptimizations(packageName)
            AppLog.i("Battery optimization exempt: $exempt")
        }
    }

    private fun releaseBootWakeLock() {
        if (bootWakeLock?.isHeld == true) {
            bootWakeLock?.release()
            AppLog.i("Boot WakeLock released")
        }
        bootWakeLock = null
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        AppLog.i("AapService: onTaskRemoved — attempting restart")
        try {
            val restartIntent = Intent(this, AapService::class.java)
            ContextCompat.startForegroundService(this, restartIntent)
        } catch (e: Exception) {
            AppLog.e("AapService: failed to restart after task removal: ${e.message}")
        }
        super.onTaskRemoved(rootIntent)
    }

    override fun onDestroy() {
        AppLog.i("AapService destroying... (wakeLock held=${bootWakeLock?.isHeld == true})")
        isDestroying = true
        mediaMetadataDecodeJob?.cancel()
        cachedAaAlbumArtBitmap = null
        mediaNotification.cancel()
        commManager.onAaMediaMetadata = null
        commManager.onAaPlaybackStatus = null
        settingsPrefs?.unregisterOnSharedPreferenceChangeListener(settingsPreferenceListener)
        settingsPrefs = null
        nativeAaHandshakeManager?.stop()
        releaseBootWakeLock()

        if (App.provide(this).settings.autoEnableHotspot) {
            AppLog.i("AapService: Auto-disabling hotspot...")
            HotspotManager.setHotspotEnabled(this, false)
        }

        wifiReadyTimeoutJob?.cancel()
        wifiReadyTimeoutJob = null
        wifiReadyCallback?.let {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
                try { cm.unregisterNetworkCallback(it) } catch (_: Exception) {}
            }
            wifiReadyCallback = null
        }

        releaseWifiLock()
        unregisterNetworkMonitor()
        stopForeground(true)
        stopWirelessServer()
        wifiDirectManager?.stop()
        nearbyManager?.stop()
        mediaSession?.isActive = false
        mediaSession?.release()
        mediaSession = null
        commManager.destroy()
        nightModeManager?.stop()
        try { unregisterReceiver(nightModeUpdateReceiver) } catch (_: Exception) {}
        try { unregisterReceiver(usbReceiver) } catch (_: Exception) {}
        try { unregisterReceiver(mediaButtonReceiver) } catch (_: Exception) {}
        try { unregisterReceiver(wakeDetectReceiver) } catch (_: Exception) {}
        uiModeManager.disableCarMode(0)
        serviceScope.cancel()
        LogExporter.stopCapture()
        super.onDestroy()
        if (killProcessOnDestroy) {
            AppLog.i("AapService: killProcessOnDestroy is true. Triggering System.exit(0).")
            System.exit(0)
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        // Handle stop before re-posting the notification to avoid a flash
        if (intent?.action == ACTION_STOP_SERVICE) {
            AppLog.i("Stop action received. Broadcasting finish request to activities.")
            sendBroadcast(Intent("com.andrerinas.headunitrevived.ACTION_FINISH_ACTIVITIES").apply {
                setPackage(packageName)
            })
            isDestroying = true
            if (commManager.isConnected) commManager.disconnect(sendByeBye = true)
            stopForeground(true)
            stopSelf()
            return START_NOT_STICKY
        }

        // Route MEDIA_BUTTON intents to the active MediaSession.
        // This is the AndroidX-recommended pattern: MediaButtonReceiver (manifest)
        // forwards the intent to this service, and handleIntent() dispatches it
        // to the MediaSession callback. This works on Android 8+ where implicit
        // broadcasts to manifest-registered receivers are restricted.
        mediaSession?.let { MediaButtonReceiver.handleIntent(it, intent) }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(1, createNotification(),
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE or ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK)
        } else {
            startForeground(1, createNotification())
        }
        // Launch the UI after boot.
        // Direct startActivity() is silently blocked on MIUI/HyperOS even from
        // a foreground service. We use an overlay window trampoline: creating a
        // zero-size overlay gives the app a "visible" context that bypasses OEM
        // background activity start restrictions. Falls back to full-screen
        // intent notification if overlay permission is not granted.
        // Acquire a partial wake lock on any boot/screen-on start to resist
        // aggressive power saving on MediaTek/Reglink head units that force-stop
        // third-party apps when ACC is off after a Quick Boot reboot.
        if (intent?.getBooleanExtra(BootCompleteReceiver.EXTRA_BOOT_START, false) == true ||
            intent?.action == ACTION_CHECK_USB) {
            acquireBootWakeLock()
        }

        if (intent?.getBooleanExtra(BootCompleteReceiver.EXTRA_BOOT_START, false) == true) {
            // Mark wake as handled so the dynamic wakeDetectReceiver doesn't double-trigger
            lastWakeHandledTimestamp = SystemClock.elapsedRealtime()
            launchMainActivityOnBoot()
        }

        when (intent?.action) {
            ACTION_START_SELF_MODE       -> startSelfMode()
            ACTION_START_WIRELESS        -> initWifiMode()
            ACTION_START_WIRELESS_SCAN   -> {
                val settings = App.provide(this).settings
                val mode = settings.wifiConnectionMode
                val strategy = settings.helperConnectionStrategy

                // [FIX] Reset exit flags on manual scan start
                userExitedAA = false
                userExitCooldownUntil = 0L
                initWifiMode(force = true)

                if (mode == 2 && strategy == 2) {
                    AppLog.i("AapService: Force-starting Nearby discovery from UI")
                    nearbyManager?.start()
                } else if (mode == 2 && strategy == 1) {
                    AppLog.i("AapService: Force-starting WiFi Direct discovery from UI")
                    val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as android.net.wifi.WifiManager
                    if (wifiManager.isWifiEnabled) {
                        wifiDirectManager?.makeVisible()
                    } else {
                        Toast.makeText(this, getString(R.string.wifi_disabled_info), Toast.LENGTH_SHORT).show()
                    }
                } else if (mode != 3) {
                    startDiscovery(oneShot = (mode != 2))
                }
            }
            ACTION_STOP_WIRELESS         -> stopWirelessServer()
            ACTION_NATIVE_AA_POKE        -> {
                val mac = intent?.getStringExtra(EXTRA_MAC)
                if (mac != null) {
                    AppLog.i("AapService: Received manual Native-AA poke request for $mac")
                    // [FIX] Reset exit flags so the subsequent connection is accepted
                    userExitedAA = false
                    userExitCooldownUntil = 0L
                    // Ensure WiFi Direct and BT servers are ready before poking
                    initWifiMode(force = true)
                    nativeAaHandshakeManager?.manualPoke(mac)
                }
            }
            ACTION_NEARBY_CONNECT         -> {
                val endpointId = intent?.getStringExtra(EXTRA_ENDPOINT_ID)
                if (endpointId != null) {
                    AppLog.i("AapService: Connecting to Nearby endpoint $endpointId")
                    nearbyManager?.connectToEndpoint(endpointId)
                }
            }
            ACTION_DISCONNECT            -> {
                AppLog.i("Disconnect action received.")
                if (commManager.isConnected) commManager.disconnect()
            }
            ACTION_CONNECT_SOCKET        -> {
                // Caller already invoked commManager.connect(socket); the connectionState
                // observer in observeConnectionState() handles the rest — nothing to do here.
            }
            ACTION_CHECK_USB             -> checkAlreadyConnectedUsb(force = true)
            else                         -> {
                if (intent?.action == null || intent.action == Intent.ACTION_MAIN) {
                    checkAlreadyConnectedUsb()
                }
            }
        }
        return START_STICKY
    }

    // -------------------------------------------------------------------------
    // USB
    // -------------------------------------------------------------------------

    override fun onUsbAttach(device: UsbDevice) {
        userExitedAA = false
        if (UsbDeviceCompat.isInAccessoryMode(device)) {
            // Device already in AOA mode (re-enumerated after UsbAttachedActivity switched it).
            AppLog.i("USB accessory device attached, connecting.")
            launchMainActivityIfNeeded("USB accessory attach")
            checkAlreadyConnectedUsb(force = true)
        } else {
            // UsbAttachedActivity normally handles normal-mode devices via a manifest intent
            // filter. However, some headunits (especially Chinese MediaTek units) don't
            // deliver USB_DEVICE_ATTACHED to activities on cold start. As a fallback,
            // check after a delay to give UsbAttachedActivity a chance to handle it first.
            val deviceName = UsbDeviceCompat(device).uniqueName
            AppLog.i("Normal USB device attached: $deviceName. Will check auto-connect in ${USB_ATTACH_FALLBACK_DELAY_MS}ms...")
            launchMainActivityIfNeeded("USB normal attach ($deviceName)")
            serviceScope.launch {
                delay(USB_ATTACH_FALLBACK_DELAY_MS)
                if (!commManager.isConnected && !isSwitchingToAccessory.get()) {
                    AppLog.i("UsbAttachedActivity didn't handle $deviceName. Trying from service...")
                    checkAlreadyConnectedUsb(force = true)
                }
            }
        }
    }

    override fun onUsbDetach(device: UsbDevice) {
        userExitedAA = false
        if (commManager.isConnectedToUsbDevice(device)) {
            // Cable physically removed — the USB connection is already dead, so skip the
            // ByeByeRequest send (which would block ~1 s trying to write to a gone device).
            commManager.disconnect(sendByeBye = false)
        }
    }

    override fun onUsbAccessoryDetach() {
        AppLog.i("USB Accessory detached. This might be a transient state (e.g., 100% battery). Attempting to re-sync...")
        userExitedAA = false
        if (commManager.isConnected) {
            commManager.disconnect(sendByeBye = false)
        }

        // Wait a bit and check if the device is still there in normal mode
        serviceScope.launch {
            delay(1500) // Give the phone/system time to settle its USB state
            AppLog.i("Accessory detach cooldown finished. Checking for re-connection...")
            checkAlreadyConnectedUsb(force = true)
        }
    }

    override fun onUsbPermission(granted: Boolean, connect: Boolean, device: UsbDevice) {
        val deviceName = UsbDeviceCompat(device).uniqueName
        if (granted) {
            AppLog.i("USB permission granted for $deviceName")
            if (UsbDeviceCompat.isInAccessoryMode(device)) {
                isSwitchingToAccessory.set(true)
                serviceScope.launch {
                    try {
                        connectUsbWithRetry(device)
                    } finally {
                        isSwitchingToAccessory.set(false)
                    }
                }
            } else {
                isSwitchingToAccessory.set(true)
                val usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
                val usbMode = UsbAccessoryMode(usbManager)
                serviceScope.launch(Dispatchers.IO) {
                    try {
                        if (usbMode.connectAndSwitch(device)) {
                            AppLog.i("Successfully requested switch to accessory mode for $deviceName")
                        } else {
                            AppLog.w("USB permission granted but connectAndSwitch failed for $deviceName")
                        }
                    } finally {
                        isSwitchingToAccessory.set(false)
                    }
                }
            }
        } else {
            AppLog.w("USB permission denied for $deviceName")
            Toast.makeText(this, getString(R.string.usb_permission_denied), Toast.LENGTH_LONG).show()
        }
    }

    private fun requestUsbPermission(device: UsbDevice) {
        val usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
        val permissionIntent = UsbReceiver.createPermissionPendingIntent(this)
        AppLog.i("Requesting USB permission for ${UsbDeviceCompat(device).uniqueName}")
        try {
            Toast.makeText(this, getString(R.string.requesting_usb_permission), Toast.LENGTH_SHORT).show()
            usbManager.requestPermission(device, permissionIntent)
        } catch (e: Exception) {
            AppLog.e("Failed to request USB permission: ${e.message}. This device might not support USB permission dialogs.", e)
            Toast.makeText(this, getString(R.string.error_usb_permission_failed), Toast.LENGTH_LONG).show()
        }
    }

    /**
     * Called when a handshake fails. If an accessory-mode device is still present,
     * it's likely a stale wireless AA dongle. Force re-enumeration by sending AOA
     * descriptors — this resets the dongle's USB state so the next connection
     * starts with clean buffers.
     */
    private fun onHandshakeFailed() {
        val usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
        val accessoryDevice = usbManager.deviceList.values.firstOrNull {
            UsbDeviceCompat.isInAccessoryMode(it)
        } ?: return

        accessoryHandshakeFailures++
        val deviceName = UsbDeviceCompat(accessoryDevice).uniqueName
        AppLog.w("Handshake failed on accessory device $deviceName (failure #$accessoryHandshakeFailures)")

        if (accessoryHandshakeFailures > MAX_STALE_ACCESSORY_RETRIES) {
            AppLog.i("Stale accessory detected: forcing re-enumeration via AOA descriptors for $deviceName")
            accessoryHandshakeFailures = 0
            val usbMode = UsbAccessoryMode(usbManager)
            isSwitchingToAccessory.set(true)
            serviceScope.launch(Dispatchers.IO) {
                try {
                    if (usbMode.connectAndSwitch(accessoryDevice)) {
                        AppLog.i("AOA re-enumeration requested for stale device $deviceName")
                    } else {
                        AppLog.w("AOA re-enumeration failed for $deviceName")
                    }
                } catch (e: Exception) {
                    AppLog.e("AOA re-enumeration for $deviceName failed with exception", e)
                } finally {
                    isSwitchingToAccessory.set(false)
                }
            }
        }
    }

    /**
     * Scans currently connected USB devices and connects to any that are already in
     * Android Open Accessory (AOA) mode, or attempts to switch a known device into AOA mode.
     *
     * @param force When `true`, bypasses the [autoConnectLastSession] guard. Use `true` when
     *              called in response to an actual USB attach event or from [UsbAttachedActivity],
     *              because the user has explicitly plugged in a device. Use `false` (default)
     *              for the startup scan in [onCreate].
     */
    private fun checkAlreadyConnectedUsb(force: Boolean = false) {
        val settings = App.provide(this).settings
        val lastSession = settings.autoConnectLastSession
        val singleUsb = settings.autoConnectSingleUsbDevice
        val usbAutoStart = settings.autoStartOnUsb

        if (!force && !lastSession && !singleUsb && !usbAutoStart) return
        if (commManager.isConnected ||
            commManager.connectionState.value is CommManager.ConnectionState.Connecting ||
            isSwitchingToAccessory.get()) return

        val usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
        val deviceList = usbManager.deviceList

        // Check for devices already in accessory mode first.
        // After AOA switch the device re-enumerates and appears as a new USB device — we must
        // request permission for this new device before openDevice(), or SecurityException occurs.
        for (device in deviceList.values) {
            if (UsbDeviceCompat.isInAccessoryMode(device)) {
                val deviceName = UsbDeviceCompat(device).uniqueName
                AppLog.i("Found device already in accessory mode: $deviceName")
                if (!usbManager.hasPermission(device)) {
                    AppLog.i("Accessory-mode device has no permission (re-enumerated); requesting permission: $deviceName")
                    requestUsbPermission(device)
                    return
                }
                isSwitchingToAccessory.set(true)
                serviceScope.launch {
                    try {
                        connectUsbWithRetry(device)
                    } finally {
                        isSwitchingToAccessory.set(false)
                    }
                }
                return
            }
        }

        // Last-session mode: reconnect to a known/allowed device
        if (lastSession) {
            for (device in deviceList.values) {
                val deviceCompat = UsbDeviceCompat(device)
                if (settings.isConnectingDevice(deviceCompat)) {
                    if (usbManager.hasPermission(device)) {
                        AppLog.i("Found known USB device with permission: ${deviceCompat.uniqueName}. Switching to accessory mode.")
                        isSwitchingToAccessory.set(true)
                        val usbMode = UsbAccessoryMode(usbManager)
                        serviceScope.launch(Dispatchers.IO) {
                            try {
                                if (usbMode.connectAndSwitch(device)) {
                                    AppLog.i("Successfully requested switch to accessory mode for ${deviceCompat.uniqueName}")
                                } else {
                                    AppLog.w("connectAndSwitch failed for ${deviceCompat.uniqueName}")
                                }
                            } finally {
                                isSwitchingToAccessory.set(false)
                            }
                        }
                        return
                    } else {
                        AppLog.i("Found known USB device but no permission: ${deviceCompat.uniqueName}, requesting...")
                        requestUsbPermission(device)
                        return
                    }
                }
            }
        }

        // USB auto-start mode: attempt AOA switch for any single non-accessory device
        if (usbAutoStart) {
            val nonAccessoryDevices = deviceList.values.filter { !UsbDeviceCompat.isInAccessoryMode(it) }
            if (nonAccessoryDevices.size == 1) {
                performSingleUsbConnect(nonAccessoryDevices[0])
                return
            }
        }

        // Single-USB mode: connect if there's exactly one candidate device.
        // If the user has marked specific devices as "Allowed" in the USB list,
        // only count those — so non-AA peripherals (dashcams, USB audio, etc.)
        // don't prevent auto-connect. Falls back to counting all devices when
        // no devices have been explicitly allowed (fresh install).
        if (singleUsb) {
            val nonAccessoryDevices = deviceList.values.filter { !UsbDeviceCompat.isInAccessoryMode(it) }
            val allowed = settings.allowedDevices
            val candidates = if (allowed.isNotEmpty()) {
                nonAccessoryDevices.filter { allowed.contains(UsbDeviceCompat(it).uniqueName) }
            } else {
                nonAccessoryDevices
            }
            if (allowed.isNotEmpty() && candidates.size != nonAccessoryDevices.size) {
                AppLog.i("Single USB auto-connect: ${nonAccessoryDevices.size} USB device(s) present, ${candidates.size} allowed")
            }
            if (candidates.size == 1) {
                performSingleUsbConnect(candidates[0])
            }
        }
    }

    private fun performSingleUsbConnect(device: UsbDevice) {
        val usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
        if (usbManager.hasPermission(device)) {
            val deviceName = UsbDeviceCompat(device).uniqueName
            AppLog.i("Single USB auto-connect: connecting to $deviceName")
            isSwitchingToAccessory.set(true)
            val usbMode = UsbAccessoryMode(usbManager)
            serviceScope.launch(Dispatchers.IO) {
                try {
                    if (usbMode.connectAndSwitch(device)) {
                        AppLog.i("Successfully requested switch to accessory mode for single USB device. Waiting for re-enumeration...")
                    } else {
                        AppLog.w("Single USB auto-connect: connectAndSwitch failed for $deviceName")
                    }
                } finally {
                    isSwitchingToAccessory.set(false)
                }
            }
        } else {
            AppLog.i("Single USB auto-connect: device found but no permission, requesting...")
            requestUsbPermission(device)
        }
    }

    // -------------------------------------------------------------------------
    // Connection
    // -------------------------------------------------------------------------

    /**
     * Attempts a USB connection up to [maxRetries] times with a 1.5 s delay between attempts.
     *
     * USB accessories occasionally fail on the first attach (the device hasn't fully
     * enumerated yet), so retrying is necessary for reliability.
     */
    private suspend fun connectUsbWithRetry(device: UsbDevice, maxRetries: Int = 3) {
        var retryCount = 0
        var success = false
        while (retryCount <= maxRetries && !success) {
            if (retryCount > 0) {
                AppLog.i("Retrying USB connection (attempt ${retryCount + 1}/$maxRetries)...")
                delay(1500)
                // A USB reattach during the delay could have already started a new connection;
                // bail out to avoid two parallel retry loops competing on the same device.
                if (commManager.isConnected ||
                    commManager.connectionState.value is CommManager.ConnectionState.Connecting) return
            }
            commManager.connect(device)
            success = commManager.connectionState.value is CommManager.ConnectionState.Connected
            retryCount++
        }
    }

    // -------------------------------------------------------------------------
    // Wireless
    // -------------------------------------------------------------------------

    /**
     * Starts the [WirelessServer] (TCP on port 5288) and kicks off the initial NSD scan.
     * No-op if the server is already running.
     */
    private fun startWirelessServer() {
        if (wirelessServer != null) return
        val settings = App.provide(this).settings
        val mode = settings.wifiConnectionMode
        val strategy = settings.helperConnectionStrategy

        // Only register NSD for Headunit Server (Auto) or Helper (Common Wifi NSD)
        val shouldRegisterNsd = mode == 1 || (mode == 2 && strategy == 0)

        wirelessServer = WirelessServer().apply { start(registerNsd = shouldRegisterNsd) }
        if (shouldRegisterNsd) {
            startDiscovery()
        }
    }

    /**
     * Starts an NSD (mDNS) scan for Android Auto Wireless services on the local network.
     *
     * @param oneShot if `true`, does not reschedule after the scan finishes —
     *                used for the "auto WiFi" reconnect case.
     */
    private fun startDiscovery(oneShot: Boolean = false) {
        val settings = App.provide(this).settings
        val mode = settings.wifiConnectionMode
        val strategy = settings.helperConnectionStrategy

        if (mode == 3) return
        // Allow discovery for Strategy 0 (NSD), 3 (Phone Hotspot) and 4 (Headunit Hotspot)
        if (mode == 2 && strategy != 0 && strategy != 3 && strategy != 4) return
        if (commManager.isConnected || (wirelessServer == null && !oneShot)) return

        networkDiscovery?.stop()
        scanningState.value = true

        networkDiscovery = NetworkDiscovery(this, object : NetworkDiscovery.Listener {
            override fun onServiceFound(ip: String, port: Int, socket: java.net.Socket?) {
                if (commManager.isConnected) {
                    // Already connected by the time this callback fired; discard the socket
                    try { socket?.close() } catch (e: Exception) {}
                    return
                }
                when (port) {
                    5277 -> {
                        // Headunit Server detected — reuse the pre-opened socket when possible
                        AppLog.i("Auto-connecting to Headunit Server at $ip:$port (reusing socket)")
                        serviceScope.launch {
                            if (socket != null && socket.isConnected)
                                commManager.connect(socket)
                            else
                                commManager.connect(ip, 5277)
                        }
                    }
                    5289 -> {
                        // WiFi Launcher detected — no connection needed, just log
                        AppLog.i("Triggered Wifi Launcher at $ip:$port.")
                    }
                }
            }

            override fun onScanFinished() {
                scanningState.value = false
                if (oneShot) {
                    AppLog.i("One-shot scan finished.")
                    return
                }
                // Reschedule the next scan after 10 s to avoid hammering the network
                serviceScope.launch {
                    delay(10000)
                    if (wirelessServer != null && !commManager.isConnected) startDiscovery()
                }
            }
        })
        networkDiscovery?.startScan()
    }

    private fun stopWirelessServer() {
        activeWifiMode = -1
        activeHelperStrategy = -1
        networkDiscovery?.stop()
        networkDiscovery = null
        wirelessServer?.stopServer()
        wirelessServer = null
        scanningState.value = false
        VpnControl.stopVpn(this)
    }

    // -------------------------------------------------------------------------
    // Notification
    // -------------------------------------------------------------------------

    private fun createNotification(): Notification {
        val stopPendingIntent = PendingIntent.getService(
            this, 0,
            Intent(this, AapService::class.java).apply { action = ACTION_STOP_SERVICE },
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) PendingIntent.FLAG_IMMUTABLE
            else PendingIntent.FLAG_UPDATE_CURRENT
        )

        // Tap the notification to go back to the projection screen (if connected) or home
        val (notificationIntent, requestCode) = if (commManager.isConnected) {
            AapProjectionActivity.intent(this).apply {
                addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT or Intent.FLAG_ACTIVITY_SINGLE_TOP)
            } to 100
        } else {
            Intent(this, MainActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
            } to 101
        }

        val contentText = if (commManager.isConnected)
            getString(R.string.notification_projection_active)
        else
            getString(R.string.notification_service_running)

        return NotificationCompat.Builder(this, App.defaultChannel)
            .setSmallIcon(R.drawable.ic_stat_aa)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setOngoing(true)
            .setContentTitle("Headunit Revived")
            .setContentText(contentText)
            .setContentIntent(PendingIntent.getActivity(
                this, requestCode, notificationIntent,
                PendingIntent.FLAG_UPDATE_CURRENT or
                    (if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) PendingIntent.FLAG_IMMUTABLE else 0)
            ))
            .addAction(R.drawable.ic_exit_to_app_white_24dp, getString(R.string.exit), stopPendingIntent)
            .build()
    }

    private fun updateNotification() {
        val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        notificationManager.notify(1, createNotification())
    }

    /**
     * Launch MainActivity after boot using a cascading fallback chain designed
     * to work across stock AOSP head units, Xiaomi MIUI/HyperOS, Samsung One UI,
     * Huawei EMUI, OPPO ColorOS, and other OEM ROMs.
     *
     * Strategy order:
     * 1. Direct startActivity (Android < 10, or any device without background
     *    activity restrictions — works on most head units running AOSP)
     * 2. Overlay window trampoline (Android 10+): creates a zero-size invisible
     *    overlay giving the app a "visible" context. Bypasses MIUI, EMUI, ColorOS
     *    background start restrictions. Requires SYSTEM_ALERT_WINDOW.
     * 3. Full-screen intent notification (Android 10+): high-priority notification
     *    with fullScreenIntent. Works on stock Android 10-13 and Samsung. On
     *    Android 14+ needs USE_FULL_SCREEN_INTENT permission.
     * 4. Tap-to-open notification (last resort): user taps notification to open.
     */
    /**
     * Launches MainActivity when reopenOnReconnection is enabled and no activity is currently
     * visible. Uses the same overlay trampoline technique as boot auto-start to bypass OEM
     * background activity start restrictions.
     */
    private fun launchMainActivityIfNeeded(source: String) {
        val settings = App.provide(this).settings
        if (!settings.autoStartOnUsb || !settings.reopenOnReconnection) return

        AppLog.i("Reopen on reconnection: launching MainActivity ($source)")
        launchMainActivityOnBoot()
    }

    private fun launchMainActivityOnBoot() {
        // Android < 10: no background activity start restrictions
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
            AppLog.i("Boot auto-start: launching directly (API ${Build.VERSION.SDK_INT} < 29)")
            launchDirectly()
            return
        }

        // Android 10+: try overlay trampoline (bypasses all known OEM restrictions)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M &&
            AndroidSettings.canDrawOverlays(this)) {
            AppLog.i("Boot auto-start: launching via overlay window trampoline")
            if (launchViaOverlayTrampoline()) return
        }

        // Fallback: full-screen intent notification
        AppLog.i("Boot auto-start: falling back to full-screen intent notification")
        launchViaFullScreenIntent()
    }

    private fun launchDirectly() {
        try {
            val launchIntent = Intent(this, MainActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
                putExtra(MainActivity.EXTRA_LAUNCH_SOURCE, "Boot auto-start")
            }
            startActivity(launchIntent)
            AppLog.i("Boot auto-start: direct startActivity succeeded")
        } catch (e: Exception) {
            AppLog.e("Boot auto-start: direct startActivity failed: ${e.message}")
            launchViaFullScreenIntent()
        }
    }

    private fun launchViaOverlayTrampoline(): Boolean {
        val wm = getSystemService(WINDOW_SERVICE) as WindowManager
        val overlayType = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
        else
            @Suppress("DEPRECATION")
            WindowManager.LayoutParams.TYPE_SYSTEM_ALERT

        val params = WindowManager.LayoutParams(
            0, 0, overlayType,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            PixelFormat.TRANSLUCENT
        )
        val view = View(this)
        return try {
            wm.addView(view, params)
            val launchIntent = Intent(this, MainActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
                putExtra(MainActivity.EXTRA_LAUNCH_SOURCE, "Boot auto-start")
            }
            startActivity(launchIntent)
            AppLog.i("Boot auto-start: startActivity called from overlay context")
            true
        } catch (e: Exception) {
            AppLog.e("Boot auto-start: overlay trampoline failed: ${e.message}")
            false
        } finally {
            try { wm.removeView(view) } catch (_: Exception) {}
        }
    }

    private fun launchViaFullScreenIntent() {
        val launchIntent = Intent(this, MainActivity::class.java).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
            putExtra(MainActivity.EXTRA_LAUNCH_SOURCE, "Boot auto-start")
        }
        val piFlags = PendingIntent.FLAG_UPDATE_CURRENT or
            (if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) PendingIntent.FLAG_IMMUTABLE else 0)
        val fullScreenPi = PendingIntent.getActivity(this, 200, launchIntent, piFlags)

        val notification = NotificationCompat.Builder(this, App.bootStartChannel)
            .setSmallIcon(R.drawable.ic_stat_aa)
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setCategory(NotificationCompat.CATEGORY_ALARM)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(getString(R.string.notification_service_running))
            .setFullScreenIntent(fullScreenPi, true)
            .setContentIntent(fullScreenPi)
            .setAutoCancel(true)
            .build()

        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        nm.notify(BOOT_START_NOTIFICATION_ID, notification)

        // Dismiss the boot notification after a short delay
        serviceScope.launch {
            delay(5000)
            nm.cancel(BOOT_START_NOTIFICATION_ID)
        }
    }

    // -------------------------------------------------------------------------
    // Self Mode
    // -------------------------------------------------------------------------

    /**
     * "Self Mode" connects the device to itself over the loopback interface.
     *
     * Starts [WirelessServer] on port 5288, then launches the Google AA Wireless Setup
     * Activity pointing at `127.0.0.1:5288`. This causes the AA Wireless app to treat
     * the device as both the head unit and the phone, enabling a loopback session.
     *
     * [createFakeNetwork] and [createFakeWifiInfo] produce the Parcelable extras the
     * AA Wireless activity requires; they are constructed reflectively because the
     * relevant Android classes have no public constructors.
     */
    private fun startSelfMode() {
        selfMode = true
        startWirelessServer()

        serviceScope.launch(Dispatchers.Main) {
            val connectivityManager = getSystemService(CONNECTIVITY_SERVICE) as ConnectivityManager
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && connectivityManager.activeNetwork == null) {
                // Wait up to 1 second for the Dummy VPN to become the active network
                for (i in 1..10) {
                    if (connectivityManager.activeNetwork != null) break
                    delay(100)
                }
            }

            val activeNetwork = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M)
                connectivityManager.activeNetwork else null
            val networkToUse = activeNetwork ?: createFakeNetwork(0)
            val fakeWifiInfo = createFakeWifiInfo()

            val magicalIntent = Intent().apply {
                setClassName(
                    "com.google.android.projection.gearhead",
                    "com.google.android.apps.auto.wireless.setup.service.impl.WirelessStartupActivity"
                )
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                putExtra("PARAM_HOST_ADDRESS", "127.0.0.1")
                putExtra("PARAM_SERVICE_PORT", 5288)
                networkToUse?.let { putExtra("PARAM_SERVICE_WIFI_NETWORK", it) }
                fakeWifiInfo?.let { putExtra("wifi_info", it) }
            }

            try {
                AppLog.i("Launching AA Wireless Startup via Activity...")
                startActivity(magicalIntent)
            } catch (e: Exception) {
                AppLog.w("Activity launch failed (${e.message}). Attempting Broadcast fallback...")
                try {

                    AppLog.w("WirelessStartupActivity not found (AA 16.4+ detected).")
                    if (Build.VERSION.SDK_INT <= 29) {
                        // On Android 10, if Activity is gone, Broadcast will definitely be blocked by Gearhead's version check.
                        AppLog.e("Self-mode blocked by Google on Android 10 (AA 16.4+). Skipping broadcast fallback.")
                        Toast.makeText(this@AapService, getString(R.string.failed_self_mode_android10), Toast.LENGTH_LONG).show()
                    } else {
                        val receiverIntent = Intent().apply {
                            setClassName(
                                "com.google.android.projection.gearhead",
                                "com.google.android.apps.auto.wireless.setup.receiver.WirelessStartupReceiver"
                            )
                            action = "com.google.android.apps.auto.wireless.setup.receiver.wirelessstartup.START"
                            putExtra("ip_address", "127.0.0.1")
                            putExtra("projection_port", 5288)
                            networkToUse?.let { putExtra("PARAM_SERVICE_WIFI_NETWORK", it) }
                            fakeWifiInfo?.let { putExtra("wifi_info", it) }
                            addFlags(Intent.FLAG_RECEIVER_FOREGROUND)
                        }
                        sendBroadcast(receiverIntent)
                        AppLog.i("Broadcast fallback sent successfully.")
                    }
                } catch (e2: Exception) {
                    AppLog.e("Both Activity and Broadcast triggers failed", e2)
                    Toast.makeText(this@AapService, getString(R.string.failed_start_android_auto), Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    /** Reflectively constructs an `android.net.Network` from a raw network ID integer. */
    private fun createFakeNetwork(netId: Int): Parcelable? {
        val parcel = Parcel.obtain()
        return try {
            parcel.writeInt(netId)
            parcel.setDataPosition(0)
            val creator = Class.forName("android.net.Network").getField("CREATOR").get(null) as Parcelable.Creator<*>
            creator.createFromParcel(parcel) as Parcelable
        } catch (e: Exception) { null } finally { parcel.recycle() }
    }

    /** Reflectively constructs a `WifiInfo` with a fake SSID for the Self Mode intent. */
    private fun createFakeWifiInfo(): Parcelable? {
        return try {
            val wifiInfoClass = Class.forName("android.net.wifi.WifiInfo")
            val wifiInfo = wifiInfoClass.getDeclaredConstructor()
                .apply { isAccessible = true }
                .newInstance() as Parcelable
            try {
                wifiInfoClass.getDeclaredField("mSSID")
                    .apply { isAccessible = true }
                    .set(wifiInfo, "\"Headunit-Fake-Wifi\"")
            } catch (e: Exception) {}
            wifiInfo
        } catch (e: Exception) { null }
    }



    // -------------------------------------------------------------------------
    // WirelessServer
    // -------------------------------------------------------------------------

    /**
     * Coroutine-based server that listens for incoming TCP connections on port 5288.
     *
     * Registers the service over mDNS (NSD) as `_aawireless._tcp` so Android Auto
     * Wireless clients can discover it automatically. Each accepted socket is handed
     * off to [CommManager.connect] on the service coroutine scope. Only one connection
     * is allowed at a time; subsequent sockets are closed immediately.
     *
     * Uses [isActive] for cooperative cancellation. [stopServer] cancels the job and
     * closes the server socket to unblock the blocking [ServerSocket.accept] call.
     */
    private inner class WirelessServer {
        private var serverSocket: ServerSocket? = null
        private var nsdManager: NsdManager? = null
        private var registrationListener: NsdManager.RegistrationListener? = null
        private var job: Job? = null

        fun start(registerNsd: Boolean = true) {
            nsdManager = getSystemService(Context.NSD_SERVICE) as? NsdManager
            if (nsdManager == null) {
                AppLog.e("WirelessServer: NsdManager not available on this device.")
            } else if (registerNsd) {
                registerNsd()
            }

            job = serviceScope.launch(Dispatchers.IO) {
                try {
                    serverSocket = ServerSocket(5288).apply { reuseAddress = true }
                    AppLog.i("Wireless Server listening on port 5288")
                    logLocalNetworkInterfaces()

                    while (isActive) {
                        AppLog.d("WirelessServer: Waiting for TCP connection on port 5288...")
                        val clientSocket = serverSocket?.accept() ?: break
                        AppLog.i("WirelessServer: Incoming connection detected from ${clientSocket.inetAddress}")
                        serviceScope.launch {
                            if (commManager.isConnected) {
                                AppLog.w("WirelessServer: Already connected, dropping client from ${clientSocket.inetAddress}")
                                withContext(Dispatchers.IO) {
                                    try { clientSocket.close() } catch (e: Exception) {}
                                }
                            } else if (android.os.SystemClock.elapsedRealtime() < userExitCooldownUntil) {
                                // [FIX] User just exited AA — reject the instant reconnection.
                                AppLog.w("WirelessServer: Rejecting connection from ${clientSocket.inetAddress} — user exit cooldown active (${userExitCooldownUntil - System.currentTimeMillis()}ms remaining)")
                                withContext(Dispatchers.IO) {
                                    try { clientSocket.close() } catch (e: Exception) {}
                                }
                            } else {
                                AppLog.i("WirelessServer: Accepted client connection from ${clientSocket.inetAddress}. Passing to CommManager...")
                                userExitedAA = false // Clear flag on genuine new connection
                                commManager.connect(clientSocket)
                            }
                        }
                    }
                } catch (e: Exception) {
                    if (isActive) AppLog.e("Wireless server error", e)
                } finally {
                    unregisterNsd()
                    try { serverSocket?.close() } catch (e: Exception) {}
                }
            }
        }

        /** Logs all non-loopback IPv4 addresses; useful for debugging connectivity issues. */
        private fun logLocalNetworkInterfaces() {
            try {
                val interfaces = java.net.NetworkInterface.getNetworkInterfaces()
                while (interfaces.hasMoreElements()) {
                    val iface = interfaces.nextElement()
                    val addresses = iface.inetAddresses
                    while (addresses.hasMoreElements()) {
                        val addr = addresses.nextElement()
                        if (!addr.isLoopbackAddress && addr is java.net.Inet4Address) {
                            AppLog.i("Interface: ${iface.name}, IP: ${addr.hostAddress}")
                        }
                    }
                }
            } catch (e: Exception) {
                AppLog.e("Error logging interfaces", e)
            }
        }

        private fun registerNsd() {
            val serviceInfo = NsdServiceInfo().apply {
                serviceName = "AAWireless"
                serviceType = "_aawireless._tcp"
                port = 5288
            }
            registrationListener = object : NsdManager.RegistrationListener {
                override fun onServiceRegistered(info: NsdServiceInfo) = AppLog.i("NSD Registered: ${info.serviceName}")
                override fun onRegistrationFailed(info: NsdServiceInfo, err: Int) = AppLog.e("NSD Reg Fail: $err")
                override fun onServiceUnregistered(info: NsdServiceInfo) = AppLog.i("NSD Unregistered")
                override fun onUnregistrationFailed(info: NsdServiceInfo, err: Int) = AppLog.e("NSD Unreg Fail: $err")
            }
            nsdManager?.registerService(serviceInfo, NsdManager.PROTOCOL_DNS_SD, registrationListener)
        }

        private fun unregisterNsd() {
            registrationListener?.let { nsdManager?.unregisterService(it) }
            registrationListener = null
        }

        fun stopServer() {
            job?.cancel()
            job = null
            // Close the socket to unblock the accept() call in the coroutine.
            try { serverSocket?.close() } catch (e: Exception) {}
        }
    }

    // -------------------------------------------------------------------------
    // Companion
    // -------------------------------------------------------------------------

    companion object {
        /**
         * If set to `true`, the service will call [System.exit] at the very end of [onDestroy].
         * This is used by `killOnDisconnect` to ensure all cleanup (like Car Mode) completes
         * before the process dies.
         */
        var killProcessOnDestroy: Boolean = false

        /** `true` while a Self Mode session is active. */
        var selfMode = false

        val wifiDirectName = MutableStateFlow<String?>(null)

        /**
         * Emits `true` while a WiFi NSD scan is in progress.
         * Observed by `HomeFragment` via a lifecycle-aware flow collector.
         */
        val scanningState = MutableStateFlow(false)

        private const val BOOT_START_NOTIFICATION_ID = 42

        // Service action strings used with startService() and sendBroadcast()
        const val ACTION_START_SELF_MODE           = "com.andrerinas.headunitrevived.ACTION_START_SELF_MODE"
        const val ACTION_START_WIRELESS            = "com.andrerinas.headunitrevived.ACTION_START_WIRELESS"
        const val ACTION_START_WIRELESS_SCAN       = "com.andrerinas.headunitrevived.ACTION_START_WIRELESS_SCAN"
        const val ACTION_STOP_WIRELESS             = "com.andrerinas.headunitrevived.ACTION_STOP_WIRELESS"
        const val ACTION_NATIVE_AA_POKE            = "com.andrerinas.headunitrevived.ACTION_NATIVE_AA_POKE"
        const val ACTION_NEARBY_CONNECT             = "com.andrerinas.headunitrevived.ACTION_NEARBY_CONNECT"
        const val ACTION_CHECK_USB                 = "com.andrerinas.headunitrevived.ACTION_CHECK_USB"
        const val ACTION_STOP_SERVICE              = "com.andrerinas.headunitrevived.ACTION_STOP_SERVICE"
        const val ACTION_DISCONNECT                = "com.andrerinas.headunitrevived.ACTION_DISCONNECT"
        const val ACTION_REQUEST_NIGHT_MODE_UPDATE = "com.andrerinas.headunitrevived.ACTION_REQUEST_NIGHT_MODE_UPDATE"
        const val ACTION_NIGHT_MODE_CHANGED      = "com.andrerinas.headunitrevived.ACTION_NIGHT_MODE_CHANGED"
        /**
         * Sent after the caller has already invoked [CommManager.connect(socket)].
         * The [observeConnectionState] flow observer handles the result — [onStartCommand]
         * does nothing for this action.
         */
        const val ACTION_CONNECT_SOCKET            = "com.andrerinas.headunitrevived.ACTION_CONNECT_SOCKET"

        /** Max handshake failures on a stale accessory device before forcing AOA re-enumeration. */
        private const val MAX_STALE_ACCESSORY_RETRIES = 1

        /** Delay before retrying USB connection after an unexpected disconnect. */
        private const val USB_RECONNECT_DELAY_MS = 3000L

        /** Cooldown period after user-initiated exit. During this window, the WirelessServer
         *  rejects incoming connections to prevent the phone from instantly reconnecting. */
        private const val USER_EXIT_COOLDOWN_MS = 5000L

        /** Delay before AapService tries to handle a normal-mode USB attach as a fallback
         *  when UsbAttachedActivity doesn't fire (common on Chinese MediaTek headunits). */
        private const val USB_ATTACH_FALLBACK_DELAY_MS = 2000L

        /** Screen-off duration (ms) above which SCREEN_ON is treated as a hibernate wake.
         *  60 seconds filters out normal screen timeouts while catching any hibernate/quick boot. */
        private const val HIBERNATE_WAKE_THRESHOLD_MS = 60_000L

        const val EXTRA_MAC = "extra_mac"
        const val EXTRA_ENDPOINT_ID = "extra_endpoint_id"
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapNavigation.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.os.Build
import androidx.core.app.NotificationCompat
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.proto.NavigationStatus
import com.andrerinas.headunitrevived.contract.NavigationUpdateIntent
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.Settings

/**
 * Handles navigation messages from the ID_NAV channel from any Android Auto-enabled app
 * (Google Maps, Yandex Maps, etc.). Shows notifications with turn-by-turn directions and current street.
 */
class AapNavigation(
    private val context: Context,
    private val settings: Settings
) {
    private var lastTurnDetail: NavigationStatus.NextTurnDetail? = null
    private var currentStreet: String = ""

    fun process(message: AapMessage): Boolean {
        if (message.channel != Channel.ID_NAV) return false

        return when (message.type) {
            NavigationStatus.MsgType.NEXTTURNDETAILS_VALUE -> {
                try {
                    val detail = message.parse(NavigationStatus.NextTurnDetail.newBuilder()).build()
                    lastTurnDetail = detail
                    currentStreet = detail.road.takeIf { it.isNotBlank() } ?: ""
                    AppLog.d("Nav: NextTurnDetail road=${detail.road} nextturn=${detail.nextturn}")
                    sendNavigationBroadcast(distanceMeters = null, timeSeconds = null, detail = detail)
                    if (settings.showNavigationNotifications) {
                        val actionText = nextEventToAction(detail.nextturn)
                        val street = currentStreet.ifBlank { detail.road.takeIf { r -> r.isNotBlank() } ?: "" }.ifBlank { "—" }
                        showNotification(distanceMeters = null, action = actionText, street = street)
                    }
                    true
                } catch (e: Exception) {
                    AppLog.e("Nav: failed to parse NextTurnDetail", e)
                    true
                }
            }
            NavigationStatus.MsgType.NEXTTURNDISTANCEANDTIME_VALUE -> {
                try {
                    val event = message.parse(NavigationStatus.NextTurnDistanceEvent.newBuilder()).build()
                    val distanceMeters = if (event.hasDistance()) event.distance else null
                    val timeSeconds = if (event.hasTime()) event.time else null
                    val detail = lastTurnDetail
                    val actionText = detail?.let { nextEventToAction(it.nextturn) } ?: context.getString(R.string.nav_action_unknown)
                    val street = currentStreet.ifBlank { detail?.road?.takeIf { r -> r.isNotBlank() } ?: "" }.ifBlank { "—" }
                    sendNavigationBroadcast(distanceMeters = distanceMeters, timeSeconds = timeSeconds, detail = detail)
                    if (settings.showNavigationNotifications) {
                        showNotification(
                            distanceMeters = distanceMeters,
                            action = actionText,
                            street = street
                        )
                    }
                    true
                } catch (e: Exception) {
                    AppLog.e("Nav: failed to parse NextTurnDistanceEvent", e)
                    true
                }
            }
            else -> {
                AppLog.d("Nav: passthrough type ${message.type}")
                false
            }
        }
    }

    private fun sendNavigationBroadcast(
        distanceMeters: Int?,
        timeSeconds: Int?,
        detail: NavigationStatus.NextTurnDetail?
    ) {
        val road = (detail?.road?.takeIf { it.isNotBlank() } ?: currentStreet).ifBlank { "—" }
        val nextEventType = detail?.nextturn?.number ?: 0
        val turnSide = detail?.side?.number
        val turnNumber = detail?.takeIf { it.hasTrunnumer() }?.trunnumer
        val turnAngle = detail?.takeIf { it.hasTurnangel() }?.turnangel
        val actionText = detail?.let { nextEventToAction(it.nextturn) } ?: context.getString(R.string.nav_action_unknown)
        val intent = NavigationUpdateIntent(
            distanceMeters = distanceMeters,
            timeSeconds = timeSeconds,
            road = road,
            nextEventType = nextEventType,
            actionText = actionText,
            turnSide = turnSide,
            turnNumber = turnNumber,
            turnAngle = turnAngle
        )
        context.applicationContext.sendBroadcast(intent)
    }

    private fun showNotification(distanceMeters: Int?, action: String, street: String) {
        val appContext = context.applicationContext
        val title = if (distanceMeters != null && distanceMeters >= 0) {
            context.getString(R.string.nav_notification_title_format, distanceMeters, action)
        } else {
            action
        }
        val pendingIntentFlags = PendingIntent.FLAG_UPDATE_CURRENT or
            (if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) PendingIntent.FLAG_IMMUTABLE else 0)
        val notification = NotificationCompat.Builder(appContext, NAV_CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_stat_aa)
            .setContentTitle(title)
            .setContentText(context.getString(R.string.nav_notification_street_format, street))
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setAutoCancel(true)
            .setContentIntent(
                PendingIntent.getActivity(
                    appContext,
                    0,
                    AapProjectionActivity.intent(appContext),
                    pendingIntentFlags
                )
            )
            .build()
        (appContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
            .notify(NAV_NOTIFICATION_ID, notification)
    }

    private fun nextEventToAction(nextEvent: NavigationStatus.NextTurnDetail.NextEvent): String {
        return when (nextEvent) {
            NavigationStatus.NextTurnDetail.NextEvent.UNKNOWN -> context.getString(R.string.nav_action_unknown)
            NavigationStatus.NextTurnDetail.NextEvent.DEPARTE -> context.getString(R.string.nav_action_depart)
            NavigationStatus.NextTurnDetail.NextEvent.NAME_CHANGE -> context.getString(R.string.nav_action_name_change)
            NavigationStatus.NextTurnDetail.NextEvent.SLIGHT_TURN -> context.getString(R.string.nav_action_slight_turn)
            NavigationStatus.NextTurnDetail.NextEvent.TURN -> context.getString(R.string.nav_action_turn)
            NavigationStatus.NextTurnDetail.NextEvent.SHARP_TURN -> context.getString(R.string.nav_action_sharp_turn)
            NavigationStatus.NextTurnDetail.NextEvent.UTURN -> context.getString(R.string.nav_action_uturn)
            NavigationStatus.NextTurnDetail.NextEvent.ONRAMPE -> context.getString(R.string.nav_action_on_ramp)
            NavigationStatus.NextTurnDetail.NextEvent.OFFRAMP -> context.getString(R.string.nav_action_off_ramp)
            NavigationStatus.NextTurnDetail.NextEvent.FORME -> context.getString(R.string.nav_action_merge)
            NavigationStatus.NextTurnDetail.NextEvent.MERGE -> context.getString(R.string.nav_action_merge)
            NavigationStatus.NextTurnDetail.NextEvent.ROUNDABOUT_ENTER -> context.getString(R.string.nav_action_roundabout_enter)
            NavigationStatus.NextTurnDetail.NextEvent.ROUNDABOUT_EXIT -> context.getString(R.string.nav_action_roundabout_exit)
            NavigationStatus.NextTurnDetail.NextEvent.ROUNDABOUT_ENTER_AND_EXIT -> context.getString(R.string.nav_action_roundabout)
            NavigationStatus.NextTurnDetail.NextEvent.STRAIGHTE -> context.getString(R.string.nav_action_straight)
            NavigationStatus.NextTurnDetail.NextEvent.FERRY_BOAT -> context.getString(R.string.nav_action_ferry)
            NavigationStatus.NextTurnDetail.NextEvent.FERRY_TRAINE -> context.getString(R.string.nav_action_ferry_train)
            NavigationStatus.NextTurnDetail.NextEvent.DESTINATION -> context.getString(R.string.nav_action_destination)
        }
    }

    companion object {
        const val NAV_CHANNEL_ID = "headunit_navigation"
        private const val NAV_NOTIFICATION_ID = 2

        fun createNotificationChannel(context: Context) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                val channel = NotificationChannel(
                    NAV_CHANNEL_ID,
                    context.getString(R.string.nav_notification_channel_name),
                    NotificationManager.IMPORTANCE_LOW
                ).apply {
                    description = context.getString(R.string.nav_notification_channel_description)
                    setShowBadge(false)
                }
                (context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager)
                    .createNotificationChannel(channel)
            }
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapControl.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import android.content.Context
import android.content.Intent
import android.media.AudioManager
import com.andrerinas.headunitrevived.aap.protocol.AudioConfigs
import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.messages.DrivingStatusEvent
import com.andrerinas.headunitrevived.aap.protocol.messages.ServiceDiscoveryResponse
import com.andrerinas.headunitrevived.aap.protocol.messages.VideoFocusEvent
import com.andrerinas.headunitrevived.aap.protocol.proto.Common
import com.andrerinas.headunitrevived.aap.protocol.proto.Control
import com.andrerinas.headunitrevived.aap.protocol.proto.Input
import com.andrerinas.headunitrevived.aap.protocol.proto.Media
import com.andrerinas.headunitrevived.aap.protocol.proto.Sensors
import com.andrerinas.headunitrevived.decoder.MicRecorder
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.Settings

interface AapControl {
    fun execute(message: AapMessage): Int
}

internal class AapControlMedia(
    private val aapTransport: AapTransport,
    private val micRecorder: MicRecorder,
    private val aapAudio: AapAudio): AapControl {

    private var lastNativeFocusRequestTime = 0L
    private var nativeFocusRequestCount = 0

    override fun execute(message: AapMessage): Int {

        when (message.type) {
            Media.MsgType.MEDIA_MESSAGE_SETUP_VALUE -> {
                val setupRequest = message.parse(Media.MediaSetupRequest.newBuilder()).build()
                return mediaSinkSetupRequest(setupRequest, message.channel)
            }
            Media.MsgType.MEDIA_MESSAGE_START_VALUE -> {
                val startRequest = message.parse(Media.Start.newBuilder()).build()
                return mediaStartRequest(startRequest, message.channel)
            }
            Media.MsgType.MEDIA_MESSAGE_STOP_VALUE -> return mediaSinkStopRequest(message.channel)
            Media.MsgType.MEDIA_MESSAGE_VIDEO_FOCUS_REQUEST_VALUE -> {
                val focusRequest = message.parse(Media.VideoFocusRequestNotification.newBuilder()).build()
                AppLog.i("RX: Video Focus Request - mode: %s, reason: %s", focusRequest.mode, focusRequest.reason)

                if (focusRequest.mode == Media.VideoFocusMode.VIDEO_FOCUS_NATIVE) {
                    AppLog.i("Video Focus NATIVE received. User likely clicked Exit. Stopping transport.")
                    aapTransport.wasUserExit = true
                    aapTransport.stop()
                }
                return 0
            }
            Media.MsgType.MEDIA_MESSAGE_MICROPHONE_REQUEST_VALUE -> {
                val micRequest = message.parse(Media.MicrophoneRequest.newBuilder()).build()
                return micRequest(micRequest)
            }
            Media.MsgType.MEDIA_MESSAGE_UPDATE_UI_CONFIG_REPLY_VALUE -> {
                AppLog.i("RX: Update UI Config Reply received. Acknowledging UI Config change.")
                aapTransport.onUpdateUiConfigReplyReceived?.invoke()
                return 0
            }
            Media.MsgType.MEDIA_MESSAGE_ACK_VALUE -> return 0
            else -> AppLog.e("Unsupported Media message type: ${message.type}")
        }
        return 0
    }

    private fun mediaStartRequest(request: Media.Start, channel: Int): Int {
        AppLog.i("Media Start Request %s: session=%d, config_index=%d", Channel.name(channel), request.sessionId, request.configurationIndex)

        aapTransport.setSessionId(channel, request.sessionId)
        return 0
    }

    private fun mediaSinkSetupRequest(request: Media.MediaSetupRequest, channel: Int): Int {

        AppLog.i("Media Sink Setup Request: %d on channel %s", request.type, Channel.name(channel))

        val configResponse = Media.Config.newBuilder().apply {
            status = Media.Config.ConfigStatus.HEADUNIT
            // Use 30 for maxUnacked on wireless to avoid stalls due to jitter, 16 for USB.
            maxUnacked = if (aapTransport.isWireless) 30 else 16

            addConfigurationIndices(0)
        }.build()
        AppLog.i("Config response: %s", configResponse)
        val msg = AapMessage(channel, Media.MsgType.MEDIA_MESSAGE_CONFIG_VALUE, configResponse)
        aapTransport.send(msg)

        if (channel == Channel.ID_VID) {
            aapTransport.gainVideoFocus()
        }

        // Pushing AudioFocusNotification
        if (Channel.isAudio(channel)) {
            val focusNotification = Control.AudioFocusNotification.newBuilder()
                .setFocusState(Control.AudioFocusNotification.AudioFocusStateType.STATE_GAIN)
                .setUnsolicited(true)
                .build()
            aapTransport.send(AapMessage(Channel.ID_CTR, Control.ControlMsgType.MESSAGE_AUDIO_FOCUS_NOTIFICATION_VALUE, focusNotification))
        }

        return 0
    }

    private fun mediaSinkStopRequest(channel: Int): Int {
        AppLog.i("Media Sink Stop Request: " + Channel.name(channel))
        if (Channel.isAudio(channel)) {
            aapAudio.stopAudio(channel)
        } else if (channel == Channel.ID_VID) {
            if (aapTransport.ignoreNextStopRequest) {
                AppLog.i("Video Sink Stopped -> Ignored (Forced Keyframe Request)")
                aapTransport.ignoreNextStopRequest = false
                return 0
            }
            AppLog.i("Video Sink Stopped -> Normal background/transition behavior")
        }
        return 0
    }

    private fun micRequest(micRequest: Media.MicrophoneRequest): Int {
        AppLog.d("Mic request: %s", micRequest)

        if (micRequest.open) {
            micRecorder.start()
        } else {
            micRecorder.stop()
        }
        return 0
    }

    companion object {
        /** Time window for counting consecutive VIDEO_FOCUS_NATIVE requests. */
        private const val NATIVE_FOCUS_DEBOUNCE_MS = 5000L
        /** Number of NATIVE focus requests within the debounce window before stopping. */
        private const val MAX_NATIVE_FOCUS_RETRIES = 3
    }
}

internal class AapControlTouch(private val aapTransport: AapTransport): AapControl {

    override fun execute(message: AapMessage): Int {

        when (message.type) {
            Input.MsgType.BINDINGREQUEST_VALUE -> {
                val request = message.parse(Input.KeyBindingRequest.newBuilder()).build()
                return inputBinding(request, message.channel)
            }
            else -> AppLog.e("Unsupported Input message type: ${message.type}")
        }
        return 0
    }

    private fun inputBinding(request: Input.KeyBindingRequest, channel: Int): Int {
        aapTransport.send(AapMessage(channel, Input.MsgType.BINDINGRESPONSE_VALUE, Input.BindingResponse.newBuilder()
                .setStatus(Common.MessageStatus.STATUS_SUCCESS)
                .build()))
        return 0
    }

}

internal class AapControlSensor(private val aapTransport: AapTransport, private val context: Context): AapControl {

    override fun execute(message: AapMessage): Int {
        when (message.type) {
            Sensors.SensorsMsgType.SENSOR_STARTREQUEST_VALUE -> {
                val request = message.parse(Sensors.SensorRequest.newBuilder()).build()
                return sensorStartRequest(request, message.channel)
            }
            else -> AppLog.e("Unsupported Sensor message type: ${message.type}")
        }
        return 0
    }

    private fun sensorStartRequest(request: Sensors.SensorRequest, channel: Int): Int {
        AppLog.i("Sensor Start Request sensor: %s, minUpdatePeriod: %d", request.type.name, request.minUpdatePeriod)

        val msg = AapMessage(channel, Sensors.SensorsMsgType.SENSOR_STARTRESPONSE_VALUE, Sensors.SensorResponse.newBuilder()
                .setStatus(Common.MessageStatus.STATUS_SUCCESS)
                .build())
        AppLog.i(msg.toString())

        aapTransport.send(msg)
        aapTransport.startSensor(request.type.number)

        if (request.type == Sensors.SensorType.NIGHT) {
            AppLog.i("Night sensor requested. Triggering immediate update.")
            val intent = Intent(AapService.ACTION_REQUEST_NIGHT_MODE_UPDATE)
            intent.setPackage(context.packageName)
            context.sendBroadcast(intent)
        }
        return 0
    }
}

internal class AapControlService(
        private val aapTransport: AapTransport,
        private val aapAudio: AapAudio,
        private val settings: Settings,
        private val context: Context): AapControl {

    override fun execute(message: AapMessage): Int {

        when (message.type) {
            Control.ControlMsgType.MESSAGE_SERVICE_DISCOVERY_REQUEST_VALUE -> {
                val request = message.parse(Control.ServiceDiscoveryRequest.newBuilder()).build()
                return serviceDiscoveryRequest(request)
            }
            Control.ControlMsgType.MESSAGE_PING_REQUEST_VALUE -> {
                val pingRequest = message.parse(Control.PingRequest.newBuilder()).build()
                return pingRequest(pingRequest, message.channel)
            }
            Control.ControlMsgType.MESSAGE_NAV_FOCUS_REQUEST_VALUE -> {
                val navigationFocusRequest = message.parse(Control.NavFocusRequestNotification.newBuilder()).build()
                return navigationFocusRequest(navigationFocusRequest, message.channel)
            }
            Control.ControlMsgType.MESSAGE_BYEBYE_REQUEST_VALUE -> {
                val shutdownRequest = message.parse(Control.ByeByeRequest.newBuilder()).build()
                return byebyeRequest(shutdownRequest, message.channel)
            }
            Control.ControlMsgType.MESSAGE_BYEBYE_RESPONSE_VALUE -> {
                AppLog.i("Byebye Response received")
                return -1
            }
            Control.ControlMsgType.MESSAGE_VOICE_SESSION_NOTIFICATION_VALUE -> {
                val voiceRequest = message.parse(Control.VoiceSessionNotification.newBuilder()).build()
                return voiceSessionNotification(voiceRequest)
            }
            Control.ControlMsgType.MESSAGE_AUDIO_FOCUS_REQUEST_VALUE -> {
                val audioFocusRequest = message.parse(Control.AudioFocusRequestNotification.newBuilder()).build()
                return audioFocusRequest(audioFocusRequest, message.channel)
            }
            Control.ControlMsgType.MESSAGE_CHANNEL_CLOSE_NOTIFICATION_VALUE -> {
                AppLog.i("RX: Channel Close Notification on chan ${message.channel}")
                return 0
            }
            else -> AppLog.e("Unsupported Control message type: ${message.type}")
        }
        return 0
    }


    private fun serviceDiscoveryRequest(request: Control.ServiceDiscoveryRequest): Int {
        AppLog.i("Service Discovery Request: %s", request.phoneName)

        val msg = ServiceDiscoveryResponse(context)
        aapTransport.send(msg)
        return 0
    }

    private fun pingRequest(request: Control.PingRequest, channel: Int): Int {
        val response = Control.PingResponse.newBuilder()
                .setTimestamp(System.nanoTime())
                .build()

        val msg = AapMessage(channel, Control.ControlMsgType.MESSAGE_PING_RESPONSE_VALUE, response)
        aapTransport.send(msg)
        return 0
    }

    private fun navigationFocusRequest(request: Control.NavFocusRequestNotification, channel: Int): Int {
        AppLog.i("Navigation Focus Request: %s", request.focusType)

        val response = Control.NavFocusNotification.newBuilder()
                .setFocusType(Control.NavFocusType.NAV_FOCUS_2)
                .build()

        val msg = AapMessage(channel, Control.ControlMsgType.MESSAGE_NAV_FOCUS_NOTIFICATION_VALUE, response)
        AppLog.i(msg.toString())

        aapTransport.send(msg)
        return 0
    }

    private fun byebyeRequest(request: Control.ByeByeRequest, channel: Int): Int {
        AppLog.i("!!! RECEIVED BYEBYE REQUEST FROM PHONE !!! Reason: ${request.reason}")

        val msg = AapMessage(channel, Control.ControlMsgType.MESSAGE_BYEBYE_RESPONSE_VALUE, Control.ByeByeResponse.newBuilder().build())
        AppLog.i("Sending BYEYERESPONSE")
        aapTransport.send(msg)
        Utils.ms_sleep(500)
        AppLog.i("Calling aapTransport.quit(clean=true)")
        aapTransport.quit(clean = true)
        return -1
    }

    private fun voiceSessionNotification(request: Control.VoiceSessionNotification): Int {
        if (request.status == Control.VoiceSessionNotification.VoiceSessionStatus.VOICE_STATUS_START)
            AppLog.i("Voice Session Notification: START")
        else if (request.status == Control.VoiceSessionNotification.VoiceSessionStatus.VOICE_STATUS_STOP)
            AppLog.i("Voice Session Notification: STOP")
        return 0
    }

    private fun audioFocusRequest(notification: Control.AudioFocusRequestNotification, channel: Int): Int {
        AppLog.i("Audio Focus Request: ${notification.request}")

        // Always respond with the mapped focus state to AA — never deny.
        // the phone must always believe the headunit
        // has audio focus, otherwise it keeps audio output on the phone itself.
        val mappedState = focusResponse[notification.request]
        if (mappedState != null) {
            val response = Control.AudioFocusNotification.newBuilder()
                .setFocusState(mappedState)
                .build()
            AppLog.i("Sending immediate AudioFocusNotification: $mappedState (always-grant)")
            aapTransport.send(AapMessage(channel, Control.ControlMsgType.MESSAGE_AUDIO_FOCUS_NOTIFICATION_VALUE, response))

            // Sync MediaSession
            val isGain = mappedState == Control.AudioFocusNotification.AudioFocusStateType.STATE_GAIN
            aapTransport.onAudioFocusStateChanged?.invoke(isGain)
        }

        // Best-effort: request system audio focus to duck other apps on the headunit.
        // The result is intentionally ignored for the protocol response above.
        aapAudio.requestFocusChange(AudioConfigs.stream(channel), notification.request.number, AudioManager.OnAudioFocusChangeListener {
            AppLog.i("System audio focus changed: $it ${systemFocusName[it]}")
        })

        return 0
    }

    companion object {
        private val systemFocusName = mapOf(
                AudioManager.AUDIOFOCUS_GAIN to "AUDIOFOCUS_GAIN",
                AudioManager.AUDIOFOCUS_GAIN_TRANSIENT to "AUDIOFOCUS_GAIN_TRANSIENT",
                AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE to "AUDIOFOCUS_GAIN_TRANSIENT_EXCLUSIVE",
                AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK to "AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK",
                AudioManager.AUDIOFOCUS_LOSS to "AUDIOFOCUS_LOSS",
                AudioManager.AUDIOFOCUS_LOSS_TRANSIENT to "AUDIOFOCUS_LOSS_TRANSIENT",
                AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK to "AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK",
                AudioManager.AUDIOFOCUS_NONE to "AUDIOFOCUS_NONE"
        )

        private val focusResponse = mapOf(
            Control.AudioFocusRequestNotification.AudioFocusRequestType.RELEASE to Control.AudioFocusNotification.AudioFocusStateType.STATE_LOSS,
            Control.AudioFocusRequestNotification.AudioFocusRequestType.GAIN to Control.AudioFocusNotification.AudioFocusStateType.STATE_GAIN,
            Control.AudioFocusRequestNotification.AudioFocusRequestType.GAIN_TRANSIENT to Control.AudioFocusNotification.AudioFocusStateType.STATE_GAIN_TRANSIENT,
            Control.AudioFocusRequestNotification.AudioFocusRequestType.GAIN_TRANSIENT_MAY_DUCK to Control.AudioFocusNotification.AudioFocusStateType.STATE_GAIN_TRANSIENT_GUIDANCE_ONLY
        )
    }
}

internal class AapControlGateway(
        private val aapTransport: AapTransport,
        private val serviceControl: AapControl,
        private val mediaControl: AapControl,
        private val touchControl: AapControl,
        private val sensorControl: AapControl): AapControl {

    constructor(aapTransport: AapTransport,
                micRecorder: MicRecorder,
                aapAudio: AapAudio,
                settings: Settings,
                context: Context) : this(
            aapTransport,
            AapControlService(aapTransport, aapAudio, settings, context),
            AapControlMedia(aapTransport, micRecorder, aapAudio),
            AapControlTouch(aapTransport),
            AapControlSensor(aapTransport, context))

    override fun execute(message: AapMessage): Int {
        if (message.type == 7) {
            val request = message.parse(Control.ChannelOpenRequest.newBuilder()).build()
            return channelOpenRequest(request, message.channel)
        }

        when (message.channel) {
            Channel.ID_CTR -> return serviceControl.execute(message)
            Channel.ID_INP -> return touchControl.execute(message)
            Channel.ID_SEN -> return sensorControl.execute(message)
            Channel.ID_VID, Channel.ID_AUD, Channel.ID_AU1, Channel.ID_AU2, Channel.ID_MIC -> return mediaControl.execute(message)
        }
        return 0
    }

    private fun channelOpenRequest(request: Control.ChannelOpenRequest, channel: Int): Int {
        val msg = AapMessage(channel, Control.ControlMsgType.MESSAGE_CHANNEL_OPEN_RESPONSE_VALUE, Control.ChannelOpenResponse.newBuilder()
                .setStatus(Common.MessageStatus.STATUS_SUCCESS)
                .build())
        aapTransport.send(msg)

        if (channel == Channel.ID_SEN) {
            aapTransport.send(DrivingStatusEvent(Sensors.SensorBatch.DrivingStatusData.Status.UNRESTRICTED))
        }
        return 0
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapMessageIncoming.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.MsgType
import com.andrerinas.headunitrevived.utils.AppLog

internal class AapMessageIncoming(header: EncryptedHeader, ba: ByteArrayWithLimit)
    : AapMessage(header.chan, header.flags.toByte(), Utils.bytesToInt(ba.data, 0, true), calcOffset(header), ba.limit, ba.data) {

    internal class EncryptedHeader {

        var chan: Int = 0
        var flags: Int = 0
        var enc_len: Int = 0
        var msg_type: Int = 0
        var buf = ByteArray(SIZE)

        fun decode() {
            this.chan = buf[0].toInt()
            this.flags = buf[1].toInt()

            // Encoded length of bytes to be decrypted (minus 4/8 byte headers)
            this.enc_len = Utils.bytesToInt(buf, 2, true)
        }

        companion object {
            const val SIZE = 4
        }

    }

    companion object {

        fun decrypt(header: EncryptedHeader, offset: Int, buf: ByteArray, ssl: AapSsl): AapMessage? {
            if (header.flags and 0x08 != 0x08) {
                AppLog.e("WRONG FLAG: enc_len: %d  chan: %d %s flags: 0x%02x  msg_type: 0x%02x %s",
                        header.enc_len, header.chan, Channel.name(header.chan), header.flags, header.msg_type, MsgType.name(header.msg_type, header.chan))
                return null
            }

            val ba = ssl.decrypt(offset, header.enc_len, buf) ?: return null

            if (ba.data.size < 2) {
                AppLog.e("Decrypted payload too short: " + ba.data.size)
                return null
            }

            val msg = AapMessageIncoming(header, ba)

            if (AppLog.LOG_VERBOSE) {
                AppLog.d("RECV: %s", msg.toString())
            }
            return msg
        }

        fun calcOffset(header: EncryptedHeader): Int {
            return 2
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapRead.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import android.content.Context
import com.andrerinas.headunitrevived.connection.AccessoryConnection
import com.andrerinas.headunitrevived.decoder.MicRecorder
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.aap.protocol.proto.MediaPlayback
import com.andrerinas.headunitrevived.utils.Settings

internal interface AapRead {
    fun read(): Int

    abstract class Base internal constructor(
            private val connection: AccessoryConnection?,
            internal val ssl: AapSsl,
            internal val handler: AapMessageHandler) : AapRead {

        override fun read(): Int {
            if (connection == null) {
                AppLog.e("No connection.")
                return -1
            }

            return doRead(connection)
        }

        protected abstract fun doRead(connection: AccessoryConnection): Int
    }

    object Factory {
        fun create(
            connection: AccessoryConnection,
            transport: AapTransport,
            recorder: MicRecorder,
            aapAudio: AapAudio,
            aapVideo: AapVideo,
            settings: Settings,
            context: Context,
            onAaMediaMetadata: ((MediaPlayback.MediaMetaData) -> Unit)? = null,
            onAaPlaybackStatus: ((MediaPlayback.MediaPlaybackStatus) -> Unit)? = null
        ): AapRead {
            val handler = AapMessageHandlerType(
                transport,
                recorder,
                aapAudio,
                aapVideo,
                settings,
                context,
                onAaMediaMetadata,
                onAaPlaybackStatus
            )

            return if (connection.isSingleMessage)
                AapReadSingleMessage(connection, transport.ssl, handler)
            else
                AapReadMultipleMessages(connection, transport.ssl, handler)
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapProjectionActivity.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
import android.os.SystemClock
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.TextureView
import android.view.View
import android.widget.Button
import android.widget.FrameLayout
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.enableEdgeToEdge
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.aap.protocol.messages.TouchEvent
import com.andrerinas.headunitrevived.aap.protocol.messages.VideoFocusEvent
import com.andrerinas.headunitrevived.app.SurfaceActivity
import com.andrerinas.headunitrevived.connection.CommManager
import com.andrerinas.headunitrevived.contract.KeyIntent
import kotlinx.coroutines.launch
import com.andrerinas.headunitrevived.decoder.VideoDecoder
import com.andrerinas.headunitrevived.decoder.VideoDimensionsListener
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.IntentFilters
import com.andrerinas.headunitrevived.view.IProjectionView
import com.andrerinas.headunitrevived.view.GlProjectionView
import com.andrerinas.headunitrevived.view.ProjectionView
import com.andrerinas.headunitrevived.view.TextureProjectionView
import com.andrerinas.headunitrevived.utils.Settings
import com.andrerinas.headunitrevived.view.OverlayTouchView
import com.andrerinas.headunitrevived.utils.HeadUnitScreenConfig
import com.andrerinas.headunitrevived.utils.SystemUI
import android.content.IntentFilter
import com.andrerinas.headunitrevived.view.ProjectionViewScaler
import com.google.android.material.dialog.MaterialAlertDialogBuilder

class AapProjectionActivity : SurfaceActivity(), IProjectionView.Callbacks, VideoDimensionsListener {

    private enum class OverlayState { STARTING, RECONNECTING, HIDDEN }

    private lateinit var projectionView: IProjectionView
    private val videoDecoder: VideoDecoder by lazy { App.provide(this).videoDecoder }
    private val settings: Settings by lazy { Settings(this) }
    private var isSurfaceSet = false
    private var overlayState = OverlayState.STARTING
    private val watchdogHandler = android.os.Handler(android.os.Looper.getMainLooper())

    private var initialX = 0f
    private var initialY = 0f
    private var isPotentialGesture = false
    private var fpsTextView: TextView? = null

    private val videoWatchdogRunnable = object : Runnable {
        override fun run() {
            val loadingOverlay = findViewById<View>(R.id.loading_overlay)
            if (loadingOverlay?.visibility == View.VISIBLE && commManager.isConnected) {
                // If the decoder already rendered something, hide the overlay immediately
                if (videoDecoder.lastFrameRenderedMs > 0) {
                    AppLog.i("Watchdog: Decoder is already rendering frames. Hiding overlay.")
                    loadingOverlay.visibility = View.GONE
                    overlayState = OverlayState.HIDDEN
                    return
                }

                AppLog.w("Watchdog: No video received yet. Requesting Keyframe (Unsolicited Focus)...")
                commManager.send(VideoFocusEvent(gain = true, unsolicited = true))
                watchdogHandler.postDelayed(this, 1500)
            }
        }
    }
    private val reconnectingWatchdog = object : Runnable {
        override fun run() {
            // Only run watchdog if we are actually supposed to be connected
            if (commManager.connectionState.value !is CommManager.ConnectionState.HandshakeComplete) {
                return
            }
            val lastFrame = videoDecoder.lastFrameRenderedMs
            if (lastFrame == 0L) {
                // First frame hasn't arrived yet — handled by the starting overlay
                watchdogHandler.postDelayed(this, 2000)
                return
            }
            val gap = SystemClock.elapsedRealtime() - lastFrame
            if (overlayState == OverlayState.HIDDEN && gap > 10000) {
                showReconnectingOverlay()
            } else if (overlayState == OverlayState.RECONNECTING && gap < 2000) {
                hideReconnectingOverlay()
            }
            watchdogHandler.postDelayed(this, 2000)
        }
    }
    private val watchdogRunnable = Runnable {
        if (!isSurfaceSet) {
            AppLog.w("Watchdog: Surface not set after 2s. Checking view state...")
            checkAndForceSurface()
        }
    }
    private fun checkAndForceSurface() {
        AppLog.i("Watchdog: checkAndForceSurface executing...")
        if (projectionView is TextureView) {
            val tv = projectionView as TextureView
            if (tv.isAvailable) {
                AppLog.w("Watchdog: TextureView IS available. Forcing onSurfaceChanged.")
                onSurfaceChanged(android.view.Surface(tv.surfaceTexture), tv.width, tv.height)
            } else {
                AppLog.e("Watchdog: TextureView NOT available. Vis=${tv.visibility}, W=${tv.width}, H=${tv.height}")
            }
        } else if (projectionView is GlProjectionView) {
             val gles = projectionView as GlProjectionView
             if (gles.isSurfaceValid()) {
                 AppLog.w("Watchdog: GlProjectionView IS valid. Forcing onSurfaceChanged.")
                 onSurfaceChanged(gles.getSurface()!!, gles.width, gles.height)
             } else {
                 AppLog.e("Watchdog: GlProjectionView NOT valid.")
             }
        } else if (projectionView is ProjectionView) {
             val sv = projectionView as ProjectionView
             if (sv.holder.surface.isValid) {
                 AppLog.w("Watchdog: SurfaceView IS valid. Forcing onSurfaceChanged.")
                 onSurfaceChanged(sv.holder.surface, sv.width, sv.height)
             } else {
                 AppLog.e("Watchdog: SurfaceView NOT valid.")
             }
        }
    }

    private val nightModeReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val isNight = intent.getBooleanExtra("isNight", false)
            updateDesaturation(isNight)
        }
    }

    private fun updateDesaturation(isNight: Boolean) {
        if (settings.aaMonochromeEnabled && projectionView is GlProjectionView) {
            val level = if (isNight) settings.aaDesaturationLevel / 100f else 0f
            (projectionView as GlProjectionView).setDesaturation(level)
        } else if (projectionView is GlProjectionView) {
            (projectionView as GlProjectionView).setDesaturation(0f)
        }
    }

    private val keyCodeReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val event: KeyEvent? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableExtra(KeyIntent.extraEvent, KeyEvent::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(KeyIntent.extraEvent)
            }
            event?.let {
                onKeyEvent(it.keyCode, it.action == KeyEvent.ACTION_DOWN)
            }
        }
    }

    private val finishReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == "com.andrerinas.headunitrevived.ACTION_FINISH_ACTIVITIES") {
                AppLog.i("AapProjectionActivity: Received finish request. Closing.")
                finish()
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            enableEdgeToEdge()
        }
        super.onCreate(savedInstanceState)

        val screenOrientation = settings.screenOrientation
        if (screenOrientation == Settings.ScreenOrientation.AUTO) {
            applyStickyOrientation()
            if (!HeadUnitScreenConfig.isResolutionLocked) {
                // Initial start: lock to current orientation at launch
                if (Build.VERSION.SDK_INT >= 18) {
                    requestedOrientation = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LOCKED
                } else {
                    requestedOrientation = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_NOSENSOR
                }
            }
        } else {
            requestedOrientation = screenOrientation.androidOrientation
        }

        setContentView(R.layout.activity_headunit)

        if (settings.showFpsCounter) {
            val container = findViewById<FrameLayout>(R.id.container)
            fpsTextView = TextView(this).apply {
                setTextColor(Color.YELLOW)
                textSize = 12f
                setTypeface(null, Typeface.BOLD)
                setBackgroundColor(Color.parseColor("#80000000"))
                setPadding(10, 5, 10, 5)
                text = "FPS: --"
                // Lift it above everything
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    elevation = 100f
                    translationZ = 100f
                }
            }
            val params = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT
            ).apply {
                gravity = Gravity.TOP or Gravity.START
                setMargins(20, 20, 0, 0)
            }
            container.addView(fpsTextView, params)

            videoDecoder.onFpsChanged = { fps ->
                runOnUiThread { fpsTextView?.text = "FPS: $fps" }
            }
        }

        videoDecoder.dimensionsListener = this

        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                showExitDialog()
            }
        })

        var isFirstEmission = true
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                commManager.connectionState.collect { state ->
                    val first = isFirstEmission
                    isFirstEmission = false

                    if (first && state is CommManager.ConnectionState.Disconnected) {
                        AppLog.i("AapProjectionActivity: Ignoring initial Disconnected state from StateFlow replay.")
                        return@collect
                    }

                    when (state) {
                        is CommManager.ConnectionState.Disconnected -> {
                            watchdogHandler.removeCallbacksAndMessages(null)
                            if (!state.isClean && !state.isUserExit) {
                                AppLog.w("AapProjectionActivity: Disconnected unexpectedly.")
                                Toast.makeText(this@AapProjectionActivity, getString(R.string.wifi_disconnect_toast), Toast.LENGTH_LONG).show()
                            }
                            // Only finish immediately if the user explicitly exited or it was a clean close.
                            if (state.isUserExit || state.isClean) {
                                AppLog.i("AapProjectionActivity: Finishing because state isUserExit=${state.isUserExit}, isClean=${state.isClean}")
                                hideReconnectingOverlay()
                                finish()
                            } else {
                                // For unexpected disconnects (especially Wireless), wait a tiny bit to see if service restarts it
                                watchdogHandler.postDelayed({
                                    if (commManager.connectionState.value is CommManager.ConnectionState.Disconnected) {
                                        AppLog.i("AapProjectionActivity: Finishing after delay due to Disconnected state.")
                                        hideReconnectingOverlay()
                                        finish()
                                    }
                                }, 2000)
                            }
                        }
                        is CommManager.ConnectionState.HandshakeComplete -> {
                            // Lock the resolution so that orientation changes don't cause re-negotiation
                            HeadUnitScreenConfig.lockResolution()

                            // Handshake done. If the surface is already ready (e.g. reconnect
                            // while the activity is in the foreground), start reading immediately.
                            // If not, onSurfaceChanged() will call startReading() when the surface
                            // becomes available.
                            if (isSurfaceSet) {
                                commManager.startReading()
                            }
                        }
                        else -> {}
                    }
                }
            }
        }

        ContextCompat.registerReceiver(this, finishReceiver, android.content.IntentFilter("com.andrerinas.headunitrevived.ACTION_FINISH_ACTIVITIES"), ContextCompat.RECEIVER_NOT_EXPORTED)

        AppLog.i("HeadUnit for Android Auto (tm) - Copyright 2011-2015 Michael A. Reid., since 2025 André Rinas All Rights Reserved...")

        val container = findViewById<android.widget.FrameLayout>(R.id.container)
        val displayMetrics = resources.displayMetrics

        if (settings.viewMode == Settings.ViewMode.TEXTURE) {
            AppLog.i("Using TextureView")
            val textureView = TextureProjectionView(this)
            textureView.layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
            projectionView = textureView
            container.setBackgroundColor(android.graphics.Color.BLACK)
        } else if (settings.viewMode == Settings.ViewMode.GLES) {
            AppLog.i("Using GlProjectionView")
            val glView = com.andrerinas.headunitrevived.view.GlProjectionView(this)
            glView.layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
            projectionView = glView
            container.setBackgroundColor(Color.BLACK)
        } else {
            AppLog.i("Using SurfaceView")
            projectionView = ProjectionView(this)
            (projectionView as View).layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        }
        // Use the same screen conf for both views for negotiation
        HeadUnitScreenConfig.init(this, displayMetrics, settings)

        val view = projectionView as View
        container.addView(view)

        projectionView.addCallback(this)

        val overlayView = OverlayTouchView(this)
        overlayView.layoutParams = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT,
            FrameLayout.LayoutParams.MATCH_PARENT
        )
        overlayView.isFocusable = true
        overlayView.isFocusableInTouchMode = true

        overlayView.setOnTouchListener { _, event ->
                if (event.action == MotionEvent.ACTION_DOWN) {
                    overlayView.requestFocus()
                }
                sendTouchEvent(event)
                true
            }

        container.addView(overlayView)
        overlayView.requestFocus()
        setFullscreen() // Call setFullscreen here as well

        val loadingOverlay = findViewById<View>(R.id.loading_overlay)

        // [FIX] If we are already connected and frames are flowing (e.g. activity recreation),
        // hide the overlay immediately to prevent the "Android Auto is starting" flicker.
        if (commManager.isConnected && videoDecoder.lastFrameRenderedMs > 0) {
            loadingOverlay?.visibility = View.GONE
            overlayState = OverlayState.HIDDEN
        }

        // Ensure loading overlay is on top of everything
        loadingOverlay?.bringToFront()

        findViewById<Button>(R.id.disconnect_button)?.setOnClickListener {
            commManager.disconnect()
        }

        videoDecoder.onFirstFrameListener = {
            runOnUiThread {
                loadingOverlay?.visibility = View.GONE
                overlayState = OverlayState.HIDDEN

                // Show one-time gesture hint
                if (!settings.gestureHintShown) {
                    Toast.makeText(this@AapProjectionActivity, R.string.gesture_hint, Toast.LENGTH_LONG).show()
                    settings.gestureHintShown = true
                }
            }
        }

        commManager.onUpdateUiConfigReplyReceived = {
            AppLog.i("[UI_DEBUG_FIX] UpdateUiConfig reply received. AA acknowledged new margins.")
        }
    }

    override fun onPause() {
        AppLog.i("AapProjectionActivity: onPause")
        super.onPause()
        watchdogHandler.removeCallbacks(watchdogRunnable)
        watchdogHandler.removeCallbacks(videoWatchdogRunnable)
        watchdogHandler.removeCallbacks(reconnectingWatchdog)
        unregisterReceiver(keyCodeReceiver)
        unregisterReceiver(nightModeReceiver)
    }

    override fun onResume() {
        AppLog.i("AapProjectionActivity: onResume")
        super.onResume()
        applyStickyOrientation()
        watchdogHandler.postDelayed(watchdogRunnable, 2000)
        watchdogHandler.postDelayed(videoWatchdogRunnable, 3000)
        watchdogHandler.postDelayed(reconnectingWatchdog, 5000)

        // Register key event receiver safely for Android 14+
        ContextCompat.registerReceiver(this, keyCodeReceiver, IntentFilters.keyEvent, ContextCompat.RECEIVER_NOT_EXPORTED)

        // Register night mode receiver for AA monochrome filter
        ContextCompat.registerReceiver(this, nightModeReceiver, IntentFilter(AapService.ACTION_NIGHT_MODE_CHANGED), ContextCompat.RECEIVER_NOT_EXPORTED)

        // Request current night mode state for initial desaturation
        sendBroadcast(Intent(AapService.ACTION_REQUEST_NIGHT_MODE_UPDATE).apply {
            setPackage(packageName)
        })

        setFullscreen()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        AppLog.i("AapProjectionActivity: onNewIntent received")
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)

        if (hasFocus) {
            setFullscreen() // Reapply fullscreen mode if window gains focus
        }
    }

    private fun showReconnectingOverlay() {
        AppLog.i("Showing reconnecting overlay")
        overlayState = OverlayState.RECONNECTING
        val overlay = findViewById<View>(R.id.loading_overlay) ?: return
        val title = findViewById<TextView>(R.id.overlay_text)
        val detail = findViewById<TextView>(R.id.overlay_detail)
        val button = findViewById<Button>(R.id.disconnect_button)
        overlay.visibility = View.VISIBLE
        title?.text = getString(R.string.connection_interrupted)
        detail?.text = getString(R.string.connection_interrupted_detail)
        detail?.visibility = View.VISIBLE
        button?.visibility = View.VISIBLE
    }

    private fun hideReconnectingOverlay() {
        AppLog.i("Hiding reconnecting overlay — frames resumed")
        overlayState = OverlayState.HIDDEN
        val overlay = findViewById<View>(R.id.loading_overlay) ?: return
        val detail = findViewById<TextView>(R.id.overlay_detail)
        val button = findViewById<Button>(R.id.disconnect_button)
        overlay.visibility = View.GONE
        detail?.visibility = View.GONE
        button?.visibility = View.GONE
    }

    private fun setFullscreen() {
        val container = findViewById<View>(R.id.container)

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.KITKAT && settings.fullscreenMode != Settings.FullscreenMode.NONE) {
            window.addFlags(android.view.WindowManager.LayoutParams.FLAG_FULLSCREEN)
        }

        SystemUI.apply(window, container, settings.fullscreenMode) {
            if (::projectionView.isInitialized) {
                ProjectionViewScaler.updateScale(projectionView as View, videoDecoder.videoWidth, videoDecoder.videoHeight)
            }
        }

        // Workaround for API < 19 (Jelly Bean) where Sticky Immersive Mode doesn't exist.
        // If bars appear (e.g. on touch), hide them again after a delay.
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.KITKAT && settings.fullscreenMode != Settings.FullscreenMode.NONE) {
            window.decorView.setOnSystemUiVisibilityChangeListener { visibility ->
                if ((visibility and View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
                    // Bars are visible. Hide them again.
                    android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                        SystemUI.apply(window, container, settings.fullscreenMode) {
            if (::projectionView.isInitialized) {
                ProjectionViewScaler.updateScale(projectionView as View, videoDecoder.videoWidth, videoDecoder.videoHeight)
            }
        }
                    }, 2000)
                }
            }
        }
    }

    private data class ExitOption(val titleResId: Int, val iconResId: Int, val iconColor: Int)

    private fun showExitDialog() {
        val options = mutableListOf<ExitOption>()
        options.add(ExitOption(R.string.exit_dialog_stop, R.drawable.ic_stop, Color.RED))

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            options.add(ExitOption(R.string.exit_dialog_pip, R.drawable.ic_pip, Color.LTGRAY))
        }

        options.add(ExitOption(R.string.exit_dialog_background, R.drawable.ic_home, Color.LTGRAY))

        val adapter = object : android.widget.BaseAdapter() {
            override fun getCount(): Int = options.size
            override fun getItem(position: Int): Any = options[position]
            override fun getItemId(position: Int): Long = position.toLong()
            override fun getView(position: Int, convertView: View?, parent: android.view.ViewGroup): View {
                val view = convertView ?: layoutInflater.inflate(R.layout.dialog_exit_item, parent, false)
                val option = options[position]
                val iconView = view.findViewById<android.widget.ImageView>(R.id.icon)
                val textView = view.findViewById<android.widget.TextView>(R.id.text)

                textView.setText(option.titleResId)
                iconView.setImageResource(option.iconResId)
                iconView.setColorFilter(option.iconColor)

                return view
            }
        }

        MaterialAlertDialogBuilder(this, R.style.DarkAlertDialog)
            .setTitle(R.string.exit_dialog_title)
            .setAdapter(adapter) { _, which ->
                val selected = options[which]
                when (selected.titleResId) {
                    R.string.exit_dialog_stop -> {
                        commManager.disconnect(sendByeBye = true)
                        finish()
                    }
                    R.string.exit_dialog_pip -> {
                        enterPiP()
                    }
                    R.string.exit_dialog_background -> {
                        moveToBackground()
                    }
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun enterPiP() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            try {
                val params = android.app.PictureInPictureParams.Builder()
                    // Default aspect ratio for AA (usually 16:9 or 16:10)
                    .setAspectRatio(android.util.Rational(videoDecoder.videoWidth.coerceAtLeast(1), videoDecoder.videoHeight.coerceAtLeast(1)))
                    .build()
                enterPictureInPictureMode(params)
            } catch (e: Exception) {
                AppLog.e("Failed to enter PiP mode: ${e.message}")
            }
        }
    }

    private fun moveToBackground() {
        val startMain = Intent(Intent.ACTION_MAIN)
        startMain.addCategory(Intent.CATEGORY_HOME)
        startMain.flags = Intent.FLAG_ACTIVITY_NEW_TASK
        startActivity(startMain)
    }

    override fun onPictureInPictureModeChanged(isInPictureInPictureMode: Boolean, newConfig: android.content.res.Configuration) {
        super.onPictureInPictureModeChanged(isInPictureInPictureMode, newConfig)
        if (isInPictureInPictureMode) {
            // Hide UI elements during PiP (like FPS counter, loading overlay)
            findViewById<View>(R.id.loading_overlay)?.visibility = View.GONE
            fpsTextView?.visibility = View.GONE
        } else {
            // Restore UI if needed
            fpsTextView?.visibility = if (settings.showFpsCounter) View.VISIBLE else View.GONE
            setFullscreen()
        }
    }

    override fun onUserLeaveHint() {
        // Optional: Auto-enter PiP if user presses home

        // For now, we only enter via dialog as requested.
        super.onUserLeaveHint()
    }

    private val commManager get() = App.provide(this).commManager

    override fun dispatchTouchEvent(ev: MotionEvent): Boolean {
        // 1. 2-finger swipe detection from the left edge (to open exit menu)
        if (ev.pointerCount == 2) {
            when (ev.actionMasked) {
                MotionEvent.ACTION_POINTER_DOWN -> {
                    initialX = ev.getX(0)
                    initialY = ev.getY(0)
                    isPotentialGesture = initialX < 100
                }
                MotionEvent.ACTION_MOVE -> {
                    if (isPotentialGesture) {
                        val deltaX = ev.getX(0) - initialX
                        val deltaY = Math.abs(ev.getY(0) - initialY)
                        if (deltaX > 200 && deltaY < 100) {
                            isPotentialGesture = false
                            showExitDialog()
                            return true // Consume
                        }
                    }
                }
            }
        }

        // 2. Legacy Touch handling for older devices (API < 19)
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.KITKAT) {
            sendTouchEvent(ev)
        }

        return super.dispatchTouchEvent(ev)
    }

    override fun onSurfaceCreated(surface: android.view.Surface) {
        AppLog.i("[UI_DEBUG] [AapProjectionActivity] onSurfaceCreated")
        // Decoder configuration is now in onSurfaceChanged
    }

    override fun onSurfaceChanged(surface: android.view.Surface, width: Int, height: Int) {
        AppLog.i("[UI_DEBUG] [AapProjectionActivity] onSurfaceChanged. Actual surface dimensions: width=$width, height=$height")
        isSurfaceSet = true

        videoDecoder.setSurface(surface)

        // --- Surface Mismatch Detection ---
        // Compare actual surface dimensions with what HeadUnitScreenConfig negotiated.
        // If they differ (e.g. system bars appeared/disappeared), update margins.
        val prevUsableW = HeadUnitScreenConfig.getUsableWidth()
        val prevUsableH = HeadUnitScreenConfig.getUsableHeight()

        if (HeadUnitScreenConfig.updateSurfaceDimensions(width, height)) {
            AppLog.i("[UI_DEBUG_FIX] Surface mismatch! Expected: ${prevUsableW}x${prevUsableH}, Actual: ${width}x${height}")

            // Cache the real surface size for next session
            settings.cachedSurfaceWidth = width
            settings.cachedSurfaceHeight = height
            settings.cachedSurfaceSettingsHash = HeadUnitScreenConfig.computeSettingsHash(settings)

            if (commManager.connectionState.value is CommManager.ConnectionState.TransportStarted) {
                // AA is already running → send corrected per-side margins dynamically
                commManager.sendUpdateUiConfigRequest(
                    HeadUnitScreenConfig.getLeftMargin(),
                    HeadUnitScreenConfig.getTopMargin(),
                    HeadUnitScreenConfig.getRightMargin(),
                    HeadUnitScreenConfig.getBottomMargin()
                )
                AppLog.i("[UI_DEBUG_FIX] AA is already running, send corrected via sendUpdateUiConfigRequest")
            }
            // If transport not started yet, ServiceDiscoveryResponse will use the corrected values automatically.
        }

        when (commManager.connectionState.value) {
            is CommManager.ConnectionState.Connected -> {
                // AapService should have started the handshake already, but as a fallback
                // (e.g. service restarted) kick it off here. The HandshakeComplete observer
                // will call startReading() once the handshake finishes.
                lifecycleScope.launch { commManager.startHandshake() }
            }
            is CommManager.ConnectionState.StartingTransport -> {
                // Handshake is in progress. The HandshakeComplete observer will call
                // startReading() when it finishes.
            }
            is CommManager.ConnectionState.HandshakeComplete -> {
                // Handshake already done before surface was ready — start reading now.
                lifecycleScope.launch { commManager.startReading() }
            }
            is CommManager.ConnectionState.TransportStarted -> {
                // Surface recreated while transport was already running; request a keyframe.
                commManager.send(VideoFocusEvent(gain = true, unsolicited = true))
            }
            else -> {
                commManager.send(VideoFocusEvent(gain = true, unsolicited = false))
            }
        }

        // Explicitly check and set video dimensions if already known by the decoder
        // This handles cases where the activity is recreated but the decoder already has dimensions
        val currentVideoWidth = videoDecoder.videoWidth
        val currentVideoHeight = videoDecoder.videoHeight

        if (currentVideoWidth > 0 && currentVideoHeight > 0) {
            AppLog.i("[AapProjectionActivity] Decoder already has dimensions: ${currentVideoWidth}x$currentVideoHeight. Applying to view.")
            runOnUiThread {
                projectionView.setVideoSize(currentVideoWidth, currentVideoHeight)
                ProjectionViewScaler.updateScale(projectionView as View, currentVideoWidth, currentVideoHeight)
            }
        }
    }

    override fun onSurfaceDestroyed(surface: android.view.Surface) {
        AppLog.i("SurfaceCallback: onSurfaceDestroyed. Surface: $surface")
        isSurfaceSet = false
        commManager.send(VideoFocusEvent(gain = false, unsolicited = false))
        videoDecoder.stop("surfaceDestroyed")
    }

    override fun onVideoDimensionsChanged(width: Int, height: Int) {
        AppLog.i("[AapProjectionActivity] Received video dimensions: ${width}x$height")
        runOnUiThread {
            projectionView.setVideoSize(width, height)
            ProjectionViewScaler.updateScale(projectionView as View, width, height)
        }
    }

    private fun sendTouchEvent(event: MotionEvent) {
        val action = TouchEvent.motionEventToAction(event) ?: return
        val ts = SystemClock.elapsedRealtime()

        val videoW = HeadUnitScreenConfig.getNegotiatedWidth()
        val videoH = HeadUnitScreenConfig.getNegotiatedHeight()

        if (videoW <= 0 || videoH <= 0 || projectionView !is View) {
            AppLog.w("sendTouchEvent: Ignoring touch, screen config or view not ready.")
            return
        }

        val view = projectionView as View
        // Use the container's "Anchor" dimensions (full touch surface) as the reference,
        // not the potentially resized projectionView's dimensions.
        val viewW = HeadUnitScreenConfig.getUsableWidth().toFloat()
        val viewH = HeadUnitScreenConfig.getUsableHeight().toFloat()

        if (viewW <= 0 || viewH <= 0) return

        val marginW = HeadUnitScreenConfig.getWidthMargin().toFloat()
        val marginH = HeadUnitScreenConfig.getHeightMargin().toFloat()

        val uiW = videoW - marginW
        val uiH = videoH - marginH

        // Logic check: When forcedScale is active, the visual behavior of 'stretchToFill'
        // is inverted (True = Aspect Ratio Centered, False = Stretched to Screen).
        // We adjust the touch mapping to match this visual reality.
        val isStretch = if (HeadUnitScreenConfig.forcedScale) {
            !settings.stretchToFill
        } else {
            settings.stretchToFill
        }

        val pointerData = mutableListOf<Triple<Int, Int, Int>>()
        repeat(event.pointerCount) { pointerIndex ->
            val pointerId = event.getPointerId(pointerIndex)
            val px = event.getX(pointerIndex)
            val py = event.getY(pointerIndex)

            var videoX = 0f
            var videoY = 0f

            if (isStretch) {
                videoX = (px / viewW) * uiW
                videoY = (py / viewH) * uiH
            } else {
                val uiRatio = uiW / uiH
                val viewRatio = viewW / viewH

                var displayedUiW = viewW
                var displayedUiH = viewH

                if (viewRatio > uiRatio) {
                    displayedUiW = viewH * uiRatio
                } else {
                    displayedUiH = viewW / uiRatio
                }

                val uiLeft = (viewW - displayedUiW) / 2f
                val uiTop = (viewH - displayedUiH) / 2f

                val localX = px - uiLeft
                val localY = py - uiTop

                videoX = (localX / displayedUiW) * uiW
                videoY = (localY / displayedUiH) * uiH
            }

            // Clamp to negotiated bounds to prevent out-of-bounds touches
            val correctedX = videoX.toInt().coerceIn(0, videoW)
            val correctedY = videoY.toInt().coerceIn(0, videoH)

            pointerData.add(Triple(pointerId, correctedX, correctedY))
        }

        commManager.send(TouchEvent(ts, action, event.actionIndex, pointerData))
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK || keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_MUTE) {
            return super.onKeyDown(keyCode, event)
        }
        onKeyEvent(keyCode, true)
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        if (keyCode == KeyEvent.KEYCODE_BACK || keyCode == KeyEvent.KEYCODE_VOLUME_UP || keyCode == KeyEvent.KEYCODE_VOLUME_DOWN || keyCode == KeyEvent.KEYCODE_VOLUME_MUTE) {
            return super.onKeyUp(keyCode, event)
        }
        onKeyEvent(keyCode, false)
        return true
    }

    private fun onKeyEvent(keyCode: Int, isPress: Boolean) {
        AppLog.d("AapProjectionActivity: onKeyEvent code=$keyCode, isPress=$isPress")
        commManager.send(keyCode, isPress)
    }

    private fun applyStickyOrientation() {
        if (settings.screenOrientation == Settings.ScreenOrientation.AUTO && HeadUnitScreenConfig.isResolutionLocked) {
            val target = if (HeadUnitScreenConfig.getNegotiatedWidth() > HeadUnitScreenConfig.getNegotiatedHeight()) {
                android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
            } else {
                android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
            }
            if (requestedOrientation != target) {
                AppLog.i("[UI_DEBUG] Sticky Orientation: Session active, forcing orientation to $target")
                requestedOrientation = target
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        try { unregisterReceiver(finishReceiver) } catch (e: Exception) {}
        AppLog.i("AapProjectionActivity.onDestroy called. isFinishing=$isFinishing")
        videoDecoder.dimensionsListener = null
    }

    companion object {
        const val EXTRA_FOCUS = "focus"

        fun intent(context: Context): Intent {
            val aapIntent = Intent(context, AapProjectionActivity::class.java)
            aapIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            return aapIntent
        }
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapTransport.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import android.app.UiModeManager
import android.content.Context
import android.content.Context.UI_MODE_SERVICE
import android.content.Intent
import android.media.AudioManager
import android.os.Handler
import android.os.HandlerThread
import android.os.Process
import android.os.SystemClock
import android.util.SparseIntArray
import android.view.KeyEvent
import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.messages.KeyCodeEvent
import com.andrerinas.headunitrevived.aap.protocol.messages.MediaAck
import com.andrerinas.headunitrevived.aap.protocol.messages.Messages
import com.andrerinas.headunitrevived.aap.protocol.messages.NightModeEvent
import com.andrerinas.headunitrevived.aap.protocol.messages.ScrollWheelEvent
import com.andrerinas.headunitrevived.aap.protocol.messages.SensorEvent
import com.andrerinas.headunitrevived.aap.protocol.messages.TouchEvent
import com.andrerinas.headunitrevived.aap.protocol.proto.Input
import com.andrerinas.headunitrevived.aap.protocol.proto.Sensors
import com.andrerinas.headunitrevived.connection.AccessoryConnection
import com.andrerinas.headunitrevived.contract.ProjectionActivityRequest
import com.andrerinas.headunitrevived.decoder.AudioDecoder
import com.andrerinas.headunitrevived.decoder.MicRecorder
import com.andrerinas.headunitrevived.decoder.VideoDecoder
import com.andrerinas.headunitrevived.main.BackgroundNotification
import com.andrerinas.headunitrevived.ssl.SingleKeyKeyManager
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.Settings
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.aap.protocol.proto.Control
import com.andrerinas.headunitrevived.aap.protocol.proto.MediaPlayback
import javax.net.ssl.SSLEngineResult

/**
 * Core AAP message pump.
 *
 * Owns two [HandlerThread]s:
 * - **Send** (`AapTransport:Handler::Send`) — encrypts and delivers outbound messages.
 * - **Poll** (`AapTransport:Handler::Poll`) — reads, decrypts, and dispatches inbound messages.
 *
 * Lifecycle: [startHandshake] → [startReading] → message loop → [stop]/[quit].
 *
 * @param audioDecoder Decodes PCM audio received from the phone.
 * @param videoDecoder Decodes H.264/H.265 video received from the phone.
 * @param audioManager Used to request and release audio focus.
 * @param settings User preferences (SSL mode, key mappings, microphone sample rate, …).
 * @param notification Background notification handle; updated as connection state changes.
 * @param context Application context; used for broadcasts and system services.
 * @param onAaMediaMetadata Optional callback when the phone sends media metadata (now-playing).
 * @param onAaPlaybackStatus Optional callback when the phone sends playback status/position.
 * @param externalSsl Optional singleton [AapSslContext] whose internal [javax.net.ssl.SSLContext]
 *   (and its `ClientSessionContext` session cache) survives across [AapTransport] recreations.
 *   When provided on the Java-SSL path, JSSE can resume the previous TLS session on reconnect,
 *   skipping 4–6 round-trips and saving 1–3 s of handshake time. Pass `null` to create a
 *   fresh [AapSslContext] per transport (no session resumption). Ignored when native SSL is
 *   active (`settings.useNativeSsl = true`).
 */
class AapTransport(
        audioDecoder: AudioDecoder,
        videoDecoder: VideoDecoder,
        audioManager: AudioManager,
        internal val settings: Settings,
        private val notification: BackgroundNotification,
        private val context: Context,
        private val onAaMediaMetadata: ((MediaPlayback.MediaMetaData) -> Unit)? = null,
        private val onAaPlaybackStatus: ((MediaPlayback.MediaPlaybackStatus) -> Unit)? = null,
        private val externalSsl: AapSslContext? = null)
    : MicRecorder.Listener {

    val ssl: AapSsl = if (settings.useNativeSsl) {
        try {
            AppLog.i("Using Native SSL implementation")
            AapSslNative()
        } catch (e: Throwable) {
            AppLog.e("Failed to instantiate Native SSL, falling back to Java SSL", e)
            // Use the shared context when available so session resumption works on fallback.
            externalSsl ?: AapSslContext(SingleKeyKeyManager(context))
        }
    } else {
        AppLog.i("Using Java SSL implementation")
        // externalSsl is the singleton AapSslContext from AppComponent whose SSLContext
        // (and its ClientSessionContext session cache) survives across transport recreations,
        // enabling TLS session resumption on reconnect.
        externalSsl ?: AapSslContext(SingleKeyKeyManager(context))
    }

    internal val aapAudio: AapAudio
    internal val aapVideo: AapVideo
    private var sendThread: HandlerThread? = null
    private var pollThread: HandlerThread? = null
    private val micRecorder: MicRecorder = MicRecorder(settings.micSampleRate, context)
    private val sessionIds = SparseIntArray(4)
    private val startedSensors = HashSet<Int>(4)
    private val keyCodes = settings.keyCodes.entries.associateTo(mutableMapOf()) {
        it.key to it.value
    }
    private val modeManager: UiModeManager =
        context.getSystemService(UI_MODE_SERVICE) as UiModeManager
    private var connection: AccessoryConnection? = null
    private var aapRead: AapRead? = null
    var isQuittingAllowed: Boolean = false

    val isWireless: Boolean
        get() = connection is com.andrerinas.headunitrevived.connection.SocketAccessoryConnection
    var ignoreNextStopRequest: Boolean = false
    /** Set by [AapControl] when VIDEO_FOCUS_NATIVE triggers a stop (user tapped Exit). */
    @Volatile var wasUserExit: Boolean = false
    @Volatile var onQuit: ((Boolean) -> Unit)? = null
    var onAudioFocusStateChanged: ((Boolean) -> Unit)? = null
    var onUpdateUiConfigReplyReceived: (() -> Unit)? = null
    private var pollHandler: Handler? = null
    private val pollHandlerCallback = Handler.Callback {
        val readInstance = aapRead
        if (readInstance == null) {
            return@Callback false
        }

        val ret = readInstance.read()

        if (ret < 0) {
            AppLog.i("Quitting because ret < 0 ($ret)")
            this.quit(clean = (ret == -2))
            return@Callback true
        }

        pollHandler?.let {
            if (!it.hasMessages(MSG_POLL)) {
                it.sendEmptyMessage(MSG_POLL)
            }
        }

        return@Callback true
    }
    private var sendHandler: Handler? = null
    private val sendHandlerCallback = Handler.Callback {
        this.sendEncryptedMessage(
            data = it.obj as ByteArray,
            length = it.arg2
        )
        return@Callback true
    }

    val isAlive: Boolean
        get() = pollThread?.isAlive ?: false

    init {
        micRecorder.listener = this
        aapAudio = AapAudio(audioDecoder, audioManager, settings)
        aapVideo = AapVideo(videoDecoder, settings) {
            send(com.andrerinas.headunitrevived.aap.protocol.messages.VideoFocusEvent(gain = true, unsolicited = true))
        }
    }

    internal fun startSensor(type: Int) {
        startedSensors.add(type)
    }

    private fun sendEncryptedMessage(data: ByteArray, length: Int): Int {
        val ba =
            ssl.encrypt(AapMessage.HEADER_SIZE, length - AapMessage.HEADER_SIZE, data) ?: return -1

        ba.data[0] = data[0]
        ba.data[1] = data[1]
        Utils.intToBytes(ba.limit - AapMessage.HEADER_SIZE, 2, ba.data)

        val size = connection?.sendBlocking(ba.data, ba.limit, 250) ?: -1

        if (AppLog.LOG_VERBOSE) {
            AppLog.v("Sent size: %d", size)
            // AapDump.logvHex("US", 0, ba.data, ba.limit) // AapDump might be removed or changed
        }
        return 0
    }

    internal fun stop() {
        AppLog.i("AapTransport stopping and sending byebye")
        val byebye = Control.ByeByeRequest.newBuilder()
            .setReason(Control.ByeByeReason.USER_SELECTION)
            .build()
        val msg =
            AapMessage(Channel.ID_CTR, Control.ControlMsgType.MESSAGE_BYEBYE_REQUEST_VALUE, byebye)
        send(msg)
        SystemClock.sleep(150)
        quit()
    }

    internal fun quit(clean: Boolean = false) {
        val cb = onQuit ?: return
        onQuit = null

        AppLog.i("AapTransport quitting (clean=$clean)")
        cb.invoke(clean)
        micRecorder.listener = null
        pollThread?.quit()
        sendThread?.quit()
        aapAudio.releaseAllFocus()

        try {            // Don't join the poll thread from within itself — it would block for the full
            // timeout since the thread can't finish while it's waiting for itself to finish.
            if (Thread.currentThread() != pollThread) pollThread?.join(1000)
            sendThread?.join(1000)
        } catch (e: InterruptedException) {
            AppLog.e("Failed to join threads", e)
        }

        aapRead = null
        ssl.release()
        pollHandler = null
        sendHandler = null
        pollThread = null
        sendThread = null
    }

    /**
     * Phase 1 of startup: creates the send/poll threads and runs the SSL handshake.
     *
     * Returns `true` on success. On failure, threads are stopped via [quit] before returning.
     * Must be followed by [startReading] (called after the projection surface is ready)
     * to actually start the message loop.
     */
    internal fun startHandshake(connection: AccessoryConnection): Boolean {
        AppLog.i("Start Aap transport handshake for $connection")
        this.connection = connection
        wasUserExit = false

        sendThread = HandlerThread("AapTransport:Handler::Send", Process.THREAD_PRIORITY_AUDIO)
        sendThread!!.start()
        sendHandler = Handler(sendThread!!.looper, sendHandlerCallback)
        sendHandler?.post { com.andrerinas.headunitrevived.utils.LegacyOptimizer.setHighPriority() }

        pollThread = HandlerThread("AapTransport:Handler::Poll", Process.THREAD_PRIORITY_AUDIO)
        pollThread!!.start()
        pollHandler = Handler(pollThread!!.looper, pollHandlerCallback)
        pollHandler?.post { com.andrerinas.headunitrevived.utils.LegacyOptimizer.setHighPriority() }

        // No sleep needed here: Handler(thread.looper, ...) already blocks internally until the
        // HandlerThread's Looper is ready (via HandlerThread.getLooper() → wait/notifyAll).

        if (!handshake(connection)) {
            quit()
            AppLog.e("Handshake failed")
            return false
        }

        return true
    }

    /**
     * Phase 2 of startup: creates [AapRead] and posts the first [MSG_POLL] to begin the
     * inbound message loop.
     *
     * Must only be called after [startHandshake] has returned `true` **and** after the
     * projection surface has been set on the [VideoDecoder]. This guarantees that no video
     * frame is ever decoded before a render target exists.
     */
    internal fun startReading() {
        AppLog.i("Start Aap transport read loop")
        aapRead = AapRead.Factory.create(
            connection!!,
            this,
            micRecorder,
            aapAudio,
            aapVideo,
            settings,
            context,
            onAaMediaMetadata,
            onAaPlaybackStatus
        )
        pollHandler?.sendEmptyMessage(MSG_POLL)
    }

    private fun handshake(connection: AccessoryConnection): Boolean {
        try {
            // Increased delay for AA 16.4+ stability - skip for Nearby (single message)
            if (!connection.isSingleMessage) {
                SystemClock.sleep(500)
            }

            val buffer = ByteArray(Messages.DEF_BUFFER_LENGTH)

            // Drain any stale data left in the USB pipe from a previous session
            // Skip for Nearby (Socket) connections where every byte from the start is important.
            var drained = 0
            if (!connection.isSingleMessage) {
                while (true) {
                    val n = try { connection.recvBlocking(buffer, buffer.size, 50, false) } catch (e: Exception) { -1 }
                    if (n <= 0) break
                    drained += n
                }
                if (drained > 0) {
                    AppLog.i("Handshake: Drained $drained bytes of stale data before version request")
                }
            }

            AppLog.d("Handshake: Starting version request. TS: ${SystemClock.elapsedRealtime()}")
            val version = Messages.versionRequest
            var ret = -1
            var attempt = 0
            var received = false
            // Outer deadline prevents the loop from running for minutes on an unresponsive device.
            // Each send+recv pair uses 2 s per operation; 3 attempts × 4 s ≈ 12 s worst-case,
            // capped here at HANDSHAKE_TIMEOUT_MS so a stuck device fails fast.
            val versionDeadline = SystemClock.elapsedRealtime() + HANDSHAKE_TIMEOUT_MS
            while (attempt < 3 && connection.isConnected) {
                if (SystemClock.elapsedRealtime() >= versionDeadline) {
                    AppLog.e("Handshake: Version exchange timed out after $attempt attempt(s).")
                    return false
                }
                attempt++
                ret = connection.sendBlocking(version, version.size, 2000)
                AppLog.d("Handshake: Version request sent. ret: $ret. attempt: $attempt. TS: ${SystemClock.elapsedRealtime()}")
                if (ret < 0) {
                    AppLog.w("Handshake: Version request send failed (ret=$ret), attempt $attempt")
                    SystemClock.sleep(200)
                    continue
                }

                AppLog.d("Handshake: Waiting for version response. TS: ${SystemClock.elapsedRealtime()}")
                // Inner loop: drain messages until we see channel=0 type=2 (VERSION_RESPONSE).
                // On first connection the phone may send a proactive message (e.g. a ping or a
                // status) before the version response arrives. Accepting any non-empty read as
                // "version response received" would hand a random payload to the SSL layer and
                // cause a 15 s timeout. Instead, discard unexpected messages and keep reading
                // until the deadline expires.
                val recvDeadline = SystemClock.elapsedRealtime() + 2000
                while (SystemClock.elapsedRealtime() < recvDeadline) {
                    val remaining = (recvDeadline - SystemClock.elapsedRealtime())
                        .toInt().coerceAtLeast(100)
                    ret = connection.recvBlocking(buffer, buffer.size, remaining, false)
                    if (ret <= 0) break  // timeout or error — fall through to outer retry
                    if (ret >= 6
                        && buffer[0] == 0.toByte()
                        && buffer[4] == 0.toByte()
                        && buffer[5] == 2.toByte()) {
                        AppLog.i("Handshake: Version response received (ret=$ret, attempt=$attempt).")
                        received = true
                        break
                    }
                    // Wrong message — log and keep draining.
                    val ch   = buffer[0].toInt() and 0xFF
                    val type = ((buffer[4].toInt() and 0xFF) shl 8) or (buffer[5].toInt() and 0xFF)
                    AppLog.w("Handshake: Ignoring unexpected message " +
                             "(ch=$ch, type=0x${type.toString(16)}, len=$ret). " +
                             "Waiting for VERSION_RESPONSE.")
                }
                if (received) break
                AppLog.w("Handshake: No VERSION_RESPONSE within 2s (attempt $attempt), ret=$ret")
                SystemClock.sleep(200)
            }

            if (!received) {
                AppLog.e("Handshake: Version request/response failed after $attempt attempt(s). last ret: $ret")
                return false
            }
            AppLog.i("Handshake: Version response recv ret: %d", ret)

            AppLog.d("Handshake: Starting SSL handshake via performHandshake(). TS: ${SystemClock.elapsedRealtime()}")
            if (!ssl.performHandshake(connection)) {
                AppLog.e("Handshake: SSL performHandshake failed.")
                return false
            }

            ssl.postHandshakeReset()
            AppLog.d("Handshake: SSL buffers reset after handshake.")

            AppLog.d("Handshake: SSL handshake complete. TS: ${SystemClock.elapsedRealtime()}")
            // Status = OK
            val status = Messages.statusOk
            ret = connection.sendBlocking(status, status.size, 2000)
            AppLog.d("Handshake: Status OK sent. ret: $ret. TS: ${SystemClock.elapsedRealtime()}")
            if (ret < 0) {
                AppLog.e("Handshake: Status request sendEncrypted ret: $ret")
                return false
            }

            AppLog.i("Handshake: Status OK sent: %d", ret)
            AppLog.d("Handshake: Handshake successful. TS: ${SystemClock.elapsedRealtime()}")

            return true
        } catch (e: Exception) {
            AppLog.e("Handshake failed with exception", e)
            return false
        }
    }

    fun send(keyCode: Int, isPress: Boolean) {
        val mapped = keyCodes[keyCode] ?: keyCode
        val aapKeyCode = KeyCode.convert(mapped)

        if (mapped == KeyEvent.KEYCODE_GUIDE) {
            // Hack for navigation button to simulate touch
            val action = if (isPress)
                Input.TouchEvent.PointerAction.TOUCH_ACTION_DOWN else Input.TouchEvent.PointerAction.TOUCH_ACTION_UP
            this.send(TouchEvent(SystemClock.elapsedRealtime(), action, 99, 444))
            return
        }

        if (mapped == KeyEvent.KEYCODE_N) {
            val intent = Intent(AapService.ACTION_REQUEST_NIGHT_MODE_UPDATE)
            intent.setPackage(context.packageName)
            context.sendBroadcast(intent)
            return
        }

        if (aapKeyCode == KeyEvent.KEYCODE_UNKNOWN) {
            AppLog.i("Unknown: $keyCode")
        }

        val ts = SystemClock.elapsedRealtime()
        if (aapKeyCode == KeyEvent.KEYCODE_SOFT_LEFT || aapKeyCode == KeyEvent.KEYCODE_SOFT_RIGHT) {
            if (isPress) {
                val delta = if (aapKeyCode == KeyEvent.KEYCODE_SOFT_LEFT) -1 else 1
                send(ScrollWheelEvent(ts, delta))
            }
            return
        }

        send(KeyCodeEvent(ts, aapKeyCode, isPress))
    }

    fun send(sensor: SensorEvent): Boolean {
        return if (isAlive && startedSensors.contains(sensor.sensorType)) {
            send(sensor as AapMessage)
            true
        } else {
            if (!isAlive) {
                //AppLog.w("AapTransport not alive, ignoring sensor event for sensor ${sensor.sensorType}")
            } else {
                //AppLog.e("Sensor " + sensor.sensorType + " is not started yet")
            }
            false
        }
    }

    fun send(message: AapMessage) {
        val handler = sendHandler
        if (handler == null) {
            AppLog.i("Cannot send message, handler is null (quitting?)")
        } else {
            if (AppLog.LOG_VERBOSE) {
                AppLog.v(message.toString())
            }
            val msg = handler.obtainMessage(MSG_SEND, 0, message.size, message.data)
            handler.sendMessage(msg)
        }
    }

    internal fun gainVideoFocus() {
        context.sendBroadcast(ProjectionActivityRequest())
    }

    internal fun sendMediaAck(channel: Int) {
        send(MediaAck(channel, sessionIds.get(channel)))
    }

    internal fun setSessionId(channel: Int, sessionId: Int) {
        sessionIds.put(channel, sessionId)
    }

    override fun onMicDataAvailable(mic_buf: ByteArray, mic_audio_len: Int) {
        if (mic_audio_len > 64) {  // If we read at least 64 bytes of audio data
            val length = mic_audio_len + 10
            val data = ByteArray(length)
            data[0] = Channel.ID_MIC.toByte()
            data[1] = 0x0b
            Utils.put_time(2, data, SystemClock.elapsedRealtime())
            System.arraycopy(mic_buf, 0, data, 10, mic_audio_len)
            send(AapMessage(Channel.ID_MIC, 0x0b.toByte(), -1, 2, length, data))
        }
    }

    companion object {
        private const val MSG_POLL = 1
        private const val MSG_SEND = 2
        // Maximum wall-clock time allowed for the version-exchange phase of the AAP handshake.
        // Prevents the retry loop from blocking for minutes on an unresponsive USB device.
        private const val HANDSHAKE_TIMEOUT_MS = 10_000L
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/KeyCode.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import android.view.KeyEvent
import com.andrerinas.headunitrevived.aap.protocol.messages.ScrollWheelEvent // Not directly in supported list, but used in AapTransport
import com.andrerinas.headunitrevived.utils.AppLog

object KeyCode {

    val supported = listOf(
        // Standard Android KeyEvents used in the convert method or common
        KeyEvent.KEYCODE_SOFT_LEFT,
        KeyEvent.KEYCODE_SOFT_RIGHT,
        KeyEvent.KEYCODE_BACK,
        KeyEvent.KEYCODE_DPAD_UP,
        KeyEvent.KEYCODE_DPAD_DOWN,
        KeyEvent.KEYCODE_DPAD_LEFT,
        KeyEvent.KEYCODE_DPAD_RIGHT,
        KeyEvent.KEYCODE_DPAD_CENTER,
        KeyEvent.KEYCODE_MEDIA_PLAY,
        KeyEvent.KEYCODE_MEDIA_PAUSE,
        KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE,
        KeyEvent.KEYCODE_MEDIA_NEXT,
        KeyEvent.KEYCODE_MEDIA_PREVIOUS,
        KeyEvent.KEYCODE_SEARCH,
        KeyEvent.KEYCODE_CALL,
        KeyEvent.KEYCODE_MUSIC,
        KeyEvent.KEYCODE_VOLUME_UP,
        KeyEvent.KEYCODE_VOLUME_DOWN,
        KeyEvent.KEYCODE_TAB,
        KeyEvent.KEYCODE_SPACE,
        KeyEvent.KEYCODE_ENTER,
        KeyEvent.KEYCODE_HOME,
        KeyEvent.KEYCODE_HEADSETHOOK,
        KeyEvent.KEYCODE_MEDIA_STOP,

        // Additional keys explicitly listed by number in BuildCarConfig.java
        // Mapped to named constants where they exist in KeyEvent
        KeyEvent.KEYCODE_ENDCALL, // 6
        KeyEvent.KEYCODE_0, // 7
        KeyEvent.KEYCODE_1, // 8
        KeyEvent.KEYCODE_2, // 9
        KeyEvent.KEYCODE_3, // 10
        KeyEvent.KEYCODE_4, // 11
        KeyEvent.KEYCODE_5, // 12
        KeyEvent.KEYCODE_6, // 13
        KeyEvent.KEYCODE_7, // 14
        KeyEvent.KEYCODE_8, // 15
        KeyEvent.KEYCODE_9, // 16
        KeyEvent.KEYCODE_STAR, // 17
        KeyEvent.KEYCODE_POUND, // 18

        // Custom/Rotary codes from BuildCarConfig.java (no direct KeyEvent.KEYCODE_X mapping)
        1, // Appears to be a custom keycode
        2, // Appears to be a custom keycode
        81, // Appears to be a custom keycode or old BOOKMARK (actual BOOKMARK is 137)
        224, // KEYCODE_WAKEUP → Voice Command
        264, 265, 267, // STEM_PRIMARY, STEM_1, STEM_3 (steering wheel)
        268, 269, 270, 271, // Rotary controller
        65536, 65537, 65538 // Rotary controller
    ).distinct().sorted()

    val KeyEvent.isMediaSessionKey: Boolean
        get() = keyCode == KeyEvent.KEYCODE_MEDIA_PLAY ||
                keyCode == KeyEvent.KEYCODE_MEDIA_PAUSE ||
                keyCode == KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE ||
                keyCode == KeyEvent.KEYCODE_MEDIA_NEXT ||
                keyCode == KeyEvent.KEYCODE_MEDIA_PREVIOUS ||
                keyCode == KeyEvent.KEYCODE_MEDIA_STOP ||
                keyCode == KeyEvent.KEYCODE_MEDIA_FAST_FORWARD ||
                keyCode == KeyEvent.KEYCODE_MEDIA_REWIND

    internal fun convert(keyCode: Int): Int {
        // If it's in our supported list or a standard media key, pass it through.
        // We no longer force ENTER -> DPAD_CENTER here to allow users to map it specifically.
        if (supported.contains(keyCode) ||
            keyCode == KeyEvent.KEYCODE_MEDIA_FAST_FORWARD ||
            keyCode == KeyEvent.KEYCODE_MEDIA_REWIND ||
            keyCode == KeyEvent.KEYCODE_MUTE ||
            keyCode == KeyEvent.KEYCODE_VOLUME_MUTE) {
            return keyCode
        }

        // Return KEYCODE_UNKNOWN for anything else to avoid sending invalid codes to AA
        AppLog.w("KeyCode: Unknown or unsupported keycode $keyCode - returning KEYCODE_UNKNOWN")
        return KeyEvent.KEYCODE_UNKNOWN
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/Channel.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol

object Channel {

    const val ID_CTR = 0
    const val ID_SEN = 1
    const val ID_VID = 2
    const val ID_INP = 3
    const val ID_AUD = 6
    const val ID_AU1 = 4
    const val ID_AU2 = 5
    const val ID_MIC = 7
    const val ID_BTH = 8
    const val ID_MPB = 9
    const val ID_NAV = 10
    const val ID_NOT = 11
    const val ID_NOTI = 11
    const val ID_PHONE = 12
    const val ID_WIFI = 13

    fun name(channel: Int): String {
        when (channel) {
            ID_CTR -> return "CONTROL"
            ID_VID -> return "VIDEO"
            ID_INP -> return "INPUT"
            ID_SEN -> return "SENSOR"
            ID_MIC -> return "MIC"
            ID_AUD -> return "AUDIO"
            ID_AU1 -> return "AUDIO1"
            ID_AU2 -> return "AUDIO2"
            ID_BTH -> return "BLUETOOTH"
            ID_MPB -> return "MUSIC_PLAYBACK"
            ID_NAV -> return "NAVIGATION_DIRECTIONS"
            ID_NOTI -> return "NOTIFICATION"
            ID_PHONE -> return "PHONE_STATUS"
        }
        return "UNK"
    }

    fun isAudio(chan: Int): Boolean {
        return chan == Channel.ID_AUD || chan == Channel.ID_AU1 || chan == Channel.ID_AU2
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/messages/KeyCodeEvent.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol.messages

import com.andrerinas.headunitrevived.aap.AapMessage
import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.proto.Input
import com.google.protobuf.Message

class KeyCodeEvent(timeStamp: Long, keycode: Int, isPress: Boolean)
    : AapMessage(Channel.ID_INP, Input.MsgType.EVENT_VALUE, makeProto(timeStamp, keycode, isPress)) {

    companion object {
        private fun makeProto(timeStamp: Long, keycode: Int, isPress: Boolean): Message {
            return Input.InputReport.newBuilder().also {
                it.timestamp = timeStamp * 1000000L
                it.keyEvent = Input.KeyEvent.newBuilder().apply {
                    addKeys(Input.Key.newBuilder().also { key ->
                        key.keycode = keycode
                        key.down = isPress
                        key.longpress = false
                        key.metastate = 0
                    })
                }.build()
            }.build()
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/messages/NightModeEvent.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol.messages

import com.andrerinas.headunitrevived.aap.protocol.proto.Sensors
import com.google.protobuf.Message



class NightModeEvent(enabled: Boolean)
    : SensorEvent(Sensors.SensorType.NIGHT_VALUE, makeProto(enabled)) {

    companion object {
        private fun makeProto(enabled: Boolean): Message {
            return Sensors.SensorBatch.newBuilder().also {
                it.addNightMode(
                        Sensors.SensorBatch.NightData.newBuilder().apply {
                            isNightMode = enabled
                        }
                )
            }.build()
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/messages/TouchEvent.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol.messages

import android.view.MotionEvent
import com.andrerinas.headunitrevived.aap.AapMessage
import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.proto.Input
import com.google.protobuf.Message

class TouchEvent(timeStamp: Long, action: Input.TouchEvent.PointerAction, actionIndex: Int, pointerData: Iterable<Triple<Int, Int, Int>>)
    : AapMessage(Channel.ID_INP, Input.MsgType.EVENT_VALUE, makeProto(timeStamp, action, actionIndex, pointerData)) {

    constructor(timeStamp: Long, action: Int, actionIndex: Int, pointerData: Iterable<Triple<Int, Int, Int>>)
        : this(timeStamp, Input.TouchEvent.PointerAction.forNumber(action), actionIndex, pointerData)

    constructor(timeStamp: Long, action: Input.TouchEvent.PointerAction, x: Int, y: Int)
        : this(timeStamp, action, 0, listOf(Triple(0, x, y)))

    companion object {
        fun motionEventToAction(event: MotionEvent): Input.TouchEvent.PointerAction? {
            return when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> Input.TouchEvent.PointerAction.TOUCH_ACTION_DOWN
                MotionEvent.ACTION_POINTER_DOWN -> Input.TouchEvent.PointerAction.TOUCH_ACTION_POINTER_DOWN
                MotionEvent.ACTION_MOVE -> Input.TouchEvent.PointerAction.TOUCH_ACTION_MOVE
                MotionEvent.ACTION_UP -> Input.TouchEvent.PointerAction.TOUCH_ACTION_UP
                MotionEvent.ACTION_POINTER_UP -> Input.TouchEvent.PointerAction.TOUCH_ACTION_POINTER_UP
                MotionEvent.ACTION_CANCEL -> Input.TouchEvent.PointerAction.TOUCH_ACTION_CANCEL
                MotionEvent.ACTION_OUTSIDE -> Input.TouchEvent.PointerAction.TOUCH_ACTION_OUTSIDE
                else -> null
            }
        }

        private fun makeProto(timeStamp: Long, action: Input.TouchEvent.PointerAction, actionIndex: Int, pointerData: Iterable<Triple<Int, Int, Int>>): Message {
            val touchEvent = Input.TouchEvent.newBuilder()
                    .also {
                        pointerData.forEach { data ->
                            it.addPointerData(
                                    Input.TouchEvent.Pointer.newBuilder().also { pointer ->
                                        pointer.pointerId = data.first
                                        pointer.x = data.second
                                        pointer.y = data.third
                                    })
                        }
                        it.actionIndex = actionIndex
                        it.action = action
                    }

            return Input.InputReport.newBuilder()
                    .setTimestamp(timeStamp * 1000000L)
                    .setTouchEvent(touchEvent).build()
        }
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/messages/UpdateUiConfigRequest.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol.messages

import com.andrerinas.headunitrevived.aap.AapMessage
import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.proto.Media
import com.google.protobuf.Message

/**
 * Message sent on the Video channel (Channel 3) to update the headunit's UI config
 * (margins, insets, theme) without a full session reconnect.
 */
class UpdateUiConfigRequest(
    left: Int, top: Int, right: Int, bottom: Int
) : AapMessage(
    Channel.ID_VID,
    Media.MsgType.MEDIA_MESSAGE_UPDATE_UI_CONFIG_REQUEST_VALUE,
    makeProto(left, top, right, bottom)
) {

    companion object {
        private fun makeProto(left: Int, top: Int, right: Int, bottom: Int): Message {
            // Protocol-correct: UiConfig with ONLY margins set.
            // No content_insets, no stable_content_insets, no ui_theme.
            val insets = Media.Insets.newBuilder()
                .setLeft(left)
                .setTop(top)
                .setRight(right)
                .setBottom(bottom)
                .build()

            val uiConfig = Media.UiConfig.newBuilder()
                .setMargins(insets)
                .build()

            return Media.UpdateUiConfigRequest.newBuilder()
                .setUiConfig(uiConfig)
                .build()
        }
    }
}


```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/messages/ScrollWheelEvent.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol.messages

import com.andrerinas.headunitrevived.aap.AapMessage
import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.proto.Input
import com.google.protobuf.Message



class ScrollWheelEvent(timeStamp: Long, delta: Int)
    : AapMessage(Channel.ID_INP, Input.MsgType.EVENT_VALUE, makeProto(timeStamp, delta)) {
    companion object {
        const val KEYCODE_SCROLL_WHEEL = 65536

        private fun makeProto(timeStamp: Long, delta: Int): Message {

            return Input.InputReport.newBuilder().also {
                it.timestamp = timeStamp * 1000000L
                it.keyEvent = Input.KeyEvent.newBuilder().build() // TODO: check if requred
                it.relativeEvent = Input.RelativeEvent.newBuilder().also { event ->
                    event.addData(Input.RelativeEvent_Rel.newBuilder().apply {
                        setDelta(delta)
                        keycode = KEYCODE_SCROLL_WHEEL
                    })
                }.build()
            }.build()

        }
    }

}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/messages/Messages.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol.messages

import com.andrerinas.headunitrevived.aap.Utils

object Messages {
    const val DEF_BUFFER_LENGTH = 131080

    val versionRequest: ByteArray
        get() = createRawMessage(0, 3, 1, VERSION_REQUEST, VERSION_REQUEST.size)

    // byte ac_buf [] = {0, 3, 0, 4, 0, 4, 8, 0};
    val statusOk: ByteArray
        get() = createRawMessage(0, 3, 4, byteArrayOf(8, 0), 2)

    fun createRawMessage(chan: Int, flags: Int, type: Int, data: ByteArray): ByteArray =
            createRawMessage(chan, flags, type, data, data.size)

    private var VERSION_REQUEST = byteArrayOf(0, 2, 0, 0)

    private fun createRawMessage(chan: Int, flags: Int, type: Int, data: ByteArray, size: Int): ByteArray {

        val total = 6 + size
        val buffer = ByteArray(total)

        buffer[0] = chan.toByte()
        buffer[1] = flags.toByte()
        Utils.intToBytes(size + 2, 2, buffer)
        Utils.intToBytes(type, 4, buffer)

        System.arraycopy(data, 0, buffer, 6, size)
        return buffer
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/messages/ServiceDiscoveryResponse.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol.messages

import android.content.Context
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.aap.AapMessage
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.aap.KeyCode
import com.andrerinas.headunitrevived.aap.protocol.AudioConfigs
import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.proto.Control
import com.andrerinas.headunitrevived.aap.protocol.proto.Media
import com.andrerinas.headunitrevived.aap.protocol.proto.Sensors
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.HeadUnitScreenConfig
import com.google.protobuf.Message

class ServiceDiscoveryResponse(private val context: Context)
    : AapMessage(Channel.ID_CTR, Control.ControlMsgType.MESSAGE_SERVICE_DISCOVERY_RESPONSE_VALUE, makeProto(context)) {

    companion object {
        private fun makeProto(context: Context): Message {
            val settings = App.provide(context).settings

            // Initialize HeadUnitScreenConfig with actual physical screen dimensions
            HeadUnitScreenConfig.init(context, context.resources.displayMetrics, settings)

            val services = mutableListOf<Control.Service>()

            val sensors = Control.Service.newBuilder().also { service ->
                service.id = Channel.ID_SEN
                service.sensorSourceService = Control.Service.SensorSourceService.newBuilder().also { sources ->
                    sources.addSensors(makeSensorType(Sensors.SensorType.DRIVING_STATUS))
                    if (settings.useGpsForNavigation) {
                        sources.addSensors(makeSensorType(Sensors.SensorType.LOCATION))
                    }

                    // Always announce Night sensor, as we control it via NightModeManager
                    sources.addSensors(makeSensorType(Sensors.SensorType.NIGHT))
                    AppLog.i("[ServiceDiscovery] Announcing NIGHT sensor support. Strategy: ${settings.nightMode}")

                }.build()
            }.build()

            services.add(sensors)

            val video = Control.Service.newBuilder().also { service ->
                service.id = Channel.ID_VID
                service.mediaSinkService = Control.Service.MediaSinkService.newBuilder().also { mediaSinkServiceBuilder ->
                    val codecToRequest = when (settings.videoCodec) {
                        "H.265" -> Media.MediaCodecType.MEDIA_CODEC_VIDEO_H265
                        "Auto" -> if (com.andrerinas.headunitrevived.decoder.VideoDecoder.isHevcSupported()) {
                            Media.MediaCodecType.MEDIA_CODEC_VIDEO_H265
                        } else {
                            Media.MediaCodecType.MEDIA_CODEC_VIDEO_H264_BP
                        }
                        else -> Media.MediaCodecType.MEDIA_CODEC_VIDEO_H264_BP
                    }

                    // Use HeadUnitScreenConfig for negotiated resolution and margins
                    val negotiatedResolution = HeadUnitScreenConfig.negotiatedResolutionType
                    val phoneWidthMargin = HeadUnitScreenConfig.getWidthMargin()
                    val phoneHeightMargin = HeadUnitScreenConfig.getHeightMargin()

                    // Enforce H.265 for 1440p resolution as required by Android Auto
                    val effectiveCodec = if (negotiatedResolution == Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._2560x1440 ||
                        negotiatedResolution == Control.Service.MediaSinkService.VideoConfiguration.VideoCodecResolutionType._1440x2560) {
                        AppLog.i("Resolution is 1440p -> Enforcing H.265 codec")
                        Media.MediaCodecType.MEDIA_CODEC_VIDEO_H265
                    } else {
                        codecToRequest
                    }

                    mediaSinkServiceBuilder.availableType = effectiveCodec
                    mediaSinkServiceBuilder.audioType = Media.AudioStreamType.NONE
                    mediaSinkServiceBuilder.availableWhileInCall = true

                    AppLog.i("[ServiceDiscovery] NegotiatedResolution is: ${HeadUnitScreenConfig.getNegotiatedWidth()}x${HeadUnitScreenConfig.getNegotiatedHeight()}")
                    AppLog.i("[ServiceDiscovery] Margins are: ${phoneWidthMargin}x${phoneHeightMargin}")

                    mediaSinkServiceBuilder.addVideoConfigs(Control.Service.MediaSinkService.VideoConfiguration.newBuilder().apply {
                        codecResolution = negotiatedResolution
                        frameRate = when (settings.fpsLimit) {
                            30 -> Control.Service.MediaSinkService.VideoConfiguration.VideoFrameRateType._30
                            else -> Control.Service.MediaSinkService.VideoConfiguration.VideoFrameRateType._60
                        }
                        setDensity(HeadUnitScreenConfig.getDensityDpi()) // Use actual densityDpi
                        setMarginWidth(phoneWidthMargin)
                        setMarginHeight(phoneHeightMargin)
                        setVideoCodecType(effectiveCodec)
                    }.build())
                }.build()
            }.build()

            services.add(video)

            val input = Control.Service.newBuilder().also { service ->
                service.id = Channel.ID_INP
                service.inputSourceService = Control.Service.InputSourceService.newBuilder().also {
                    it.touchscreen = Control.Service.InputSourceService.TouchConfig.newBuilder().apply {
                        setWidth(HeadUnitScreenConfig.getNegotiatedWidth()) // Use negotiated width
                        setHeight(HeadUnitScreenConfig.getNegotiatedHeight()) // Use negotiated height
                    }.build()

                    if (settings.enableRotary) {
                        AppLog.i("[ServiceDiscovery] Announcing Rotary/Touchpad support")
                        it.touchpad = Control.Service.InputSourceService.TouchConfig.newBuilder().apply {
                            setWidth(HeadUnitScreenConfig.getNegotiatedWidth())
                            setHeight(HeadUnitScreenConfig.getNegotiatedHeight())
                        }.build()
                    }

                    it.addAllKeycodesSupported(KeyCode.supported)
                }.build()
            }.build()

            services.add(input)

            val audioType = if (settings.useAacAudio) Media.MediaCodecType.MEDIA_CODEC_AUDIO_AAC_LC else Media.MediaCodecType.MEDIA_CODEC_AUDIO_PCM

            // Always add Audio2 (System Sounds) to keep connection alive
            val audio2 = Control.Service.newBuilder().also { service ->
                service.id = Channel.ID_AU2
                service.mediaSinkService = Control.Service.MediaSinkService.newBuilder().also {
                    it.availableType = audioType
                    it.audioType = Media.AudioStreamType.SYSTEM
                    it.addAudioConfigs(AudioConfigs.get(Channel.ID_AU2))
                }.build()
            }.build()
            services.add(audio2)

            if (settings.enableAudioSink) {
                if (!AapService.selfMode) {
                    val audio1 = Control.Service.newBuilder().also { service ->
                        service.id = Channel.ID_AU1
                        service.mediaSinkService = Control.Service.MediaSinkService.newBuilder().also {
                            it.availableType = audioType
                            it.audioType = Media.AudioStreamType.SPEECH
                            it.addAudioConfigs(AudioConfigs.get(Channel.ID_AU1))
                        }.build()
                    }.build()
                    services.add(audio1)
                }

                if (!AapService.selfMode) {
                    val audio0 = Control.Service.newBuilder().also { service ->
                        service.id = Channel.ID_AUD
                        service.mediaSinkService = Control.Service.MediaSinkService.newBuilder().also {
                            it.availableType = audioType
                            it.audioType = Media.AudioStreamType.MEDIA
                            it.addAudioConfigs(AudioConfigs.get(Channel.ID_AUD))
                        }.build()
                    }.build()
                    services.add(audio0)
                }
            }

            // Microphone Service (Channel 7) - Always required for AA connection (Assistant)
            val mic = Control.Service.newBuilder().also { service ->
                service.id = Channel.ID_MIC
                service.mediaSourceService = Control.Service.MediaSourceService.newBuilder().also {
                    it.type = Media.MediaCodecType.MEDIA_CODEC_AUDIO_PCM
                    it.audioConfig = Media.AudioConfiguration.newBuilder().apply {
                        sampleRate = 16000
                        numberOfBits = 16
                        numberOfChannels = 1
                    }.build()
                }.build()
            }.build()
            services.add(mic)

            // Bluetooth Service
            if (settings.bluetoothAddress.isNotEmpty()) {
                val bluetooth = Control.Service.newBuilder().also { service ->
                    service.id = Channel.ID_BTH
                    service.bluetoothService = Control.Service.BluetoothService.newBuilder().also {
                        it.carAddress = settings.bluetoothAddress
                        it.addAllSupportedPairingMethods(
                                listOf(Control.BluetoothPairingMethod.A2DP,
                                        Control.BluetoothPairingMethod.HFP)
                        )
                    }.build()
                }.build()
                services.add(bluetooth)
            } else {
                AppLog.i("BT MAC Address is empty. Skip bluetooth service")
            }

            val mediaPlaybackStatus = Control.Service.newBuilder().also { service ->
                service.id = Channel.ID_MPB
                service.mediaPlaybackService = Control.Service.MediaPlaybackStatusService.newBuilder().build()
            }.build()
            services.add(mediaPlaybackStatus)

            // Navigation Status Service — head unit receives turn-by-turn data from any AA nav app
            val navigationStatus = Control.Service.newBuilder().also { service ->
                service.id = Channel.ID_NAV
                service.navigationStatusService = Control.Service.NavigationStatusService.newBuilder()
                    .setMinimumIntervalMs(1000)
                    .setType(Control.Service.NavigationStatusService.ClusterType.ImageCodesOnly)
                    .build()
            }.build()
            services.add(navigationStatus)

            return Control.ServiceDiscoveryResponse.newBuilder().apply {
                make = settings.vehicleMake
                model = settings.vehicleModel
                year = settings.vehicleYear
                vehicleId = settings.vehicleId
                headUnitModel = settings.headUnitModel
                headUnitMake = settings.headUnitMake
                headUnitSoftwareBuild = "1"
                headUnitSoftwareVersion = "0.1.0"
                driverPosition = if (settings.rightHandDrive) Control.DriverPosition.DRIVER_POSITION_RIGHT else Control.DriverPosition.DRIVER_POSITION_LEFT
                canPlayNativeMediaDuringVr = false
                hideProjectedClock = false
                setDisplayName(settings.vehicleDisplayName)

                setHeadunitInfo(com.andrerinas.headunitrevived.aap.protocol.proto.Common.HeadUnitInfo.newBuilder().apply {
                    setHeadUnitMake(settings.headUnitMake)
                    setHeadUnitModel(settings.headUnitModel)
                    setMake(settings.vehicleMake)
                    setModel(settings.vehicleModel)
                    setYear(settings.vehicleYear)
                    setVehicleId(settings.vehicleId)
                    setHeadUnitSoftwareBuild("1")
                    setHeadUnitSoftwareVersion("0.1.0")
                }.build())

                addAllServices(services)
            }.build()
        }

        private fun makeSensorType(type: Sensors.SensorType): Control.Service.SensorSourceService.Sensor {
            return Control.Service.SensorSourceService.Sensor.newBuilder()
                    .setType(type).build()
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/messages/LocationUpdateEvent.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol.messages

import android.location.Location
import com.andrerinas.headunitrevived.aap.protocol.proto.Sensors
import com.google.protobuf.Message

class LocationUpdateEvent(location: Location)
    : SensorEvent(Sensors.SensorType.LOCATION_VALUE, makeProto(location)) {

    companion object {
        private fun makeProto(location: Location): Message {
            return Sensors.SensorBatch.newBuilder().also {
                it.addLocationData(
                        Sensors.SensorBatch.LocationData.newBuilder().apply {
                            timestamp = location.time
                            latitude = (location.latitude * 1E7).toInt()
                            longitude = (location.longitude * 1E7).toInt()
                            altitude = (location.altitude * 1E2).toInt()
                            bearing = (location.bearing * 1E6).toInt()
                            // AA expects speed in mm/s (m/s * 1000)
                            speed = (location.speed * 1E3).toInt()
                            accuracy = (location.accuracy * 1E3).toInt()
                        }
                )
            }.build()
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/messages/VideoFocusEvent.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol.messages

import com.andrerinas.headunitrevived.aap.AapMessage
import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.proto.Media
import com.google.protobuf.Message



class VideoFocusEvent(gain: Boolean, unsolicited: Boolean)
    : AapMessage(Channel.ID_VID, Media.MsgType.MEDIA_MESSAGE_VIDEO_FOCUS_NOTIFICATION_VALUE, makeProto(gain, unsolicited)) {

    companion object {
        private fun makeProto(gain: Boolean, unsolicited: Boolean): Message {
            return Media.VideoFocusNotification.newBuilder().apply {
                mode = if (gain) Media.VideoFocusMode.VIDEO_FOCUS_PROJECTED else Media.VideoFocusMode.VIDEO_FOCUS_NATIVE
                this.unsolicited = unsolicited
            }.build()
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/messages/SensorEvent.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol.messages

import com.andrerinas.headunitrevived.aap.AapMessage
import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.proto.Sensors
import com.google.protobuf.Message

open class SensorEvent(val sensorType: Int, proto: Message)
    : AapMessage(Channel.ID_SEN, Sensors.SensorsMsgType.SENSOR_EVENT_VALUE, proto)

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/messages/MediaAck.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol.messages

import com.andrerinas.headunitrevived.aap.AapMessage
import com.andrerinas.headunitrevived.aap.protocol.proto.Media
import com.google.protobuf.Message

class MediaAck(channel: Int, sessionId: Int)
    : AapMessage(channel, Media.MsgType.MEDIA_MESSAGE_ACK_VALUE, makeProto(sessionId)) {

    companion object {
        private fun makeProto(sessionId: Int): Message {
            return Media.Ack.newBuilder().apply {
                this.sessionId = sessionId
                this.ack = 1
            }.build()
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/messages/DrivingStatusEvent.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol.messages

import com.andrerinas.headunitrevived.aap.AapMessage
import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.proto.Sensors
import com.google.protobuf.Message

class DrivingStatusEvent(status: Sensors.SensorBatch.DrivingStatusData.Status)
    : AapMessage(Channel.ID_SEN, Sensors.SensorsMsgType.SENSOR_EVENT_VALUE, makeProto(status)) {

    companion object {
        private fun makeProto(status: Sensors.SensorBatch.DrivingStatusData.Status): Message {
            return Sensors.SensorBatch.newBuilder()
                    .addDrivingStatus(Sensors.SensorBatch.DrivingStatusData.newBuilder()
                            .setStatus(status.number))
                    .build()
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/AudioConfigs.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol

import android.media.AudioManager
import android.util.SparseArray
import com.andrerinas.headunitrevived.aap.protocol.proto.Media

import com.andrerinas.headunitrevived.decoder.AudioDecoder

object AudioConfigs {
    private val audioTracks = SparseArray<Media.AudioConfiguration>(3)

    fun stream(channel: Int) : Int
    {
        return when(channel) {
            Channel.ID_AUD -> AudioManager.STREAM_MUSIC
            Channel.ID_AU1 -> AudioManager.STREAM_VOICE_CALL
            Channel.ID_AU2 -> AudioManager.STREAM_NOTIFICATION
            else -> AudioManager.STREAM_MUSIC
        }
    }

    fun get(channel: Int): Media.AudioConfiguration {
        return audioTracks.get(channel)
    }

    init {
        val audioConfig0 = Media.AudioConfiguration.newBuilder().apply {
            sampleRate = AudioDecoder.SAMPLE_RATE_HZ_48
            numberOfBits = 16
            numberOfChannels = 2
        }.build()
        audioTracks.put(Channel.ID_AUD, audioConfig0)

        val audioConfig1 = Media.AudioConfiguration.newBuilder().apply {
            sampleRate = AudioDecoder.SAMPLE_RATE_HZ_16
            numberOfBits = 16
            numberOfChannels = 1
        }.build()
        audioTracks.put(Channel.ID_AU1, audioConfig1)

        val audioConfig2 = Media.AudioConfiguration.newBuilder().apply {
            sampleRate = AudioDecoder.SAMPLE_RATE_HZ_16
            numberOfBits = 16
            numberOfChannels = 1
        }.build()
        audioTracks.put(Channel.ID_AU2, audioConfig2)
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/protocol/MsgType.kt`:

```kt
package com.andrerinas.headunitrevived.aap.protocol

import com.andrerinas.headunitrevived.aap.protocol.proto.Control
import com.andrerinas.headunitrevived.aap.protocol.proto.Media

object MsgType {

    const val SIZE = 2

    fun isControl(type: Int): Boolean {
        return type >= 1 && type <= 26
    }

    fun name(type: Int, channel: Int): String {

        when (type) {
            Control.ControlMsgType.MESSAGE_VERSION_REQUEST_VALUE -> return "Version Request"
            Control.ControlMsgType.MESSAGE_VERSION_RESPONSE_VALUE -> return "Version Response"
            Control.ControlMsgType.MESSAGE_ENCAPSULATED_SSL_VALUE -> return "SSL Handshake Data"
            Control.ControlMsgType.MESSAGE_AUTH_COMPLETE_VALUE -> return "SSL Authentication Complete Notification"
            Control.ControlMsgType.MESSAGE_SERVICE_DISCOVERY_REQUEST_VALUE -> return "Service Discovery Request"
            Control.ControlMsgType.MESSAGE_SERVICE_DISCOVERY_RESPONSE_VALUE -> return "Service Discovery Response"
            Control.ControlMsgType.MESSAGE_CHANNEL_OPEN_REQUEST_VALUE -> return "Channel Open Request"
            Control.ControlMsgType.MESSAGE_CHANNEL_OPEN_RESPONSE_VALUE -> return "Channel Open Response"
            Control.ControlMsgType.MESSAGE_CHANNEL_CLOSE_NOTIFICATION_VALUE -> return "Channel Close Notification"
            Control.ControlMsgType.MESSAGE_PING_REQUEST_VALUE -> return "Ping Request"
            Control.ControlMsgType.MESSAGE_PING_RESPONSE_VALUE -> return "Ping Response"
            Control.ControlMsgType.MESSAGE_NAV_FOCUS_REQUEST_VALUE -> return "Navigation Focus Request"
            Control.ControlMsgType.MESSAGE_NAV_FOCUS_NOTIFICATION_VALUE -> return "Navigation Focus Notification"
            Control.ControlMsgType.MESSAGE_BYEBYE_REQUEST_VALUE -> return "Byebye Request"
            Control.ControlMsgType.MESSAGE_BYEBYE_RESPONSE_VALUE -> return "Byebye Response"
            Control.ControlMsgType.MESSAGE_VOICE_SESSION_NOTIFICATION_VALUE -> return "Voice Session Notification"
            Control.ControlMsgType.MESSAGE_AUDIO_FOCUS_REQUEST_VALUE -> return "Audio Focus Request"
            Control.ControlMsgType.MESSAGE_AUDIO_FOCUS_NOTIFICATION_VALUE -> return "Audio Focus Notification"
            Control.ControlMsgType.MESSAGE_CAR_CONNECTED_DEVICES_REQUEST_VALUE -> return "Car Connected Devices Request"
            Control.ControlMsgType.MESSAGE_CAR_CONNECTED_DEVICES_RESPONSE_VALUE -> return "Car Connected Devices Response"
            Control.ControlMsgType.MESSAGE_USER_SWITCH_REQUEST_VALUE -> return "User Switch Request"
            Control.ControlMsgType.MESSAGE_BATTERY_STATUS_NOTIFICATION_VALUE -> return "Battery Status Notification"
            Control.ControlMsgType.MESSAGE_CALL_AVAILABILITY_STATUS_VALUE -> return "Call Availability Status"
            Control.ControlMsgType.MESSAGE_USER_SWITCH_RESPONSE_VALUE -> return "User Switch Response"
            Control.ControlMsgType.MESSAGE_SERVICE_DISCOVERY_UPDATE_VALUE -> return "Service Discovery Update"

            Media.MsgType.MEDIA_MESSAGE_DATA_VALUE -> return "Media Data"
            Media.MsgType.MEDIA_MESSAGE_CODEC_CONFIG_VALUE -> return "Codec Config"
            Media.MsgType.MEDIA_MESSAGE_SETUP_VALUE -> return "Media Setup Request"
            Media.MsgType.MEDIA_MESSAGE_START_VALUE -> {
                return when (channel) {
                    Channel.ID_SEN -> "Sensor Start Request"
                    Channel.ID_INP -> "Input Event"
                    Channel.ID_MPB -> "Media Playback Status"
                    else -> "Media Start Request"
                }
            }
            Media.MsgType.MEDIA_MESSAGE_STOP_VALUE -> {
                return when (channel) {
                    Channel.ID_SEN -> "Sensor Start Response"
                    Channel.ID_INP -> "Input Binding Request"
                    Channel.ID_MPB -> "Media Playback Status"
                    else -> "Media Stop Request"
                }
            }
            Media.MsgType.MEDIA_MESSAGE_CONFIG_VALUE -> {
                return when (channel) {
                    Channel.ID_SEN -> "Sensor Event"
                    Channel.ID_INP -> "Input Binding Response"
                    Channel.ID_MPB -> "Media Playback Status"
                    else -> "Media Config Response"
                }
            }
            Media.MsgType.MEDIA_MESSAGE_ACK_VALUE -> return "Codec/Media Data Ack"
            Media.MsgType.MEDIA_MESSAGE_MICROPHONE_REQUEST_VALUE -> return "Mic Start/Stop Request"
            Media.MsgType.MEDIA_MESSAGE_MICROPHONE_RESPONSE_VALUE -> return "Mic Response"
            Media.MsgType.MEDIA_MESSAGE_VIDEO_FOCUS_REQUEST_VALUE -> return "Video Focus Request"
            Media.MsgType.MEDIA_MESSAGE_VIDEO_FOCUS_NOTIFICATION_VALUE -> return "Video Focus Notification"
            Media.MsgType.MEDIA_MESSAGE_UPDATE_UI_CONFIG_REQUEST_VALUE -> return "Update UI Config Request"
            Media.MsgType.MEDIA_MESSAGE_UPDATE_UI_CONFIG_REPLY_VALUE -> return "Update UI Config Reply"
            Media.MsgType.MEDIA_MESSAGE_AUDIO_UNDERFLOW_NOTIFICATION_VALUE -> return "Audio Underflow Notification"

            Control.ControlMsgType.MESSAGE_UNEXPECTED_MESSAGE_VALUE -> return "Unexpected Message"
            Control.ControlMsgType.MESSAGE_FRAMING_ERROR_VALUE -> return "Framing Error Notification"
        }
        return "Unknown ($type)"
    }

}
```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapSslContext.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import com.andrerinas.headunitrevived.aap.protocol.messages.Messages
import com.andrerinas.headunitrevived.connection.AccessoryConnection
import com.andrerinas.headunitrevived.ssl.ConscryptInitializer
import com.andrerinas.headunitrevived.ssl.NoCheckTrustManager
import com.andrerinas.headunitrevived.ssl.SingleKeyKeyManager
import com.andrerinas.headunitrevived.utils.AppLog
import java.nio.ByteBuffer
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLEngine
import javax.net.ssl.SSLEngineResult

class AapSslContext(keyManager: SingleKeyKeyManager): AapSsl {
    private val sslContext: SSLContext = createSslContext(keyManager)
    private lateinit var sslEngine: SSLEngine
    private lateinit var txBuffer: ByteBuffer
    private lateinit var rxBuffer: ByteBuffer

    @Volatile var isUserDisconnect = false

    override fun performHandshake(connection: AccessoryConnection): Boolean {
        if (prepare() < 0) return false

        // Buffer for unencrypted TLS records extracted from AAP messages.
        // We use a local queue or buffer to keep track of bytes ready for the SSLEngine.
        var pendingTlsData = ByteArray(0)

        // Hard cap on the entire SSL phase.
        val deadline = android.os.SystemClock.elapsedRealtime() + SSL_HANDSHAKE_TIMEOUT_MS

        while (getHandshakeStatus() != SSLEngineResult.HandshakeStatus.FINISHED &&
                getHandshakeStatus() != SSLEngineResult.HandshakeStatus.NOT_HANDSHAKING) {

            if (android.os.SystemClock.elapsedRealtime() >= deadline) {
                AppLog.e("SSL Handshake: Timed out after ${SSL_HANDSHAKE_TIMEOUT_MS}ms")
                return false
            }

            when (getHandshakeStatus()) {
                SSLEngineResult.HandshakeStatus.NEED_UNWRAP -> {
                    // If we don't have enough data for a meaningful unwrap, read a full AAP message
                    if (pendingTlsData.isEmpty()) {
                        val messageData = readAapMessage(connection) ?: return false
                        pendingTlsData = messageData
                    }

                    rxBuffer.clear()
                    val data = ByteBuffer.wrap(pendingTlsData)
                    val result = sslEngine.unwrap(data, rxBuffer)
                    runDelegatedTasks(result, sslEngine)

                    when (result.status) {
                        SSLEngineResult.Status.OK -> {
                            // Keep any unconsumed bytes (e.g. next TLS record already in the buffer).
                            pendingTlsData = if (data.hasRemaining())
                                ByteArray(data.remaining()).also { data.get(it) }
                            else ByteArray(0)
                        }
                        SSLEngineResult.Status.BUFFER_UNDERFLOW -> {
                            // The current pendingTlsData doesn't contain a full TLS record.
                            // Read another AAP message and append it.
                            val nextMessage = readAapMessage(connection) ?: return false
                            pendingTlsData += nextMessage
                            AppLog.d("SSL Handshake: buffered ${pendingTlsData.size} B after underflow")
                        }
                        else -> {
                            AppLog.e("SSL Handshake: unwrap failed with status ${result.status}")
                            return false
                        }
                    }
                }

                SSLEngineResult.HandshakeStatus.NEED_WRAP -> {
                    val handshakeData = handshakeRead()
                    val bio = Messages.createRawMessage(0, 3, 3, handshakeData)
                    if (connection.sendBlocking(bio, bio.size, 2000) < 0) {
                        AppLog.e("SSL Handshake: Send failed")
                        return false
                    }
                }

                SSLEngineResult.HandshakeStatus.NEED_TASK -> {
                    runDelegatedTasks()
                }

                else -> {
                    AppLog.e("SSL Handshake: Unexpected status ${getHandshakeStatus()}")
                    return false
                }
            }
        }

        val sessionId = sslEngine.session?.id
        if (sessionId != null && sessionId.isNotEmpty()) {
            AppLog.i("SSL handshake complete. Session id: ${android.util.Base64.encodeToString(sessionId, android.util.Base64.NO_WRAP)}")
        } else {
            AppLog.i("SSL handshake complete. No session id (full handshake).")
        }
        return true
    }

    /**
     * Reads a single complete AAP message from the connection.
     * This ensures that we always respect AAP framing boundaries.
     */
    private fun readAapMessage(connection: AccessoryConnection): ByteArray? {
        val header = ByteArray(6)
        // Read exactly 6 bytes for the AAP header
        if (connection.recvBlocking(header, 6, 2000, true) != 6) {
            AppLog.e("SSL Handshake: Failed to read AAP header")
            return null
        }

        // AAP Header: [0]=Channel, [1]=Flags, [2..3]=Length (Big Endian), [4..5]=Type
        // The length in the header includes the 4 bytes of channel/flags/length itself?
        // No, in Messages.kt: size + 2 is stored in bytes 2..3.
        // So payload length = (header[2]*256 + header[3]) - 2.
        val totalLength = ((header[2].toInt() and 0xFF) shl 8) or (header[3].toInt() and 0xFF)
        val payloadLength = totalLength - 2 // Minus the 2 bytes for the type field (bytes 4-5)

        if (payloadLength < 0 || payloadLength > Messages.DEF_BUFFER_LENGTH) {
            AppLog.e("SSL Handshake: Invalid AAP payload length: $payloadLength")
            return null
        }

        val payload = ByteArray(payloadLength)
        if (connection.recvBlocking(payload, payloadLength, 2000, true) != payloadLength) {
            AppLog.e("SSL Handshake: Failed to read AAP payload ($payloadLength bytes)")
            return null
        }

        return payload
    }

    private fun prepare(): Int {
        // Use a consistent (host, port) key so JSSE's ClientSessionContext can find and reuse
        // the session from the previous connection.  The values are arbitrary — they are never
        // used for DNS resolution; they just serve as the cache lookup key.
        sslEngine = sslContext.createSSLEngine("android-auto", 5277).apply {
            useClientMode = true
            session.also {
                val appBufferMax = it.applicationBufferSize
                val netBufferMax = it.packetBufferSize

                txBuffer = ByteBuffer.allocateDirect(netBufferMax)
                rxBuffer = ByteBuffer.allocateDirect(Messages.DEF_BUFFER_LENGTH.coerceAtLeast(appBufferMax + 50))
            }
        }
        sslEngine.beginHandshake()
        return 0
    }

    override fun postHandshakeReset() {
        // Clear buffers. In this implementation, the buffers are re-created for each wrap/unwrap
        // operation (implicitly by ByteBuffer.wrap), but clearing them ensures no stale data.
        txBuffer.clear()
        rxBuffer.clear()
    }

    override fun release() {
        // No-op for SSLEngine (garbage collection handles it)
    }

    private fun getHandshakeStatus(): SSLEngineResult.HandshakeStatus {
        return sslEngine.handshakeStatus
    }

    private fun runDelegatedTasks() {
        if (sslEngine.handshakeStatus === SSLEngineResult.HandshakeStatus.NEED_TASK) {
            var runnable: Runnable? = sslEngine.delegatedTask
            while (runnable != null) {
                runnable.run()
                runnable = sslEngine.delegatedTask
            }
            val hsStatus = sslEngine.handshakeStatus
            if (hsStatus === SSLEngineResult.HandshakeStatus.NEED_TASK) {
                throw Exception("handshake shouldn't need additional tasks")
            }
        }
    }

    private fun handshakeRead(): ByteArray {
        txBuffer.clear()
        val result = sslEngine.wrap(emptyArray(), txBuffer)
        runDelegatedTasks(result, sslEngine)
        val resultBuffer = ByteArray(result.bytesProduced())
        txBuffer.flip()
        txBuffer.get(resultBuffer)
        return resultBuffer
    }

    private fun handshakeWrite(start: Int, length: Int, buffer: ByteArray): Int {
        rxBuffer.clear()
        val receivedHandshakeData = ByteArray(length)
        System.arraycopy(buffer, start, receivedHandshakeData, 0, length)

        val data = ByteBuffer.wrap(receivedHandshakeData)
        while (data.hasRemaining()) {
            val result = sslEngine.unwrap(data, rxBuffer)
            runDelegatedTasks(result, sslEngine)
            // Break on any non-OK status (especially BUFFER_UNDERFLOW on a partial TLS record)
            // to prevent an infinite loop. performHandshake() no longer calls this method for
            // NEED_UNWRAP — it handles fragmented records directly via pendingTlsData.
            if (result.status != SSLEngineResult.Status.OK) break
        }
        return receivedHandshakeData.size
    }

    override fun decrypt(start: Int, length: Int, buffer: ByteArray): ByteArrayWithLimit? {
        synchronized(this) {
            if (!::sslEngine.isInitialized || !::rxBuffer.isInitialized) {
                AppLog.w("SSL Decrypt: Not initialized yet")
                return null
            }
            try {
                rxBuffer.clear()
                val encrypted = ByteBuffer.wrap(buffer, start, length)
                val result = sslEngine.unwrap(encrypted, rxBuffer)
                runDelegatedTasks(result, sslEngine)

                if (AppLog.LOG_VERBOSE || result.bytesProduced() == 0) {
                    AppLog.d("SSL Decrypt Status: ${result.status}, Produced: ${result.bytesProduced()}, Consumed: ${result.bytesConsumed()}")
                }

                val resultBuffer = ByteArray(result.bytesProduced())
                rxBuffer.flip()
                rxBuffer.get(resultBuffer)
                return ByteArrayWithLimit(resultBuffer, resultBuffer.size)
            } catch (e: Exception) {
                // Check for Magic Garbage disconnect signal from Wireless Helper
                if (length >= 16) {
                    var allFF = true
                    for (i in 0 until 16) {
                        if (buffer[start + i] != 0xFF.toByte()) {
                            allFF = false
                            break
                        }
                    }
                    if (allFF) {
                        AppLog.i("SSL Decrypt: Magic Garbage detected. Marking as clean user disconnect.")
                        isUserDisconnect = true
                    }
                }

                if (!isUserDisconnect) {
                    AppLog.e("SSL Decrypt failed", e)
                }
                return null
            }
        }
    }

    override fun encrypt(offset: Int, length: Int, buffer: ByteArray): ByteArrayWithLimit? {
        synchronized(this) {
            if (!::sslEngine.isInitialized || !::txBuffer.isInitialized) {
                AppLog.w("SSL Encrypt: Not initialized yet")
                return null
            }
            try {
                txBuffer.clear()
                val byteBuffer = ByteBuffer.wrap(buffer, offset, length)
                val result = sslEngine.wrap(byteBuffer, txBuffer)
                runDelegatedTasks(result, sslEngine)
                val resultBuffer = ByteArray(result.bytesProduced() + offset)
                txBuffer.flip()
                txBuffer.get(resultBuffer, offset, result.bytesProduced())
                return ByteArrayWithLimit(resultBuffer, resultBuffer.size)
            } catch (e: Exception) {
                AppLog.e("SSL Encrypt failed", e)
                return null
            }
        }
    }

    private fun runDelegatedTasks(result: SSLEngineResult, engine: SSLEngine) {
        if (result.handshakeStatus === SSLEngineResult.HandshakeStatus.NEED_TASK) {
            var runnable: Runnable? = engine.delegatedTask
            while (runnable != null) {
                runnable.run()
                runnable = engine.delegatedTask
            }
            val hsStatus = engine.handshakeStatus
            if (hsStatus === SSLEngineResult.HandshakeStatus.NEED_TASK) {
                throw Exception("handshake shouldn't need additional tasks")
            }
        }
    }

    companion object {
        // Maximum wall-clock time for the entire SSL handshake loop. Caps worst-case stall at
        // 15 s regardless of how many round-trips remain when the phone stops responding.
        private const val SSL_HANDSHAKE_TIMEOUT_MS = 15_000L

        private fun createSslContext(keyManager: SingleKeyKeyManager): SSLContext {
            val providerName = ConscryptInitializer.getProviderName()

            val sslContext = if (providerName != null) {
                try {
                    AppLog.d("Creating SSLContext with Conscrypt provider")
                    SSLContext.getInstance("TLS", providerName)
                } catch (e: Exception) {
                    AppLog.w("Failed to create SSLContext with Conscrypt, using default", e)
                    SSLContext.getInstance("TLS")
                }
            } else {
                AppLog.d("Creating SSLContext with default provider")
                SSLContext.getInstance("TLS")
            }

            return sslContext.apply {
                init(arrayOf(keyManager), arrayOf(NoCheckTrustManager()), null)
                // Keep the default session cache (size 10, timeout 86400 s) so that a
                // reconnect within the same app session can use an abbreviated handshake.
            }
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapMessageHandler.kt`:

```kt
package com.andrerinas.headunitrevived.aap

internal interface AapMessageHandler {
    @Throws(HandleException::class)
    fun handle(message: AapMessage)

    class HandleException internal constructor(cause: Throwable) : Exception(cause)
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapSslNative.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import androidx.annotation.Keep
import com.andrerinas.headunitrevived.aap.protocol.messages.Messages
import com.andrerinas.headunitrevived.connection.AccessoryConnection
import com.andrerinas.headunitrevived.utils.AppLog

/**
 * Native SSL implementation using OpenSSL via JNI.
 * This is generally faster on older devices than Java SSLEngine.
 */
@Keep
internal class AapSslNative : AapSsl {

    companion object {
        init {
            try {
                System.loadLibrary("crypto_1_1")
                System.loadLibrary("ssl_1_1")
                System.loadLibrary("hu_jni")
            } catch (e: UnsatisfiedLinkError) {
                AppLog.e("Failed to load native SSL libraries", e)
            }
        }
    }

    @Keep
    private external fun native_ssl_prepare(): Int
    @Keep
    private external fun native_ssl_do_handshake(): Int
    @Keep
    private external fun native_ssl_bio_read(offset: Int, res_len: Int, res_buf: ByteArray): Int
    @Keep
    private external fun native_ssl_bio_write(offset: Int, msg_len: Int, msg_buf: ByteArray): Int
    @Keep
    private external fun native_ssl_read(offset: Int, res_len: Int, res_buf: ByteArray): Int
    @Keep
    private external fun native_ssl_write(offset: Int, msg_len: Int, msg_buf: ByteArray): Int
    @Keep
    private external fun native_ssl_cleanup()

    private val bio_read = ByteArray(Messages.DEF_BUFFER_LENGTH)
    private val enc_buf = ByteArray(Messages.DEF_BUFFER_LENGTH)
    private val dec_buf = ByteArray(Messages.DEF_BUFFER_LENGTH)

    override fun performHandshake(connection: AccessoryConnection): Boolean {
        if (prepare() < 0) {
            AppLog.e("Native SSL prepare failed")
            return false
        }

        val buffer = ByteArray(Messages.DEF_BUFFER_LENGTH)
        var hs_ctr = 0
        while (hs_ctr < 2) {
            hs_ctr++

            val handshakeData = handshakeRead()
            if (handshakeData == null) {
                AppLog.e("Native SSL handshakeRead failed")
                return false
            }

            // Wrap in AAP Message: Channel 0, Flags 3, Type 3
            val bio = Messages.createRawMessage(0, 3, 3, handshakeData)

            if (connection.sendBlocking(bio, bio.size, 5000) < 0) {
                AppLog.e("Native SSL handshake send failed")
                return false
            }

            val size = connection.recvBlocking(buffer, buffer.size, 5000, false)
            if (size <= 6) {
                AppLog.e("Native SSL handshake recv failed or too small")
                return false
            }

            handshakeWrite(6, size - 6, buffer)
        }
        return true
    }

    private fun prepare(): Int {
        val ret = native_ssl_prepare()
        if (ret < 0) {
            AppLog.e("SSL prepare failed: $ret")
        }
        return ret
    }

    private fun handshakeRead(): ByteArray? {
        native_ssl_do_handshake()
        val size = native_ssl_bio_read(0, Messages.DEF_BUFFER_LENGTH, bio_read)
        if (size <= 0) {
            AppLog.i("SSL BIO read error")
            return null
        }
        val result = ByteArray(size)
        System.arraycopy(bio_read, 0, result, 0, size)
        return result
    }

    private fun handshakeWrite(start: Int, length: Int, buffer: ByteArray): Int {
        return native_ssl_bio_write(start, length, buffer)
    }

    // Stub for delegated tasks (Native SSL handles this internally or synchronously)
    private fun runDelegatedTasks() {
        // No-op for Native SSL
    }

    // Stub for handshake status (Native SSL manages state internally)
    private fun getHandshakeStatus(): javax.net.ssl.SSLEngineResult.HandshakeStatus {
        return javax.net.ssl.SSLEngineResult.HandshakeStatus.NOT_HANDSHAKING
    }

    // Stub for reset
    override fun postHandshakeReset() {
        // No-op
    }

    override fun release() {
        AppLog.i("Native SSL: Releasing resources")
        native_ssl_cleanup()
    }

    override fun decrypt(start: Int, length: Int, buffer: ByteArray): ByteArrayWithLimit? {
        val bytes_written = native_ssl_bio_write(start, length, buffer)
        // Write encrypted to SSL input BIO
        if (bytes_written <= 0) {
            AppLog.e("BIO_write() bytes_written: %d", bytes_written)
            return null
        }

        val bytes_read = native_ssl_read(0, Messages.DEF_BUFFER_LENGTH, dec_buf)
        // Read decrypted to decrypted rx buf
        if (bytes_read <= 0) {
            // Only log if it's a real error, not just a cleanup state
            if (bytes_read < 0) AppLog.e("SSL_read bytes_read: %d", bytes_read)
            return null
        }

        return ByteArrayWithLimit(dec_buf, bytes_read)
    }

    override fun encrypt(offset: Int, length: Int, buffer: ByteArray): ByteArrayWithLimit? {

        val bytes_written = native_ssl_write(offset, length, buffer)
        // Write plaintext to SSL
        if (bytes_written <= 0) {
            AppLog.e("SSL_write() bytes_written: %d", bytes_written)
            return null
        }

        if (bytes_written != length) {
            AppLog.e("SSL Write len: %d  bytes_written: %d", length, bytes_written)
        }

        // AppLog.v("SSL Write len: %d  bytes_written: %d", length, bytes_written)

        val bytes_read = native_ssl_bio_read(offset, Messages.DEF_BUFFER_LENGTH - offset, enc_buf)
        if (bytes_read <= 0) {
            AppLog.e("BIO read  bytes_read: %d", bytes_read)
            return null
        }

        // AppLog.v("BIO read bytes_read: %d", bytes_read)

        return ByteArrayWithLimit(enc_buf, bytes_read + offset)
    }

}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapMessageHandlerType.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import android.content.Context
import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.decoder.MicRecorder
import com.andrerinas.headunitrevived.aap.protocol.proto.MediaPlayback
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.Settings

internal class AapMessageHandlerType(
        private val transport: AapTransport,
        recorder: MicRecorder,
        private val aapAudio: AapAudio,
        private val aapVideo: AapVideo,
        settings: Settings,
        context: Context,
        onAaMediaMetadata: ((MediaPlayback.MediaMetaData) -> Unit)? = null,
        onAaPlaybackStatus: ((MediaPlayback.MediaPlaybackStatus) -> Unit)? = null) : AapMessageHandler {

    private val aapControl: AapControl = AapControlGateway(transport, recorder, aapAudio, settings, context)
    private val mediaPlayback = AapMediaPlayback(onAaMediaMetadata, onAaPlaybackStatus)
    private val aapNavigation = AapNavigation(context, settings)
    private var videoPacketCount = 0

    @Throws(AapMessageHandler.HandleException::class)
    override fun handle(message: AapMessage) {

        val msgType = message.type
        val flags = message.flags

        // 1. Try processing as Video stream first (ID_VID)
        // High priority for the smoothest possible display.
        if (message.channel == Channel.ID_VID) {
             if (aapVideo.process(message)) {
                 videoPacketCount++
                 // Send ACK AFTER processing
                 if (msgType == 0 || msgType == 1) {
                     transport.sendMediaAck(message.channel)
                 }
                 return
             }
        }

        // 2. Try processing as Audio stream (Speech, System, Media)
        if (message.isAudio) {
            if (aapAudio.process(message)) {
                // Send ACK AFTER processing
                if (msgType == 0 || msgType == 1) {
                    transport.sendMediaAck(message.channel)
                }
                return
            }
        }

        // 3. Media Playback Status (separate channel)
        if (message.channel == Channel.ID_MPB && msgType > 31) {
            mediaPlayback.process(message)
            return
        }

        // 4. Navigation (turn-by-turn from any AA nav app)
        // Process only payload messages on NAV channel (>31).
        // Control/handshake messages on NAV channel must pass through to AapControl.
        if (message.channel == Channel.ID_NAV && msgType > 31) {
            if (aapNavigation.process(message)) {
                return
            }
        }

        // 5. Control Message Fallback
        if (msgType in 0..31 || msgType in 32768..32799 || msgType in 65504..65535) {
            try {
                aapControl.execute(message)
            } catch (e: Exception) {
                AppLog.e(e)
                throw AapMessageHandler.HandleException(e)
            }
        } else {
            AppLog.e("Unknown msg_type: %d, flags: %d, channel: %d", msgType, flags, message.channel)
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapReadSingleMessage.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import com.andrerinas.headunitrevived.connection.AccessoryConnection
import com.andrerinas.headunitrevived.utils.AppLog

internal class AapReadSingleMessage(connection: AccessoryConnection, ssl: AapSsl, handler: AapMessageHandler)
    : AapRead.Base(connection, ssl, handler) {

    private val recvHeader = AapMessageIncoming.EncryptedHeader()
    // Increase to 4MB to handle large 1080p/4K/HEVC I-frames
    private val msgBuffer = ByteArray(4 * 1024 * 1024)
    private val fragmentSizeBuffer = ByteArray(4)

    override fun doRead(connection: AccessoryConnection): Int {
        try {
            // Step 1: Read the encrypted header.
            // No timeout limit (0 = infinite) because this waits for the
            // NEXT message — the phone can be idle for minutes and that's normal.
            // TCP keepAlive will detect a truly dead connection.
            val headerSize = connection.recvBlocking(recvHeader.buf, recvHeader.buf.size, 0, true)
            if (headerSize != AapMessageIncoming.EncryptedHeader.SIZE) {
                if (headerSize == -1) {
                    AppLog.i("AapRead: Connection closed (EOF). Disconnecting.")
                    return -1
                } else if (headerSize == 0) {
                    // Timeout (shouldn't happen with timeout=0, but safety fallback)
                    return 0
                } else {
                    AppLog.e("AapRead: Partial header read. Expected ${AapMessageIncoming.EncryptedHeader.SIZE}, got $headerSize. Skipping.")
                    return 0
                }
            }

            recvHeader.decode()

            // Immediate check for Magic Garbage in the header bytes.
            // This is the most reliable path for intentional disconnects from the Helper.
            if (isMagicGarbage(recvHeader.buf, 0, recvHeader.buf.size)) {
                AppLog.i("AapRead: Magic Garbage detected in header. Clean disconnect.")
                return -2
            }

            if (recvHeader.flags == 0x09) {
                // Once header arrived, data should be flowing — 10s timeout is valid here
                val readSize = connection.recvBlocking(fragmentSizeBuffer, 4, 10000, true)
                if(readSize != 4) {
                    AppLog.e("AapRead: Failed to read fragment total size. Skipping.")
                    return 0
                }
            }

            // Step 2: Read the encrypted message body
            // Header arrived so body should follow quickly — 10s timeout
            if (recvHeader.enc_len > msgBuffer.size || recvHeader.enc_len < 0) {
                AppLog.e("AapRead: Invalid message size (${recvHeader.enc_len} bytes). Skipping.")
                return 0
            }

            val msgSize = connection.recvBlocking(msgBuffer, recvHeader.enc_len, 10000, true)
            if (msgSize != recvHeader.enc_len) {
                if (msgSize == -1) {
                    AppLog.i("AapRead: Connection closed during body read.")
                    return -1
                }
                AppLog.e("AapRead: Failed to read full message body. Expected ${recvHeader.enc_len}, got $msgSize. Skipping.")
                return 0
            }

            // Step 3: Decrypt the message
            val msg = AapMessageIncoming.decrypt(recvHeader, 0, msgBuffer, ssl)

            if (msg == null) {
                // If decryption failed because of a Magic Garbage signal, return -2 to signal clean quit
                if (ssl is AapSslContext && ssl.isUserDisconnect) {
                    AppLog.i("AapRead: Magic Garbage detected in decryption. Triggering clean disconnect.")
                    return -2
                }
                return 0
            }

            // Step 4: Handle the decrypted message
            handler.handle(msg)
            return 0
        } catch (e: Exception) {
            AppLog.e("AapRead: Error in read loop (ignored): ${e.message}")
            return 0
        }
    }

    private fun isMagicGarbage(buffer: ByteArray, start: Int, length: Int): Boolean {
        if (length < 4) return false // Need at least some bytes to verify
        // Check if at least the first 4 bytes are 0xFF
        for (i in 0 until 4.coerceAtMost(length)) {
            if (buffer[start + i] != 0xFF.toByte()) return false
        }
        return true
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapReadMultipleMessages.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.messages.Messages
import com.andrerinas.headunitrevived.connection.AccessoryConnection
import com.andrerinas.headunitrevived.utils.AppLog
import java.nio.BufferUnderflowException
import java.nio.ByteBuffer

internal class AapReadMultipleMessages(
        connection: AccessoryConnection,
        ssl: AapSsl,
        handler: AapMessageHandler)
    : AapRead.Base(connection, ssl, handler) {

    // Increase buffers to 4MB to handle large 1080p/4K/HEVC I-frames
    private val fifo = ByteBuffer.allocate(4 * 1024 * 1024)
    private val recvBuffer = ByteArray(Messages.DEF_BUFFER_LENGTH)
    private val recvHeader = AapMessageIncoming.EncryptedHeader()
    private val msgBuffer = ByteArray(4 * 1024 * 1024)
    private val skipBuffer = ByteArray(4)

    override fun doRead(connection: AccessoryConnection): Int {
        val size = try {
            connection.recvBlocking(recvBuffer, recvBuffer.size, 150, false)
        } catch (e: Exception) {
            AppLog.e("AapRead: Fatal read error: ${e.message}")
            return -1
        }

        if (size < 0) {
            // read failure — discard any partial data accumulated in the FIFO
            // so the parser re-syncs cleanly on the next successful read.
            fifo.clear()
            // If the connection is dead (e.g. resetInterface failed to re-claim),
            // signal the transport to quit instead of spinning on a broken connection.
            if (!connection.isConnected) {
                AppLog.e("AapRead: Connection lost. Stopping read loop.")
                return -1
            }
            return 0
        }
        if (size == 0) return 0

        try {
            if (fifo.remaining() < size) {
                AppLog.w("AapRead: FIFO overflow! Size: $size, Remaining: ${fifo.remaining()}. Clearing buffer.")
                fifo.clear()
            }
            fifo.put(recvBuffer, 0, size)
            processBulk()
        } catch (e: Exception) {
            AppLog.e("AapRead: Error in processBulk: ${e.message}")
            fifo.clear() // Hard reset on error
        }
        return 0
    }

    private fun processBulk() {
        fifo.flip()

        while (fifo.remaining() >= AapMessageIncoming.EncryptedHeader.SIZE) {
            fifo.mark()
            fifo.get(recvHeader.buf, 0, recvHeader.buf.size)
            recvHeader.decode()

            if (recvHeader.flags == 0x09) {
                if (fifo.remaining() < 4) {
                    fifo.reset()
                    break
                }
                fifo.get(skipBuffer, 0, 4)
            }

            if (recvHeader.enc_len > msgBuffer.size || recvHeader.enc_len < 0) {
                AppLog.e("AapRead: Invalid message length (${recvHeader.enc_len}). Resetting FIFO.")
                fifo.clear()
                return
            }

            if (fifo.remaining() < recvHeader.enc_len) {
                fifo.reset()
                break
            }

            fifo.get(msgBuffer, 0, recvHeader.enc_len)

            try {
                val msg = AapMessageIncoming.decrypt(recvHeader, 0, msgBuffer, ssl)

                if (msg != null) {
                    handler.handle(msg)
                }
            } catch (e: Exception) {
                AppLog.e("AapRead: Decryption/Handling error: ${e.message}")
            }
        }

        fifo.compact()
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/ByteArrayWithLimit.kt`:

```kt
package com.andrerinas.headunitrevived.aap

class ByteArrayWithLimit(val data: ByteArray, var limit: Int)

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapAudio.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Build
import com.andrerinas.headunitrevived.aap.protocol.AudioConfigs
import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.aap.protocol.proto.Control
import com.andrerinas.headunitrevived.decoder.AudioDecoder
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.Settings

internal class AapAudio(
        private val audioDecoder: AudioDecoder,
        private val audioManager: AudioManager,
        private val settings: Settings) {

    private var audioFocusRequest: AudioFocusRequest? = null
    private var legacyFocusListener: AudioManager.OnAudioFocusChangeListener? = null

    fun requestFocusChange(stream: Int, focusRequest: Int, callback: AudioManager.OnAudioFocusChangeListener): Int {
        AppLog.i("Audio Focus Request: stream=$stream, type=$focusRequest")

        var result = AudioManager.AUDIOFOCUS_REQUEST_FAILED

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) { // API 26+
            if (focusRequest == Control.AudioFocusRequestNotification.AudioFocusRequestType.RELEASE_VALUE) {
                AppLog.i("Releasing audio focus")
                audioFocusRequest?.let { audioManager.abandonAudioFocusRequest(it) }
                audioFocusRequest = null
                result = AudioManager.AUDIOFOCUS_REQUEST_GRANTED
            } else {
                val usage = when (stream) {
                    AudioManager.STREAM_NOTIFICATION -> AudioAttributes.USAGE_NOTIFICATION
                    AudioManager.STREAM_VOICE_CALL -> AudioAttributes.USAGE_VOICE_COMMUNICATION
                    else -> AudioAttributes.USAGE_MEDIA
                }

                val audioAttributes = AudioAttributes.Builder()
                        .setUsage(usage)
                        .setContentType(if (usage == AudioAttributes.USAGE_MEDIA) AudioAttributes.CONTENT_TYPE_MUSIC else AudioAttributes.CONTENT_TYPE_SPEECH)
                        .build()

                audioFocusRequest = AudioFocusRequest.Builder(focusRequest)
                        .setAudioAttributes(audioAttributes)
                        .setWillPauseWhenDucked(false)
                        .setOnAudioFocusChangeListener(callback)
                        .build()

                result = audioManager.requestAudioFocus(audioFocusRequest!!)
                AppLog.i("Audio focus request result: ${if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) "GRANTED" else "FAILED ($result)"}")
            }
        } else { // API < 26
            @Suppress("DEPRECATION")
            result = when (focusRequest) {
                Control.AudioFocusRequestNotification.AudioFocusRequestType.RELEASE_VALUE -> {
                    audioManager.abandonAudioFocus(callback)
                    legacyFocusListener?.let { audioManager.abandonAudioFocus(it) }
                    legacyFocusListener = null
                    AudioManager.AUDIOFOCUS_REQUEST_GRANTED
                }
                Control.AudioFocusRequestNotification.AudioFocusRequestType.GAIN_VALUE -> {
                    legacyFocusListener = callback
                    audioManager.requestAudioFocus(callback, stream, AudioManager.AUDIOFOCUS_GAIN)
                }
                Control.AudioFocusRequestNotification.AudioFocusRequestType.GAIN_TRANSIENT_VALUE -> {
                    legacyFocusListener = callback
                    audioManager.requestAudioFocus(callback, stream, AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
                }
                Control.AudioFocusRequestNotification.AudioFocusRequestType.GAIN_TRANSIENT_MAY_DUCK_VALUE -> {
                    legacyFocusListener = callback
                    audioManager.requestAudioFocus(callback, stream, AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK)
                }
                else -> AudioManager.AUDIOFOCUS_REQUEST_FAILED
            }
            AppLog.i("Audio focus request result (legacy): ${if (result == AudioManager.AUDIOFOCUS_REQUEST_GRANTED) "GRANTED" else "FAILED"}")
        }
        return result
    }

    fun releaseAllFocus() {
        AppLog.i("AapAudio: Releasing all audio focus.")
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            audioFocusRequest?.let { audioManager.abandonAudioFocusRequest(it) }
            audioFocusRequest = null
        } else {
            @Suppress("DEPRECATION")
            legacyFocusListener?.let { audioManager.abandonAudioFocus(it) }
            legacyFocusListener = null
        }
    }

    /**
     * Processes a message as an audio stream packet.
     * Returns true if the packet was identified and processed as audio data, false otherwise.
     */
    fun process(message: AapMessage): Boolean {
        // Media stream packets have msgType 0 or 1.
        // Control packets on audio channels (Setup, Start, Stop) have types > 32767.
        if (message.type == 0 || message.type == 1) {
            if (message.size >= 10) {
                decode(message.channel, 10, message.data, message.size - 10)
            }
            return true
        }
        return false
    }

    private fun decode(channel: Int, start: Int, buf: ByteArray, len: Int) {
        var length = len
        if (length > AUDIO_BUFS_SIZE) {
            AppLog.e("Error audio len: %d  aud_buf_BUFS_SIZE: %d", length, AUDIO_BUFS_SIZE)
            length = AUDIO_BUFS_SIZE
        }

        if (audioDecoder.getTrack(channel) == null) {
            val config = AudioConfigs.get(channel)
            val stream = AudioConfigs.stream(channel)

            val offset = when (channel) {
                Channel.ID_AUD -> settings.mediaVolumeOffset
                Channel.ID_AU1 -> settings.assistantVolumeOffset
                Channel.ID_AU2 -> settings.navigationVolumeOffset
                else -> 0
            }
            val gain = (1.0f + (offset / 100.0f)).coerceIn(0.0f, 2.0f)

            // Voice and Navigation benefit from lower latency. Cap the multiplier for those channels.
            val effectiveMultiplier = if (channel == Channel.ID_AUD) {
                settings.audioLatencyMultiplier
            } else {
                settings.audioLatencyMultiplier.coerceAtMost(4)
            }

            AppLog.i("AudioDecoder.start: channel=$channel, stream=$stream, gain=$gain, sampleRate=${config.sampleRate}, numberOfBits=${config.numberOfBits}, numberOfChannels=${config.numberOfChannels}, isAac=${settings.useAacAudio}, latencyMultiplier=$effectiveMultiplier, queueCapacity=${settings.audioQueueCapacity}")
            audioDecoder.start(channel, stream, config.sampleRate, config.numberOfBits, config.numberOfChannels, settings.useAacAudio, gain, effectiveMultiplier, settings.audioQueueCapacity)
        }

        audioDecoder.decode(channel, buf, start, length)
    }

    fun stopAudio(channel: Int) {
        AppLog.i("Audio Stop: " + Channel.name(channel))
        audioDecoder.stop(channel)
    }

    companion object {
        private const val AUDIO_BUFS_SIZE = 65536 * 4  // Up to 256 Kbytes
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapDump.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import android.util.Log
import com.andrerinas.headunitrevived.aap.protocol.Channel
import com.andrerinas.headunitrevived.utils.AppLog
import java.util.Locale

internal object AapDump {
    private val MAX_HEX_DUMP = 64//32;
    private val HD_MW = 16

    fun logd(prefix: String, src: String, chan: Int, flags: Int, buf: ByteArray, len: Int): Int {
        // Source:  HU = HU > AA   AA = AA > HU

        if (!AppLog.LOG_DEBUG) {
            return 0
        }

        if (len < 2) {
            // If less than 2 bytes, needed for msg_type...
            AppLog.e("hu_aad_dmp len: %d", len)
            return 0
        }

        var rmv = 0
        var lft = len
        val msg_type = (buf[0].toInt() shl 8) + buf[1].toInt() and 0xFFFF

        var is_media = false
        if (chan == Channel.ID_VID || chan == Channel.ID_MIC || Channel.isAudio(chan))
            is_media = true

        if (is_media && (flags == 8 || flags == 0x0a || msg_type == 0))
        // || msg_type ==1)    // Ignore Video/Audio Data
            return rmv

        if (is_media && msg_type == 32768 + 4)
        // Ignore Video/Audio Ack
            return rmv

        // msg_type = 2 bytes
        rmv += 2
        lft -= 2

        val msg_type_str = iaad_msg_type_str_get(msg_type, src[0], lft)   // Get msg_type string
        if (flags == 0x08)
            Log.d(AppLog.TAG, String.format("%s src: %s  lft: %5d  Media Data Mid", prefix, src, lft))
        else if (flags == 0x0a)
            Log.d(AppLog.TAG, String.format("%s src: %s  lft: %5d  Media Data End", prefix, src, lft))
        else
            Log.d(AppLog.TAG, String.format("%s src: %s  lft: %5d  msg_type: %5d %s", prefix, src, lft, msg_type, msg_type_str))

        logvHex(prefix, 2, buf, lft)                                  // Hexdump

        if (flags == 0x08)
        // If Media Data Mid...
            return len                                                     // Done

        if (flags == 0x0a)
        // If Media Data End...
            return len                                                     // Done

        if (msg_type_enc_get(msg_type) == null) {                          // If encrypted data...
            return len                                                     // Done
        }

        if (lft == 0)
        // If no content
            return rmv                                                     // Done

        if (lft < 2) {                                                      // If less than 2 bytes for content (impossible if content; need at least 1 byte for 0x08 and 1 byte for varint)
            AppLog.e("hu_aad_dmp len: %d  lft: %d", len, lft)
            return rmv                                                     // Done with error
        }

        //adj = iaad_dmp_n(1, 1, buf, lft);                                  // Dump the content w/ n=1

        // iaad_adj(&rmv, &buf, &lft, adj);                                // Adjust past the content (to nothing, presumably)

        //        if (lft != 0 || rmv != len || rmv < 0)                              // If content left... (must be malformed)
        //            AppLog.e ("hu_aad_dmp after content len: %d  lft: %d  rmv: %d  buf: %p", len, lft, rmv, buf);

        AppLog.i("--------------------------------------------------------")  // Empty line / 56 characters

        return rmv
    }


    private val MSG_TYPE_32 = 32768
    private fun iaad_msg_type_str_get(msg_type: Int, src: Char, len: Int): String {   // Source:  HU = HU > AA   AA = AA > HU

        when (msg_type) {
            0 -> return "Media Data"
            1 -> {
                if (src == 'H')
                    return "Version Request"    // Start AA Protocol
                else if (src == 'A')
                    return "Codec Data"         // First Video packet, respond with Media Ack
                else
                    return "1 !"
            }
            2 -> return "Version Response"
            3 -> return "SSL Handshake Data"                          // First/Request from HU, Second/Response from AA
        //case 5123:return ("SSL Change Cipher Spec");                      // 0x1403
        //case 5379:return ("SSL Alert");                                   // 0x1503
        //case 5635:return ("SSL Handshake");                               // 0x1603
        //case 5891:return ("SSL App Data");                                // 0x1703
            4 -> return "SSL Authentication Complete Notification"
            5 -> return "Service Discovery Request"
            6 -> return "Service Discovery Response"
            7 -> return "Channel Open Request"
            8 -> return "Channel Open Response"                       // byte:status
            9 -> return "9 ??"
            10 -> return "10 ??"
            11 -> return "Ping Request"
            12 -> return "Ping Response"
            13 -> return "Navigation Focus Request"
            14 -> return "Navigation Focus Notification"               // NavFocusType
            15 -> return "Byebye Request"
            16 -> return "Byebye Response"
            17 -> return "Voice Session Notification"
            18 -> return "Audio Focus Request"
            19 -> return "Audio Focus Notification"                    // AudioFocusType   (AudioStreamType ?)

            MSG_TYPE_32// + 0:
            -> return "Media Setup Request"                        // Video and Audio sinks receive this and send k3 3 / 32771
            MSG_TYPE_32 + 1 -> {
                if (src == 'H')
                    return "Touch Notification"
                else if (src == 'A')
                    return "Sensor/Media Start Request"
                else
                    return "32769 !"            // src AA also Media Start Request ????
            }
            MSG_TYPE_32 + 2 -> {
                if (src == 'H')
                    return "Sensor Start Response"
                else if (src == 'A')
                    return "Touch/Input/Audio Start/Stop Request"
                else
                    return "32770 !"
            }
            MSG_TYPE_32 + 3 -> {
                if (len == 6)
                    return "Media Setup Response"
                else if (len == 2)
                    return "Key Binding Response"
                else
                    return "Sensor Notification"
            }
            MSG_TYPE_32 + 4 -> return "Codec/Media Data Ack"
            MSG_TYPE_32 + 5 -> return "Mic Start/Stop Request"
            MSG_TYPE_32 + 6 -> return "k3 6 ?"
            MSG_TYPE_32 + 7 -> return "Media Video ? Request"
            MSG_TYPE_32 + 8 -> return "Video Focus Notification"

            65535 -> return "Framing Error Notification"
        }
        return "Unknown"
    }

    private fun msg_type_enc_get(msg_type: Int): String? {

        when (msg_type) {
            5123 -> return "SSL Change Cipher Spec"                    // 0x1403
            5379 -> return "SSL Alert"                                 // 0x1503
            5635 -> return "SSL Handshake"                             // 0x1603
            5891 -> return "SSL App Data"                              // 0x1703
        }
        return null
    }

    fun logvHex(prefix: String, start: Int, buf: ByteArray, len: Int) {

        if (!AppLog.LOG_VERBOSE) {
            return
        }

        Log.v(AppLog.TAG, logHex(prefix, start, buf, len, StringBuilder()).toString())
    }

    fun logHex(message: AapMessage): String {
        return AapDump.logHex("", 0, message.data, message.size, StringBuilder()).toString()
    }

    fun logHex(prefix: String, start: Int, buf: ByteArray, length: Int, sb: StringBuilder): StringBuilder {
        var len = length

        if (len + start > MAX_HEX_DUMP)
            len = MAX_HEX_DUMP + start

        var i: Int = start
        var n: Int

        sb.append(prefix)
        var line = String.format(Locale.US, " %08d ", 0)
        sb.append(line)

        n = 1
        while (i < len) {                           // i keeps incrementing, n gets reset to 0 each line

            val hex = String.format(Locale.US, "%02X ", buf[i])
            sb.append(hex)

            if (n == HD_MW) {                                                 // If at specified line width
                n = 0                                                          // Reset position in line counter
                Log.v(AppLog.TAG, sb.toString())                                                    // Log line

                sb.setLength(0)

                sb.append(prefix)
                line = String.format(Locale.US, "     %04d ", i + 1)
                sb.append(line)
            }
            i++
            n++
        }

        return sb
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapVideo.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import com.andrerinas.headunitrevived.aap.protocol.messages.Messages
import com.andrerinas.headunitrevived.decoder.VideoDecoder
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.Settings
import java.nio.ByteBuffer

internal class AapVideo(private val videoDecoder: VideoDecoder, private val settings: Settings, private val onFrameCorrupted: () -> Unit) {

    private val messageBuffer = ByteBuffer.allocate(
        if (settings.videoCodec == VideoDecoder.CodecType.H265.mimeType) {
            Messages.DEF_BUFFER_LENGTH * 64 // ~8MB for H.265 support
        } else {
            Messages.DEF_BUFFER_LENGTH * 16 // ~2MB for H.264 legacy support
        }
    )
    private var legacyAssembledBuffer: ByteArray? = null
    private var isFrameCorrupt = false
    private var lastKeyframeRequestMs = 0L

    private fun markCorruptAndRequestRecovery() {
        if (!isFrameCorrupt) {
            val now = android.os.SystemClock.elapsedRealtime()
            if (now - lastKeyframeRequestMs > 1000) {
                lastKeyframeRequestMs = now
                AppLog.w("AapVideo: Frame corrupted, requesting keyframe to recover stream")
                onFrameCorrupted()
            }
        }
        isFrameCorrupt = true
    }

    private fun findStartCode(buf: ByteArray, offset: Int): Int {
        if (offset + 3 > buf.size) return -1
        if (buf[offset].toInt() == 0 && buf[offset + 1].toInt() == 0) {
            if (buf[offset + 2].toInt() == 1) return 3 // 3-byte start code
            if (offset + 4 <= buf.size && buf[offset + 2].toInt() == 0 && buf[offset + 3].toInt() == 1) return 4 // 4-byte start code
        }
        return -1
    }

    fun process(message: AapMessage): Boolean {

        val flags = message.flags.toInt()
        val buf = message.data
        val len = message.size

        when (flags) {
            11 -> {
                // Single fragment frame - corruption only affects this frame
                isFrameCorrupt = false
                messageBuffer.clear()

                // Timestamp Indication (Offset 10)
                val sc10 = findStartCode(buf, 10)
                if (len > 10 + sc10 && sc10 > 0) {
                    videoDecoder.decode(buf, 10, len - 10, settings.forceSoftwareDecoding, settings.videoCodec)
                    return true
                }

                // Media Indication or Config (Offset 2)
                val sc2 = findStartCode(buf, 2)
                if (len > 2 + sc2 && sc2 > 0) {
                    videoDecoder.decode(buf, 2, len - 2, settings.forceSoftwareDecoding, settings.videoCodec)
                    return true
                }
                AppLog.w("AapVideo: Dropped Flag 11 packet. len=$len")
            }
            9 -> {
                // First fragment - reset corruption state for the new frame
                isFrameCorrupt = false
                messageBuffer.clear()

                // Timestamp Indication (Offset 10)
                val sc10 = findStartCode(buf, 10)
                if (len > 10 + sc10 && sc10 > 0) {
                    messageBuffer.put(message.data, 10, message.size - 10)
                    return true
                }
                // Media Indication (Offset 2)
                val sc2 = findStartCode(buf, 2)
                if (len > 2 + sc2 && sc2 > 0) {
                    messageBuffer.put(message.data, 2, message.size - 2)
                    return true
                }
            }
            8 -> {
                if (isFrameCorrupt) return true // Skip fragments of an already corrupt frame

                // Middle fragment - append to buffer with overflow detection
                if (messageBuffer.remaining() >= message.size) {
                    messageBuffer.put(message.data, 0, message.size)
                } else {
                    AppLog.e("AapVideo: Fragment overflow (Flag 8)! Size ${message.size} exceeds remaining ${messageBuffer.remaining()}. Invalidating frame.")
                    markCorruptAndRequestRecovery()
                    messageBuffer.clear()
                }
                return true
            }
            10 -> {
                if (isFrameCorrupt) return true // Skip fragments of an already corrupt frame

                // Last fragment - append, assemble, and decode
                if (messageBuffer.remaining() >= message.size) {
                    messageBuffer.put(message.data, 0, message.size)
                } else {
                    AppLog.e("AapVideo: Final fragment overflow (Flag 10)! Invalidating frame.")
                    markCorruptAndRequestRecovery()
                    messageBuffer.clear()
                    return true
                }

                messageBuffer.flip()
                val assembledSize = messageBuffer.limit()

                if (android.os.Build.VERSION.SDK_INT < android.os.Build.VERSION_CODES.LOLLIPOP) {
                    if (legacyAssembledBuffer == null || legacyAssembledBuffer!!.size < assembledSize) {
                        legacyAssembledBuffer = ByteArray(assembledSize + 1024)
                    }
                    messageBuffer.get(legacyAssembledBuffer!!, 0, assembledSize)
                    videoDecoder.decode(legacyAssembledBuffer!!, 0, assembledSize, settings.forceSoftwareDecoding, settings.videoCodec)
                } else {
                    videoDecoder.decode(messageBuffer.array(), 0, assembledSize, settings.forceSoftwareDecoding, settings.videoCodec)
                }

                messageBuffer.clear()
                return true
            }
        }

        return false
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/Utils.java`:

```java
package com.andrerinas.headunitrevived.aap;

import com.andrerinas.headunitrevived.utils.AppLog;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

public class Utils {

    public static long ms_sleep(long ms) {

        try {
            Thread.sleep(ms);                                                // Wait ms milliseconds
            return (ms);
        } catch (InterruptedException e) {
            //Thread.currentThread().interrupt();
            e.printStackTrace();
            AppLog.INSTANCE.e("Exception e: " + e);
            return (0);
        }
    }

    public static long tmr_ms_get() {        // Current timestamp of the most precise timer available on the local system, in nanoseconds. Equivalent to Linux's CLOCK_MONOTONIC.
        // Values returned by this method do not have a defined correspondence to wall clock times; the zero value is typically whenever the device last booted
        //AppLog.i ("ms: " + ms);           // Changing system time will not affect results.
        return (System.nanoTime() / 1000000);
    }

    public static String hex_get(byte b) {
        byte c1 = (byte) ((b & 0x00F0) >> 4);
        byte c2 = (byte) ((b & 0x000F) >> 0);

        byte[] buffer = new byte[2];

        if (c1 < 10)
            buffer[0] = (byte) (c1 + '0');
        else
            buffer[0] = (byte) (c1 + 'A' - 10);
        if (c2 < 10)
            buffer[1] = (byte) (c2 + '0');
        else
            buffer[1] = (byte) (c2 + 'A' - 10);

        return new String(buffer);
    }

    public static String hex_get(short s) {
        byte byte_lo = (byte) (s >> 0 & 0xFF);
        byte byte_hi = (byte) (s >> 8 & 0xFF);
        return (hex_get(byte_hi) + hex_get(byte_lo));
    }

    public static byte[] toByteArray(InputStream is) throws IOException {
        ByteArrayOutputStream buffer = new ByteArrayOutputStream();
        int nRead;
        int buffSize = 16384 * 1024; // 16M
        byte[] data = new byte[buffSize];

        while ((nRead = is.read(data, 0, data.length)) != -1) {
            buffer.write(data, 0, nRead);
        }
        buffer.flush();
        return buffer.toByteArray();
    }

    public static void put_time(int offset, byte[] arr, long time) {
        for (int ctr = 7; ctr >= 0; ctr--) {                           // Fill 8 bytes backwards
            arr[offset + ctr] = (byte) (time & 0xFF);
            time = time >> 8;
        }
    }

    public static void intToBytes(int value, int offset, byte[] buf) {
        buf[offset] = (byte) (value / 256);                                            // Encode length of following data:
        buf[offset+1] = (byte) (value % 256);
    }

    public static int bytesToInt(byte[] buf,int idx, boolean isShort)
    {
        if (isShort)
        {
            return ((buf[idx] & 0xFF) << 8) + (buf[idx + 1] & 0xFF);
        }
        return ((buf[idx] & 0xFF) << 24) + ((buf[idx + 1] & 0xFF) << 16) + ((buf[idx + 2] & 0xFF) << 8) + (buf[idx + 3] & 0xFF);
    }

    public static int getAccVersion(byte[] buffer)
    {
       return (buffer[1] << 8) | buffer[0];
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/aap/AapMediaPlayback.kt`:

```kt
package com.andrerinas.headunitrevived.aap

import com.andrerinas.headunitrevived.aap.protocol.messages.Messages
import com.andrerinas.headunitrevived.aap.protocol.proto.MediaPlayback
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.protoUint32ToLong
import java.nio.ByteBuffer

class AapMediaPlayback(
    private val onAaMediaMetadata: ((MediaPlayback.MediaMetaData) -> Unit)?,
    private val onAaPlaybackStatus: ((MediaPlayback.MediaPlaybackStatus) -> Unit)?
) {
    private val messageBuffer = ByteBuffer.allocate(1024 * 1024) // 1MB to handle large album art
    private var started = false

    fun process(message: AapMessage) {

        val flags = message.flags.toInt()

        when (message.type) {
            MSG_MEDIA_PLAYBACK_METADATA -> processMetadataPacket(message, flags)
            MSG_MEDIA_PLAYBACK_STATUS -> processStatusPacket(message)
            MSG_MEDIA_PLAYBACK_INPUT -> Unit
            else -> {
                if (started && (flags == FLAG_MIDDLE_FRAGMENT || flags == FLAG_LAST_FRAGMENT)) {
                    processMetadataPacket(message, flags)
                    return
                }
                AppLog.e("Unsupported %s", message.toString())
            }
        }
    }

    private fun processStatusPacket(message: AapMessage) {
        try {
            val status = message.parse(MediaPlayback.MediaPlaybackStatus.newBuilder()).build()
            onAaPlaybackStatus?.invoke(status)
            AppLog.d(
                "AapMediaPlayback: status mediaSource='${status.mediaSource}', " +
                    "playbackSeconds(u32)=${status.playbackSeconds.protoUint32ToLong()}, state=${status.state}"
            )
        } catch (e: Exception) {
            AppLog.w("AapMediaPlayback: Failed to parse playback status: ${e.message}")
        }
    }

    private fun processMetadataPacket(message: AapMessage, flags: Int) {
        when (flags) {
            FLAG_FIRST_FRAGMENT -> {
                messageBuffer.clear()
                messageBuffer.put(message.data, message.dataOffset, message.size - message.dataOffset)
                started = true
            }

            FLAG_MIDDLE_FRAGMENT -> {
                if (!started) return
                messageBuffer.put(message.data, 0, message.size)
            }

            FLAG_LAST_FRAGMENT -> {
                if (!started) return
                messageBuffer.put(message.data, 0, message.size)
                messageBuffer.flip()
                try {
                    val request = MediaPlayback.MediaMetaData.newBuilder()
                        .mergeFrom(messageBuffer.array(), 0, messageBuffer.limit())
                        .build()
                    notifyRequest(request)
                } catch (e: Exception) {
                    AppLog.w("AapMediaPlayback: Failed to parse metadata (fragmented): ${e.message}")
                } finally {
                    started = false
                    messageBuffer.clear()
                }
            }

            else -> {
                try {
                    val request = message.parse(MediaPlayback.MediaMetaData.newBuilder()).build()
                    notifyRequest(request)
                } catch (e: Exception) {
                    AppLog.w("AapMediaPlayback: Failed to parse metadata (single packet): ${e.message}")
                }
            }
        }
    }

    private fun notifyRequest(request: MediaPlayback.MediaMetaData) {
        onAaMediaMetadata?.invoke(request)
    }

    private companion object {
        // Based on AA protocol enum MediaPlaybackStatusMessageId from protos.proto.
        const val MSG_MEDIA_PLAYBACK_STATUS = 32769
        const val MSG_MEDIA_PLAYBACK_INPUT = 32770
        const val MSG_MEDIA_PLAYBACK_METADATA = 32771

        // AAP fragmentation flags used by incoming payload packets.
        const val FLAG_FIRST_FRAGMENT = 0x09
        const val FLAG_MIDDLE_FRAGMENT = 0x08
        const val FLAG_LAST_FRAGMENT = 0x0A
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/connection/UsbReceiver.kt`:

```kt
package com.andrerinas.headunitrevived.connection

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build


import com.andrerinas.headunitrevived.utils.AppLog

class UsbReceiver(private val mListener: Listener)          // USB Broadcast Receiver enabled by start() & disabled by stop()
    : BroadcastReceiver() {

    init {
        AppLog.d("UsbReceiver registered")
    }

    interface Listener {
        fun onUsbDetach(device: UsbDevice)
        fun onUsbAttach(device: UsbDevice)
        fun onUsbAccessoryDetach()
        fun onUsbPermission(granted: Boolean, connect: Boolean, device: UsbDevice)
    }

    override fun onReceive(context: Context, intent: Intent) {
        val action = intent.action ?: return
        AppLog.i("USB Intent: $intent")

        if (action == UsbManager.ACTION_USB_ACCESSORY_DETACHED) {
            mListener.onUsbAccessoryDetach()
            return
        }

        val device: UsbDevice = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
        } else {
            @Suppress("DEPRECATION")
            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
        } ?: return

        when (action) {
            UsbManager.ACTION_USB_DEVICE_DETACHED -> // If detach...
                mListener.onUsbDetach(device)
            // Handle detached device
            UsbManager.ACTION_USB_DEVICE_ATTACHED -> // If attach...
                mListener.onUsbAttach(device)
            ACTION_USB_DEVICE_PERMISSION -> {
                // If Our App specific Intent for permission request...
                val permissionGranted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                val connect = intent.getBooleanExtra(EXTRA_CONNECT, false)
                mListener.onUsbPermission(permissionGranted, connect, device)
            }
        }
    }

    companion object {
        const val ACTION_USB_DEVICE_PERMISSION = "com.andrerinas.headunitrevived" + ".ACTION_USB_DEVICE_PERMISSION"
        const val EXTRA_CONNECT = "EXTRA_CONNECT"

        fun createPermissionPendingIntent(context: Context): PendingIntent {
            return PendingIntent.getBroadcast(
                context, 0,
                Intent(ACTION_USB_DEVICE_PERMISSION).apply {
                    setPackage(context.packageName)
                },
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                    PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE
                else PendingIntent.FLAG_UPDATE_CURRENT
            )
        }

        fun createFilter(): IntentFilter {
            val filter = IntentFilter()
            filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
            filter.addAction(UsbManager.ACTION_USB_ACCESSORY_DETACHED)
            filter.addAction(ACTION_USB_DEVICE_PERMISSION)
            return filter
        }

        fun match(action: String): Boolean {
            return when (action) {
                UsbManager.ACTION_USB_DEVICE_DETACHED -> true
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> true
                UsbManager.ACTION_USB_ACCESSORY_DETACHED -> true
                ACTION_USB_DEVICE_PERMISSION -> true
                else -> false
            }
        }
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/connection/SocketAccessoryConnection.kt`:

```kt
package com.andrerinas.headunitrevived.connection

import android.content.Context
import android.net.ConnectivityManager
import android.os.Build
import com.andrerinas.headunitrevived.utils.AppLog
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.DataInputStream
import java.io.IOException
import java.io.OutputStream
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketTimeoutException

class SocketAccessoryConnection(private val ip: String, private val port: Int, private val context: Context) : AccessoryConnection {
    private var output: OutputStream? = null
    private var input: DataInputStream? = null
    private var transport: Socket

    init {
        transport = Socket()
    }

    constructor(socket: Socket, context: Context) : this(socket.inetAddress.hostAddress ?: "", socket.port, context) {
        this.transport = socket
        // Pre-connected sockets (like NearbySocket) need their streams initialized immediately
        // because connect() might not be called or might be bypassed.
        if (socket.isConnected) {
            try {
                this.input = DataInputStream(socket.getInputStream())
                this.output = socket.getOutputStream()
            } catch (e: IOException) {
                AppLog.e("Failed to get streams from pre-connected socket", e)
            }
        }
    }


    override val isSingleMessage: Boolean
        get() = true

    override fun sendBlocking(buf: ByteArray, length: Int, timeout: Int): Int {
        val out = output ?: return -1
        return try {
            out.write(buf, 0, length)
            out.flush()
            length
        } catch (e: IOException) {
            AppLog.e(e)
            -1
        }
    }

    override fun recvBlocking(buf: ByteArray, length: Int, timeout: Int, readFully: Boolean): Int {
        val inp = input ?: return -1
        return try {
            // Dynamically apply the caller's timeout so handshake (short timeouts)
            // and streaming (long timeouts) both work correctly on the same socket.
            try { transport.soTimeout = timeout } catch (_: Exception) {}
            if (readFully) {
                inp.readFully(buf, 0, length)
                length
            } else {
                inp.read(buf, 0, length)
            }
        } catch (e: SocketTimeoutException) {
            // With raw DataInputStream (no BufferedInputStream), timeout during
            // small reads (4-byte header) virtually never causes partial consumption.
            // Let the caller decide if this is fatal based on context.
            0
        } catch (e: IOException) {
            -1
        }
    }

    override val isConnected: Boolean
        get() = transport.isConnected

    override suspend fun connect(): Boolean = withContext(Dispatchers.IO) {
        try {
            if (!transport.isConnected) {
                val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                   val net = cm.activeNetwork

                    if (net != null) {
                        try {
                            net.bindSocket(transport)
                            AppLog.i("Bound socket to active network: $net")
                        } catch (e: Exception) {
                            AppLog.w("Failed to bind socket to network", e)
                        }
                    }
                } else {
                    // Legacy API < 23 (Lollipop & KitKat & JB)
                    @Suppress("DEPRECATION")
                    if (cm.getNetworkInfo(ConnectivityManager.TYPE_WIFI)?.isConnected == true) {
                        try {
                            val addr = InetAddress.getByName(ip)
                            val b = addr.address
                            val ipInt = ((b[3].toInt() and 0xFF) shl 24) or
                                        ((b[2].toInt() and 0xFF) shl 16) or
                                        ((b[1].toInt() and 0xFF) shl 8) or
                                        (b[0].toInt() and 0xFF)
                            // cm.requestRouteToHost(ConnectivityManager.TYPE_WIFI, ipInt)
                            // Use reflection because requestRouteToHost is removed in newer SDKs
                            val m = cm.javaClass.getMethod("requestRouteToHost", Int::class.javaPrimitiveType, Int::class.javaPrimitiveType)
                            m.invoke(cm, ConnectivityManager.TYPE_WIFI, ipInt)
                            AppLog.i("Legacy: Requested route to host $ip")
                        } catch (e: Exception) {
                            AppLog.w("Legacy: Failed requestRouteToHost", e)
                        }
                    }
                }

                // Chinese Headunit Mediatek Correction
                try {
                    transport.connect(InetSocketAddress(ip, port), 5000)
                } catch (e: Throwable) {
                    val errorMessage = e.message ?: e.toString()
                    if (errorMessage.contains("com.mediatek.cta.CtaHttp") || errorMessage.contains("CtaHttp")) {
                        AppLog.e("HUR_DEBUG: MediaTek crash intercepted.")
                    } else {
                        throw IOException(e)
                    }
                }
                // Chinese Headunit Mediatek Correction
            }
            // WiFi needs tolerance for retransmissions,
            // power-save wakes, and bufferbloat. 1s was causing readFully to timeout
            // mid-header, desynchronizing the stream ("Failed to read full header").
            transport.soTimeout = 10000
            transport.tcpNoDelay = true
            transport.keepAlive = true
            transport.reuseAddress = true
            transport.trafficClass = 16 // IPTOS_LOWDELAY
            // Raw DataInputStream — no BufferedInputStream wrapper.
            // BufferedInputStream + readFully + timeout = internal buffer state corruption.
            input = DataInputStream(transport.getInputStream())
            output = transport.getOutputStream()
            return@withContext true
        } catch (e: IOException) {
            AppLog.e(e)
            return@withContext false
        }
    }

    override fun disconnect() {
        if (transport.isConnected) {
            try {
                transport.close()
            } catch (e: IOException) {
                AppLog.e(e)
            }

        }
        input = null
        output = null
    }

    companion object {
        private const val DEF_BUFFER_LENGTH = 131080
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/connection/NetworkDiscovery.kt`:

```kt
package com.andrerinas.headunitrevived.connection

import android.content.Context
import android.net.ConnectivityManager
import android.os.Build
import com.andrerinas.headunitrevived.utils.AppLog
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.net.Inet4Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.NetworkInterface
import java.net.Socket
import java.util.Collections

class NetworkDiscovery(private val context: Context, private val listener: Listener) {

    interface Listener {
        fun onServiceFound(ip: String, port: Int, socket: Socket? = null)
        fun onScanFinished()
    }

    private var scanJob: Job? = null
    private val reportedIps = java.util.Collections.synchronizedSet(mutableSetOf<String>())

    fun startScan() {
        if (scanJob?.isActive == true) return

        reportedIps.clear()
        AppLog.i("NetworkDiscovery: Starting scan...")

        scanJob = CoroutineScope(Dispatchers.IO).launch {
            try {
                // 1. Quick Scan: Check likely Gateways first
                AppLog.i("NetworkDiscovery: Step 1 - Quick Gateway Scan")
                val gatewayFound = scanGateways()

                if (gatewayFound) {
                    AppLog.i("NetworkDiscovery: Gateway found service, skipping subnet scan.")
                    return@launch
                }

                // 2. Deep Scan: Check entire Subnet
                AppLog.i("NetworkDiscovery: Step 2 - Full Subnet Scan")
                scanSubnet()
            } finally {
                withContext(Dispatchers.Main) {
                    listener.onScanFinished()
                }
            }
        }
    }

    private suspend fun scanGateways(): Boolean {
        var foundAny = false
        try {
            val suspects = mutableSetOf<String>()

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
                val activeNet = cm.activeNetwork
                if (activeNet != null) {
                    val lp = cm.getLinkProperties(activeNet)
                    lp?.routes?.forEach { route ->
                        if (route.isDefaultRoute && route.gateway is Inet4Address) {
                            route.gateway?.hostAddress?.let { suspects.add(it) }
                        }
                    }
                }
            }
            // Always try heuristics (X.X.X.1) for all interfaces
            collectInterfaceSuspects(suspects)

            // Special case for emulators: 10.0.2.2 is the host machine
            if (isEmulator()) {
                suspects.add("10.0.2.2")
            }

            if (suspects.isNotEmpty()) {
                AppLog.i("NetworkDiscovery: Checking suspects: $suspects")
                for (ip in suspects) {
                    if (checkAndReport(ip)) {
                        foundAny = true
                    }
                }
            }
        } catch (e: Exception) {
            AppLog.e("NetworkDiscovery: Gateway scan error", e)
        }
        return foundAny
    }

    private suspend fun scanSubnet() {
        val subnet = getSubnet()
        if (subnet == null) {
            AppLog.e("NetworkDiscovery: Could not determine subnet for deep scan")
            return
        }

        val myIp = getLocalIpAddress()
        AppLog.i("NetworkDiscovery: Scanning subnet: $subnet.*")

        val tasks = mutableListOf<Deferred<Boolean>>()

        // Scan range 1..254
        for (i in 1..254) {
            val ip = "$subnet.$i"
            if (ip == myIp) continue // Skip self

            tasks.add(CoroutineScope(Dispatchers.IO).async {
                checkAndReport(ip)
            })
        }

        tasks.awaitAll()
    }

    private fun getLocalIpAddress(): String? {
        try {
            val interfaces = Collections.list(NetworkInterface.getNetworkInterfaces())
            for (intf in interfaces) {
                if (intf.isLoopback || !intf.isUp) continue

                val addrs = Collections.list(intf.inetAddresses)
                for (addr in addrs) {
                    if (addr is java.net.Inet4Address) {
                        return addr.hostAddress
                    }
                }
            }
        } catch (e: Exception) {
            AppLog.e("NetworkDiscovery: Error getting local IP", e)
        }
        return null
    }

    private suspend fun checkAndReport(ip: String): Boolean {
        if (reportedIps.contains(ip)) return true

        // Check Port 5289 (Wifi Launcher) - prioritizing this
        val launcherSocket = checkPort(ip, 5289, timeout = 300)
        if (launcherSocket != null) {
            AppLog.i("NetworkDiscovery: Found Wifi Launcher on $ip:5289")
            reportedIps.add(ip)
            withContext(Dispatchers.Main) {
                try { launcherSocket.close() } catch (e: Exception) {}
                listener.onServiceFound(ip, 5289)
            }
            return true
        }

        // Check Port 5277 (Standard Headunit)
        val serverSocket = checkPort(ip, 5277, timeout = 300)
        if (serverSocket != null) {
            AppLog.i("NetworkDiscovery: Found Headunit Server on $ip:5277")
            reportedIps.add(ip)
            withContext(Dispatchers.Main) {
                // DO NOT CLOSE serverSocket! Pass it to the listener.
                listener.onServiceFound(ip, 5277, serverSocket)
            }
            return true
        }

        return false
    }

    private fun checkPort(ip: String, port: Int, timeout: Int = 500): Socket? {
        return try {
            val socket = Socket()
            socket.connect(InetSocketAddress(ip, port), timeout)
            socket
        } catch (e: Exception) {
            null
        }
    }

    private fun collectInterfaceSuspects(suspects: MutableSet<String>) {
        try {
            val interfaces = NetworkInterface.getNetworkInterfaces() ?: return
            while (interfaces.hasMoreElements()) {
                val iface = interfaces.nextElement()
                if (iface.isLoopback || !iface.isUp) continue

                val addresses = iface.inetAddresses
                while (addresses.hasMoreElements()) {
                    val addr = addresses.nextElement()
                    if (addr is Inet4Address) {
                        // Heuristic: Gateway is usually .1 in the same subnet
                        val ipBytes = addr.address
                        ipBytes[3] = 1
                        val suspectIp = InetAddress.getByAddress(ipBytes).hostAddress
                        // Only add if it's not our own IP (though checking own IP is fast anyway)
                        suspects.add(suspectIp)
                    }
                }
            }
        } catch (e: Exception) {
            AppLog.e("NetworkDiscovery: Interface collection failed", e)
        }
    }

    private fun getSubnet(): String? {
        // Reuse similar logic to collectInterfaceSuspects but return subnet string
        try {
             val interfaces = Collections.list(NetworkInterface.getNetworkInterfaces())
             for (networkInterface in interfaces) {
                 if (!networkInterface.isUp || networkInterface.isLoopback) continue

                 for (addr in Collections.list(networkInterface.inetAddresses)) {
                     if (addr is Inet4Address) {
                         val host = addr.hostAddress
                         val lastDot = host.lastIndexOf('.')
                         if (lastDot > 0) {
                             return host.substring(0, lastDot)
                         }
                     }
                 }
             }
        } catch (e: Exception) {
            AppLog.e("NetworkDiscovery: Failed to get subnet", e)
        }
        return null
    }

    fun stop() {
        scanJob?.cancel()
        scanJob = null
    }

    private fun isEmulator(): Boolean {
        return (Build.BRAND.startsWith("generic") && Build.DEVICE.startsWith("generic"))
                || Build.FINGERPRINT.startsWith("generic")
                || Build.FINGERPRINT.startsWith("unknown")
                || Build.HARDWARE.contains("goldfish")
                || Build.HARDWARE.contains("ranchu")
                || Build.MODEL.contains("google_sdk")
                || Build.MODEL.contains("Emulator")
                || Build.MODEL.contains("Android SDK built for x86")
                || Build.MANUFACTURER.contains("Genymotion")
                || Build.PRODUCT.contains("sdk_google")
                || Build.PRODUCT.contains("google_sdk")
                || Build.PRODUCT.contains("sdk")
                || Build.PRODUCT.contains("sdk_x86")
                || Build.PRODUCT.contains("vbox86p")
                || Build.PRODUCT.contains("emulator")
                || Build.PRODUCT.contains("simulator")
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/connection/UsbDeviceCompat.kt`:

```kt
package com.andrerinas.headunitrevived.connection

import android.hardware.usb.UsbDevice
import com.andrerinas.headunitrevived.aap.Utils
import java.util.Locale

class UsbDeviceCompat(val wrappedDevice: UsbDevice) {

    val deviceName: String
        get() = wrappedDevice.deviceName

    val uniqueName: String
        get() = getUniqueName(wrappedDevice)

    override fun toString(): String {
        return String.format(Locale.US, "%s - %s", uniqueName, wrappedDevice.toString())
    }

    val isInAccessoryMode: Boolean
        get() = isInAccessoryMode(wrappedDevice)

    companion object {
        private const val USB_VID_GOO = 0x18D1   // 6353   Nexus or ACC mode, see PID to distinguish
        private const val USB_VID_HTC = 0x0bb4   // 2996
        private const val USB_VID_SAM = 0x04e8   // 1256
        private const val USB_VID_O1A = 0xfff6   // 65526    Samsung ?
        private const val USB_VID_SON = 0x0fce   // 4046
        private const val USB_VID_LGE = 0x1004   // 65525
        private const val USB_VID_MOT = 0x22b8   // 8888
        private const val USB_VID_ACE = 0x0502
        private const val USB_VID_HUA = 0x12d1
        private const val USB_VID_ZTE = 0x19d2
        private const val USB_VID_XIA = 0x2717
        private const val USB_VID_ASU = 0x0b05
        private const val USB_VID_MEI = 0x2a45
        private const val USB_VID_WIL = 0x4ee7

        private const val USB_PID_ACC = 0x2D00      // Accessory                  100
        private const val USB_PID_ACC_ADB = 0x2D01      // Accessory + ADB            110

        private val VENDOR_NAMES = mapOf(
            USB_VID_GOO to "Google",
            USB_VID_HTC to "HTC",
            USB_VID_SAM to "Samsung",
            USB_VID_SON to "Sony",
            USB_VID_MOT to "Motorola",
            USB_VID_LGE to "LG",
            USB_VID_O1A to "O1A",
            USB_VID_HUA to "Huawei",
            USB_VID_ACE to "Acer",
            USB_VID_ZTE to "ZTE",
            USB_VID_XIA to "Xiaomi",
            USB_VID_ASU to "Asus",
            USB_VID_MEI to "Meizu",
            USB_VID_WIL to "Wileyfox"
        )

        fun getUniqueName(device: UsbDevice): String {
            val vendorId = device.vendorId  // mVendorId=2996               HTC
            val productId = device.productId  // mProductId=1562              OneM8

//            if (App.IS_LOLLIPOP) {                                 // Android 5.0+ only
//                try {
//                    dev_man = usb_man_get(device).toUpperCase(Locale.getDefault())                             // mManufacturerName=HTC
//                    dev_prod = usb_pro_get(device).toUpperCase(Locale.getDefault())                                // mProductName=Android Phone
//                    dev_ser = usb_ser_get(device).toUpperCase(Locale.getDefault())                              // mSerialNumber=FA46RWM22264
//                } catch (e: Throwable) {
//                    AppLog.e(e)
//                }
//            }

            var usb_dev_name = ""
            usb_dev_name += VENDOR_NAMES[vendorId] ?: "$vendorId"
            usb_dev_name += " "
            usb_dev_name += Utils.hex_get(vendorId.toShort())
            usb_dev_name += ":"
            usb_dev_name += Utils.hex_get(productId.toShort())

            return usb_dev_name
        }

        fun isInAccessoryMode(device: UsbDevice): Boolean {
            val dev_vend_id = device.vendorId
            val dev_prod_id = device.productId
            return dev_vend_id == UsbDeviceCompat.USB_VID_GOO &&
                    (dev_prod_id == UsbDeviceCompat.USB_PID_ACC || dev_prod_id == UsbDeviceCompat.USB_PID_ACC_ADB)
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/connection/CommManager.kt`:

````kt
package com.andrerinas.headunitrevived.connection
import android.app.Application
import android.content.Context
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import com.andrerinas.headunitrevived.aap.AapSslContext
import com.andrerinas.headunitrevived.aap.AapTransport
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.main.BackgroundNotification
import com.andrerinas.headunitrevived.ssl.SingleKeyKeyManager
import com.andrerinas.headunitrevived.utils.Settings
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import com.andrerinas.headunitrevived.decoder.AudioDecoder
import com.andrerinas.headunitrevived.decoder.VideoDecoder
import android.media.AudioManager
import com.andrerinas.headunitrevived.aap.AapMessage
import com.andrerinas.headunitrevived.aap.protocol.messages.SensorEvent
import com.andrerinas.headunitrevived.aap.protocol.proto.MediaPlayback
import java.net.Socket

/**
 * Central connection and transport lifecycle manager.
 *
 * CommManager owns the full lifecycle of both the physical connection ([AccessoryConnection])
 * and the AAP protocol layer ([AapTransport]). It exposes a single [connectionState] flow as
 * the source of truth; all consumers (AapService, AapProjectionActivity, UI fragments) observe
 * this flow reactively instead of being called imperatively.
 *
 * ## State machine
 * ```
 *   Disconnected ──connect()──► Connecting ──success──► Connected
 *                                                            │
 *                                                   startHandshake()
 *                                                            │
 *                                                   StartingTransport
 *                                                            │
 *                                                     SSL done
 *                                                            │
 *                                                   HandshakeComplete
 *                                                            │
 *                                                    startReading()
 *                                                            │
 *                                                    TransportStarted
 *                                                            │
 *                                      disconnect() / read error / phone bye-bye
 *                                                            │
 *                                                      Disconnected
 * ```
 *
 * ## Thread safety
 * [_transport] and [_connection] are `@Volatile`. All state mutations go through
 * [_connectionState] (a [MutableStateFlow]) which is thread-safe. The internal [_scope] uses
 * [Dispatchers.IO] with a [SupervisorJob] so individual coroutine failures do not cancel the
 * entire scope.
 */
class CommManager(
    private val context: Context,
    private val settings: Settings,
    private val audioDecoder: AudioDecoder,
    private val videoDecoder: VideoDecoder) {

    // Single AapSslContext for the lifetime of this CommManager. Its internal SSLContext holds
    // JSSE's ClientSessionContext session cache, which survives transport recreations and enables
    // abbreviated TLS handshakes on reconnect (session resumption).
    private val aapSslContext: AapSslContext = AapSslContext(SingleKeyKeyManager(context))

    /**
     * Represents the lifecycle state of the Android Auto connection.
     *
     * State transitions are strictly sequential — see the class-level diagram.
     */
    sealed class ConnectionState {
        /**
         * No active connection.
         * @param isClean `true` if the phone sent a graceful `ByeByeRequest` before closing;
         *                `false` for all other disconnect causes (USB detach, read error,
         *                socket timeout, explicit user disconnect).
         */
        data class Disconnected(
            val isClean: Boolean = false,
            val isUserExit: Boolean = false
        ) : ConnectionState()

        /** Physical connection handshake in progress (USB open or TCP connect). */
        object Connecting : ConnectionState()

        /** Physical connection established; AAP handshake not yet started. */
        object Connected : ConnectionState()

        /** AAP SSL handshake in progress. */
        object StartingTransport : ConnectionState()

        /**
         * SSL handshake complete;
         */
        object HandshakeComplete : ConnectionState()

        /** AAP handshake complete; the transport is ready to send and receive messages. */
        object TransportStarted : ConnectionState()

        /** A non-fatal error occurred. The manager transitions to [Disconnected] immediately after. */
        data class Error(val message: String) : ConnectionState()
    }

    /** IO-bound coroutine scope for all async connection work. SupervisorJob prevents one
     *  failing child from cancelling the rest. */
    private val _scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected())

    /** Callback for audio focus state changes (isPlaying). Set by AapService. */
    var onAudioFocusStateChanged: ((Boolean) -> Unit)? = null

    /** Now-playing metadata from the phone (AAP media channel). Set by AapService. */
    var onAaMediaMetadata: ((MediaPlayback.MediaMetaData) -> Unit)? = null
    /** Playback status from the phone (AAP media channel), includes current position. */
    var onAaPlaybackStatus: ((MediaPlayback.MediaPlaybackStatus) -> Unit)? = null

    /** @Volatile: written on IO thread, read on Main and IO threads. */
    @Volatile private var _transport: AapTransport? = null
    var onUpdateUiConfigReplyReceived: (() -> Unit)? = null
    @Volatile private var _connection: AccessoryConnection? = null

    /**
     * Tracks the most-recently-launched [doDisconnect] coroutine.
     * [connect] overloads join this job before opening a new connection, ensuring the previous
     * device is fully closed before `openDevice()` is called on it again.
     */
    @Volatile private var _disconnectJob: kotlinx.coroutines.Job? = null

    private val _backgroundNotification = BackgroundNotification(context)

    /** Public read-only view of [_connectionState]. */
    val connectionState = _connectionState.asStateFlow()

    /**
     * `true` while a physical connection exists, regardless of whether the AAP transport
     * handshake has completed. Covers [ConnectionState.Connected], [ConnectionState.StartingTransport],
     * and [ConnectionState.TransportStarted].
     */
    val isConnected: Boolean
        get() = connectionState.value.let {
            it is ConnectionState.Connected ||
            it is ConnectionState.StartingTransport ||
            it is ConnectionState.HandshakeComplete ||
            it is ConnectionState.TransportStarted
        }

    /**
     * Returns `true` if the current USB connection is to [device].
     * Used by AapService to decide whether a USB detach event should trigger a disconnect.
     */
    fun isConnectedToUsbDevice(device: UsbDevice): Boolean =
        (_connection as? UsbAccessoryConnection)?.isDeviceRunning(device) == true

    // -----------------------------------------------------------------------------------------
    // connect() overloads — one for each transport type
    // -----------------------------------------------------------------------------------------

    /**
     * Opens a USB accessory connection to [device].
     *
     * On success emits [ConnectionState.Connected] and persists the device as the last-used
     * connection so it can be auto-reconnected on the next launch.
     */
    suspend fun connect(device: UsbDevice) = withContext(Dispatchers.IO) {
        // Another caller already started the connection — do nothing.
        if (_connectionState.value is ConnectionState.Connecting)
            return@withContext

        val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager
        if (!usbManager.hasPermission(device)) {
            _connectionState.emit(ConnectionState.Error("USB permission not granted for device"))
            return@withContext
        }

        // Wait for any in-progress cleanup to finish before opening the USB device.
        // Without this, openDevice() on the same hardware can return null because the previous
        // UsbDeviceConnection hasn't been close()d yet.
        _disconnectJob?.join()

        try {
            _connectionState.emit(ConnectionState.Connecting)
            _connection?.disconnect()
            _connection = UsbAccessoryConnection(usbManager, device)

            if (_connection?.connect() ?: false) {
                settings.saveLastConnection(type = Settings.CONNECTION_TYPE_USB, usbDevice = UsbDeviceCompat.getUniqueName(device))
                _connectionState.emit(ConnectionState.Connected)
            } else {
                _connectionState.emit(ConnectionState.Disconnected())
            }
        } catch (e: Exception) {
            _connectionState.emit(ConnectionState.Error("Connection failed: ${e.message}"))
            disconnect()
        }
    }

    /**
     * Wraps an already-connected [Socket] (e.g. accepted by `WirelessServer`) in a
     * [SocketAccessoryConnection] and advances to [ConnectionState.Connected].
     *
     * The socket must already be connected; this overload skips the TCP handshake and only
     * sets up the AAP framing layer.
     */
    suspend fun connect(socket: Socket) = withContext(Dispatchers.IO) {
        // Another caller already started the connection — do nothing.
        if (_connectionState.value is ConnectionState.Connecting)
            return@withContext

        _disconnectJob?.join()

        try {
            _connectionState.emit(ConnectionState.Connecting)
            _connection?.disconnect()
            _connection = SocketAccessoryConnection(socket, context)

            if (_connection?.connect() ?: false) {
                // [FIX] Don't overwrite NEARBY connection type with WIFI + localhost IP (::1)
                if (socket !is NearbySocket) {
                    settings.saveLastConnection(type = Settings.CONNECTION_TYPE_WIFI, ip = socket.inetAddress?.hostAddress ?: "")
                }
                _connectionState.emit(ConnectionState.Connected)
            } else {
                _connectionState.emit(ConnectionState.Disconnected())
            }
        } catch (e: Exception) {
            _connectionState.emit(ConnectionState.Error("Connection failed: ${e.message}"))
            disconnect()
        }
    }

    /**
     * Opens a TCP connection to [ip]:[port] and advances to [ConnectionState.Connected].
     *
     * Used by the manual IP entry flow and the NSD-discovered device list.
     */
    suspend fun connect(ip: String, port: Int) = withContext(Dispatchers.IO) {
        // Another caller already started the connection — do nothing.
        if (_connectionState.value is ConnectionState.Connecting)
            return@withContext

        _disconnectJob?.join()

        try {
            _connectionState.emit(ConnectionState.Connecting)
            _connection?.disconnect()
            _connection = SocketAccessoryConnection(ip, port, context)

            if (_connection?.connect() ?: false) {
                settings.saveLastConnection(type = Settings.CONNECTION_TYPE_WIFI, ip = ip)
                _connectionState.emit(ConnectionState.Connected)
            } else {
                _connectionState.emit(ConnectionState.Disconnected())
            }
        } catch (e: Exception) {
            _connectionState.emit(ConnectionState.Error("Connection failed: ${e.message}"))
            disconnect()
        }
    }

    // -----------------------------------------------------------------------------------------
    // Transport lifecycle
    // -----------------------------------------------------------------------------------------

    /**
     * Phase 1: runs the SSL handshake over the current connection.
     *
     * Must only be called when state is [ConnectionState.Connected]. On success, emits
     * [ConnectionState.HandshakeComplete] and returns; the inbound message loop is NOT
     * started yet. Call [startReading] after [VideoDecoder.setSurface] has been invoked
     * to begin receiving messages.
     *
     * The [AapTransport.onQuit] callback is wired here; it fires whenever the transport
     * stops (read error, phone bye-bye, timeout) and triggers [transportedQuited].
     *
     * Called by [com.andrerinas.headunitrevived.aap.AapService] in parallel with the
     * projection activity startup, so the handshake latency is hidden behind activity
     * inflation time rather than added on top of it.
     */
    suspend fun startHandshake() = withContext(Dispatchers.IO) {
        // Another caller already started the handshake — do nothing.
        if (_connectionState.value is ConnectionState.StartingTransport) return@withContext

        try {
            if (_connectionState.value is ConnectionState.Connected) {
                _connectionState.emit(ConnectionState.StartingTransport)

                if (_transport == null) {
                    val audioManager = context.getSystemService(Application.AUDIO_SERVICE) as AudioManager
                    _transport = AapTransport(
                        audioDecoder,
                        videoDecoder,
                        audioManager,
                        settings,
                        _backgroundNotification,
                        context,
                        externalSsl = aapSslContext,
                        onAaMediaMetadata = { meta -> onAaMediaMetadata?.invoke(meta) },
                        onAaPlaybackStatus = { status -> onAaPlaybackStatus?.invoke(status) }
                    )
                    _transport!!.onQuit = { isClean -> transportedQuited(isClean) }
                    _transport!!.onAudioFocusStateChanged = { isPlaying -> onAudioFocusStateChanged?.invoke(isPlaying) }
                    _transport!!.onUpdateUiConfigReplyReceived = { onUpdateUiConfigReplyReceived?.invoke() }
                }
                if (_transport?.startHandshake(_connection!!) == true) {
                    _connectionState.emit(ConnectionState.HandshakeComplete)
                } else {
                    _connectionState.emit(ConnectionState.Error("Handshake failed"))
                    disconnect()
                }
            } else {
                _connectionState.emit(ConnectionState.Error("Starting handshake without connection"))
            }
        } catch (e: Exception) {
            _connectionState.emit(ConnectionState.Error("Handshake failed: ${e.message}"))
            disconnect()
        }
    }

    /**
     * Phase 2: starts the inbound message loop.
     *
     * Must only be called when state is [ConnectionState.HandshakeComplete], which implies
     * both that the SSL handshake has succeeded **and** that [VideoDecoder.setSurface] has
     * already been called by [com.andrerinas.headunitrevived.aap.AapProjectionActivity].
     * This ordering guarantees no video frame is ever decoded before a render target exists.
     *
     * On success:
     * 1. Claims audio focus for `STREAM_MUSIC`.
     * 2. Starts the [AapTransport] read loop.
     * 3. Emits [ConnectionState.TransportStarted].
     */
    suspend fun startReading() = withContext(Dispatchers.IO) {
        if (_connectionState.value !is ConnectionState.HandshakeComplete) return@withContext

        try {
            _transport?.aapAudio?.requestFocusChange(
                AudioManager.STREAM_MUSIC,
                AudioManager.AUDIOFOCUS_GAIN,
                AudioManager.OnAudioFocusChangeListener { }
            )
            _transport?.startReading()
            _connectionState.emit(ConnectionState.TransportStarted)
        } catch (e: Exception) {
            _connectionState.emit(ConnectionState.Error("Start reading failed: ${e.message}"))
            disconnect()
        }
    }

    /**
     * Called by `AapTransport.onQuit` when the transport stops itself (read error, socket
     * timeout, or phone-initiated graceful close).
     *
     * Sets state to [ConnectionState.Disconnected] synchronously (so [isConnected] returns
     * `false` immediately) then schedules cleanup. `sendByeBye` is `false` because the
     * connection is already dead — there is no point sending a `ByeByeRequest`.
     */
    private fun transportedQuited(isClean: Boolean) {
        val wasUserExit = _transport?.wasUserExit ?: false
        _connectionState.value = ConnectionState.Disconnected(isClean, isUserExit = wasUserExit)
        // Transport already quit on its own — no ByeByeRequest needed (connection is dead).
        _disconnectJob = _scope.launch { doDisconnect(sendByeBye = false) }
        if (settings.killOnDisconnect) {
            android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                // Stop the foreground service first to remove the notification
                val stopIntent = android.content.Intent(context, com.andrerinas.headunitrevived.aap.AapService::class.java).apply {
                    action = com.andrerinas.headunitrevived.aap.AapService.ACTION_STOP_SERVICE
                }
                com.andrerinas.headunitrevived.aap.AapService.killProcessOnDestroy = true
                context.stopService(stopIntent)
                // Finish all tasks
                val app = context.applicationContext as Application
                val activityManager = app.getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager
                activityManager.appTasks.forEach { it.finishAndRemoveTask() }
            }, 500)
        }
    }

    // -----------------------------------------------------------------------------------------
    // send() overloads — fire-and-forget; silently dropped if not TransportStarted
    // -----------------------------------------------------------------------------------------

    /** Sends a key press or release event to the phone. */
    fun send(keyCode: Int, isPress: Boolean) {
        if (_connectionState.value is ConnectionState.TransportStarted) {
            _transport?.send(keyCode, isPress)
        }
    }

    /** Sends a sensor event (e.g. driving status, night mode) to the phone. */
    fun send(sensor: SensorEvent) {
        if (_connectionState.value is ConnectionState.TransportStarted) {
            _transport?.send(sensor)
        }
    }

    /** Sends a raw [AapMessage] (e.g. touch event, video focus request) to the phone. */
    fun send(message: AapMessage) {
        if (_connectionState.value is ConnectionState.TransportStarted) {
            _transport?.send(message)
        }
    }

    fun sendUpdateUiConfigRequest(left: Int, top: Int, right: Int, bottom: Int) {
        val request = com.andrerinas.headunitrevived.aap.protocol.messages.UpdateUiConfigRequest(left, top, right, bottom)
        AppLog.i("[UI_DEBUG_FIX] TX UpdateUiConfigRequest: L=$left T=$top R=$right B=$bottom")
        send(request)
        // HUR always sends VideoFocusNotification(PROJECTED, unsolicited=true) after
        // updating the UI config. This triggers a keyframe from the phone.
        send(com.andrerinas.headunitrevived.aap.protocol.messages.VideoFocusEvent(gain = true, unsolicited = true))
    }

    // -----------------------------------------------------------------------------------------
    // Disconnect
    // -----------------------------------------------------------------------------------------

    /**
     * Initiates a user-requested disconnect.
     *
     * Sets state to [ConnectionState.Disconnected] synchronously so callers see the change
     * immediately, then schedules async cleanup via [doDisconnect]. A ByeByeRequest is sent
     * to the phone before closing the connection.
     */
    fun disconnect(sendByeBye: Boolean = true) {
        if (_connectionState.value is ConnectionState.Disconnected) return

        com.andrerinas.headunitrevived.utils.HeadUnitScreenConfig.unlockResolution()

        _connectionState.value = ConnectionState.Disconnected(isUserExit = true)
        _transport?.wasUserExit = true
        _disconnectJob = _scope.launch { doDisconnect(sendByeBye) }
        if (settings.killOnDisconnect) {
            android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                // Stop the foreground service first to remove the notification
                val stopIntent = android.content.Intent(context, com.andrerinas.headunitrevived.aap.AapService::class.java).apply {
                    action = com.andrerinas.headunitrevived.aap.AapService.ACTION_STOP_SERVICE
                }
                com.andrerinas.headunitrevived.aap.AapService.killProcessOnDestroy = true
                context.stopService(stopIntent)
                // Finish all tasks
                val app = context.applicationContext as Application
                val activityManager = app.getSystemService(Context.ACTIVITY_SERVICE) as android.app.ActivityManager
                activityManager.appTasks.forEach { it.finishAndRemoveTask() }
            }, 500)
        }
    }

    /**
     * Tears down the transport and physical connection.
     *
     * **Null-first pattern**: [_transport] and [_connection] are captured and nulled at the
     * very start. This prevents re-entrant double-cleanup: `AapTransport.stop()` fires `onQuit`
     * → [transportedQuited] → a second [doDisconnect] call — which now finds both fields null
     * and exits cleanly.
     *
     * @param sendByeBye `true` (default) when the user initiates the disconnect — calls
     *   `AapTransport.stop()`, which sends a `ByeByeRequest` to the phone and waits ~150 ms
     *   for acknowledgement. `false` when the transport self-quit (read error, socket timeout):
     *   the connection is already dead, so `AapTransport.quit()` is called directly to skip
     *   the send and the sleep.
     */
    private fun doDisconnect(sendByeBye: Boolean = true) {
        // Capture and null out immediately to prevent a second doDisconnect() call
        // (from transportedQuited firing onQuit during stop()) from double-stopping.
        val transport = _transport
        val connection = _connection
        _transport = null
        _connection = null
        try {
            // Only send ByeByeRequest when we are initiating the disconnect (e.g. user pressed
            // disconnect). When the transport self-quit (read error, soTimeout), the connection
            // is already dead — skip the send and the 150 ms sleep inside stop().
            if (sendByeBye) transport?.stop() else transport?.quit()
            connection?.disconnect()
        } catch (e: Exception) {
            AppLog.e("doDisconnect error: ${e.message}")
        } finally {
            if (_connectionState.value !is ConnectionState.Disconnected) {
                _connectionState.value = ConnectionState.Disconnected()
            }
        }
    }

    /**
     * Performs a final disconnect and cancels the internal coroutine scope.
     *
     * Call this when the owning component (e.g. the foreground service) is destroyed.
     * After [destroy], the CommManager instance must not be used again.
     */
    fun destroy() {
        doDisconnect()
        _scope.cancel()
    }
}

````

`app/src/main/java/com/andrerinas/headunitrevived/connection/UsbAccessoryConnection.kt`:

```kt
package com.andrerinas.headunitrevived.connection

import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbEndpoint
import android.hardware.usb.UsbInterface
import android.hardware.usb.UsbManager

import android.os.SystemClock
import com.andrerinas.headunitrevived.utils.AppLog
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

class UsbAccessoryConnection(private val usbMgr: UsbManager, private val device: UsbDevice) : AccessoryConnection {
    // @Volatile so isConnected / isDeviceRunning see the latest value without a lock.
    @Volatile private var usbDeviceConnected: UsbDeviceCompat? = null
    @Volatile private var usbDeviceConnection: UsbDeviceConnection? = null
    private var usbInterface: UsbInterface? = null
    // @Volatile so sendBlocking / recvBlocking see updates from connect() / resetInterface()
    // without holding sStateLock during the transfer.
    @Volatile private var endpointIn: UsbEndpoint? = null
    @Volatile private var endpointOut: UsbEndpoint? = null

    // Internal buffer — only ever accessed by the poll thread (recvBlocking); no lock needed.
    private val internalBuffer = ByteArray(16384)
    private var internalBufferPos = 0
    private var internalBufferAvailable = 0

    fun isDeviceRunning(device: UsbDevice): Boolean {
        synchronized(sStateLock) {
            val connected = usbDeviceConnected ?: return false
            return UsbDeviceCompat.getUniqueName(device) == connected.uniqueName
        }
    }

    override suspend fun connect() = withContext(Dispatchers.IO) {
        return@withContext try {
            connect(device)
        } catch (e: UsbOpenException) {
            AppLog.e(e)
            false
        }
    }

    @Throws(UsbOpenException::class)
    private fun connect(device: UsbDevice): Boolean {
        if (usbDeviceConnection != null) {
            disconnect()
        }
        synchronized(sStateLock) {
            try {
                usbOpen(device)
            } catch (e: UsbOpenException) {
                disconnect()
                throw e
            }

            val ret = initEndpoint()
            if (ret < 0) {
                disconnect()
                return false
            }

            usbDeviceConnected = UsbDeviceCompat(device)
            return true
        }
    }

    @Throws(UsbOpenException::class)
    private fun usbOpen(device: UsbDevice) {
        var connection: UsbDeviceConnection? = null
        var lastError: Throwable? = null

        for (i in 0 until 3) {
            try {
                if (!usbMgr.hasPermission(device)) {
                    AppLog.w("No permission for USB device ${UsbDeviceCompat.getUniqueName(device)}. Waiting for system popup...")
                    // We can't request permission here easily because we need a BroadcastReceiver.
                    // But we can wait and see if the user granted it via the system dialog.
                } else {
                    connection = usbMgr.openDevice(device)
                    if (connection != null) break
                }
            } catch (t: Throwable) {
                lastError = t
                AppLog.w("Attempt ${i+1} to openDevice failed: ${t.message}")
            }
            if (i < 2) try { Thread.sleep(1000) } catch (_: Exception) {}
        }

        usbDeviceConnection = connection ?: throw UsbOpenException(lastError ?: Throwable("openDevice: connection is null (Permission missing?)"))

        AppLog.i("Established connection: " + usbDeviceConnection!!)

        try {
            val interfaceCount = device.interfaceCount
            if (interfaceCount <= 0) {
                AppLog.e("interfaceCount: $interfaceCount")
                throw UsbOpenException("No usb interfaces")
            }
            AppLog.i("interfaceCount: $interfaceCount")
            usbInterface = device.getInterface(0)

            if (!usbDeviceConnection!!.claimInterface(usbInterface, true)) {
                throw UsbOpenException("Error claiming interface")
            }
        } catch (e: Throwable) {
            AppLog.e(e)
            throw UsbOpenException(e)
        }
    }

    private fun initEndpoint(): Int {
        AppLog.i("Check accessory endpoints")
        endpointIn = null
        endpointOut = null

        for (i in 0 until usbInterface!!.endpointCount) {
            val ep = usbInterface!!.getEndpoint(i)
            if (ep.direction == UsbConstants.USB_DIR_IN) {
                if (endpointIn == null) endpointIn = ep
            } else {
                if (endpointOut == null) endpointOut = ep
            }
        }
        if (endpointIn == null || endpointOut == null) {
            AppLog.e("Unable to find bulk endpoints")
            return -1
        }

        AppLog.i("Connected have EPs")
        return 0
    }

    private fun resetInterface() {
        if (usbDeviceConnection == null) return
        synchronized(sStateLock) {
            val connection = usbDeviceConnection ?: return
            val iface = usbInterface ?: return
            AppLog.w("Attempting USB interface soft-reset...")
            try {
                connection.releaseInterface(iface)
                Thread.sleep(100)
                if (connection.claimInterface(iface, true)) {
                    AppLog.i("USB interface re-claimed successfully")
                    internalBufferPos = 0
                    internalBufferAvailable = 0
                    initEndpoint()
                } else {
                    AppLog.e("Failed to re-claim USB interface — disconnecting")
                    disconnect()
                }
            } catch (e: Exception) {
                AppLog.e("Error during USB reset: ${e.message}")
            }
        }
    }

    override fun disconnect() {
        // close() is thread-safe and immediately aborts any in-flight bulkTransfer(),
        // so both sendBlocking and recvBlocking unblock within milliseconds.
        usbDeviceConnection?.close()

        synchronized(sStateLock) {
            if (usbDeviceConnected != null) {
                AppLog.i(usbDeviceConnected!!.toString())
            }
            endpointIn = null
            endpointOut = null

            if (usbDeviceConnection != null) {
                var bret = false
                if (usbInterface != null) {
                    // releaseInterface() may fail since close() was already called; log and continue.
                    bret = try { usbDeviceConnection!!.releaseInterface(usbInterface) } catch (_: Exception) { false }
                }
                if (bret) {
                    AppLog.i("OK releaseInterface()")
                } else {
                    AppLog.e("Error releaseInterface()")
                }
            }
            usbDeviceConnection = null
            usbInterface = null
            usbDeviceConnected = null
            internalBufferPos = 0
            internalBufferAvailable = 0
        }
    }

    override val isConnected: Boolean
        get() = usbDeviceConnected != null

    override val isSingleMessage: Boolean
        get() = false

    // Read error tracking — only accessed by the poll thread; no lock needed.
    private var consecutiveReadErrors = 0
    private var firstErrorTimeMs = 0L
    private var lastSuccessTimeMs = SystemClock.elapsedRealtime()
    private val maxErrorDurationBeforeDisconnect = 60_000L  // 60s patience for dongle WiFi recovery
    /** Minimum gap (ms) between a successful read and a new error before treating it as a new burst. */
    private val errorBurstResetGapMs = 5_000L

    // Volatile reads capture the latest connection/endpoint references; bulkTransfer runs
    // entirely outside any lock. If disconnect() calls close() concurrently, bulkTransfer
    // returns -1 immediately — a safe, recoverable outcome.
    override fun sendBlocking(buf: ByteArray, length: Int, timeout: Int): Int {
        val connection = usbDeviceConnection ?: return -1
        val ep = endpointOut ?: return -1
        return try {
            connection.bulkTransfer(ep, buf, length, timeout)
        } catch (e: Exception) {
            AppLog.e("USB Write Error: ${e.message}")
            -1
        }
    }

    override fun recvBlocking(buf: ByteArray, length: Int, timeout: Int, readFully: Boolean): Int {
        val connection = usbDeviceConnection ?: return -1
        val ep = endpointIn ?: return -1

        return try {
            var totalReturned = 0

            while (totalReturned < length) {
                // 1. Serve from internal buffer if data is available
                if (internalBufferAvailable > 0) {
                    val toCopy = minOf(length - totalReturned, internalBufferAvailable)
                    System.arraycopy(internalBuffer, internalBufferPos, buf, totalReturned, toCopy)
                    internalBufferPos += toCopy
                    internalBufferAvailable -= toCopy
                    totalReturned += toCopy

                    if (totalReturned >= length || !readFully) break
                    continue
                }

                // 2. Internal buffer empty, read new 16KB chunk from USB
                val read = try {
                    connection.bulkTransfer(ep, internalBuffer, internalBuffer.size, timeout)
                } catch (e: Exception) {
                    AppLog.e("USB Read Error: ${e.message}")
                    -1
                }

                if (read < 0) {
                    consecutiveReadErrors++
                    if (consecutiveReadErrors == 1) {
                        // New error burst: if the last successful read was long enough ago,
                        // this is a fresh burst — reset the timer.
                        val gapSinceSuccess = SystemClock.elapsedRealtime() - lastSuccessTimeMs
                        if (gapSinceSuccess > errorBurstResetGapMs || firstErrorTimeMs == 0L) {
                            firstErrorTimeMs = SystemClock.elapsedRealtime()
                        }
                    }
                    val errorDurationMs = SystemClock.elapsedRealtime() - firstErrorTimeMs
                    if (errorDurationMs > maxErrorDurationBeforeDisconnect) {
                        AppLog.e("USB read errors persisting for ${errorDurationMs / 1000}s — disconnecting")
                        disconnect()
                        return -1
                    }
                    if (consecutiveReadErrors % 10 == 0) {
                        AppLog.w("USB read errors ($consecutiveReadErrors) for ${errorDurationMs / 1000}s — waiting for recovery...")
                    }
                    if (consecutiveReadErrors >= 5) {
                        try { Thread.sleep(200) } catch (_: InterruptedException) {}
                    }
                    return if (totalReturned > 0) totalReturned else -1
                }
                // If we reach here, read is 0 or positive, meaning no error.
                // Reset error counters if they were active.
                if (consecutiveReadErrors > 0) {
                    AppLog.i("USB reads recovered after $consecutiveReadErrors errors")
                    consecutiveReadErrors = 0
                    firstErrorTimeMs = 0
                }
                lastSuccessTimeMs = SystemClock.elapsedRealtime()

                if (read == 0) {
                    return totalReturned
                }

                internalBufferPos = 0
                internalBufferAvailable = read
                // Loop will continue and serve from the new internalBuffer data
            }

            totalReturned

        } catch (e: Exception) {
            AppLog.e("USB Read Error: ${e.message}")
            -1
        }
    }

    private class UsbOpenException : Exception {
        constructor(message: String) : super(message)
        constructor(tr: Throwable) : super(tr)
    }

    companion object {
        // Held only during state mutations (connect / disconnect / reset).
        // Neither sendBlocking nor recvBlocking holds this lock during bulkTransfer.
        private val sStateLock = Any()
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/connection/AccessoryConnection.kt`:

```kt
package com.andrerinas.headunitrevived.connection

interface AccessoryConnection {
    val isSingleMessage: Boolean
    fun sendBlocking(buf: ByteArray, length: Int, timeout: Int): Int
    fun recvBlocking(buf: ByteArray, length: Int, timeout: Int, readFully: Boolean): Int
    val isConnected: Boolean
    suspend fun connect(): Boolean
    fun disconnect()
}

```

`app/src/main/java/com/andrerinas/headunitrevived/connection/NearbySocket.kt`:

```kt
package com.andrerinas.headunitrevived.connection

import java.io.InputStream
import java.io.OutputStream
import java.net.InetAddress
import java.net.Socket
import java.util.concurrent.CountDownLatch

class NearbySocket : Socket() {
    private var internalInputStream: InputStream? = null
    private var internalOutputStream: OutputStream? = null

    private val inputLatch = CountDownLatch(1)
    private val outputLatch = CountDownLatch(1)

    var inputStreamWrapper: InputStream?
        get() = internalInputStream
        set(value) {
            internalInputStream = value
            if (value != null) {
                com.andrerinas.headunitrevived.utils.AppLog.i("NearbySocket: InputStream is now AVAILABLE. Releasing latch.")
                inputLatch.countDown()
            }
        }

    var outputStreamWrapper: OutputStream?
        get() = internalOutputStream
        set(value) {
            internalOutputStream = value
            if (value != null) outputLatch.countDown()
        }

    override fun isConnected() = true

    override fun getInetAddress(): InetAddress = InetAddress.getLoopbackAddress()

    override fun getInputStream(): InputStream {
        com.andrerinas.headunitrevived.utils.AppLog.d("NearbySocket: getInputStream() called")
        return object : InputStream() {
            private fun waitForStream(): InputStream {
                if (inputLatch.count > 0L) {
                    com.andrerinas.headunitrevived.utils.AppLog.i("NearbySocket: Blocking read until InputStream is AVAILABLE via Nearby Payload...")
                }
                inputLatch.await()
                return internalInputStream!!
            }

            override fun read(): Int {
                val b = waitForStream().read()
                return b
            }

            override fun read(b: ByteArray): Int = read(b, 0, b.size)
            override fun read(b: ByteArray, off: Int, len: Int): Int {
                val readValue = waitForStream().read(b, off, len)
                return readValue
            }
            override fun available(): Int = if (inputLatch.count == 0L) internalInputStream!!.available() else 0
            override fun close() = if (inputLatch.count == 0L) internalInputStream!!.close() else Unit
        }
    }

    override fun getOutputStream(): OutputStream {
        com.andrerinas.headunitrevived.utils.AppLog.d("NearbySocket: getOutputStream() called")
        return object : OutputStream() {
            private fun waitForStream(): OutputStream {
                if (outputLatch.count > 0L) {
                    com.andrerinas.headunitrevived.utils.AppLog.d("NearbySocket: Waiting for outputLatch...")
                }
                outputLatch.await()
                return internalOutputStream!!
            }

            override fun write(b: Int) {
                com.andrerinas.headunitrevived.utils.AppLog.v("NearbySocket: writing 1 byte to pipe")
                waitForStream().write(b)
            }

            override fun write(b: ByteArray) = write(b, 0, b.size)
            override fun write(b: ByteArray, off: Int, len: Int) {
                com.andrerinas.headunitrevived.utils.AppLog.v("NearbySocket: writing $len bytes to pipe")
                waitForStream().write(b, off, len)
                // Force flush since GMS Nearby Stream payloads might buffer a lot
                waitForStream().flush()
            }
            override fun flush() {
                com.andrerinas.headunitrevived.utils.AppLog.v("NearbySocket: flush() called")
                if (outputLatch.count == 0L) internalOutputStream!!.flush()
            }
            override fun close() = if (outputLatch.count == 0L) internalOutputStream!!.close() else Unit
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/connection/CarKeyReceiver.kt`:

```kt
package com.andrerinas.headunitrevived.connection

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.view.KeyEvent
import androidx.core.content.IntentCompat
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.contract.KeyIntent
import com.andrerinas.headunitrevived.utils.AppLog

/**
 * A comprehensive receiver for steering wheel and hardware buttons,
 * covering both standard Android events and proprietary Chinese headunit broadcasts.
 */
class CarKeyReceiver : BroadcastReceiver() {

    companion object {
        val ACTIONS = arrayOf(
            "android.intent.action.MEDIA_BUTTON",
            "hy.intent.action.MEDIA_BUTTON", // Huayu / Hyundai Protocol
            "com.nwd.action.ACTION_KEY_VALUE", // NWD (NewWell)
            "com.microntek.irkeyUp", // Microntek (MTCE/MTCB)
            "com.microntek.irkeyDown",
            "com.winca.service.Setting.KEY_ACTION", // Winca
            "android.intent.action.C3_HARDKEY", // FlyAudio / C3
            "IKeyClick.KEY_CLICK",
            "com.eryanet.music.prev", // Eryanet (Eonon etc.)
            "com.eryanet.music.next",
            "com.eryanet.media.playorpause",
            "com.eryanet.media.play",
            "com.eryanet.media.pause",
            "com.bz.action.phone.pickup", // BZ (Joying etc.)
            "com.bz.action.phone.hangup",
            "com.tencent.qqmusiccar.action.MEDIA_BUTTON_INNER_ONKEY",
            "cn.kuwo.kwmusicauto.action.MEDIA_BUTTON"
        )
    }

    override fun onReceive(context: Context, intent: Intent?) {
        val action = intent?.action ?: return
        AppLog.d("CarKeyReceiver: Received action: $action")

        // Broadcast for KeymapFragment debugger (raw intent data)
        context.sendBroadcast(Intent("com.andrerinas.headunitrevived.DEBUG_KEY").apply {
            setPackage(context.packageName)
            putExtra("action", action)
            intent.extras?.let { putExtras(it) }
        })

        // Try to abort broadcast to prevent other apps (like built-in radio) from reacting
        if (isOrderedBroadcast) {
            abortBroadcast()
        }

        val commManager = App.provide(context).commManager

        // 1. Standard Media Button extraction (already has KeyEvent with proper DOWN/UP)
        if (action == "android.intent.action.MEDIA_BUTTON" || action == "hy.intent.action.MEDIA_BUTTON"
            || action == "com.tencent.qqmusiccar.action.MEDIA_BUTTON_INNER_ONKEY"
            || action == "cn.kuwo.kwmusicauto.action.MEDIA_BUTTON") {
            val event = IntentCompat.getParcelableExtra(intent, Intent.EXTRA_KEY_EVENT, KeyEvent::class.java)
            if (event != null) {
                if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
                    handleKey(context, commManager, event.keyCode, true)
                } else if (event.action == KeyEvent.ACTION_UP) {
                    handleKey(context, commManager, event.keyCode, false)
                }
            }
            return
        }

        // 2. Proprietary extraction — extract keycode or use virtual IDs for mapping
        when (action) {
            // --- Protocols with proper DOWN/UP separation ---
            "com.microntek.irkeyDown", "com.microntek.irkeyUp" -> {
                val keyCode = intent.getIntExtra("keyCode", -1)
                if (keyCode != -1) handleKey(context, commManager, keyCode, action.endsWith("keyDown"))
            }
            "android.intent.action.C3_HARDKEY" -> {
                val keyCode = intent.getIntExtra("c3_hardkey_keycode", -1)
                val c3Action = intent.getIntExtra("c3_hardkey_action", -1)
                if (keyCode != -1 && c3Action != -1) handleKey(context, commManager, keyCode, c3Action == 1)
            }

            // --- Protocols that fire once (no DOWN/UP) → use virtual IDs for mapping ---
            "com.nwd.action.ACTION_KEY_VALUE" -> {
                val value = intent.getByteExtra("extra_key_value", 0).toInt()
                if (value != 0) handleClick(context, commManager, 1000 + value)
            }
            "com.winca.service.Setting.KEY_ACTION" ->
                intent.getIntExtra("com.winca.service.Setting.KEY_ACTION_EXTRA", -1)
                    .takeIf { it != -1 }?.let { handleClick(context, commManager, it) }
            "IKeyClick.KEY_CLICK" ->
                intent.getIntExtra("CLICK_KEY", -1)
                    .takeIf { it != -1 }?.let { handleClick(context, commManager, it) }
            "com.eryanet.music.prev" -> handleClick(context, commManager, 2001)
            "com.eryanet.music.next" -> handleClick(context, commManager, 2002)
            "com.eryanet.media.playorpause" -> handleClick(context, commManager, 2003)
            "com.eryanet.media.play" -> handleClick(context, commManager, 2004)
            "com.eryanet.media.pause" -> handleClick(context, commManager, 2005)
            "com.bz.action.phone.pickup" -> handleClick(context, commManager, 3001)
            "com.bz.action.phone.hangup" -> handleClick(context, commManager, 3002)
        }
    }

    /** Single key press or release — broadcasts for learning and projection handling. */
    private fun handleKey(context: Context, commManager: CommManager, keyCode: Int, isDown: Boolean) {
        AppLog.d("CarKeyReceiver: Broadcasting key event: code=$keyCode, isDown=$isDown")
        context.sendBroadcast(Intent(KeyIntent.action).apply {
            setPackage(context.packageName)
            putExtra(KeyIntent.extraEvent, KeyEvent(
                if (isDown) KeyEvent.ACTION_DOWN else KeyEvent.ACTION_UP, keyCode
            ))
        })
        if (commManager.isConnected) commManager.send(keyCode, isDown)
    }

    /** Full click (DOWN + UP) — broadcasts both events for learning AND sends to AA. */
    private fun handleClick(context: Context, commManager: CommManager, keyCode: Int) {
        handleKey(context, commManager, keyCode, true)
        handleKey(context, commManager, keyCode, false)
    }
}



```

`app/src/main/java/com/andrerinas/headunitrevived/connection/WifiDirectManager.kt`:

```kt
package com.andrerinas.headunitrevived.connection

import android.annotation.SuppressLint
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.NetworkInfo
import android.net.wifi.p2p.WifiP2pDevice
import android.net.wifi.p2p.WifiP2pInfo
import android.net.wifi.p2p.WifiP2pManager
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.widget.Toast
import androidx.core.content.ContextCompat
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.utils.AppLog
import java.net.InetSocketAddress
import java.net.Socket

class WifiDirectManager(private val context: Context) : WifiP2pManager.ConnectionInfoListener, WifiP2pManager.GroupInfoListener {

    private val manager: WifiP2pManager? = context.getSystemService(Context.WIFI_P2P_SERVICE) as? WifiP2pManager
    private var channel: WifiP2pManager.Channel? = null
    private var isGroupOwner = false
    private var isConnected = false
    private val handler = Handler(Looper.getMainLooper())

    private var onCredentialsReady: ((ssid: String, psk: String, ip: String, bssid: String) -> Unit)? = null

    fun setCredentialsListener(callback: (String, String, String, String) -> Unit) {
        this.onCredentialsReady = callback
    }

    private val discoveryRunnable = object : Runnable {
        override fun run() {
            if (!isConnected) {
                startDiscovery()
                handler.postDelayed(this, 10000L) // Repeat every 10s to stay visible
            }
        }
    }

    private val receiver = object : BroadcastReceiver() {
        @SuppressLint("MissingPermission")
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                WifiP2pManager.WIFI_P2P_THIS_DEVICE_CHANGED_ACTION -> {
                    val device = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        intent.getParcelableExtra(WifiP2pManager.EXTRA_WIFI_P2P_DEVICE, WifiP2pDevice::class.java)
                    } else {
                        @Suppress("DEPRECATION")
                        intent.getParcelableExtra(WifiP2pManager.EXTRA_WIFI_P2P_DEVICE)
                    }
                    device?.let {
                        if (com.andrerinas.headunitrevived.App.provide(context).settings.wifiConnectionMode != 3) {
                            AppLog.i("WifiDirectManager: Local name: ${it.deviceName}")
                        }
                        AapService.wifiDirectName.value = it.deviceName
                    }
                }
                WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION -> {
                    val networkInfo = intent.getParcelableExtra<NetworkInfo>(WifiP2pManager.EXTRA_NETWORK_INFO)
                    if (networkInfo?.isConnected == true) {
                        AppLog.i("WifiDirectManager: Connected. Requesting info...")
                        manager?.requestConnectionInfo(channel, this@WifiDirectManager)
                    } else {
                        isConnected = false
                    }
                }
            }
        }
    }

    init {
        try {
            if (context.packageManager.hasSystemFeature(android.content.pm.PackageManager.FEATURE_WIFI_DIRECT)) {
                manager?.let { mgr ->
                    channel = mgr.initialize(context, context.mainLooper, null)
                    val filter = IntentFilter().apply {
                        addAction(WifiP2pManager.WIFI_P2P_THIS_DEVICE_CHANGED_ACTION)
                        addAction(WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION)
                    }
                    ContextCompat.registerReceiver(context, receiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)
                }
            }
        } catch (e: SecurityException) {
            AppLog.w("WifiDirectManager: WiFi Direct unavailable — permission denied: ${e.message}")
        }
    }

    @SuppressLint("MissingPermission")
    override fun onConnectionInfoAvailable(info: WifiP2pInfo) {
        if (info.groupFormed) {
            isConnected = true
            isGroupOwner = info.isGroupOwner
            val goIp = info.groupOwnerAddress?.hostAddress ?: "unknown"
            AppLog.i("WifiDirectManager: Group formed. Owner: $isGroupOwner, GO IP: $goIp")

            if (isGroupOwner) {
                // Request group info to get SSID and Passphrase, and check for connected clients
                manager?.requestGroupInfo(channel, this)
            } else if (info.groupOwnerAddress != null) {
                Thread {
                    var socket: Socket? = null
                    try {
                        AppLog.i("WifiDirectManager: Pinging Phone (GO) at $goIp to announce tablet...")
                        socket = Socket()
                        socket.connect(InetSocketAddress(info.groupOwnerAddress, 5289), 2000)
                    } catch (e: Exception) {
                        AppLog.w("WifiDirectManager: Ping to GO failed: ${e.message}")
                    } finally {
                        try { socket?.close() } catch (e: Exception) {}
                    }
                }.start()
            }
        } else {
            AppLog.d("WifiDirectManager: onConnectionInfoAvailable: group not formed yet")
        }
    }

    private var groupInfoRetries = 0

    @SuppressLint("MissingPermission")
    override fun onGroupInfoAvailable(group: android.net.wifi.p2p.WifiP2pGroup?) {
        if (group != null) {
            groupInfoRetries = 0
            val ssid = group.networkName
            val psk = group.passphrase ?: ""
            val bssid = getWifiDirectMac(group.`interface`)
            val isOwner = group.isGroupOwner

            // Try to get frequency via reflection (hidden field in WifiP2pGroup)
            var frequency = 0
            try {
                // Try several common field names used by different OEMs
                val fieldNames = arrayOf("frequency", "mFrequency")
                for (name in fieldNames) {
                    try {
                        val field = group.javaClass.getDeclaredField(name)
                        field.isAccessible = true
                        frequency = field.getInt(group)
                        if (frequency > 0) break
                    } catch (e: Exception) {}
                }
            } catch (e: Exception) {}

            val band = if (frequency > 4000) "5GHz" else if (frequency > 0) "2.4GHz" else "unknown"
            AppLog.i("WifiDirectManager: onGroupInfoAvailable: SSID: $ssid, BSSID: $bssid, GO: $isOwner, Freq: $frequency MHz ($band)")

            if (ssid.isNotEmpty()) {
                // Wait for the IP address to be assigned to the interface
                Thread {
                    try {
                        var ip = getWifiDirectIp(group.`interface`)
                        var retries = 0
                        while (ip == null && retries < 15) {
                            AppLog.d("WifiDirectManager: Waiting for IP on interface ${group.`interface`} (Attempt ${retries + 1}/15)...")
                            Thread.sleep(1000)
                            ip = getWifiDirectIp(group.`interface`)
                            retries++
                        }

                        val finalIp = ip ?: "192.168.49.1"
                        AppLog.i("WifiDirectManager: SUCCESS - Providing credentials to HandshakeManager. SSID=$ssid, IP=$finalIp")
                        onCredentialsReady?.invoke(ssid, psk, finalIp, bssid)
                    } catch (e: Exception) {
                        AppLog.e("WifiDirectManager: Error in credential delivery thread", e)
                    }
                }.start()
            }
        } else {
            if (groupInfoRetries < 20) {
                groupInfoRetries++
                AppLog.w("WifiDirectManager: Group info was null! Retrying in 1s (Attempt $groupInfoRetries/20)...")
                handler.postDelayed({
                    manager?.requestGroupInfo(channel, this)
                }, 1000L)
            } else {
                AppLog.e("WifiDirectManager: FATAL: Group info remained null after 20 retries.")
            }
        }
    }

    private fun getWifiDirectMac(ifaceName: String?): String {
        try {
            val interfaces = java.net.NetworkInterface.getNetworkInterfaces()
            while (interfaces.hasMoreElements()) {
                val iface = interfaces.nextElement()
                if (ifaceName != null && iface.name != ifaceName) continue
                if (ifaceName == null && !iface.name.contains("p2p")) continue

                val mac = iface.hardwareAddress
                if (mac != null) {
                    val sb = StringBuilder()
                    for (i in mac.indices) {
                        sb.append(String.format("%02X%s", mac[i], if (i < mac.size - 1) ":" else ""))
                    }
                    return sb.toString()
                }
            }
        } catch (e: Exception) {}
        return "00:00:00:00:00:00"
    }

    private fun getWifiDirectIp(ifaceName: String?): String? {
        try {
            val interfaces = java.net.NetworkInterface.getNetworkInterfaces()
            while (interfaces.hasMoreElements()) {
                val iface = interfaces.nextElement()
                val addresses = iface.inetAddresses
                while (addresses.hasMoreElements()) {
                    val addr = addresses.nextElement()
                    if (!addr.isLoopbackAddress && addr is java.net.Inet4Address) {
                        // Prioritize explicitly requested interface, or generic p2p interfaces
                        if (ifaceName != null && iface.name == ifaceName) return addr.hostAddress
                        if (iface.name.contains("p2p")) return addr.hostAddress
                    }
                }
            }
            // Fallback pass: return any valid IPv4 that isn't loopback
            val interfaces2 = java.net.NetworkInterface.getNetworkInterfaces()
            while (interfaces2.hasMoreElements()) {
                val iface = interfaces2.nextElement()
                val addresses = iface.inetAddresses
                while (addresses.hasMoreElements()) {
                    val addr = addresses.nextElement()
                    if (!addr.isLoopbackAddress && addr is java.net.Inet4Address) {
                        return addr.hostAddress
                    }
                }
            }
        } catch (e: Exception) {
            AppLog.e("WifiDirectManager: Error getting local IP", e)
        }
        return null
    }

    @SuppressLint("MissingPermission")
    fun makeVisible() {
        val mgr = manager ?: return
        val ch = channel ?: return

        // Ensure WiFi is enabled (Required for P2P)
        val wifiManager = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as android.net.wifi.WifiManager
        if (!wifiManager.isWifiEnabled) {
            AppLog.w("WifiDirectManager: WiFi is disabled. Cannot start P2P discovery.")
            Toast.makeText(context, context.getString(R.string.wifi_disabled_info), Toast.LENGTH_LONG).show()
            return
        }

        // Reflection Hack to set name
        try {
            val method = mgr.javaClass.getMethod("setDeviceName", WifiP2pManager.Channel::class.java, String::class.java, WifiP2pManager.ActionListener::class.java)
            method.invoke(mgr, ch, "HURev", object : WifiP2pManager.ActionListener {
                override fun onSuccess() { AppLog.i("WifiDirectManager: Name set to HURev") }
                override fun onFailure(reason: Int) {}
            })
        } catch (e: Exception) {}

        // 1. Stop any ongoing discovery and remove group to start fresh
        mgr.stopPeerDiscovery(ch, object : WifiP2pManager.ActionListener {
            override fun onSuccess() { removeGroupAndCreate() }
            override fun onFailure(reason: Int) { removeGroupAndCreate() }
        })
    }

    @SuppressLint("MissingPermission")
    private fun removeGroupAndCreate() {
        manager?.removeGroup(channel, object : WifiP2pManager.ActionListener {
            override fun onSuccess() { delayedCreateGroup(0) }
            override fun onFailure(reason: Int) { delayedCreateGroup(0) }
        })
    }

    private fun delayedCreateGroup(retryCount: Int) {
        handler.postDelayed({ createNewGroup(retryCount) }, 500L)
    }

    @SuppressLint("MissingPermission")
    private fun createNewGroup(retryCount: Int) {
        manager?.createGroup(channel, object : WifiP2pManager.ActionListener {
            override fun onSuccess() {
                AppLog.i("WifiDirectManager: P2P Group created.")
                isGroupOwner = true
                startDiscoveryLoop()
            }
            override fun onFailure(reason: Int) {
                if (reason == 2 && retryCount < 3) { // 2 = BUSY
                    AppLog.w("WifiDirectManager: Chip is BUSY, retrying in 2s...")
                    handler.postDelayed({ createNewGroup(retryCount + 1) }, 2000L)
                } else {
                    AppLog.e("WifiDirectManager: createGroup failed: $reason")
                }
            }
        })
    }

    private fun startDiscoveryLoop() {
        handler.removeCallbacks(discoveryRunnable)
        handler.post(discoveryRunnable)
    }

    @SuppressLint("MissingPermission")
    private fun startDiscovery() {
        manager?.discoverPeers(channel, object : WifiP2pManager.ActionListener {
            override fun onSuccess() { AppLog.d("WifiDirectManager: Discovery active") }
            override fun onFailure(reason: Int) { AppLog.w("WifiDirectManager: Discovery failed: $reason") }
        })
    }

    /**
     * Boomerang Hack: Briefly triggers system WiFi settings to wake up the radio.
     * Currently not used by default but kept in code for future use.
     */
    private fun triggerWifiSettings() {
        try {
            val intent = Intent().apply {
                component = android.content.ComponentName("com.android.settings", "com.android.settings.Settings\$WifiP2pSettingsActivity")
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(intent)
        } catch (e: Exception) {
            try {
                val intent = Intent(android.provider.Settings.ACTION_WIFI_SETTINGS).apply {
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                }
                context.startActivity(intent)
            } catch (e2: Exception) {}
        }

        handler.postDelayed({
            try {
                val intent = Intent(context, com.andrerinas.headunitrevived.main.MainActivity::class.java).apply {
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
                }
                context.startActivity(intent)
            } catch (e: Exception) {}
        }, 800L)
    }

    @SuppressLint("MissingPermission")
    fun startNativeAaQuietHost() {
        val mgr = manager
        val ch = channel

        if (mgr == null || ch == null) {
            AppLog.e("WifiDirectManager: Cannot start Quiet Host - manager ($mgr) or channel ($ch) is null!")
            return
        }

        // Ensure WiFi is enabled (Required for P2P)
        val wifiManager = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as android.net.wifi.WifiManager
        if (!wifiManager.isWifiEnabled) {
            AppLog.i("WifiDirectManager: WiFi is disabled but needed for Native AA. Attempting to enable...")
            if (Build.VERSION.SDK_INT < 29) {
                @Suppress("DEPRECATION")
                wifiManager.isWifiEnabled = true
            } else {
                Toast.makeText(context, "Native AA requires Wi-Fi. Please turn it on.", Toast.LENGTH_LONG).show()
                // We return for now, the user must turn it on. In the future we could open settings.
                return
            }
            // Wait a bit for WiFi to wake up
            handler.postDelayed({ startNativeAaQuietHost() }, 2000L)
            return
        }

        AppLog.i("WifiDirectManager: startNativeAaQuietHost() requested. Removing old group if any...")
        mgr.removeGroup(ch, object : WifiP2pManager.ActionListener {
            override fun onSuccess() {
                AppLog.d("WifiDirectManager: removeGroup SUCCESS. Creating quiet group...")
                delayedCreateQuietGroup(0)
            }
            override fun onFailure(reason: Int) {
                AppLog.d("WifiDirectManager: removeGroup failed (reason=$reason). This is expected if no group existed. Creating quiet group anyway...")
                delayedCreateQuietGroup(0)
            }
        })
    }

    private fun delayedCreateQuietGroup(retryCount: Int) {
        handler.postDelayed({ createQuietGroup(retryCount) }, 500L)
    }

    @SuppressLint("MissingPermission")
    private fun createQuietGroup(retryCount: Int) {
        val mgr = manager ?: return
        val ch = channel ?: return

        AppLog.i("WifiDirectManager: Attempting createGroup for Native AA (Attempt $retryCount)...")

        // 5GHz Hack: Try to force 5GHz band using reflection
        try {
            val configClass = Class.forName("android.net.wifi.p2p.WifiP2pConfig")
            val config = configClass.newInstance()

            val groupOwnerIntentField = configClass.getDeclaredField("groupOwnerIntent")
            groupOwnerIntentField.isAccessible = true
            groupOwnerIntentField.set(config, 15)

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                val setGroupOperatingBandMethod = configClass.getMethod("setGroupOperatingBand", Int::class.javaPrimitiveType)
                setGroupOperatingBandMethod.invoke(config, 2) // 2 = 5GHz band
            }

            // The hidden method signature is createGroup(Channel, WifiP2pConfig, ActionListener)
            val createGroupMethod = mgr.javaClass.getMethod("createGroup",
                WifiP2pManager.Channel::class.java,
                configClass,
                WifiP2pManager.ActionListener::class.java)

            createGroupMethod.invoke(mgr, ch, config, object : WifiP2pManager.ActionListener {
                override fun onSuccess() {
                    AppLog.i("WifiDirectManager: 5GHz Forced createGroup SUCCESS!")
                    isGroupOwner = true
                    handler.postDelayed({
                        mgr.requestConnectionInfo(ch, this@WifiDirectManager)
                        mgr.requestGroupInfo(ch, this@WifiDirectManager)
                    }, 1000L)
                }
                override fun onFailure(reason: Int) {
                    val reasonStr = getP2pErrorString(reason)
                    AppLog.w("WifiDirectManager: 5GHz Forced createGroup failed ($reasonStr), falling back to standard...")
                    standardCreateGroup(mgr, ch, retryCount)
                }
            })
            return
        } catch (e: Exception) {
            AppLog.w("WifiDirectManager: 5GHz Hack failed: ${e.message}. Using standard createGroup.")
        }

        standardCreateGroup(mgr, ch, retryCount)
    }

    private fun getP2pErrorString(reason: Int): String {
        return when(reason) {
            0 -> "ERROR (Internal Error)"
            1 -> "P2P_UNSUPPORTED"
            2 -> "BUSY (System is busy, retry needed)"
            else -> "UNKNOWN ($reason)"
        }
    }

    @SuppressLint("MissingPermission")
    private fun standardCreateGroup(mgr: WifiP2pManager, ch: WifiP2pManager.Channel, retryCount: Int) {
        mgr.createGroup(ch, object : WifiP2pManager.ActionListener {
            override fun onSuccess() {
                AppLog.i("WifiDirectManager: Standard createGroup SUCCESS!")
                isGroupOwner = true
                handler.postDelayed({
                    mgr.requestConnectionInfo(ch, this@WifiDirectManager)
                    mgr.requestGroupInfo(ch, this@WifiDirectManager)
                }, 1000L)
            }
            override fun onFailure(reason: Int) {
                val reasonStr = getP2pErrorString(reason)
                if (reason == 2 && retryCount < 3) {
                    AppLog.w("WifiDirectManager: createGroup failed ($reasonStr), removing group and retrying in 2s...")
                    mgr.removeGroup(ch, object : WifiP2pManager.ActionListener {
                        override fun onSuccess() { delayedCreateQuietGroup(retryCount + 1) }
                        override fun onFailure(r: Int) { delayedCreateQuietGroup(retryCount + 1) }
                    })
                } else {
                    AppLog.e("WifiDirectManager: createQuietGroup failed completely! Reason: $reasonStr")
                }
            }
        })
    }

    fun stop() {
        AppLog.i("WifiDirectManager: Stopping and cleaning up...")
        handler.removeCallbacks(discoveryRunnable)
        try { context.unregisterReceiver(receiver) } catch (e: Exception) {}
        if (isGroupOwner) {
            manager?.removeGroup(channel, object : WifiP2pManager.ActionListener {
                override fun onSuccess() { AppLog.d("WifiDirectManager: Final group removal success") }
                override fun onFailure(reason: Int) { AppLog.d("WifiDirectManager: Final group removal failed: $reason") }
            })
        }
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/connection/NativeAaHandshakeManager.kt`:

```kt
package com.andrerinas.headunitrevived.connection

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothServerSocket
import android.bluetooth.BluetoothSocket
import android.content.Context
import com.andrerinas.headunitrevived.aap.protocol.proto.Wireless
import com.andrerinas.headunitrevived.utils.AppLog
import kotlinx.coroutines.*
import java.io.DataInputStream
import java.io.IOException
import java.io.OutputStream
import java.nio.ByteBuffer
import java.util.*

/**
 * Manages the official Android Auto Wireless Bluetooth handshake.
 * This class implements the RFCOMM server protocol to exchange WiFi credentials with the phone.
 */
class NativeAaHandshakeManager(
    private val context: Context,
    private val scope: CoroutineScope
) {
    companion object {
        private val AA_UUID = UUID.fromString("4de17a00-52cb-11e6-bdf4-0800200c9a66")
        private val HFP_UUID = UUID.fromString("0000111e-0000-1000-8000-00805f9b34fb")
        private val A2DP_SOURCE_UUID = UUID.fromString("00001112-0000-1000-8000-00805f9b34fb")

        fun checkCompatibility(): Boolean {
            val adapter = BluetoothAdapter.getDefaultAdapter() ?: return false
            if (!adapter.isEnabled) return false
            return try {
                val socket = adapter.listenUsingRfcommWithServiceRecord("Compatibility Check", AA_UUID)
                socket.close()
                AppLog.i("NativeAA: Compatibility Check SUCCESS")
                true
            } catch (e: Exception) {
                AppLog.w("NativeAA: Compatibility Check FAILED: ${e.message}")
                false
            }
        }
    }

    private var aaServerSocket: BluetoothServerSocket? = null
    private var hfpServerSocket: BluetoothServerSocket? = null
    private var isRunning = false

    private var currentSsid: String? = null
    private var currentPsk: String? = null
    private var currentIp: String? = null
    private var currentBssid: String? = null

    /**
     * Updates the WiFi credentials that will be sent to the phone during the next handshake.
     */
    fun updateWifiCredentials(ssid: String, psk: String, ip: String, bssid: String) {
        AppLog.i("NativeAA: Credentials updated. SSID=$ssid, IP=$ip, BSSID=$bssid")
        currentSsid = ssid
        currentPsk = psk
        currentIp = ip
        currentBssid = bssid
    }

    @SuppressLint("MissingPermission")
    fun start() {
        if (isRunning) return
        isRunning = true

        val adapter = BluetoothAdapter.getDefaultAdapter()
        if (adapter == null || !adapter.isEnabled) {
            AppLog.e("NativeAA: Bluetooth adapter not available or disabled")
            return
        }

        AppLog.i("NativeAA: Starting Bluetooth Handshake Servers...")

        // Start AA RFCOMM Server
        scope.launch(Dispatchers.IO + CoroutineName("NativeAa-RfcommServer")) {
            try {
                aaServerSocket = adapter.listenUsingRfcommWithServiceRecord("AA BT Listener", AA_UUID)
                AppLog.i("NativeAA: ACTIVELY LISTENING on Android Auto UUID ($AA_UUID)... Waiting for phone to connect back!")
                while (isRunning && isActive) {
                    val socket = aaServerSocket?.accept()
                    if (socket != null) {
                        AppLog.i("NativeAA: Connection accepted from ${socket.remoteDevice.name}")
                        // [FIX] Launch handshake in a separate coroutine so the server can accept the next connection!
                        scope.launch(Dispatchers.IO + CoroutineName("NativeAa-Handshake-${socket.remoteDevice.address}")) {
                            handleHandshake(socket)
                        }
                    }
                }
            } catch (e: IOException) {
                if (isRunning) AppLog.d("NativeAA: AA Server socket closed: ${e.message}")
            }
        }

        // Start HFP RFCOMM Server (Required by some phones to detect HU)
        scope.launch(Dispatchers.IO + CoroutineName("NativeAa-HfpServer")) {
            try {
                hfpServerSocket = adapter.listenUsingRfcommWithServiceRecord("Hands-Free Unit", HFP_UUID)
                while (isRunning && isActive) {
                    val socket = hfpServerSocket?.accept()
                    if (socket != null) {
                        // Just consume and close, HFP is only a "presence" signal for us
                        scope.launch(Dispatchers.IO) {
                            try {
                                val buf = ByteArray(1024)
                                socket.inputStream.read(buf)
                            } catch (e: Exception) {}
                            finally { try { socket.close() } catch (e: Exception) {} }
                        }
                    }
                }
            } catch (e: IOException) {
                if (isRunning) AppLog.d("NativeAA: HFP Server socket closed: ${e.message}")
            }
        }

    }

    /**
     * Wakes up the phone by attempting a brief connection to the A2DP profile.
     * This acts as a signal for the phone to start looking for the headunit.
     */
    fun triggerPoke() {
        val adapter = BluetoothAdapter.getDefaultAdapter() ?: return
        val settings = com.andrerinas.headunitrevived.App.provide(context).settings
        val lastMac = settings.autoStartBluetoothDeviceMac

        scope.launch(Dispatchers.IO + CoroutineName("NativeAa-Wakeup")) {
            AppLog.d("NativeAA: triggerPoke() delay starting (2s)...")
            delay(2000) // Small safety delay before connecting

            val devicesToPoke = if (lastMac.isNotEmpty()) {
                listOf(adapter.getRemoteDevice(lastMac))
            } else {
                AppLog.w("NativeAA: No 'Auto Start BT Device' selected in settings. Poking all paired devices as fallback...")
                adapter.bondedDevices.toList()
            }

            if (devicesToPoke.isEmpty()) {
                AppLog.w("NativeAA: No paired Bluetooth devices found to poke.")
                return@launch
            }

            for (device in devicesToPoke) {
                if (!isRunning || !isActive) break
                AppLog.i("NativeAA: Attempting active A2DP poke to device: ${device.name} (${device.address})...")
                try {
                    val socket = device.createRfcommSocketToServiceRecord(A2DP_SOURCE_UUID)
                    AppLog.i("NativeAA: Calling socket.connect() for ${device.name}...")
                    socket.connect()
                    AppLog.i("NativeAA: Successfully poked ${device.name}. Keeping socket alive for 15s...")
                    delay(15000)
                    socket.close()
                    AppLog.i("NativeAA: Poke socket for ${device.name} closed.")
                } catch (e: Exception) {
                    AppLog.d("NativeAA: Poke for ${device.name} failed (normal if device disconnected): ${e.message}")
                }
            }
        }
    }

    /**
     * Start a manual poke (wakeup) for a specific Bluetooth device.
     */
    fun manualPoke(address: String) {
        val adapter = android.bluetooth.BluetoothAdapter.getDefaultAdapter() ?: return
        try {
            val device = adapter.getRemoteDevice(address)
            AppLog.i("NativeAA: Manual poke requested for ${device.name} ($address)")

            scope.launch(Dispatchers.IO + CoroutineName("NativeAa-ManualWakeup")) {
                AppLog.i("NativeAA: Attempting manual A2DP poke to ${device.name}...")
                try {
                    val socket = device.createRfcommSocketToServiceRecord(A2DP_SOURCE_UUID)
                    AppLog.i("NativeAA: Calling socket.connect() for ${device.name}...")
                    socket.connect()
                    AppLog.i("NativeAA: Successfully poked ${device.name}. Keeping socket alive for 20s...")
                    delay(20000)
                    socket.close()
                    AppLog.i("NativeAA: Manual poke socket for ${device.name} closed.")
                } catch (e: Exception) {
                    AppLog.d("NativeAA: Manual poke for ${device.name} failed: ${e.message}")
                }
            }
        } catch (e: Exception) {
            AppLog.e("NativeAA: Manual poke error", e)
        }
    }

    private suspend fun handleHandshake(socket: BluetoothSocket) = withContext(Dispatchers.IO) {
        try {
            val device = socket.remoteDevice
            AppLog.i("NativeAA: Handling handshake for ${device.name} (${device.address})")

            // Auto-save this device as the last successful one for future pokes
            val settings = com.andrerinas.headunitrevived.App.provide(context).settings
            if (settings.autoStartBluetoothDeviceMac != device.address) {
                AppLog.i("NativeAA: Saving ${device.address} (${device.name}) as the new default auto-start device.")
                settings.autoStartBluetoothDeviceMac = device.address
                settings.autoStartBluetoothDeviceName = device.name ?: "Unknown Device"
                com.andrerinas.headunitrevived.utils.Settings.syncAutoStartBtMacToDeviceStorage(context, device.address)
            }

            val input = DataInputStream(socket.inputStream)
            val output = socket.outputStream

            AppLog.i("NativeAA: Phone connected. Current credentials state: SSID=${currentSsid ?: "<null>"}, IP=${currentIp ?: "<null>"}")
            AppLog.i("NativeAA: Waiting for WiFi credentials to be ready (Max 60s)...")

            // Wait up to 60 seconds for credentials (P2P group creation can be slow)
            var attempts = 0
            while ((currentSsid == null || currentIp == null) && attempts < 120 && isRunning && isActive) {
                if (attempts % 10 == 0 && attempts > 0) {
                    AppLog.d("NativeAA: Still waiting... SSID=${currentSsid != null}, IP=${currentIp != null} (Attempt $attempts/120)")
                }
                delay(500)
                attempts++
            }

            if (currentSsid == null || currentIp == null) {
                AppLog.e("NativeAA: Handshake failed - No WiFi credentials available after 60s wait. Missing: ${if(currentSsid == null) "SSID " else ""}${if(currentIp == null) "IP" else ""}")
                return@withContext
            }

            val ip = currentIp!!
            val ssid = currentSsid!!
            val psk = currentPsk ?: ""
            val bssid = currentBssid ?: ""

            AppLog.i("NativeAA: Initializing Handshake Sequence...")
            AppLog.i("  - Group SSID: $ssid")
            AppLog.i("  - Group IP: $ip")
            AppLog.i("  - Group BSSID: $bssid")
            AppLog.i("  - Group PSK: ${if (psk.isNotEmpty()) "****" else "<empty>"}")

            AppLog.i("NativeAA: Sending WifiStartRequest (Type 1) to $ip:5288")
            sendWifiStartRequest(output, ip, 5288)

            AppLog.i("NativeAA: Waiting for response from phone...")
            val response = readProtobuf(input)
            AppLog.i("NativeAA: Received response Type ${response.type} from phone (size: ${response.payload.size})")

            if (response.type == 2) {
                AppLog.i("NativeAA: Phone requested security info (Ready for WiFi association).")
                AppLog.i("NativeAA: Sending WifiInfoResponse (Type 3) with full credentials...")
                sendWifiSecurityResponse(output, ssid, psk, bssid)
                AppLog.i("NativeAA: Handshake completed successfully on Bluetooth side.")
                // Instead of closing after 20 seconds, keep the socket open indefinitely
                // as long as the phone remains connected.
                while (isRunning && isActive && socket.isConnected) {
                    delay(2000)
                }
                AppLog.i("NativeAA: Handshake coroutine finishing (isRunning=$isRunning, isConnected=${socket.isConnected})")
            } else {
                AppLog.w("NativeAA: Unexpected response type from phone: ${response.type}. Expected Type 2.")
            }

        } catch (e: Exception) {
            AppLog.e("NativeAA: Handshake error: ${e.message}", e)
        } finally {
            try { socket.close() } catch (e: Exception) {}
            AppLog.i("NativeAA: BT Handshake socket closed.")
        }
    }

    private fun sendWifiStartRequest(output: OutputStream, ip: String, port: Int) {
        val request = Wireless.WifiStartRequest.newBuilder()
            .setIpAddress(ip)
            .setPort(port)
            .setStatus(0)
            .build()
        sendProtobuf(output, request.toByteArray(), 1)
    }

    private fun sendWifiSecurityResponse(output: OutputStream, ssid: String, key: String, bssid: String) {
        val response = Wireless.WifiInfoResponse.newBuilder()
            .setSsid(ssid)
            .setKey(key)
            .setBssid(bssid)
            .setSecurityMode(Wireless.SecurityMode.WPA2_PERSONAL)
            .setAccessPointType(Wireless.AccessPointType.STATIC)
            .build()
        sendProtobuf(output, response.toByteArray(), 3)
    }

    private fun sendProtobuf(output: OutputStream, data: ByteArray, type: Short) {
        val buffer = ByteBuffer.allocate(data.size + 4)
        buffer.put((data.size shr 8).toByte())
        buffer.put((data.size and 0xFF).toByte())
        buffer.putShort(type)
        buffer.put(data)
        output.write(buffer.array())
        output.flush()
        AppLog.i("NativeAA: Successfully delivered Protobuf TYPE $type (size ${data.size}) over Bluetooth!")
    }

    private fun readProtobuf(input: DataInputStream): ProtobufMessage {
        val header = ByteArray(4)
        input.readFully(header)
        val size = ((header[0].toInt() and 0xFF) shl 8) or (header[1].toInt() and 0xFF)
        val type = ((header[2].toInt() and 0xFF) shl 8) or (header[3].toInt() and 0xFF)
        val payload = if (size > 0) {
            val p = ByteArray(size)
            input.readFully(p)
            p
        } else ByteArray(0)
        return ProtobufMessage(type, payload)
    }

    data class ProtobufMessage(val type: Int, val payload: ByteArray)

    fun stop() {
        isRunning = false
        try { aaServerSocket?.close() } catch (e: Exception) {}
        try { hfpServerSocket?.close() } catch (e: Exception) {}
        aaServerSocket = null
        hfpServerSocket = null
        currentSsid = null
        currentIp = null
        currentPsk = null
        currentBssid = null
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/connection/UsbAccessoryMode.kt`:

```kt
package com.andrerinas.headunitrevived.connection

import android.hardware.usb.UsbConstants
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import com.andrerinas.headunitrevived.aap.Utils
import com.andrerinas.headunitrevived.utils.AppLog

class UsbAccessoryMode(private val usbMgr: UsbManager) {

    fun connectAndSwitch(device: UsbDevice): Boolean {
        val connection: UsbDeviceConnection?
        try {
            connection = usbMgr.openDevice(device)                 // Open device for connection
        } catch (e: Throwable) {
            AppLog.e(e)
            return false
        }

        if (connection == null) {
            AppLog.e("Cannot open device")
            return false
        }

        val result = switch(connection)
        connection.close()

        AppLog.i("Result: $result")
        return result
    }

    private fun switch(connection: UsbDeviceConnection): Boolean {
        // Do accessory negotiation and attempt to switch to accessory mode. Called only by usb_connect()
        val buffer = ByteArray(2)
        var len = connection.controlTransfer(UsbConstants.USB_DIR_IN or UsbConstants.USB_TYPE_VENDOR, ACC_REQ_GET_PROTOCOL, 0, 0, buffer, 2, USB_TIMEOUT_IN_MS)
        if (len != 2) {
            AppLog.e("Error controlTransfer len: $len")
            return false
        }
        val acc_ver = Utils.getAccVersion(buffer)
        // Get OAP / ACC protocol version
        AppLog.i("Success controlTransfer len: $len  acc_ver: $acc_ver")
        if (acc_ver < 1) {
            // If error or version too low...
            AppLog.e("No support acc")
            return false
        }
        AppLog.i("acc_ver: $acc_ver")

        // Send all accessory identification strings. Abort if any transfer fails — a partial
        // identification (e.g. manufacturer sent but model missing) can cause the phone to
        // ignore the ACC_REQ_START or fail to switch into accessory mode.
        if (!initStringControlTransfer(connection, ACC_IDX_MAN, MANUFACTURER) ||
            !initStringControlTransfer(connection, ACC_IDX_MOD, MODEL) ||
            !initStringControlTransfer(connection, ACC_IDX_DES, DESCRIPTION) ||
            !initStringControlTransfer(connection, ACC_IDX_VER, VERSION) ||
            !initStringControlTransfer(connection, ACC_IDX_URI, URI) ||
            !initStringControlTransfer(connection, ACC_IDX_SER, SERIAL)) {
            return false
        }

        AppLog.i("Sending acc start")
        // Send accessory start request. Device should re-enumerate as an accessory.
        len = connection.controlTransfer(UsbConstants.USB_TYPE_VENDOR, ACC_REQ_START, 0, 0, byteArrayOf(), 0, USB_TIMEOUT_IN_MS)

        // len == 0: clean ACK before re-enumeration (expected path).
        // len < 0: phone disconnected before the ACK because it started re-enumerating
        //          immediately upon receipt — the command was still received. Treat as success.
        AppLog.i("Acc start sent (len=$len). Waiting for re-enumeration...")
        try { Thread.sleep(500) } catch (e: Exception) {}
        return true
    }

    private fun initStringControlTransfer(conn: UsbDeviceConnection, index: Int, string: String): Boolean {
        val len = conn.controlTransfer(UsbConstants.USB_TYPE_VENDOR, ACC_REQ_SEND_STRING, 0, index, string.toByteArray(), string.length, USB_TIMEOUT_IN_MS)
        return if (len < 0) {
            // Negative means the USB transfer itself failed (e.g. device disconnected or
            // timed out). Abort the switch — ACC_REQ_START would be pointless.
            AppLog.e("Error controlTransfer len: $len  index: $index  string: \"$string\"")
            false
        } else {
            // len == string.length is the ideal ACK. Some phones return 0 for a successful
            // OUT control transfer (they accept the data but report 0 bytes in the data
            // stage). Treat any non-negative return as success; log a warning if unexpected.
            if (len != string.length) {
                AppLog.w("Unexpected controlTransfer len: $len (expected ${string.length})  index: $index  string: \"$string\"")
            } else {
                AppLog.i("Success controlTransfer len: $len  index: $index  string: \"$string\"")
            }
            true
        }
    }

    companion object {
        private const val USB_TIMEOUT_IN_MS = 100
        private const val MANUFACTURER = "Android"
        private const val MODEL = "Android Auto"
        private const val DESCRIPTION = "Android Auto"//"Android Open Automotive Protocol"
        private const val VERSION = "2.0.1"
        private const val URI = "https://developer.android.com/auto/index.html"
        private const val SERIAL = "HU-AAAAAA001"

        // Indexes for strings sent by the host via ACC_REQ_SEND_STRING:
        private const val ACC_IDX_MAN = 0
        private const val ACC_IDX_MOD = 1
        private const val ACC_IDX_DES = 2
        private const val ACC_IDX_VER = 3
        private const val ACC_IDX_URI = 4
        private const val ACC_IDX_SER = 5

        // OAP Control requests:
        private const val ACC_REQ_GET_PROTOCOL = 51
        private const val ACC_REQ_SEND_STRING = 52
        private const val ACC_REQ_START = 53
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/connection/NearbyManager.kt`:

```kt
package com.andrerinas.headunitrevived.connection

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.content.ContextCompat
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.Settings
import com.google.android.gms.nearby.Nearby
import com.google.android.gms.nearby.connection.ConnectionInfo
import com.google.android.gms.nearby.connection.ConnectionLifecycleCallback
import com.google.android.gms.nearby.connection.ConnectionResolution
import com.google.android.gms.nearby.connection.ConnectionsStatusCodes
import com.google.android.gms.nearby.connection.DiscoveredEndpointInfo
import com.google.android.gms.nearby.connection.DiscoveryOptions
import com.google.android.gms.nearby.connection.EndpointDiscoveryCallback
import com.google.android.gms.nearby.connection.Payload
import com.google.android.gms.nearby.connection.PayloadCallback
import com.google.android.gms.nearby.connection.PayloadTransferUpdate
import com.google.android.gms.nearby.connection.Strategy
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import java.net.Socket

/**
 * Manages Google Nearby Connections on the Headunit (Tablet).
 * The Tablet acts as a DISCOVERER only.
 */
class NearbyManager(
    private val context: Context,
    private val scope: CoroutineScope,
    private val onSocketReady: (Socket) -> Unit
) {

    data class DiscoveredEndpoint(val id: String, val name: String)

    companion object {
        private val _discoveredEndpoints = MutableStateFlow<List<DiscoveredEndpoint>>(emptyList())
        val discoveredEndpoints: StateFlow<List<DiscoveredEndpoint>> = _discoveredEndpoints
    }

    private val connectionsClient = Nearby.getConnectionsClient(context)
    private val SERVICE_ID = "com.andrerinas.hurev"
    private val STRATEGY = Strategy.P2P_POINT_TO_POINT
    private var isRunning = false
    private var isConnecting = false
    private var activeNearbySocket: NearbySocket? = null
    private var activeEndpointId: String? = null
    private var activePipes: Array<android.os.ParcelFileDescriptor>? = null
    private val settings = Settings(context)

    fun start() {
        if (!hasRequiredPermissions()) {
            AppLog.w("NearbyManager: Missing required location/bluetooth permissions. Skipping start.")
            return
        }
        if (isRunning) {
            AppLog.i("NearbyManager: Already running discovery.")
            return
        }
        AppLog.i("NearbyManager: Starting Nearby (Discoverer only)...")
        isRunning = true
        _discoveredEndpoints.value = emptyList()
        startDiscovery()
    }

    private fun hasRequiredPermissions(): Boolean {
        val hasCoarse = ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_COARSE_LOCATION) == PackageManager.PERMISSION_GRANTED
        val hasFine = ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        if (!hasCoarse && !hasFine) return false

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val hasAdvertise = ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_ADVERTISE) == PackageManager.PERMISSION_GRANTED
            val hasScan = ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
            val hasConnect = ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
            if (!hasAdvertise || !hasScan || !hasConnect) return false
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val hasNearby = ContextCompat.checkSelfPermission(context, Manifest.permission.NEARBY_WIFI_DEVICES) == PackageManager.PERMISSION_GRANTED
            if (!hasNearby) return false
        }

        return true
    }

    fun stop() {
        AppLog.i("NearbyManager: Stopping discovery and disconnecting from any active endpoint...")
        isRunning = false
        isConnecting = false
        connectionsClient.stopDiscovery()
        activeEndpointId?.let {
            connectionsClient.disconnectFromEndpoint(it)
            activeEndpointId = null
        }
        activeNearbySocket?.close()
        activeNearbySocket = null
        activePipes?.forEach { try { it.close() } catch (e: Exception) {} }
        activePipes = null
        _discoveredEndpoints.value = emptyList()
    }

    /**
     * Manually initiate a connection to a specific discovered endpoint.
     * Called from HomeFragment when user taps a device in the list.
     */
    fun connectToEndpoint(endpointId: String) {
        if (isConnecting) {
            AppLog.w("NearbyManager: Already connecting, ignoring request for $endpointId")
            return
        }
        AppLog.i("NearbyManager: Requesting connection to endpoint: $endpointId")
        isConnecting = true

        connectionsClient.requestConnection(android.os.Build.MODEL, endpointId, connectionLifecycleCallback)
            .addOnFailureListener { e ->
                AppLog.e("NearbyManager: Failed to request connection: ${e.message}")
                isConnecting = false
            }
    }

    private fun startDiscovery() {
        val discoveryOptions = DiscoveryOptions.Builder()
            .setStrategy(STRATEGY)
            .build()

        AppLog.i("NearbyManager: Requesting Discovery with SERVICE_ID: $SERVICE_ID (Strategy: P2P_POINT_TO_POINT)")
        connectionsClient.startDiscovery(SERVICE_ID, endpointDiscoveryCallback, discoveryOptions)
            .addOnSuccessListener { AppLog.d("NearbyManager: [OK] Discovery started.") }
            .addOnFailureListener { e ->
                AppLog.e("NearbyManager: [ERROR] Discovery failed: ${e.message}")
                isRunning = false
            }
    }

    private val endpointDiscoveryCallback = object : EndpointDiscoveryCallback() {
        override fun onEndpointFound(endpointId: String, info: DiscoveredEndpointInfo) {
            AppLog.i("NearbyManager: Endpoint FOUND: ${info.endpointName} ($endpointId)")
            val current = _discoveredEndpoints.value.toMutableList()
            if (current.none { it.id == endpointId }) {
                current.add(DiscoveredEndpoint(endpointId, info.endpointName))
                _discoveredEndpoints.value = current
            }

            // Auto-connect logic
            val autoConnectMode = settings.autoConnectLastSession
            AppLog.i("NearbyManager: Auto-connect check: Enabled=$autoConnectMode, isConnecting=$isConnecting, activeEndpointId=$activeEndpointId")

            if (autoConnectMode && !isConnecting && activeEndpointId == null) {
                val lastDevice = settings.lastNearbyDeviceName
                AppLog.i("NearbyManager: Comparing found '${info.endpointName}' with last known '$lastDevice'")
                if (lastDevice.isNotEmpty() && lastDevice == info.endpointName) {
                    AppLog.i("NearbyManager: MATCH! Auto-connecting to known device '$lastDevice'...")
                    connectToEndpoint(endpointId)
                }
            }
        }

        override fun onEndpointLost(endpointId: String) {
            AppLog.i("NearbyManager: Endpoint LOST: $endpointId")
            val current = _discoveredEndpoints.value.toMutableList()
            current.removeAll { it.id == endpointId }
            _discoveredEndpoints.value = current
        }
    }

    private val connectionLifecycleCallback = object : ConnectionLifecycleCallback() {
        override fun onConnectionInitiated(endpointId: String, info: ConnectionInfo) {
            AppLog.i("NearbyManager: Connection INITIATED with $endpointId (${info.endpointName}). Token: ${info.authenticationToken}")
            AppLog.i("NearbyManager: Automatically ACCEPTING connection...")

            // Save last connected device name for auto-reconnect
            AppLog.i("NearbyManager: Saving '${info.endpointName}' as last connected device candidate.")
            settings.lastNearbyDeviceName = info.endpointName

            // Stop discovery as soon as it finds the target.
            isRunning = false
            connectionsClient.stopDiscovery()

            connectionsClient.acceptConnection(endpointId, payloadCallback)
                .addOnFailureListener { e -> AppLog.e("NearbyManager: Failed to accept connection: ${e.message}") }
        }

        override fun onConnectionResult(endpointId: String, result: ConnectionResolution) {
            val status = result.status
            AppLog.i("NearbyManager: Connection RESULT for $endpointId: StatusCode=${status.statusCode} (${status.statusMessage})")

            if (status.statusCode != ConnectionsStatusCodes.STATUS_OK) {
                isConnecting = false
            }

            when (status.statusCode) {
                ConnectionsStatusCodes.STATUS_OK -> {
                    isConnecting = false
                    activeEndpointId = endpointId

                    val socket = NearbySocket()
                    activeNearbySocket = socket

                    scope.launch(Dispatchers.IO) {
                        val sock = activeNearbySocket ?: return@launch

                        // [CRITICAL] Wait a bit before sending the payload.
                        // The phone (WirelessHelper) has a ~500ms delay in its connection logic.
                        // If we send too early, the phone won't have its 'activeNearbySocket'
                        // set yet, and our incoming stream will be dropped/ignored by the phone.
                        AppLog.i("NearbyManager: Waiting 800ms for phone state synchronization...")
                        kotlinx.coroutines.delay(800)

                        // 1. Create outgoing pipe (Tablet -> Phone)
                        val pipes = android.os.ParcelFileDescriptor.createPipe()
                        activePipes = pipes
                        val outputStream = android.os.ParcelFileDescriptor.AutoCloseOutputStream(pipes[1])
                        sock.outputStreamWrapper = outputStream

                        // 2. Initiate stream tunnel
                        AppLog.i("NearbyManager: Initiating stream tunnel to $endpointId...")
                        val tabletToPhonePayload = Payload.fromStream(pipes[0])
                        AppLog.i("NearbyManager: Sending STREAM payload (ID: ${tabletToPhonePayload.id})")

                        connectionsClient.sendPayload(endpointId, tabletToPhonePayload)
                            .addOnSuccessListener {
                                AppLog.i("NearbyManager: [OK] Tablet->Phone stream payload registered.")
                            }
                            .addOnFailureListener { e ->
                                AppLog.e("NearbyManager: [ERROR] Failed to send stream: ${e.message}")
                            }

                        // [CRITICAL] Start AA handshake immediately.
                        // NearbySocket.read() will block internally until Phone stream arrives.
                        AppLog.i("NearbyManager: Starting AA handshake now. Input will block until stream arrives.")
                        onSocketReady(sock)
                    }
                }
                ConnectionsStatusCodes.STATUS_CONNECTION_REJECTED -> AppLog.w("NearbyManager: Connection REJECTED by $endpointId")
                ConnectionsStatusCodes.STATUS_ERROR -> AppLog.e("NearbyManager: Connection ERROR with $endpointId")
                else -> AppLog.w("NearbyManager: Unknown connection result code: ${status.statusCode}")
            }
        }

        override fun onDisconnected(endpointId: String) {
            AppLog.i("NearbyManager: DISCONNECTED from $endpointId")
            if (activeEndpointId == endpointId) {
                activeEndpointId = null
                isConnecting = false
            }
        }
    }

    private val payloadCallback = object : PayloadCallback() {
        override fun onPayloadReceived(endpointId: String, payload: Payload) {
            AppLog.i("NearbyManager: Payload RECEIVED from $endpointId. Type: ${payload.type}")
            if (payload.type == Payload.Type.STREAM) {
                AppLog.i("NearbyManager: Received incoming STREAM payload. Completing bidirectional tunnel.")
                activeNearbySocket?.let { socket ->
                    socket.inputStreamWrapper = payload.asStream()?.asInputStream()
                    AppLog.i("NearbyManager: InputStream assigned to socket. Handshake should continue.")
                }
            } else if (payload.type == Payload.Type.BYTES) {
                val msg = String(payload.asBytes() ?: byteArrayOf())
                AppLog.i("NearbyManager: Received BYTES payload: $msg")
                if (msg == "PING") {
                    AppLog.i("NearbyManager: Received PING from Phone. Connections are alive.")
                }
            }
        }


        override fun onPayloadTransferUpdate(endpointId: String, update: PayloadTransferUpdate) {
            if (update.status == PayloadTransferUpdate.Status.SUCCESS) {
                AppLog.d("NearbyManager: Payload transfer SUCCESS for endpoint $endpointId")
            } else if (update.status == PayloadTransferUpdate.Status.FAILURE) {
                AppLog.e("NearbyManager: Payload transfer FAILURE for endpoint $endpointId")
            }
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/app/SurfaceActivity.kt`:

```kt
package com.andrerinas.headunitrevived.app

import android.content.Context
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.utils.LocaleHelper

/**
 * Base for the projection activity. Does NOT extend [BaseActivity] to avoid
 * [BaseActivity.onResume] calling [recreate] on night-mode or theme changes,
 * which would destroy the video surface mid-session.
 */
abstract class SurfaceActivity : AppCompatActivity() {

    override fun attachBaseContext(newBase: Context) {
        super.attachBaseContext(LocaleHelper.wrapContext(newBase))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_headunit)
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/app/BootCompleteReceiver.kt`:

```kt
package com.andrerinas.headunitrevived.app

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import androidx.core.content.ContextCompat
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.Settings

class BootCompleteReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
        val action = intent.action
        if (action !in BOOT_ACTIONS) return

        AppLog.i("Boot auto-start: received action=$action")

        val bootEnabled = Settings.isAutoStartOnBootEnabled(context)
        val screenOnEnabled = Settings.isAutoStartOnScreenOnEnabled(context)
        val usbEnabled = Settings.isAutoStartOnUsbEnabled(context)

        if (bootEnabled) {
            AppLog.i("Boot auto-start: starting AapService with BOOT_START (trigger=$action)")
            val serviceIntent = Intent(context, AapService::class.java).apply {
                putExtra(EXTRA_BOOT_START, true)
            }
            ContextCompat.startForegroundService(context, serviceIntent)
        } else if (screenOnEnabled) {
            // "Start on screen on" needs the service alive to register its dynamic
            // SCREEN_ON receiver. On Quick Boot devices this is a real reboot, so
            // the service must be started after boot to listen for future SCREEN_ON.
            AppLog.i("Boot auto-start: screen-on auto-start enabled, starting AapService to register SCREEN_ON receiver (trigger=$action)")
            val serviceIntent = Intent(context, AapService::class.java)
            ContextCompat.startForegroundService(context, serviceIntent)
        } else if (usbEnabled) {
            // On hibernating head units, USB_DEVICE_ATTACHED may not fire after wake.
            // Start the service in the background so it can register its UsbReceiver
            // and check for already-connected USB devices.
            AppLog.i("Boot auto-start: USB auto-start enabled, starting AapService to check USB (trigger=$action)")
            val serviceIntent = Intent(context, AapService::class.java).apply {
                this.action = AapService.ACTION_CHECK_USB
            }
            ContextCompat.startForegroundService(context, serviceIntent)
        } else {
            AppLog.i("Boot auto-start: disabled, skipping")
        }
    }

    companion object {
        const val EXTRA_BOOT_START = "com.andrerinas.headunitrevived.EXTRA_BOOT_START"

        private val BOOT_ACTIONS = setOf(
            // Standard Android boot
            Intent.ACTION_BOOT_COMPLETED,
            Intent.ACTION_LOCKED_BOOT_COMPLETED,
            // Generic / OEM quick boot
            "android.intent.action.QUICKBOOT_POWERON",
            "com.htc.intent.action.QUICKBOOT_POWERON",
            // MediaTek IPO (Instant Power On)
            "com.mediatek.intent.action.QUICKBOOT_POWERON",
            "com.mediatek.intent.action.BOOT_IPO",
            // FYT / GLSX head units (ACC ignition wake)
            "com.fyt.boot.ACCON",
            "com.glsx.boot.ACCON",
            "android.intent.action.ACTION_MT_COMMAND_SLEEP_OUT",
            // Microntek / MTCD / PX3 head units (ACC wake)
            "com.cayboy.action.ACC_ON",
            "com.carboy.action.ACC_ON"
        )
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/app/AutoStartReceiver.kt`:

```kt
package com.andrerinas.headunitrevived.app

import android.bluetooth.BluetoothDevice
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.main.MainActivity
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.Settings

class AutoStartReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
        val action = intent.action
        // Use device-protected storage so the BT MAC is readable during locked boot
        val targetMac = Settings.getAutoStartBtMac(context)

        if (targetMac.isEmpty()) return

        // [FIX] Don't trigger auto-start if we are already connected!
        // This prevents activity restarts if BT reconnects during a session.
        if (com.andrerinas.headunitrevived.App.provide(context).commManager.isConnected) {
            AppLog.d("AutoStartReceiver: Already connected to Android Auto. Ignoring BT event.")
            return
        }

        if (action == BluetoothDevice.ACTION_ACL_CONNECTED) {
            val device = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
            }

            AppLog.i("BT Device connected: ${device?.name} (${device?.address})")

            if (device?.address == targetMac) {
                AppLog.i("MATCH! Starting AapService via Bluetooth Auto-start...")

                // Start the service to make the app alive
                val serviceIntent = Intent(context, AapService::class.java)
                try {
                    androidx.core.content.ContextCompat.startForegroundService(context, serviceIntent)
                } catch (e: Exception) {
                    AppLog.e("Failed to start AapService from background: ${e.message}")
                }

                // Also attempt to start the UI (might be blocked on Android 10+ without special permission)
                val launchIntent = Intent(context, MainActivity::class.java).apply {
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    putExtra(MainActivity.EXTRA_LAUNCH_SOURCE, "Bluetooth auto-start")
                }
                try {
                    context.startActivity(launchIntent)
                } catch (e: Exception) {
                    AppLog.w("Could not start UI from background (expected on Android 10+): ${e.message}")
                }
            }
        }
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/app/BaseActivity.kt`:

```kt
package com.andrerinas.headunitrevived.app

import android.content.Context
import android.content.res.Configuration
import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.andrerinas.headunitrevived.utils.AppThemeManager
import com.andrerinas.headunitrevived.utils.LocaleHelper
import com.andrerinas.headunitrevived.utils.Settings

/**
 * Base Activity that handles app language configuration and live theme switching.
 * All activities should extend this class to properly apply the user's language preference.
 */
open class BaseActivity : AppCompatActivity() {

    private var currentLanguage: String? = null
    private var currentAppTheme: Settings.AppTheme? = null
    private var currentNightMode: Int = 0
    private var currentUseGradientBackground: Boolean = false
    private var currentUseExtremeDarkMode: Boolean = false

    override fun attachBaseContext(newBase: Context) {
        super.attachBaseContext(LocaleHelper.wrapContext(newBase))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val settings = Settings(this)
        currentLanguage = settings.appLanguage
        currentAppTheme = settings.appTheme
        currentNightMode = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
        currentUseGradientBackground = settings.useGradientBackground
        currentUseExtremeDarkMode = settings.useExtremeDarkMode

        val appliedVersion = AppThemeManager.themeVersion.value
        AppThemeManager.themeVersion.observe(this) { version ->
            if (version != appliedVersion) {
                recreate()
            }
        }
    }

    override fun onResume() {
        super.onResume()
        val settings = Settings(this)
        val actualNightMode = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
        if (currentLanguage != settings.appLanguage ||
            currentAppTheme != settings.appTheme ||
            currentNightMode != actualNightMode ||
            currentUseGradientBackground != settings.useGradientBackground ||
            currentUseExtremeDarkMode != settings.useExtremeDarkMode) {
            recreate()
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/app/RemoteControlReceiver.kt`:

```kt
package com.andrerinas.headunitrevived.app

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.view.KeyEvent
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.connection.CommManager
import com.andrerinas.headunitrevived.utils.AppLog

class RemoteControlReceiver : BroadcastReceiver() {

    override fun onReceive(context: Context, intent: Intent) {
        val action = intent.action ?: return
        AppLog.i("RemoteControlReceiver received: $action")

        // Broadcast for UI debugging (KeymapFragment)
        val debugIntent = Intent("com.andrerinas.headunitrevived.DEBUG_KEY").apply {
            putExtra("action", action)
            intent.extras?.let { putExtras(it) }
            setPackage(context.packageName)
        }
        context.sendBroadcast(debugIntent)

        if (Intent.ACTION_MEDIA_BUTTON == action) {
            val event = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableExtra(Intent.EXTRA_KEY_EVENT, KeyEvent::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(Intent.EXTRA_KEY_EVENT)
            }

            event?.let {
                AppLog.i("ACTION_MEDIA_BUTTON: " + it.keyCode + " (handled via MediaSession)")
                // We no longer send to commManager here to prevent double-skips.
                // The active MediaSession in AapService handles ACTION_MEDIA_BUTTON.

                // Also broadcast for the UI
                val keyIntent = Intent(com.andrerinas.headunitrevived.contract.KeyIntent.action).apply {
                    putExtra(com.andrerinas.headunitrevived.contract.KeyIntent.extraEvent, it)
                    setPackage(context.packageName)
                }
                context.sendBroadcast(keyIntent)
            }
        } else {
            // Handle command-based intents (common on many Android Headunits)
            val command = intent.getStringExtra("command") ?: intent.getStringExtra("action") ?: intent.getStringExtra("action_command")

            // Broadcast command for UI debug (if not already handled by ACTION_MEDIA_BUTTON block)
            if (action != Intent.ACTION_MEDIA_BUTTON) {
                val debugIntent = Intent("com.andrerinas.headunitrevived.DEBUG_KEY").apply {
                    putExtra("action", action)
                    putExtra("command", command)
                    intent.extras?.let { putExtras(it) }
                    setPackage(context.packageName)
                }
                context.sendBroadcast(debugIntent)
            }

            val commManager = App.provide(context).commManager
            if (commManager.connectionState.value !is CommManager.ConnectionState.TransportStarted) {
                AppLog.i("RemoteControlReceiver: Transport not started, skipping command execution")
                return
            }

            when (command) {
                "next", "skip_next", "skip", "forward", "skip_forward" -> {
                    commManager.send(KeyEvent.KEYCODE_MEDIA_NEXT, true)
                    commManager.send(KeyEvent.KEYCODE_MEDIA_NEXT, false)
                }
                "previous", "skip_previous", "prev", "rewind", "back", "skip_back", "skip_backward" -> {
                    commManager.send(KeyEvent.KEYCODE_MEDIA_PREVIOUS, true)
                    commManager.send(KeyEvent.KEYCODE_MEDIA_PREVIOUS, false)
                }
                "play", "start", "resume" -> {
                    commManager.send(KeyEvent.KEYCODE_MEDIA_PLAY, true)
                    commManager.send(KeyEvent.KEYCODE_MEDIA_PLAY, false)
                }
                "pause", "stop" -> {
                    commManager.send(KeyEvent.KEYCODE_MEDIA_PAUSE, true)
                    commManager.send(KeyEvent.KEYCODE_MEDIA_PAUSE, false)
                }
                "togglepause", "playpause", "play_pause", "media_play_pause" -> {
                    commManager.send(KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE, true)
                    commManager.send(KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE, false)
                }
                "mute", "volume_mute" -> {
                    commManager.send(KeyEvent.KEYCODE_VOLUME_MUTE, true)
                    commManager.send(KeyEvent.KEYCODE_VOLUME_MUTE, false)
                }
                "voice", "mic", "microphone", "search" -> {
                    commManager.send(KeyEvent.KEYCODE_SEARCH, true)
                    commManager.send(KeyEvent.KEYCODE_SEARCH, false)
                }
                else -> {
                    // Some headunits send a raw keycode as an int extra
                    val extraKeyCode = intent.getIntExtra("keycode", -1)
                        .takeIf { it > 0 }
                        ?: intent.getIntExtra("key_code", -1).takeIf { it > 0 }
                    if (extraKeyCode != null) {
                        AppLog.i("RemoteControlReceiver: raw keycode=$extraKeyCode from command=$command")
                        commManager.send(extraKeyCode, true)
                        commManager.send(extraKeyCode, false)
                    } else {
                        AppLog.i("RemoteControlReceiver: Unknown command='$command' from action='$action'")
                    }
                }
            }
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/app/UsbAttachedActivity.kt`:

```kt
package com.andrerinas.headunitrevived.app

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Bundle
import android.widget.Toast
import androidx.core.content.ContextCompat
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.connection.CommManager
import com.andrerinas.headunitrevived.connection.UsbAccessoryMode
import com.andrerinas.headunitrevived.connection.UsbDeviceCompat
import com.andrerinas.headunitrevived.connection.UsbReceiver
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.DeviceIntent
import com.andrerinas.headunitrevived.utils.LocaleHelper
import com.andrerinas.headunitrevived.main.MainActivity
import com.andrerinas.headunitrevived.utils.Settings


class UsbAttachedActivity : Activity() {

    override fun attachBaseContext(newBase: Context) {
        super.attachBaseContext(LocaleHelper.wrapContext(newBase))
    }

    private fun resolveUsbDevice(intent: Intent?): UsbDevice? {
        DeviceIntent(intent).device?.let { return it }

        val usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
        val devices = usbManager.deviceList.values.toList()
        return if (devices.size == 1) {
            val device = devices[0]
            AppLog.i("No USB device in intent extras, falling back to single device from deviceList: ${UsbDeviceCompat(device).uniqueName}")
            device
        } else {
            AppLog.e("No USB device in intent extras and ${devices.size} devices in deviceList, cannot determine target")
            null
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        AppLog.i("USB Intent: $intent")

        val device = resolveUsbDevice(intent)
        if (device == null) {
            finish()
            return
        }

        val settings = Settings(this)

        if (App.provide(this).commManager.connectionState.value is CommManager.ConnectionState.TransportStarted) {
            AppLog.e("Thread already running")
            finish()
            return
        }

        if (UsbDeviceCompat.isInAccessoryMode(device)) {
            val usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
            if (!usbManager.hasPermission(device)) {
                AppLog.i("Usb in accessory mode but no permission. Requesting...")
                val permissionIntent = UsbReceiver.createPermissionPendingIntent(this)
                usbManager.requestPermission(device, permissionIntent)
                finish()
                return
            }
            AppLog.i("Usb in accessory mode and has permission. Starting AapService.")
            ContextCompat.startForegroundService(this, Intent(this, AapService::class.java).apply {
                action = AapService.ACTION_CHECK_USB
            })
            finish()
            return
        }

        val deviceCompat = UsbDeviceCompat(device)

        // Launch app UI if USB auto-start is enabled (for any device — a non-AA
        // device simply won't complete the AOA handshake, no harm done)
        // Use device-protected storage for the auto-start check so it works
        // during locked boot (before credential storage is available)
        val autoStartOnUsb = Settings.isAutoStartOnUsbEnabled(this)
        if (autoStartOnUsb && !App.provide(this).commManager.isConnected) {
            AppLog.i("USB auto-start: launching app for ${deviceCompat.uniqueName}")
            try {
                startActivity(Intent(this, MainActivity::class.java).apply {
                    addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    putExtra(MainActivity.EXTRA_LAUNCH_SOURCE, "USB auto-start")
                })
            } catch (e: Exception) {
                AppLog.w("Could not start UI from USB auto-start: ${e.message}")
            }
        }

        if (!autoStartOnUsb && !settings.isConnectingDevice(deviceCompat)) {
            AppLog.i("Skipping device ${deviceCompat.uniqueName} (not allowed and USB auto-start disabled)")
            finish()
            return
        }

        val usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
        val usbMode = UsbAccessoryMode(usbManager)
        AppLog.i("Switching USB device to accessory mode " + deviceCompat.uniqueName)
        Toast.makeText(this, getString(R.string.switching_usb_accessory_mode, deviceCompat.uniqueName), Toast.LENGTH_SHORT).show()
        // Run the USB control transfers on a background thread — they block for several
        // hundred ms and must not execute on the main thread (ANR risk).
        Thread {
            val result = usbMode.connectAndSwitch(device)
            runOnUiThread {
                if (result) {
                    Toast.makeText(this, getString(R.string.success), Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(this, getString(R.string.failed), Toast.LENGTH_SHORT).show()
                }
                finish()
            }
        }.start()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)

        val device = resolveUsbDevice(getIntent())
        if (device == null) {
            finish()
            return
        }

        AppLog.i(UsbDeviceCompat.getUniqueName(device))

        if (App.provide(this).commManager.connectionState.value !is CommManager.ConnectionState.TransportStarted) {
            if (UsbDeviceCompat.isInAccessoryMode(device)) {
                AppLog.e("Usb in accessory mode")
                ContextCompat.startForegroundService(this, Intent(this, AapService::class.java).apply {
                    action = AapService.ACTION_CHECK_USB
                })
            }
        } else {
            AppLog.e("Thread already running")
        }

        finish()
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/extractor/SystemMediaExtractor.kt`:

```kt
package com.andrerinas.headunitrevived.extractor

import android.content.Context
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri

import java.io.IOException
import java.nio.ByteBuffer

class SystemMediaExtractor : MediaExtractorInterface {
    private val mExtractor = MediaExtractor()

    override fun readSampleData(buffer: ByteBuffer, offset: Int): Int {
        return mExtractor.readSampleData(buffer, offset)
    }

    override fun getSampleCryptoInfo(cryptoInfo: MediaCodec.CryptoInfo) {
        mExtractor.getSampleCryptoInfo(cryptoInfo)
    }

    override fun release() {
        mExtractor.release()
    }

    override fun setDataSource(content: ByteArray, width: Int, height: Int) {

    }

    @Throws(IOException::class)
    override fun setDataSource(context: Context, uri: Uri, headers: Map<String, String>) {
        mExtractor.setDataSource(context, uri, headers)
    }

    override val trackCount: Int
        get() = mExtractor.trackCount

    override fun unselectTrack(index: Int) {
        mExtractor.unselectTrack(index)
    }

    override fun getTrackFormat(index: Int): MediaFormat {
        return mExtractor.getTrackFormat(index)
    }

    override fun selectTrack(index: Int) {
        mExtractor.selectTrack(index)
    }

    override val sampleFlags: Int
        get() = mExtractor.sampleFlags

    override val sampleTime: Long
        get() = mExtractor.sampleTime

    override fun advance() {
        mExtractor.advance()
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/extractor/MediaExtractorInterface.kt`:

```kt
package com.andrerinas.headunitrevived.extractor

import android.content.Context
import android.media.MediaCodec
import android.media.MediaFormat
import android.net.Uri

import java.io.IOException
import java.nio.ByteBuffer

interface MediaExtractorInterface {
    fun readSampleData(buffer: ByteBuffer, offset: Int): Int

    fun getSampleCryptoInfo(cryptoInfo: MediaCodec.CryptoInfo)

    fun release()

    fun setDataSource(content: ByteArray, width: Int, height: Int)

    @Throws(IOException::class)
    fun setDataSource(context: Context, uri: Uri, headers: Map<String, String>)

    val trackCount: Int

    fun unselectTrack(index: Int)

    fun getTrackFormat(index: Int): MediaFormat

    fun selectTrack(index: Int)

    val sampleFlags: Int

    val sampleTime: Long

    fun advance()
}

```

`app/src/main/java/com/andrerinas/headunitrevived/extractor/StreamVideoExtractor.kt`:

```kt
package com.andrerinas.headunitrevived.extractor

import android.content.Context
import android.media.MediaCodec
import android.media.MediaFormat
import android.net.Uri
import com.andrerinas.headunitrevived.utils.AppLog
import java.io.IOException
import java.nio.ByteBuffer
import java.security.InvalidParameterException

class StreamVideoExtractor : MediaExtractorInterface {

    private var mFormat: MediaFormat? = null
    private var mContentData: ByteArray? = null
    private var mSampleOffset = -1

    override var sampleFlags: Int = 0
        private set

    override fun readSampleData(buffer: ByteBuffer, offset: Int): Int {
        if (mSampleOffset >= 0) {
            var nextSample = findNextNAL(mSampleOffset + 4)
            if (nextSample == -1) {
                nextSample = mContentData!!.size - 1
            }
            val size = nextSample - mSampleOffset
            buffer.clear()
            buffer.put(mContentData!!)
            AppLog.i("readSampleData (offset: %d,next: %d,size: %d,length: %d, flags: 0x%08x)", mSampleOffset, nextSample, size, mContentData!!.size, sampleFlags)
            return size
        }
        return 0
    }

    override fun getSampleCryptoInfo(cryptoInfo: MediaCodec.CryptoInfo) {

    }

    override fun release() {
        mContentData = null
    }

    override fun setDataSource(content: ByteArray, width: Int, height: Int) {
        mContentData = content
        sampleFlags = 0
        mFormat = MediaFormat.createVideoFormat("video/avc", width, height)

        mSampleOffset = findSPS()
        if (mSampleOffset == -1) {
            throw InvalidParameterException("Cannot find SPS in content")
        }
    }

    @Throws(IOException::class)
    override fun setDataSource(context: Context, uri: Uri, headers: Map<String, String>) {

    }

    override val trackCount: Int
        get() = 1

    override fun unselectTrack(index: Int) {

    }

    override fun getTrackFormat(index: Int): MediaFormat {
        return mFormat!!
    }

    override fun selectTrack(index: Int) {

    }

    override val sampleTime: Long
        get() = 0

    override fun advance() {
        sampleFlags = sampleFlags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG.inv()
        mSampleOffset = findNextNAL(mSampleOffset + 4)
        if (mSampleOffset == -1) {
            sampleFlags = sampleFlags or MediaCodec.BUFFER_FLAG_END_OF_STREAM
        }
    }

    private fun findNextNAL(offset: Int): Int {
        var naloffset = offset
        while (naloffset < mContentData!!.size) {
            if (mContentData!![naloffset].toInt() == 0 && mContentData!![naloffset + 1].toInt() == 0 && mContentData!![naloffset + 2].toInt() == 0 && mContentData!![naloffset + 3].toInt() == 1) {
                AppLog.i("Found sequence at %d: 0x0 0x0 0x0 0x1 0x%01x (%d)", mSampleOffset, mContentData!![naloffset + 4], mContentData!![naloffset + 4])
                return naloffset
            }
            naloffset++
        }
        return -1
    }

    // SPS (Sequence Parameter Set) NAL Unit first
    private fun findSPS(): Int {
        var offset = 0
        while (offset >= 0) {
            offset = findNextNAL(offset)
            if (offset == -1) {
                return -1
            }

            if (mContentData!![offset + 4] == SPS_BIT) {
                sampleFlags = sampleFlags or MediaCodec.BUFFER_FLAG_CODEC_CONFIG
                return offset
            }
        }
        return -1
    }

    companion object {
        private const val SPS_BIT: Byte = 0x67
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/Main.kt`:

```kt
package com.andrerinas.headunitrevived

import java.util.Locale

object Main {

    @JvmStatic fun main(args: Array<String>) {
        println("Main")

/*
        val mediaAck = Media.Ack()
        mediaAck.clear()
        mediaAck.sessionId = Integer.MAX_VALUE
        mediaAck.ack = Integer.MAX_VALUE

        print(mediaAck.serializedSize)

        val sensors = Control.Service()
        sensors.id = 2
        sensors.sensorSourceService = Control.Service.SensorSourceService()
        sensors.sensorSourceService.sensors = arrayOfNulls<Control.Service.SensorSourceService.Sensor>(2)
        sensors.sensorSourceService.sensors[0] = Control.Service.SensorSourceService.Sensor()
        sensors.sensorSourceService.sensors[0].type = Sensors.SENSOR_TYPE_DRIVING_STATUS
        sensors.sensorSourceService.sensors[1] = Control.Service.SensorSourceService.Sensor()
        sensors.sensorSourceService.sensors[1].type = Sensors.SENSOR_TYPE_NIGHT
        //        input.inputSourceService.keycodesSupported = new int[] { 84 };

        printByteArray(MessageNano.toByteArray(sensors))
        val rsp2 = byteArrayOf(0x08, 0x02, 0x12, 0x0C, 0x0A, 0x02, 0x08, 0x01, 0x0A, 0x02, 0x08, 0x0A, 0x0A, 0x02, 0x08, 0x0D)
        printByteArray(rsp2)

        val actual = Control.Service()
        MessageNano.mergeFrom(actual, rsp2)
        printByteArray(MessageNano.toByteArray(actual))

        print(actual.toString())
        */
    }


    private fun printByteArray(ba: ByteArray) {
        for (i in ba.indices) {
            val hex = String.format(Locale.US, "%02X", ba[i])
            print(hex)
            //            int pos = (ba[i] >> 3);
            //            if (pos > 0) {
            //                System.out.print("[" + pos + "]");
            //            }
            print(' ')
        }
        println()
    }

}

```

`app/src/main/java/com/andrerinas/headunitrevived/AppComponent.kt`:

```kt
package com.andrerinas.headunitrevived

import android.app.NotificationManager
import android.content.Context
import android.net.wifi.WifiManager
import com.andrerinas.headunitrevived.connection.CommManager
import com.andrerinas.headunitrevived.decoder.AudioDecoder
import com.andrerinas.headunitrevived.decoder.VideoDecoder
import com.andrerinas.headunitrevived.utils.Settings

class AppComponent(private val app: App) {

    val settings = Settings(app)
    val videoDecoder = VideoDecoder(settings)
    val audioDecoder = AudioDecoder()

    val notificationManager: NotificationManager
        get() = app.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    val wifiManager: WifiManager
        get() = app.getSystemService(Context.WIFI_SERVICE) as WifiManager

    val commManager = CommManager(app, settings, audioDecoder, videoDecoder)
}

```

`app/src/main/java/com/andrerinas/headunitrevived/location/GpsLocationService.kt`:

```kt
package com.andrerinas.headunitrevived.location

import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder

class GpsLocationService : Service() {
    private var gpsLocation: GpsLocation? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (gpsLocation == null) {
            gpsLocation = GpsLocation(this)
        }

        gpsLocation?.start()

        return START_STICKY
    }

    override fun onDestroy() {
        super.onDestroy()

        gpsLocation?.stop()
    }

    override fun onBind(intent: Intent): IBinder? {
        return null
    }

    companion object {
        fun intent(context: Context): Intent {
            return Intent(context, GpsLocationService::class.java)
        }
    }

}
```

`app/src/main/java/com/andrerinas/headunitrevived/location/GpsLocation.kt`:

```kt
package com.andrerinas.headunitrevived.location

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Bundle
import androidx.core.content.PermissionChecker
import com.andrerinas.headunitrevived.contract.LocationUpdateIntent
import com.andrerinas.headunitrevived.utils.AppLog

class GpsLocation constructor(private val context: Context): LocationListener {
    private val locationManager = context.getSystemService(Context.LOCATION_SERVICE) as LocationManager
    private var requested: Boolean = false

    @SuppressLint("MissingPermission")
    fun start() {
        if (requested) {
            return
        }
        AppLog.i("Request location updates")
        if (PermissionChecker.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) == PermissionChecker.PERMISSION_GRANTED
                && locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
            locationManager.requestLocationUpdates(LocationManager.GPS_PROVIDER, 500, 0.0f, this)
            val location = locationManager.getLastKnownLocation(LocationManager.GPS_PROVIDER)
            AppLog.i("Last known location:  ${location?.toString() ?: "Unknown"}")
            requested = true
        }
    }

    override fun onLocationChanged(location: Location) {
        context.sendBroadcast(LocationUpdateIntent(location))
    }

    override fun onStatusChanged(provider: String, status: Int, extras: Bundle) {
        AppLog.i("$provider: $status")
    }

    override fun onProviderEnabled(provider: String) {
        AppLog.i(provider)
    }

    override fun onProviderDisabled(provider: String) {
        AppLog.i(provider)
    }

    fun stop() {
        AppLog.i("Remove location updates")
        requested = false
        locationManager.removeUpdates(this)
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/AboutFragment.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.os.Build
import android.os.Bundle
import android.text.Html
import android.text.Spanned
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import com.andrerinas.headunitrevived.R
import com.google.android.material.appbar.MaterialToolbar
import java.util.Calendar

class AboutFragment : Fragment() {

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return inflater.inflate(R.layout.fragment_about, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        val toolbar = view.findViewById<MaterialToolbar>(R.id.toolbar)
        toolbar.setNavigationOnClickListener {
            findNavController().popBackStack()
        }

        val copyrightText = view.findViewById<TextView>(R.id.copyright_text)
        val currentYear = Calendar.getInstance().get(Calendar.YEAR)
        copyrightText.text = getString(R.string.copyright, currentYear)

        val contentText = view.findViewById<TextView>(R.id.about_content_text)

        val sb = StringBuilder()
        sb.append("<b>Special thanks to Mike Reidis for the original idea and android auto protocol and code.</b><br/>")
        sb.append("<a href=\"https://github.com/mikereidis/headunit\">https://github.com/mikereidis/headunit</a><br/><br/>")
        sb.append("<h3>Issues, bugs, and feedback or questions</h3>")
        sb.append("If you need any help, go to the github page of this app. You will additionally can support me via <a href=\"https://www.paypal.me/anrinas\">Paypal</a><br/>")
        sb.append("<a href=\"https://github.com/andreknieriem/headunit-revived\">https://github.com/andreknieriem/headunit-revived</a><br/><br/>")

        sb.append(parseMarkdownToHtml(readAsset("CHANGELOG.md")))
        sb.append("<br/><br/>")

        sb.append("<h3>LICENSE</h3>")
        // License is plain text, preserve newlines
        val license = readAsset("LICENSE").replace("\n", "<br/>")
        sb.append(license)

        contentText.text = fromHtml(sb.toString())
        contentText.movementMethod = android.text.method.LinkMovementMethod.getInstance() // Make links clickable
    }

    private fun readAsset(fileName: String): String {
        return try {
            requireContext().assets.open(fileName).bufferedReader().use { it.readText() }
        } catch (e: Exception) {
            "Error loading $fileName"
        }
    }

    private fun parseMarkdownToHtml(markdown: String): String {
        var html = markdown
        // Headers
        html = html.replace(Regex("### (.*)"), "<h4>$1</h4>")
        html = html.replace(Regex("## (.*)"), "<h3>$1</h3>")
        html = html.replace(Regex("# (.*)"), "<h2>$1</h2>")

        // Bold
        html = html.replace(Regex("\\*\\*(.*?)\\*\\*"), "<b>$1</b>")

        // Lists
        html = html.replace(Regex("\n- (.*)"), "<br/>&#8226; $1")

        // Newlines (Markdown preserves single newlines as space, but we want break for readability in log?)
        // Actually, let's just replace double newlines with paragraph, and single with br?
        // Simple approach: Replace \n with <br/> but be careful not to break tags.
        // For list items we already handled the newline prefix.

        // Let's replace remaining newlines that are not part of tags
        // This is tricky with regex.
        // Better: replace all \n with <br/> at the end?
        // The list replacement consumed the \n before the dash.

        return html
    }

    private fun fromHtml(html: String): Spanned {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            Html.fromHtml(html, Html.FROM_HTML_MODE_LEGACY)
        } else {
            @Suppress("DEPRECATION")
            Html.fromHtml(html)
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/MainViewModel.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.app.Application
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import androidx.core.content.ContextCompat
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.MutableLiveData
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.connection.UsbDeviceCompat
import com.andrerinas.headunitrevived.connection.UsbReceiver
import com.andrerinas.headunitrevived.utils.Settings

class MainViewModel(application: Application): AndroidViewModel(application), UsbReceiver.Listener {

    val usbDevices = MutableLiveData<List<UsbDeviceCompat>>()

    private val app: App
        get() = getApplication()
    private val settings = Settings(application)
    private val usbReceiver = UsbReceiver(this)

    fun register() {
        ContextCompat.registerReceiver(app, usbReceiver, UsbReceiver.createFilter(), ContextCompat.RECEIVER_NOT_EXPORTED)
        usbDevices.value = createDeviceList(settings.allowedDevices)
    }

    override fun onCleared() {
        app.unregisterReceiver(usbReceiver)
        super.onCleared()
    }

    override fun onUsbDetach(device: android.hardware.usb.UsbDevice) {
        usbDevices.value = createDeviceList(settings.allowedDevices)
    }

    override fun onUsbAccessoryDetach() {
        usbDevices.value = createDeviceList(settings.allowedDevices)
    }

    override fun onUsbAttach(device: android.hardware.usb.UsbDevice) {
        usbDevices.value = createDeviceList(settings.allowedDevices)
    }

    override fun onUsbPermission(granted: Boolean, connect: Boolean, device: UsbDevice) {
        usbDevices.value = createDeviceList(settings.allowedDevices)
    }

    private fun createDeviceList(allowDevices: Set<String>): List<UsbDeviceCompat> {
        val manager = app.getSystemService(android.content.Context.USB_SERVICE) as UsbManager
        return manager.deviceList.entries
                .map { (_, device) -> UsbDeviceCompat(device) }
                .sortedWith(Comparator { lhs, rhs ->
                    if (lhs.isInAccessoryMode) {
                        return@Comparator -1
                    }
                    if (rhs.isInAccessoryMode) {
                        return@Comparator 1
                    }
                    if (allowDevices.contains(lhs.uniqueName)) {
                        return@Comparator -1
                    }
                    if (allowDevices.contains(rhs.uniqueName)) {
                        return@Comparator 1
                    }
                    lhs.uniqueName.compareTo(rhs.uniqueName)
                })
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/main/SettingsActivity.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.os.Bundle
import android.view.View
import androidx.activity.enableEdgeToEdge
import androidx.navigation.fragment.NavHostFragment
import android.content.res.Configuration
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.app.BaseActivity
import com.andrerinas.headunitrevived.utils.Settings
import com.andrerinas.headunitrevived.utils.SystemUI

class SettingsActivity : BaseActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)

        val appSettings = Settings(this)
        val isNightActive = (resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES
        if (appSettings.appTheme == Settings.AppTheme.EXTREME_DARK ||
            (appSettings.useExtremeDarkMode && isNightActive)) {
            theme.applyStyle(R.style.ThemeOverlay_ExtremeDark, true)
        } else if (appSettings.useGradientBackground) {
            theme.applyStyle(R.style.ThemeOverlay_GradientBackground, true)
        }
        requestedOrientation = appSettings.screenOrientation.androidOrientation

        setContentView(R.layout.activity_settings)

        val navHostFragment = supportFragmentManager.findFragmentById(R.id.settings_nav_host) as NavHostFragment
        val navController = navHostFragment.navController

        // Set the start destination to settingsFragment instead of homeFragment
        val navGraph = navController.navInflater.inflate(R.navigation.nav_graph)
        navGraph.startDestination = R.id.settingsFragment
        navController.graph = navGraph

        // Restore sub-screen after recreate() (e.g. theme change from DarkModeFragment)
        val restoredDestination = savedInstanceState?.getInt(KEY_CURRENT_DESTINATION, 0) ?: 0
        if (restoredDestination != 0 && restoredDestination != R.id.settingsFragment) {
            try {
                navController.navigate(restoredDestination)
            } catch (_: Exception) {}
        }

        val root = findViewById<View>(R.id.settings_nav_host)
        SystemUI.apply(window, root, appSettings.fullscreenMode)
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        val navHostFragment = supportFragmentManager.findFragmentById(R.id.settings_nav_host) as? NavHostFragment
        val currentDest = navHostFragment?.navController?.currentDestination?.id ?: 0
        outState.putInt(KEY_CURRENT_DESTINATION, currentDest)
    }

    companion object {
        private const val KEY_CURRENT_DESTINATION = "current_nav_destination"
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            val appSettings = Settings(this)
            val root = findViewById<View>(R.id.settings_nav_host)
            SystemUI.apply(window, root, appSettings.fullscreenMode)
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/SettingsFragment.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.app.AlertDialog
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings as SystemSettings
import android.text.InputType
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.EditText
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.main.settings.SettingItem
import com.andrerinas.headunitrevived.main.settings.SettingsAdapter
import com.andrerinas.headunitrevived.utils.Settings
import com.andrerinas.headunitrevived.utils.LocaleHelper
import com.andrerinas.headunitrevived.BuildConfig
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder

class SettingsFragment : Fragment() {
    private lateinit var settings: Settings
    private lateinit var settingsRecyclerView: RecyclerView
    private lateinit var settingsAdapter: SettingsAdapter
    private lateinit var toolbar: MaterialToolbar
    private var saveButton: MaterialButton? = null

    // Local state to hold changes before saving
    private var pendingMicSampleRate: Int? = null
    private var pendingUseGps: Boolean? = null
    private var pendingShowNavigationNotifications: Boolean? = null
    private var pendingSyncMediaSessionAaMetadata: Boolean? = null
    private var pendingResolution: Int? = null
    private var pendingDpi: Int? = null
    private var pendingFullscreenMode: Settings.FullscreenMode? = null
    private var pendingViewMode: Settings.ViewMode? = null
    private var pendingForceSoftware: Boolean? = null
    private var pendingVideoCodec: String? = null
    private var pendingFpsLimit: Int? = null
    private var pendingBluetoothAddress: String? = null
    private var pendingEnableAudioSink: Boolean? = null
    private var pendingUseAacAudio: Boolean? = null
    private var pendingMicInputSource: Int? = null
    private var pendingUseNativeSsl: Boolean? = null
    private var pendingEnableRotary: Boolean? = null
    private var pendingAudioLatencyMultiplier: Int? = null
    private var pendingAudioQueueCapacity: Int? = null
    private var pendingShowFpsCounter: Boolean? = null
    private var pendingScreenOrientation: Settings.ScreenOrientation? = null
    private var pendingAppLanguage: String? = null
    private var pendingFakeSpeed: Boolean? = null

    private var pendingWifiConnectionMode: Int? = null
    private var pendingHelperConnectionStrategy: Int? = null
    private var pendingAutoEnableHotspot: Boolean? = null
    private var pendingWaitForWifi: Boolean? = null
    private var pendingWaitForWifiTimeout: Int? = null

    // Flag to determine if the projection should stretch to fill the screen
    private var pendingStretchToFill: Boolean? = null
    private var pendingForcedScale: Boolean? = null

    private var pendingKillOnDisconnect: Boolean? = null

    // Custom Insets
    private var pendingInsetLeft: Int? = null
    private var pendingInsetTop: Int? = null
    private var pendingInsetRight: Int? = null
    private var pendingInsetBottom: Int? = null

    private var pendingMediaVolumeOffset: Int? = null
    private var pendingAssistantVolumeOffset: Int? = null
    private var pendingNavigationVolumeOffset: Int? = null

    private var requiresRestart = false
    private var hasChanges = false
    private val SAVE_ITEM_ID = 1001

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return inflater.inflate(R.layout.fragment_settings, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        settings = App.provide(requireContext()).settings

        // Initialize local state with current values
        pendingMicSampleRate = settings.micSampleRate
        pendingUseGps = settings.useGpsForNavigation
        pendingShowNavigationNotifications = settings.showNavigationNotifications
        pendingSyncMediaSessionAaMetadata = settings.syncMediaSessionWithAaMetadata
        pendingResolution = settings.resolutionId
        pendingDpi = settings.dpiPixelDensity
        pendingFullscreenMode = settings.fullscreenMode
        pendingViewMode = settings.viewMode
        pendingForceSoftware = settings.forceSoftwareDecoding
        pendingVideoCodec = settings.videoCodec
        pendingFpsLimit = settings.fpsLimit
        pendingBluetoothAddress = settings.bluetoothAddress
        pendingEnableAudioSink = settings.enableAudioSink
        pendingUseAacAudio = settings.useAacAudio
        pendingMicInputSource = settings.micInputSource
        pendingUseNativeSsl = settings.useNativeSsl
        pendingEnableRotary = settings.enableRotary
        pendingAudioLatencyMultiplier = settings.audioLatencyMultiplier
        pendingAudioQueueCapacity = settings.audioQueueCapacity
        pendingShowFpsCounter = settings.showFpsCounter
        pendingScreenOrientation = settings.screenOrientation
        pendingAppLanguage = settings.appLanguage

        // Initialize local state for stretch to fill
        pendingStretchToFill = settings.stretchToFill
        pendingForcedScale = settings.forcedScale

        pendingKillOnDisconnect = settings.killOnDisconnect
        pendingAutoEnableHotspot = settings.autoEnableHotspot
        pendingFakeSpeed = settings.fakeSpeed

        pendingWifiConnectionMode = settings.wifiConnectionMode
        pendingHelperConnectionStrategy = settings.helperConnectionStrategy
        pendingWaitForWifi = settings.waitForWifiBeforeWifiDirect
        pendingWaitForWifiTimeout = settings.waitForWifiTimeout

        pendingInsetLeft = settings.insetLeft
        pendingInsetTop = settings.insetTop
        pendingInsetRight = settings.insetRight
        pendingInsetBottom = settings.insetBottom

        pendingMediaVolumeOffset = settings.mediaVolumeOffset
        pendingAssistantVolumeOffset = settings.assistantVolumeOffset
        pendingNavigationVolumeOffset = settings.navigationVolumeOffset

        // Intercept system back button
        requireActivity().onBackPressedDispatcher.addCallback(viewLifecycleOwner, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                handleBackPress()
            }
        })

        toolbar = view.findViewById(R.id.toolbar)
        settingsAdapter = SettingsAdapter()
        settingsRecyclerView = view.findViewById(R.id.settingsRecyclerView)
        settingsRecyclerView.layoutManager = LinearLayoutManager(requireContext())
        settingsRecyclerView.adapter = settingsAdapter

        updateSettingsList()
        setupToolbar()

        savedInstanceState?.getParcelable<android.os.Parcelable>("recycler_scroll")?.let {
            settingsRecyclerView.layoutManager?.onRestoreInstanceState(it)
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        if (::settingsRecyclerView.isInitialized) {
            settingsRecyclerView.layoutManager?.onSaveInstanceState()?.let {
                outState.putParcelable("recycler_scroll", it)
            }
        }
    }

    private fun setupToolbar() {
        toolbar.setNavigationOnClickListener {
            handleBackPress()
        }

        // Add the Save item with custom layout
        val saveItem = toolbar.menu.add(0, SAVE_ITEM_ID, 0, getString(R.string.save))
        saveItem.setShowAsAction(android.view.MenuItem.SHOW_AS_ACTION_ALWAYS)
        saveItem.setActionView(R.layout.layout_save_button)

        // Get the button from the action view
        saveButton = saveItem.actionView?.findViewById(R.id.save_button_widget)
        saveButton?.setOnClickListener {
            saveSettings()
        }

        updateSaveButtonState()
    }

    private fun handleBackPress() {
        if (hasChanges) {
            MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                .setTitle(R.string.unsaved_changes)
                .setMessage(R.string.unsaved_changes_message)
                .setPositiveButton(R.string.discard) { _, _ ->
                    navigateBack()
                }
                .setNegativeButton(R.string.cancel, null)
                .show()
        } else {
            navigateBack()
        }
    }

    private fun navigateBack() {
        try {
            val navController = findNavController()
            if (!navController.navigateUp()) {
                requireActivity().finish()
            }
        } catch (e: Exception) {
            requireActivity().finish()
        }
    }

    private fun updateSaveButtonState() {
        saveButton?.isEnabled = hasChanges
        saveButton?.text = if (requiresRestart) getString(R.string.save_and_restart) else getString(R.string.save)
    }

    private fun saveSettings() {
        val languageChanged = pendingAppLanguage != settings.appLanguage

        pendingMicSampleRate?.let { settings.micSampleRate = it }
        pendingUseGps?.let { settings.useGpsForNavigation = it }
        pendingShowNavigationNotifications?.let { settings.showNavigationNotifications = it }
        pendingSyncMediaSessionAaMetadata?.let { settings.syncMediaSessionWithAaMetadata = it }
        pendingResolution?.let { settings.resolutionId = it }
        pendingDpi?.let { settings.dpiPixelDensity = it }
        pendingFullscreenMode?.let { settings.fullscreenMode = it }
        pendingViewMode?.let { settings.viewMode = it }
        pendingForceSoftware?.let { settings.forceSoftwareDecoding = it }
        pendingVideoCodec?.let { settings.videoCodec = it }
        pendingFpsLimit?.let { settings.fpsLimit = it }
        pendingBluetoothAddress?.let { settings.bluetoothAddress = it }
        pendingEnableAudioSink?.let { settings.enableAudioSink = it }
        pendingUseAacAudio?.let { settings.useAacAudio = it }
        pendingMicInputSource?.let { settings.micInputSource = it }
        pendingUseNativeSsl?.let { settings.useNativeSsl = it }
        pendingEnableRotary?.let { settings.enableRotary = it }
        pendingAudioLatencyMultiplier?.let { settings.audioLatencyMultiplier = it }
        pendingAudioQueueCapacity?.let { settings.audioQueueCapacity = it }
        pendingShowFpsCounter?.let { settings.showFpsCounter = it }
        pendingScreenOrientation?.let { settings.screenOrientation = it }

        pendingMediaVolumeOffset?.let { settings.mediaVolumeOffset = it }
        pendingAssistantVolumeOffset?.let { settings.assistantVolumeOffset = it }
        pendingNavigationVolumeOffset?.let { settings.navigationVolumeOffset = it }

        pendingAppLanguage?.let { settings.appLanguage = it }

        // Save the stretch to fill preference
        pendingStretchToFill?.let { settings.stretchToFill = it }
        pendingForcedScale?.let { settings.forcedScale = it }

        pendingKillOnDisconnect?.let { settings.killOnDisconnect = it }
        pendingAutoEnableHotspot?.let { settings.autoEnableHotspot = it }
        pendingFakeSpeed?.let { settings.fakeSpeed = it }

        val oldWifiMode = settings.wifiConnectionMode
        val oldHelperStrategy = settings.helperConnectionStrategy
        pendingWifiConnectionMode?.let { settings.wifiConnectionMode = it }
        pendingHelperConnectionStrategy?.let { settings.helperConnectionStrategy = it }
        pendingWaitForWifi?.let { settings.waitForWifiBeforeWifiDirect = it }
        pendingWaitForWifiTimeout?.let { settings.waitForWifiTimeout = it }

        pendingInsetLeft?.let { settings.insetLeft = it }
        pendingInsetTop?.let { settings.insetTop = it }
        pendingInsetRight?.let { settings.insetRight = it }
        pendingInsetBottom?.let { settings.insetBottom = it }

        settings.commit()

        if (oldWifiMode != settings.wifiConnectionMode || oldHelperStrategy != settings.helperConnectionStrategy) {
            val intent = Intent(requireContext(), AapService::class.java).apply {
                val mode = settings.wifiConnectionMode
                action = if (mode == 1 || mode == 2 || mode == 3)
                    AapService.ACTION_START_WIRELESS else AapService.ACTION_STOP_WIRELESS
            }
            requireContext().startService(intent)
        }

        if (requiresRestart) {
            if (App.provide(requireContext()).commManager.isConnected) {
                Toast.makeText(context, getString(R.string.stopping_service), Toast.LENGTH_SHORT).show()
                val stopServiceIntent = Intent(requireContext(), AapService::class.java).apply {
                    action = AapService.ACTION_STOP_SERVICE
                }
                ContextCompat.startForegroundService(requireContext(), stopServiceIntent)
            }
        }

        // Reset change tracking
        hasChanges = false
        requiresRestart = false
        updateSaveButtonState()
        updateSettingsList()

        Toast.makeText(context, getString(R.string.settings_saved), Toast.LENGTH_SHORT).show()

        if (languageChanged) {
            requireActivity().recreate()
        }
    }

    private fun checkChanges() {
        // Check for any changes
        val anyChange = pendingMicSampleRate != settings.micSampleRate ||
                        pendingUseGps != settings.useGpsForNavigation ||
                        pendingShowNavigationNotifications != settings.showNavigationNotifications ||
                        pendingSyncMediaSessionAaMetadata != settings.syncMediaSessionWithAaMetadata ||
                        pendingResolution != settings.resolutionId ||
                        pendingDpi != settings.dpiPixelDensity ||
                        pendingFullscreenMode != settings.fullscreenMode ||
                        pendingViewMode != settings.viewMode ||
                        pendingForceSoftware != settings.forceSoftwareDecoding ||
                        pendingVideoCodec != settings.videoCodec ||
                        pendingFpsLimit != settings.fpsLimit ||
                        pendingBluetoothAddress != settings.bluetoothAddress ||
                        pendingEnableAudioSink != settings.enableAudioSink ||
                        pendingUseAacAudio != settings.useAacAudio ||
                        pendingMicInputSource != settings.micInputSource ||
                        pendingUseNativeSsl != settings.useNativeSsl ||
                        pendingEnableRotary != settings.enableRotary ||
                        pendingAudioLatencyMultiplier != settings.audioLatencyMultiplier ||
                        pendingAudioQueueCapacity != settings.audioQueueCapacity ||
                        pendingShowFpsCounter != settings.showFpsCounter ||
                        pendingScreenOrientation != settings.screenOrientation ||
                        pendingAppLanguage != settings.appLanguage ||
                        pendingStretchToFill != settings.stretchToFill ||
                        pendingForcedScale != settings.forcedScale ||
                        pendingInsetLeft != settings.insetLeft ||
                        pendingInsetTop != settings.insetTop ||
                        pendingInsetRight != settings.insetRight ||
                        pendingInsetBottom != settings.insetBottom ||
                        pendingMediaVolumeOffset != settings.mediaVolumeOffset ||
                        pendingAssistantVolumeOffset != settings.assistantVolumeOffset ||
                        pendingNavigationVolumeOffset != settings.navigationVolumeOffset ||
                        pendingKillOnDisconnect != settings.killOnDisconnect ||
                        pendingAutoEnableHotspot != settings.autoEnableHotspot ||
                        pendingFakeSpeed != settings.fakeSpeed ||
                        pendingWifiConnectionMode != settings.wifiConnectionMode ||
                        pendingHelperConnectionStrategy != settings.helperConnectionStrategy ||
                        pendingWaitForWifi != settings.waitForWifiBeforeWifiDirect ||
                        pendingWaitForWifiTimeout != settings.waitForWifiTimeout

        hasChanges = anyChange

        // Check for restart requirement
        requiresRestart = pendingResolution != settings.resolutionId ||
                          pendingVideoCodec != settings.videoCodec ||
                          pendingFpsLimit != settings.fpsLimit ||
                          pendingDpi != settings.dpiPixelDensity ||
                          pendingForceSoftware != settings.forceSoftwareDecoding ||
                          pendingEnableRotary != settings.enableRotary ||
                          pendingEnableAudioSink != settings.enableAudioSink ||
                          pendingUseAacAudio != settings.useAacAudio ||
                          pendingAudioLatencyMultiplier != settings.audioLatencyMultiplier ||
                          pendingAudioQueueCapacity != settings.audioQueueCapacity ||
                          pendingUseNativeSsl != settings.useNativeSsl ||
                          pendingInsetLeft != settings.insetLeft ||
                          pendingInsetTop != settings.insetTop ||
                          pendingInsetRight != settings.insetRight ||
                          pendingInsetBottom != settings.insetBottom ||
                          pendingWifiConnectionMode != settings.wifiConnectionMode

        updateSaveButtonState()
    }

    private fun updateSettingsList() {
        val scrollState = settingsRecyclerView.layoutManager?.onSaveInstanceState()
        val items = mutableListOf<SettingItem>()

        // --- General Settings ---
        items.add(SettingItem.CategoryHeader("general", R.string.category_general))

        // Auto-Optimize Wizard
        items.add(SettingItem.SettingEntry(
            stableId = "autoOptimize",
            nameResId = R.string.auto_optimize,
            value = getString(R.string.auto_optimize_desc),
            onClick = { _ ->
                com.andrerinas.headunitrevived.utils.SetupWizard(requireContext()) {
                    requireActivity().recreate()
                }.start()
            }
        ))

        // Language Selector
        val availableLocales = LocaleHelper.getAvailableLocales(requireContext())
        val currentLocale = LocaleHelper.stringToLocale(pendingAppLanguage ?: "")
        val currentLanguageDisplay = if (currentLocale != null) {
            LocaleHelper.getDisplayName(currentLocale)
        } else {
            getString(R.string.system_default)
        }

        items.add(SettingItem.SettingEntry(
            stableId = "appLanguage",
            nameResId = R.string.app_language,
            value = currentLanguageDisplay,
            onClick = { _ ->
                val languageNames = mutableListOf(getString(R.string.system_default))
                val localeCodes = mutableListOf("")

                availableLocales.forEach { locale ->
                    languageNames.add(LocaleHelper.getDisplayName(locale))
                    localeCodes.add(LocaleHelper.localeToString(locale))
                }

                val currentIndex = localeCodes.indexOf(pendingAppLanguage ?: "").coerceAtLeast(0)

                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                    .setTitle(R.string.change_language)
                    .setSingleChoiceItems(languageNames.toTypedArray(), currentIndex) { dialog, which ->
                        pendingAppLanguage = localeCodes[which]
                        checkChanges()
                        dialog.dismiss()
                        updateSettingsList()
                    }
                    .show()
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "vehicleInfoSettings",
            nameResId = R.string.vehicle_info_settings,
            value = getString(R.string.vehicle_info_settings_description),
            onClick = {
                try {
                    findNavController().navigate(R.id.action_settingsFragment_to_vehicleInfoFragment)
                } catch (e: Exception) { }
            }
        ))

        // --- Wireless Connection ---
        items.add(SettingItem.CategoryHeader("wirelessConnection", R.string.category_wireless))

        // Add 2.4GHz Warning Banner
        items.add(SettingItem.InfoBanner(
            stableId = "wireless24ghzWarning",
            textResId = R.string.wireless_24ghz_warning
        ))

        val wirelessModeOptions = listOf(
            getString(R.string.wireless_mode_helper),
            getString(R.string.wireless_mode_native),
            getString(R.string.wireless_mode_server)
        )

        val wirelessSelectedIndex = when (pendingWifiConnectionMode) {
            2 -> 0 // Helper
            3 -> 1 // Native
            0, 1 -> 2 // Server
            else -> 2
        }

        items.add(SettingItem.SegmentedButtonSettingEntry(
            stableId = "wifiConnectionMode",
            nameResId = R.string.wireless_mode,
            options = wirelessModeOptions,
            selectedIndex = wirelessSelectedIndex,
            onOptionSelected = { index ->
                val newMode = when (index) {
                    0 -> 2 // Helper
                    1 -> 3 // Native
                    2 -> if (pendingWifiConnectionMode == 0) 0 else 1 // Keep manual/auto choice if already in server mode
                    else -> 1
                }

                if (newMode == 3) {
                    // Compatibility check for Native AA
                    if (com.andrerinas.headunitrevived.connection.NativeAaHandshakeManager.checkCompatibility()) {
                        MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                            .setTitle(R.string.supported_nativeaa)
                            .setMessage(R.string.supported_nativeaa_desc)
                            .setPositiveButton(android.R.string.ok) { dialog, _ ->
                                pendingWifiConnectionMode = 3
                                checkChanges()
                                updateSettingsList()
                                dialog.dismiss()
                            }
                            .setNegativeButton(android.R.string.cancel, null)
                            .show()
                    } else {
                        MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                            .setTitle(R.string.not_supported_nativeaa)
                            .setMessage(R.string.not_supported_nativeaa_desc)
                            .setPositiveButton(android.R.string.ok) { dialog, _ ->
                                pendingWifiConnectionMode = 3
                                checkChanges()
                                updateSettingsList()
                                dialog.dismiss()
                            }
                            .setNegativeButton(android.R.string.cancel, null)
                            .show()
                    }
                } else {
                    pendingWifiConnectionMode = newMode
                    checkChanges()
                    updateSettingsList()
                }
            }
        ))

        // Sub-setting for Headunit Server (Manual vs Auto)
        if (pendingWifiConnectionMode == 0 || pendingWifiConnectionMode == 1) {
            items.add(SettingItem.SegmentedButtonSettingEntry(
                stableId = "serverModeSelection",
                nameResId = R.string.server_mode_label,
                options = listOf(getString(R.string.server_mode_manual), getString(R.string.server_mode_auto)),
                selectedIndex = if (pendingWifiConnectionMode == 0) 0 else 1,
                onOptionSelected = { index ->
                    pendingWifiConnectionMode = if (index == 0) 0 else 1
                    checkChanges()
                    updateSettingsList()
                }
            ))

            // Mode 1 (Auto Server) can also use the auto-hotspot feature
            if (pendingWifiConnectionMode == 1) {
                addHotspotToggle(items)
            }
        }

        // Sub-setting for Wireless Helper Strategy
        if (pendingWifiConnectionMode == 2) {
            val helperStrategies = resources.getStringArray(R.array.helper_strategies)
            items.add(SettingItem.SettingEntry(
                stableId = "helperStrategy",
                nameResId = R.string.helper_strategy_label,
                value = helperStrategies.getOrElse(pendingHelperConnectionStrategy!!) { "" },
                onClick = {
                    MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                        .setTitle(R.string.helper_strategy_label)
                        .setSingleChoiceItems(helperStrategies, pendingHelperConnectionStrategy!!) { dialog, which ->
                            pendingHelperConnectionStrategy = which
                            checkChanges()
                            dialog.dismiss()
                            updateSettingsList()
                        }
                        .show()
                }
            ))

            // Mode 2 only shows Hotspot toggle for Strategy 4 (Headunit Hotspot)
            if (pendingHelperConnectionStrategy == 4) {
                addHotspotToggle(items)
            }

            if (pendingHelperConnectionStrategy == 1) { // WiFi Direct (P2P)
                items.add(SettingItem.ToggleSettingEntry(
                    stableId = "waitForWifi",
                    nameResId = R.string.wait_for_wifi,
                    descriptionResId = R.string.wait_for_wifi_description,
                    isChecked = pendingWaitForWifi ?: false,
                    onCheckedChanged = { isChecked ->
                        pendingWaitForWifi = isChecked
                        checkChanges()
                        updateSettingsList()
                    }
                ))

                if (pendingWaitForWifi == true) {
                    items.add(SettingItem.SliderSettingEntry(
                        stableId = "waitForWifiTimeout",
                        nameResId = R.string.wait_for_wifi_timeout,
                        value = "${pendingWaitForWifiTimeout}s",
                        sliderValue = (pendingWaitForWifiTimeout ?: 10).toFloat(),
                        valueFrom = 5f,
                        valueTo = 30f,
                        stepSize = 1f,
                        onValueChanged = { value ->
                            pendingWaitForWifiTimeout = value.toInt()
                            checkChanges()
                            updateSettingsList()
                        }
                    ))
                }
            }
        }

        // --- Dark Mode ---
        items.add(SettingItem.CategoryHeader("darkMode", R.string.category_dark_mode))

        val appThemeTitles = resources.getStringArray(R.array.app_theme)
        val nightModeTitles = resources.getStringArray(R.array.night_mode)
        val darkModeValue = "${getString(R.string.app_theme_short)}: ${appThemeTitles[settings.appTheme.value]} · " +
                "${getString(R.string.night_mode_short)}: ${nightModeTitles[settings.nightMode.value]}"
        items.add(SettingItem.SettingEntry(
            stableId = "darkModeSettings",
            nameResId = R.string.dark_mode_settings,
            value = darkModeValue,
            onClick = {
                try {
                    findNavController().navigate(R.id.action_settingsFragment_to_darkModeFragment)
                } catch (e: Exception) {
                    // Failover
                }
            }
        ))

        // --- Automation ---
        items.add(SettingItem.CategoryHeader("automation", R.string.category_automation))

        items.add(SettingItem.SettingEntry(
            stableId = "autoStartSettings",
            nameResId = R.string.auto_start_settings,
            value = getString(R.string.auto_start_settings_description),
            onClick = {
                try {
                    findNavController().navigate(R.id.action_settingsFragment_to_autoStartFragment)
                } catch (e: Exception) { }
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "autoConnectSettings",
            nameResId = R.string.auto_connect_settings,
            value = getAutoConnectSummary(),
            onClick = {
                try {
                    findNavController().navigate(R.id.action_settingsFragment_to_autoConnectFragment)
                } catch (e: Exception) { }
            }
        ))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "killOnDisconnect",
            nameResId = R.string.kill_on_disconnect,
            descriptionResId = R.string.kill_on_disconnect_description,
            isChecked = pendingKillOnDisconnect!!,
            onCheckedChanged = { isChecked ->
                if (isChecked) {
                    val conflicts = getKillOnDisconnectConflicts()
                    val hasAutoStartOnBoot = settings.autoStartOnBoot
                    val hasAutoStartOnScreenOn = settings.autoStartOnScreenOn
                    if (conflicts.isNotEmpty() || hasAutoStartOnBoot || hasAutoStartOnScreenOn) {
                        pendingKillOnDisconnect = true
                        updateSettingsList()
                        showKillOnDisconnectWarning(conflicts, hasAutoStartOnBoot, hasAutoStartOnScreenOn)
                    } else {
                        pendingKillOnDisconnect = true
                        checkChanges()
                        updateSettingsList()
                    }
                } else {
                    pendingKillOnDisconnect = false
                    checkChanges()
                    updateSettingsList()
                }
            }
        ))

        // --- Navigation Settings ---
        items.add(SettingItem.CategoryHeader("navigation", R.string.category_navigation))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "gpsNavigation",
            nameResId = R.string.gps_for_navigation,
            descriptionResId = R.string.gps_for_navigation_description,
            isChecked = pendingUseGps!!,
            onCheckedChanged = { isChecked ->
                pendingUseGps = isChecked
                checkChanges()
                updateSettingsList()
            }
        ))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "showNavigationNotifications",
            nameResId = R.string.show_navigation_notifications,
            descriptionResId = R.string.show_navigation_notifications_description,
            isChecked = pendingShowNavigationNotifications!!,
            onCheckedChanged = { isChecked ->
                pendingShowNavigationNotifications = isChecked
                checkChanges()
                updateSettingsList()
            }
        ))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "fakeSpeed",
            nameResId = R.string.fake_speed_title,
            descriptionResId = R.string.fake_speed_description,
            isChecked = pendingFakeSpeed!!,
            onCheckedChanged = { isChecked ->
                pendingFakeSpeed = isChecked
                checkChanges()
                updateSettingsList()
            }
        ))

        // --- Graphic Settings ---
        items.add(SettingItem.CategoryHeader("graphic", R.string.category_graphic))

        items.add(SettingItem.SettingEntry(
            stableId = "resolution",
            nameResId = R.string.resolution,
            value = Settings.Resolution.fromId(pendingResolution!!)?.resName ?: "",
            onClick = { _ ->
                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                    .setTitle(R.string.change_resolution)
                    .setSingleChoiceItems(Settings.Resolution.allRes, pendingResolution!!) { dialog, which ->
                        pendingResolution = which
                        checkChanges()
                        dialog.dismiss()
                        updateSettingsList()
                    }
                    .show()
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "dpiPixelDensity",
            nameResId = R.string.dpi,
            value = if (pendingDpi == 0) getString(R.string.auto) else pendingDpi.toString(),
            onClick = { _ ->
                showNumericInputDialog(
                    title = getString(R.string.enter_dpi_value),
                    message = null,
                    initialValue = pendingDpi ?: 0,
                    onConfirm = { newVal ->
                        pendingDpi = newVal
                        checkChanges()
                        updateSettingsList()
                    }
                )
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "customInsets",
            nameResId = R.string.custom_insets,
            value = "${pendingInsetLeft ?: 0}, ${pendingInsetTop ?: 0}, ${pendingInsetRight ?: 0}, ${pendingInsetBottom ?: 0}",
            onClick = {
                showCustomInsetsDialog()
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "startInFullscreenMode",
            nameResId = R.string.start_in_fullscreen_mode,
            value = when (pendingFullscreenMode) {
                Settings.FullscreenMode.NONE -> getString(R.string.fullscreen_none)
                Settings.FullscreenMode.IMMERSIVE -> getString(R.string.fullscreen_immersive)
                Settings.FullscreenMode.STATUS_ONLY -> getString(R.string.fullscreen_status_only)
                Settings.FullscreenMode.IMMERSIVE_WITH_NOTCH -> getString(R.string.fullscreen_immersive_avoid_notch)
                else -> getString(R.string.auto)
            },
            onClick = {
                val modes = arrayOf(
                    getString(R.string.fullscreen_none),
                    getString(R.string.fullscreen_immersive),
                    getString(R.string.fullscreen_status_only),
                    getString(R.string.fullscreen_immersive_avoid_notch)
                )
                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                    .setTitle(R.string.start_in_fullscreen_mode)
                    .setSingleChoiceItems(modes, pendingFullscreenMode?.value ?: 0) { dialog, which ->
                        val newMode = Settings.FullscreenMode.fromInt(which) ?: Settings.FullscreenMode.NONE
                        pendingFullscreenMode = newMode

                        // PERSIST IMMEDIATELY (Rescue Mode)
                        settings.fullscreenMode = newMode
                        settings.commit()

                        checkChanges()
                        dialog.dismiss()
                        updateSettingsList()

                        // Apply immediately to current UI
                        requireActivity().recreate()
                    }
                    .setNegativeButton(R.string.cancel, null)
                    .show()
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "viewMode",
            nameResId = R.string.view_mode,
            value = when (pendingViewMode) {
                Settings.ViewMode.SURFACE -> getString(R.string.surface_view)
                Settings.ViewMode.TEXTURE -> getString(R.string.texture_view)
                Settings.ViewMode.GLES -> getString(R.string.gles_view)
                else -> getString(R.string.surface_view)
            },
            onClick = { _ ->
                val viewModes = arrayOf(getString(R.string.surface_view), getString(R.string.texture_view), getString(R.string.gles_view))
                val currentIdx = pendingViewMode!!.value
                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                    .setTitle(R.string.change_view_mode)
                    .setSingleChoiceItems(viewModes, currentIdx) { dialog, which ->
                        pendingViewMode = Settings.ViewMode.fromInt(which)!!
                        checkChanges()
                        dialog.dismiss()
                        updateSettingsList()
                    }
                    .show()
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "screenOrientation",
            nameResId = R.string.screen_orientation,
            value = resources.getStringArray(R.array.screen_orientation)[pendingScreenOrientation!!.value],
            onClick = { _ ->
                val orientationOptions = resources.getStringArray(R.array.screen_orientation)
                val currentIdx = pendingScreenOrientation!!.value
                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                    .setTitle(R.string.change_screen_orientation)
                    .setSingleChoiceItems(orientationOptions, currentIdx) { dialog, whiches ->
                        pendingScreenOrientation = Settings.ScreenOrientation.fromInt(whiches)
                        checkChanges()
                        dialog.dismiss()
                        updateSettingsList()
                    }
                    .show()
            }
        ))

        // Add the toggle for Stretch to Fill
        items.add(SettingItem.ToggleSettingEntry(
            stableId = "stretchToFill",
            nameResId = R.string.pref_stretch_screen_title,
            descriptionResId = R.string.pref_stretch_screen_summary,
            isChecked = pendingStretchToFill!!,
            onCheckedChanged = { isChecked ->
                pendingStretchToFill = isChecked
                requiresRestart = true // Requires a reconnect to apply the new rendering bounds
                checkChanges()
                updateSettingsList()
            }
        ))

        if (pendingViewMode == Settings.ViewMode.SURFACE) {
            items.add(SettingItem.ToggleSettingEntry(
                stableId = "forcedScale",
                nameResId = R.string.forced_scale,
                descriptionResId = R.string.forced_scale_description,
                isChecked = pendingForcedScale!!,
                onCheckedChanged = { isChecked ->
                    pendingForcedScale = isChecked
                    requiresRestart = true
                    checkChanges()
                    updateSettingsList()
                }
            ))
        }

        // --- Video Settings ---
        items.add(SettingItem.CategoryHeader("video", R.string.category_video))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "forceSoftwareDecoding",
            nameResId = R.string.force_software_decoding,
            descriptionResId = R.string.force_software_decoding_description,
            isChecked = pendingForceSoftware!!,
            onCheckedChanged = { isChecked ->
                pendingForceSoftware = isChecked
                checkChanges()
                updateSettingsList()
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "videoCodec",
            nameResId = R.string.video_codec,
            value = pendingVideoCodec!!,
            onClick = { _ ->
                val codecs = arrayOf("Auto", "H.264", "H.265")
                val currentCodecIndex = codecs.indexOf(pendingVideoCodec)
                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                    .setTitle(R.string.video_codec)
                    .setSingleChoiceItems(codecs, currentCodecIndex) { dialog, which ->
                        pendingVideoCodec = codecs[which]
                        checkChanges()
                        dialog.dismiss()
                        updateSettingsList()
                    }
                    .show()
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "fpsLimit",
            nameResId = R.string.fps_limit,
            value = "${pendingFpsLimit} FPS",
            onClick = { _ ->
                val fpsOptions = arrayOf("30", "60")
                val currentFpsIndex = fpsOptions.indexOf(pendingFpsLimit.toString())
                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                    .setTitle(R.string.fps_limit)
                    .setSingleChoiceItems(fpsOptions, currentFpsIndex) { dialog, which ->
                        pendingFpsLimit = fpsOptions[which].toInt()
                        checkChanges()
                        dialog.dismiss()
                        updateSettingsList()
                    }
                    .show()
            }
        ))

        // --- Input Settings ---
        items.add(SettingItem.CategoryHeader("input", R.string.category_input))

        items.add(SettingItem.SettingEntry(
            stableId = "keymap",
            nameResId = R.string.keymap,
            value = getString(R.string.keymap_description),
            onClick = { _ ->
                try {
                    findNavController().navigate(R.id.action_settingsFragment_to_keymapFragment)
                } catch (e: Exception) {
                    // Failover
                }
            }
        ))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "enableRotary",
            nameResId = R.string.enable_rotary,
            descriptionResId = R.string.enable_rotary_description,
            isChecked = pendingEnableRotary ?: false,
            onCheckedChanged = { isChecked ->
                pendingEnableRotary = isChecked
                checkChanges()
                updateSettingsList()
            }
        ))

        // --- Audio Settings ---
        items.add(SettingItem.CategoryHeader("audio", R.string.category_audio))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "enableAudioSink",
            nameResId = R.string.enable_audio_sink,
            descriptionResId = R.string.enable_audio_sink_description,
            isChecked = pendingEnableAudioSink!!,
            onCheckedChanged = { isChecked ->
                pendingEnableAudioSink = isChecked
                checkChanges()
                updateSettingsList()
            }
        ))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "useAacAudio",
            nameResId = R.string.use_aac_audio,
            descriptionResId = R.string.use_aac_audio_description,
            isChecked = pendingUseAacAudio!!,
            onCheckedChanged = { isChecked ->
                pendingUseAacAudio = isChecked
                checkChanges()
                updateSettingsList()
            }
        ))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "syncMediaSessionAaMetadata",
            nameResId = R.string.sync_media_session_aa_metadata,
            descriptionResId = R.string.sync_media_session_aa_metadata_description,
            isChecked = pendingSyncMediaSessionAaMetadata!!,
            onCheckedChanged = { isChecked ->
                pendingSyncMediaSessionAaMetadata = isChecked
                checkChanges()
                updateSettingsList()
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "micSampleRate",
            nameResId = R.string.mic_sample_rate,
            value = "${pendingMicSampleRate!! / 1000}kHz",
            onClick = { _ ->
                val currentSampleRateIndex = Settings.MicSampleRates.indexOf(pendingMicSampleRate!!)
                val sampleRateNames = Settings.MicSampleRates.map { "${it / 1000}kHz" }.toTypedArray()

                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                    .setTitle(R.string.mic_sample_rate)
                    .setSingleChoiceItems(sampleRateNames, currentSampleRateIndex) { dialog, which ->
                        val newValue = Settings.MicSampleRates.elementAt(which)
                        pendingMicSampleRate = newValue
                        checkChanges()
                        dialog.dismiss()
                        updateSettingsList()
                    }
                    .show()
            }
        ))

        val micSources = resources.getStringArray(R.array.mic_input_sources)
        val micSourceValues = intArrayOf(0, 1, 6, 7, 100) // DEFAULT, MIC, VOICE_RECOGNITION, VOICE_COMMUNICATION, BT_SCO
        val currentSourceIndex = micSourceValues.indexOf(pendingMicInputSource ?: 0).coerceAtLeast(0)

        items.add(SettingItem.SettingEntry(
            stableId = "micInputSource",
            nameResId = R.string.mic_input_source,
            value = micSources[currentSourceIndex],
            onClick = {
                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                    .setTitle(R.string.mic_input_source)
                    .setSingleChoiceItems(micSources, currentSourceIndex) { dialog, which ->
                        pendingMicInputSource = micSourceValues[which]
                        checkChanges()
                        updateSettingsList()
                        dialog.dismiss()
                    }
                    .show()
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "audioVolumeOffsets",
            nameResId = R.string.audio_volume_offset,
            value = "${(100 + (pendingMediaVolumeOffset ?: 0))}% / ${(100 + (pendingAssistantVolumeOffset ?: 0))}% / ${(100 + (pendingNavigationVolumeOffset ?: 0))}%",
            onClick = {
                showAudioOffsetsDialog()
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "audioLatencyMultiplier",
            nameResId = R.string.audio_latency_multiplier,
            value = "${pendingAudioLatencyMultiplier}x",
            onClick = { _ ->
                val options = arrayOf("1x (Lowest Latency)", "2x (Low Latency)", "4x (High Latency)", "8x (Very High Latency)")
                val values = intArrayOf(1, 2, 4, 8)
                val currentIndex = values.indexOf(pendingAudioLatencyMultiplier ?: 8).coerceAtLeast(0)
                AlertDialog.Builder(requireContext())
                    .setTitle(R.string.audio_latency_multiplier)
                    .setSingleChoiceItems(options, currentIndex) { dialog, which ->
                        pendingAudioLatencyMultiplier = values[which]
                        checkChanges()
                        dialog.dismiss()
                        updateSettingsList()
                    }
                    .show()
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "audioQueueCapacity",
            nameResId = R.string.audio_queue_capacity,
            value = if (pendingAudioQueueCapacity == 0) "Unbounded (Legacy)" else "${pendingAudioQueueCapacity} chunks",
            onClick = { _ ->
                val options = arrayOf("10 chunks (Low Latency)", "20 chunks (Balanced)", "50 chunks (High Latency)", "Unbounded (Max Backlog)")
                val values = intArrayOf(10, 20, 50, 0)
                val currentIndex = values.indexOf(pendingAudioQueueCapacity ?: 0).coerceAtLeast(0)
                AlertDialog.Builder(requireContext())
                    .setTitle(R.string.audio_queue_capacity)
                    .setSingleChoiceItems(options, currentIndex) { dialog, which ->
                        pendingAudioQueueCapacity = values[which]
                        checkChanges()
                        dialog.dismiss()
                        updateSettingsList()
                    }
                    .show()
            }
        ))

        // --- Debug Settings ---
        items.add(SettingItem.CategoryHeader("debug", R.string.category_debug))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "showFpsCounter",
            nameResId = R.string.show_fps_counter,
            descriptionResId = R.string.show_fps_counter_description,
            isChecked = pendingShowFpsCounter!!,
            onCheckedChanged = { isChecked ->
                pendingShowFpsCounter = isChecked
                checkChanges()
                updateSettingsList()
            }
        ))

        val logLevels = com.andrerinas.headunitrevived.utils.LogExporter.LogLevel.entries
        val logLevelNames = logLevels.map { it.name.lowercase().replaceFirstChar { c -> c.uppercase() } }.toTypedArray()
        items.add(SettingItem.SettingEntry(
            stableId = "logLevel",
            nameResId = R.string.log_level,
            value = settings.exporterLogLevel.name.lowercase().replaceFirstChar { it.uppercase() },
            onClick = {
                val currentIndex = logLevels.indexOf(settings.exporterLogLevel)
                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                    .setTitle(R.string.log_level)
                    .setSingleChoiceItems(logLevelNames, currentIndex) { dialog, which ->
                        settings.exporterLogLevel = logLevels[which]
                        dialog.dismiss()
                        updateSettingsList()
                    }
                    .show()
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "captureLog",
            nameResId = if (com.andrerinas.headunitrevived.utils.LogExporter.isCapturing) R.string.stop_log_capture else R.string.start_log_capture,
            value = getString(if (com.andrerinas.headunitrevived.utils.LogExporter.isCapturing) R.string.stop_log_capture_description else R.string.start_log_capture_description),
            onClick = {
                val context = requireContext()
                if (com.andrerinas.headunitrevived.utils.LogExporter.isCapturing) {
                    com.andrerinas.headunitrevived.utils.LogExporter.stopCapture()
                } else {
                    com.andrerinas.headunitrevived.utils.LogExporter.startCapture(context, settings.exporterLogLevel)
                }
                updateSettingsList()
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "exportLogs",
            nameResId = R.string.export_logs,
            value = getString(R.string.export_logs_description),
            onClick = {
                val context = requireContext()
                if (com.andrerinas.headunitrevived.utils.LogExporter.isCapturing) {
                    com.andrerinas.headunitrevived.utils.LogExporter.stopCapture()
                }
                val logFile = com.andrerinas.headunitrevived.utils.LogExporter.saveLogToPublicFile(context, settings.exporterLogLevel)
                updateSettingsList()

                if (logFile != null) {
                    MaterialAlertDialogBuilder(context, R.style.DarkAlertDialog)
                        .setTitle(R.string.logs_exported)
                        .setMessage(getString(R.string.log_saved_to, logFile.absolutePath))
                        .setPositiveButton(R.string.share) { _, _ ->
                            com.andrerinas.headunitrevived.utils.LogExporter.shareLogFile(context, logFile)
                        }
                        .setNegativeButton(R.string.close) { dialog, _ ->
                            dialog.dismiss()
                        }
                        .show()
                } else {
                    Toast.makeText(context, getString(R.string.failed_export_logs), Toast.LENGTH_SHORT).show()
                }
            }
        ))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "useNativeSsl",
            nameResId = R.string.use_native_ssl,
            descriptionResId = R.string.use_native_ssl_description,
            isChecked = pendingUseNativeSsl!!,
            onCheckedChanged = { isChecked ->
                pendingUseNativeSsl = isChecked
                checkChanges()
                updateSettingsList()
            }
        ))

        // --- Info Settings ---
        items.add(SettingItem.CategoryHeader("info", R.string.category_info))

        items.add(SettingItem.SettingEntry(
            stableId = "version",
            nameResId = R.string.version,
            value = BuildConfig.VERSION_NAME,
            onClick = { /* Read only */ }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "about",
            nameResId = R.string.about,
            value = getString(R.string.about_description),
            onClick = {
                try {
                    findNavController().navigate(R.id.action_settingsFragment_to_aboutFragment)
                } catch (e: Exception) {
                    // Failover
                }
            }
        ))

        // Add a dedicated Save button at the bottom if there are changes
        if (hasChanges) {
            items.add(SettingItem.ActionButton(
                stableId = "bottomSaveButton",
                textResId = if (requiresRestart) R.string.save_and_restart else R.string.save,
                onClick = { saveSettings() }
            ))
        }

        settingsAdapter.submitList(items) {
            scrollState?.let { settingsRecyclerView.layoutManager?.onRestoreInstanceState(it) }
        }
    }

    private fun showAudioOffsetsDialog() {
        val dialogView = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_audio_offsets, null)

        val seekMedia = dialogView.findViewById<android.widget.SeekBar>(R.id.seek_media)
        val seekAssistant = dialogView.findViewById<android.widget.SeekBar>(R.id.seek_assistant)
        val seekNavigation = dialogView.findViewById<android.widget.SeekBar>(R.id.seek_navigation)

        val textMedia = dialogView.findViewById<android.widget.TextView>(R.id.text_media_val)
        val textAssistant = dialogView.findViewById<android.widget.TextView>(R.id.text_assistant_val)
        val textNavigation = dialogView.findViewById<android.widget.TextView>(R.id.text_navigation_val)

        // Mapping: 0 to 100 on SeekBar -> 0% to 200% Gain. Default is 50 (100% Gain, 0 Offset)
        // Offset = (seekValue - 50) * 2
        // seekValue = (offset / 2) + 50

        seekMedia.progress = ((pendingMediaVolumeOffset ?: 0) / 2) + 50
        seekAssistant.progress = ((pendingAssistantVolumeOffset ?: 0) / 2) + 50
        seekNavigation.progress = ((pendingNavigationVolumeOffset ?: 0) / 2) + 50

        val updateLabels = {
            textMedia.text = "${(seekMedia.progress * 2)}%"
            textAssistant.text = "${(seekAssistant.progress * 2)}%"
            textNavigation.text = "${(seekNavigation.progress * 2)}%"
        }
        updateLabels()

        val listener = object : android.widget.SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: android.widget.SeekBar?, progress: Int, fromUser: Boolean) {
                updateLabels()
            }
            override fun onStartTrackingTouch(seekBar: android.widget.SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: android.widget.SeekBar?) {}
        }

        seekMedia.setOnSeekBarChangeListener(listener)
        seekAssistant.setOnSeekBarChangeListener(listener)
        seekNavigation.setOnSeekBarChangeListener(listener)

        MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(R.string.audio_volume_offset)
            .setView(dialogView)
            .setPositiveButton(android.R.string.ok) { dialog, _ ->
                pendingMediaVolumeOffset = (seekMedia.progress - 50) * 2
                pendingAssistantVolumeOffset = (seekAssistant.progress - 50) * 2
                pendingNavigationVolumeOffset = (seekNavigation.progress - 50) * 2
                checkChanges()
                updateSettingsList()
                dialog.dismiss()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun showPermissionDialog() {
        MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(R.string.hotspot_permission_title)
            .setMessage(R.string.hotspot_permission_message)
            .setPositiveButton(R.string.open_settings) { dialog, _ ->
                val intent = Intent(SystemSettings.ACTION_MANAGE_WRITE_SETTINGS).apply {
                    data = Uri.parse("package:${requireContext().packageName}")
                }
                startActivity(intent)
                dialog.dismiss()
            }
            .setNegativeButton(R.string.cancel) { _, _ ->
                pendingAutoEnableHotspot = false
                checkChanges()
                updateSettingsList()
            }
            .show()
    }

    private fun showExperimentalWarning() {
        MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(R.string.hotspot_warning_title)
            .setMessage(R.string.hotspot_warning_message)
            .setPositiveButton(android.R.string.ok) { dialog, _ ->
                pendingAutoEnableHotspot = true
                checkChanges()
                updateSettingsList()
                dialog.dismiss()
            }
            .setNegativeButton(android.R.string.cancel) { _, _ ->
                pendingAutoEnableHotspot = false
                checkChanges()
                updateSettingsList()
            }
            .show()
    }

    private fun showCustomInsetsDialog() {
        val dialogView = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_custom_insets, null)

        val inputLeft = dialogView.findViewById<EditText>(R.id.input_left)
        val inputTop = dialogView.findViewById<EditText>(R.id.input_top)
        val inputRight = dialogView.findViewById<EditText>(R.id.input_right)
        val inputBottom = dialogView.findViewById<EditText>(R.id.input_bottom)

        // Set initial values from pending state
        inputLeft.setText((pendingInsetLeft ?: 0).toString())
        inputTop.setText((pendingInsetTop ?: 0).toString())
        inputRight.setText((pendingInsetRight ?: 0).toString())
        inputBottom.setText((pendingInsetBottom ?: 0).toString())

        // Helper to update pending values and UI preview
        fun updatePreview() {
            val l = inputLeft.text.toString().toIntOrNull() ?: 0
            val t = inputTop.text.toString().toIntOrNull() ?: 0
            val r = inputRight.text.toString().toIntOrNull() ?: 0
            val b = inputBottom.text.toString().toIntOrNull() ?: 0

            pendingInsetLeft = l
            pendingInsetTop = t
            pendingInsetRight = r
            pendingInsetBottom = b

            // Live Preview: Set padding on the root view of the Activity
            val root = requireActivity().findViewById<View>(R.id.settings_nav_host)
            root?.setPadding(l, t, r, b)
        }

        // Helper to bind buttons
        fun bindButton(btnId: Int, input: EditText, delta: Int) {
            dialogView.findViewById<View>(btnId).setOnClickListener {
                val current = input.text.toString().toIntOrNull() ?: 0
                val newVal = (current + delta).coerceAtLeast(0)
                input.setText(newVal.toString())
                updatePreview()
            }
        }

        bindButton(R.id.btn_left_minus, inputLeft, -10)
        bindButton(R.id.btn_left_plus, inputLeft, 10)
        bindButton(R.id.btn_top_minus, inputTop, -10)
        bindButton(R.id.btn_top_plus, inputTop, 10)
        bindButton(R.id.btn_right_minus, inputRight, -10)
        bindButton(R.id.btn_right_plus, inputRight, 10)
        bindButton(R.id.btn_bottom_minus, inputBottom, -10)
        bindButton(R.id.btn_bottom_plus, inputBottom, 10)

        // Text Watchers? Maybe overkill, buttons are safer.
        // Let's add simple focus change listener to update preview on manual entry
        val focusListener = View.OnFocusChangeListener { _, hasFocus ->
            if (!hasFocus) updatePreview()
        }
        inputLeft.onFocusChangeListener = focusListener
        inputTop.onFocusChangeListener = focusListener
        inputRight.onFocusChangeListener = focusListener
        inputBottom.onFocusChangeListener = focusListener

        MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(R.string.custom_insets)
            .setView(dialogView)
            .setPositiveButton(android.R.string.ok) { dialog, _ ->
                val l = inputLeft.text.toString().toIntOrNull() ?: 0
                val t = inputTop.text.toString().toIntOrNull() ?: 0
                val r = inputRight.text.toString().toIntOrNull() ?: 0
                val b = inputBottom.text.toString().toIntOrNull() ?: 0

                // PERSIST IMMEDIATELY (Rescue Mode)
                settings.insetLeft = l
                settings.insetTop = t
                settings.insetRight = r
                settings.insetBottom = b
                settings.commit()

                // Update pending to keep UI in sync
                pendingInsetLeft = l
                pendingInsetTop = t
                pendingInsetRight = r
                pendingInsetBottom = b

                checkChanges()
                updateSettingsList()
                dialog.dismiss()

                // Refresh activity to apply padding immediately
                requireActivity().recreate()
            }
            .setNegativeButton(android.R.string.cancel) { dialog, _ ->
                // Revert Preview immediately
                val root = requireActivity().findViewById<View>(R.id.settings_nav_host)
                root?.setPadding(
                    settings.insetLeft, settings.insetTop,
                    settings.insetRight, settings.insetBottom
                )
                // Reset pending to old values
                pendingInsetLeft = settings.insetLeft
                pendingInsetTop = settings.insetTop
                pendingInsetRight = settings.insetRight
                pendingInsetBottom = settings.insetBottom

                dialog.dismiss()
            }
            .show()
    }

    override fun onResume() {
        super.onResume()
        // Refresh settings list when returning from sub-screens (e.g. AutoConnectFragment, DarkModeFragment)
        if (::settingsAdapter.isInitialized) {
            settings = App.provide(requireContext()).settings
            updateSettingsList()
        }
    }

    private fun getKillOnDisconnectConflicts(): List<String> {
        val conflicts = mutableListOf<String>()
        // Only reconnection-related settings conflict with close-on-disconnect.
        // Initial connection settings (auto-connect last session, single USB,
        // self mode, auto-start on USB) should keep working when the car starts.
        if (settings.reopenOnReconnection) {
            conflicts.add(getString(R.string.reopen_on_reconnection_label))
        }
        return conflicts
    }

    private fun showKillOnDisconnectWarning(conflicts: List<String>, hasAutoStartOnBoot: Boolean, hasAutoStartOnScreenOn: Boolean = false) {
        val message = buildString {
            if (conflicts.isNotEmpty()) {
                val conflictList = conflicts.joinToString("\n") { "• $it" }
                append(getString(R.string.kill_on_disconnect_warning, conflictList))
            }
            if (hasAutoStartOnBoot) {
                if (conflicts.isNotEmpty()) append("\n\n")
                append(getString(R.string.kill_on_disconnect_boot_warning))
            }
            if (hasAutoStartOnScreenOn) {
                if (conflicts.isNotEmpty() || hasAutoStartOnBoot) append("\n\n")
                append(getString(R.string.kill_on_disconnect_screen_on_warning))
            }
        }

        var confirmed = false

        val hasDisableableConflicts = conflicts.isNotEmpty()
        val positiveTextRes = if (hasDisableableConflicts) {
            R.string.kill_on_disconnect_disable_and_enable
        } else {
            R.string.kill_on_disconnect_enable_anyway
        }

        val dialog = MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(R.string.kill_on_disconnect_warning_title)
            .setMessage(message)
            .setPositiveButton(positiveTextRes) { _, _ ->
                confirmed = true
                if (hasDisableableConflicts) {
                    disableKillOnDisconnectConflicts()
                    Toast.makeText(context, getString(R.string.kill_on_disconnect_conflicts_disabled), Toast.LENGTH_LONG).show()
                }
                pendingKillOnDisconnect = true
                checkChanges()
                updateSettingsList()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .create()

        dialog.show()

        // Disable the positive button and show a countdown
        val positiveButton = dialog.getButton(android.app.AlertDialog.BUTTON_POSITIVE)
        positiveButton.isEnabled = false
        positiveButton.alpha = 0.4f
        val baseText = getString(positiveTextRes)
        val handler = android.os.Handler(android.os.Looper.getMainLooper())
        var remaining = 4

        val countdownRunnable = object : Runnable {
            override fun run() {
                if (remaining > 0) {
                    positiveButton.text = "$baseText (${remaining}s)"
                    remaining--
                    handler.postDelayed(this, 1000)
                } else {
                    positiveButton.text = baseText
                    positiveButton.isEnabled = true
                    positiveButton.alpha = 1.0f
                }
            }
        }
        handler.post(countdownRunnable)

        dialog.setOnDismissListener {
            handler.removeCallbacks(countdownRunnable)
            if (!confirmed) {
                pendingKillOnDisconnect = false
                checkChanges()
                updateSettingsList()
            }
        }
    }

    private fun disableKillOnDisconnectConflicts() {
        // Only disable reconnection-related settings.
        // Initial connection settings are kept so they work when the car starts.
        settings.reopenOnReconnection = false
    }

    private fun showHotspotPermissionDialog() {
        MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(R.string.hotspot_permission_title)
            .setMessage(R.string.hotspot_permission_message)
            .setPositiveButton(R.string.open_settings) { dialog, _ ->
                val intent = Intent(android.provider.Settings.ACTION_MANAGE_WRITE_SETTINGS).apply {
                    data = Uri.parse("package:${requireContext().packageName}")
                }
                startActivity(intent)
                dialog.dismiss()
            }
            .setNegativeButton(R.string.cancel) { _, _ ->
                pendingAutoEnableHotspot = false
                checkChanges()
                updateSettingsList()
            }
            .show()
    }

    private fun showHotspotExperimentalWarning() {
        MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(R.string.hotspot_warning_title)
            .setMessage(R.string.hotspot_warning_message)
            .setPositiveButton(android.R.string.ok) { dialog, _ ->
                pendingAutoEnableHotspot = true
                checkChanges()
                updateSettingsList()
                dialog.dismiss()
            }
            .setNegativeButton(android.R.string.cancel) { _, _ ->
                pendingAutoEnableHotspot = false
                checkChanges()
                updateSettingsList()
            }
            .show()
    }
    private fun addHotspotToggle(items: MutableList<SettingItem>) {
        items.add(SettingItem.ToggleSettingEntry(
            stableId = "autoEnableHotspot",
            nameResId = R.string.auto_enable_hotspot,
            descriptionResId = R.string.auto_enable_hotspot_description,
            isChecked = pendingAutoEnableHotspot ?: false,
            onCheckedChanged = { isChecked ->
                if (isChecked) {
                    if (Build.VERSION.SDK_INT >= 23 && !SystemSettings.System.canWrite(requireContext())) {
                        showPermissionDialog()
                    } else {
                        showExperimentalWarning()
                    }
                } else {
                    pendingAutoEnableHotspot = false
                    checkChanges()
                    updateSettingsList()
                }
            }
        ))
    }


    private fun getAutoConnectSummary(): String {
        val order = settings.autoConnectPriorityOrder
        val enabledNames = order.mapNotNull { id ->
            val isEnabled = when (id) {
                Settings.AUTO_CONNECT_LAST_SESSION -> settings.autoConnectLastSession
                Settings.AUTO_CONNECT_SELF_MODE -> settings.autoStartSelfMode
                Settings.AUTO_CONNECT_SINGLE_USB -> settings.autoConnectSingleUsbDevice
                else -> false
            }
            if (isEnabled) {
                when (id) {
                    Settings.AUTO_CONNECT_LAST_SESSION -> getString(R.string.auto_connect_last_session)
                    Settings.AUTO_CONNECT_SELF_MODE -> getString(R.string.auto_start_self_mode)
                    Settings.AUTO_CONNECT_SINGLE_USB -> getString(R.string.auto_connect_single_usb)
                    else -> null
                }
            } else null
        }
        return if (enabledNames.isEmpty()) {
            getString(R.string.auto_connect_all_disabled)
        } else {
            enabledNames.joinToString(" → ")
        }
    }

    private fun showNumericInputDialog(
        title: String,
        message: String?,
        initialValue: Int,
        onConfirm: (Int) -> Unit
    ) {
        val context = requireContext()
        val editView = EditText(context).apply {
            inputType = InputType.TYPE_CLASS_NUMBER
            setText(if (initialValue == 0 && title.contains("DPI", true)) "" else initialValue.toString())
        }

        // Use a container to add padding around the EditText
        val container = android.widget.FrameLayout(context)
        val params = android.widget.FrameLayout.LayoutParams(
            android.widget.FrameLayout.LayoutParams.MATCH_PARENT,
            android.widget.FrameLayout.LayoutParams.WRAP_CONTENT
        )
        val margin = (24 * context.resources.displayMetrics.density).toInt()
        params.setMargins(margin, 8, margin, 8)
        container.addView(editView, params)

        MaterialAlertDialogBuilder(context, R.style.DarkAlertDialog)
            .setTitle(title)
            .apply { if (message != null) setMessage(message) }
            .setView(container)
            .setPositiveButton(android.R.string.ok) { dialog, _ ->
                val newVal = (editView.text.toString().toIntOrNull() ?: 0).coerceAtLeast(0)
                onConfirm(newVal)
                dialog.dismiss()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    companion object {
        private val SAVE_ITEM_ID = 1001
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/AddNetworkAddressDialog.kt`:

```kt
package com.andrerinas.headunitrevived.main

import com.google.android.material.dialog.MaterialAlertDialogBuilder
import android.os.Build
import android.os.Bundle
import android.view.LayoutInflater
import android.widget.Button
import android.widget.EditText
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.FragmentManager
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.utils.AppLog
import java.net.InetAddress

class AddNetworkAddressDialog : DialogFragment() {

    override fun onCreateDialog(savedInstanceState: Bundle?): android.app.Dialog {
        val builder = MaterialAlertDialogBuilder(requireActivity(), R.style.DarkAlertDialog)
        val content = LayoutInflater.from(builder.context)
                .inflate(R.layout.fragment_add_network_address, null, false)

        val first = content.findViewById<EditText>(R.id.first)
        val second = content.findViewById<EditText>(R.id.second)
        val third = content.findViewById<EditText>(R.id.third)
        val fourth = content.findViewById<EditText>(R.id.fourth)
        val btnAdd = content.findViewById<Button>(R.id.btn_add)
        val btnCancel = content.findViewById<Button>(R.id.btn_cancel)

        val ip = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            arguments?.getSerializable("ip", InetAddress::class.java)
        } else {
            @Suppress("DEPRECATION")
            arguments?.getSerializable("ip") as? InetAddress
        }
        if (ip != null) {
            val addr = ip.address
            first.setText("${addr[0].toInt() and 0xFF}")
            second.setText("${addr[1].toInt() and 0xFF}")
            third.setText("${addr[2].toInt() and 0xFF}")
        }

        fourth.requestFocus()

        // Create the dialog without default buttons
        val dialog = builder.setView(content)
                .setTitle(R.string.enter_ip_address)
                .create()

        // Set listeners on our custom buttons
        btnAdd.setOnClickListener {
            val newAddr = ByteArray(4)
            try {
                newAddr[0] = strToByte(first.text.toString())
                newAddr[1] = strToByte(second.text.toString())
                newAddr[2] = strToByte(third.text.toString())
                newAddr[3] = strToByte(fourth.text.toString())

                val f = parentFragment as? NetworkListFragment
                f?.addAddress(InetAddress.getByAddress(newAddr))
                dialog.dismiss()
            } catch (e: java.net.UnknownHostException) {
                AppLog.e(e)
            } catch (e: NumberFormatException) {
                AppLog.e(e)
            }
        }

        btnCancel.setOnClickListener {
            dialog.cancel()
        }

        return dialog
    }

    companion object {

        fun show(ip: InetAddress?, manager: FragmentManager) {
            create(ip).show(manager, "AddNetworkAddressDialog")
        }

        fun create(ip: InetAddress?) = AddNetworkAddressDialog().apply {
            arguments = Bundle()
            if (ip != null) {
                arguments!!.putSerializable("ip", ip)
            }
        }

        fun strToByte(str: String): Byte {
            val i = Integer.valueOf(str)
            return i.toByte()
        }
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/main/WirelessConnectionFragment.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings as SystemSettings
import android.view.View
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.main.settings.SettingItem
import com.andrerinas.headunitrevived.main.settings.SettingsAdapter
import com.andrerinas.headunitrevived.utils.Settings
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.andrerinas.headunitrevived.connection.NativeAaHandshakeManager

class WirelessConnectionFragment : Fragment(R.layout.fragment_wireless_connection) {

    private lateinit var settings: Settings
    private lateinit var recyclerView: RecyclerView
    private lateinit var adapter: SettingsAdapter
    private lateinit var toolbar: MaterialToolbar

    private var pendingWifiConnectionMode: Int? = null
    private var pendingAutoEnableHotspot: Boolean? = null
    private var pendingWaitForWifi: Boolean? = null
    private var pendingWaitForWifiTimeout: Int? = null

    private var hasChanges = false
    private val SAVE_ITEM_ID = 1001

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        settings = App.provide(requireContext()).settings

        pendingWifiConnectionMode = settings.wifiConnectionMode
        pendingAutoEnableHotspot = settings.autoEnableHotspot
        pendingWaitForWifi = settings.waitForWifiBeforeWifiDirect
        pendingWaitForWifiTimeout = settings.waitForWifiTimeout

        toolbar = view.findViewById(R.id.toolbar)
        recyclerView = view.findViewById(R.id.settingsRecyclerView)
        adapter = SettingsAdapter()
        recyclerView.layoutManager = LinearLayoutManager(requireContext())
        recyclerView.adapter = adapter

        setupToolbar()
        updateSettingsList()

        requireActivity().onBackPressedDispatcher.addCallback(viewLifecycleOwner, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                handleBack()
            }
        })
    }

    private fun setupToolbar() {
        toolbar.setNavigationOnClickListener { handleBack() }
        updateSaveButtonState()
    }

    private fun updateSaveButtonState() {
        toolbar.menu.clear()
        if (hasChanges) {
            val saveItem = toolbar.menu.add(0, SAVE_ITEM_ID, 0, getString(R.string.save))
            saveItem.setShowAsAction(android.view.MenuItem.SHOW_AS_ACTION_ALWAYS)

            toolbar.setOnMenuItemClickListener { item ->
                if (item.itemId == SAVE_ITEM_ID) {
                    saveSettings()
                    true
                } else false
            }
        }
    }

    private fun updateSettingsList() {
        val items = mutableListOf<SettingItem>()
        val wifiModes = resources.getStringArray(R.array.wireless_connection_modes)

        // Add 2.4GHz Warning Banner at the top
        items.add(SettingItem.InfoBanner(
            stableId = "wireless24ghzWarning",
            textResId = R.string.wireless_24ghz_warning
        ))

        items.add(SettingItem.CategoryHeader("wireless_mode", R.string.wireless_mode))

        items.add(SettingItem.SettingEntry(
            stableId = "wifiConnectionMode",
            nameResId = R.string.wireless_mode,
            value = wifiModes.getOrElse(pendingWifiConnectionMode!!) { "" },
            onClick = { _ ->
                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                    .setTitle(R.string.wireless_mode)
                    .setSingleChoiceItems(wifiModes, pendingWifiConnectionMode!!) { dialog, which ->
                        dialog.dismiss()

                        if (which == 3) {
                            // Run the compatibility check for Native AA Mode
                            if (NativeAaHandshakeManager.checkCompatibility()) {
                                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                                    .setTitle(R.string.supported_nativeaa)
                                    .setMessage(R.string.supported_nativeaa_desc)
                                    .setPositiveButton(android.R.string.ok) { dialog2, _ ->
                                        pendingWifiConnectionMode = which
                                        checkChanges()
                                        updateSettingsList()
                                        dialog2.dismiss()
                                    }
                                    .setNegativeButton(android.R.string.cancel, null)
                                    .show()
                            } else {
                                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                                    .setTitle(R.string.not_supported_nativeaa)
                                    .setMessage(R.string.not_supported_nativeaa_desc)
                                    .setPositiveButton(android.R.string.ok) { dialog2, _ ->
                                        pendingWifiConnectionMode = which
                                        checkChanges()
                                        updateSettingsList()
                                        dialog2.dismiss()
                                    }
                                    .setNegativeButton(android.R.string.cancel, null)
                                    .show()
                            }
                        } else {
                            pendingWifiConnectionMode = which
                            checkChanges()
                            updateSettingsList()
                        }
                    }
                    .show()
            }
        ))

        if (pendingWifiConnectionMode == 1 || pendingWifiConnectionMode == 2) {
            items.add(SettingItem.ToggleSettingEntry(
                stableId = "autoEnableHotspot",
                nameResId = R.string.auto_enable_hotspot,
                descriptionResId = R.string.auto_enable_hotspot_description,
                isChecked = pendingAutoEnableHotspot ?: false,
                onCheckedChanged = { isChecked ->
                    if (isChecked) {
                        if (Build.VERSION.SDK_INT >= 23 && !SystemSettings.System.canWrite(requireContext())) {
                            showPermissionDialog()
                        } else {
                            showExperimentalWarning()
                        }
                    } else {
                        pendingAutoEnableHotspot = false
                        checkChanges()
                        updateSettingsList()
                    }
                }
            ))
        }

        if (pendingWifiConnectionMode == 2) {
            items.add(SettingItem.ToggleSettingEntry(
                stableId = "waitForWifi",
                nameResId = R.string.wait_for_wifi,
                descriptionResId = R.string.wait_for_wifi_description,
                isChecked = pendingWaitForWifi ?: false,
                onCheckedChanged = { isChecked ->
                    pendingWaitForWifi = isChecked
                    checkChanges()
                    updateSettingsList()
                }
            ))

            if (pendingWaitForWifi == true) {
                items.add(SettingItem.SliderSettingEntry(
                    stableId = "waitForWifiTimeout",
                    nameResId = R.string.wait_for_wifi_timeout,
                    value = "${pendingWaitForWifiTimeout}s",
                    sliderValue = (pendingWaitForWifiTimeout ?: 10).toFloat(),
                    valueFrom = 5f,
                    valueTo = 30f,
                    stepSize = 1f,
                    onValueChanged = { value ->
                        pendingWaitForWifiTimeout = value.toInt()
                        checkChanges()
                        updateSettingsList()
                    }
                ))
            }
        }

        // Add bottom save button
        if (hasChanges) {
            items.add(SettingItem.ActionButton(
                stableId = "bottomSaveButton",
                textResId = R.string.save,
                onClick = { saveSettings() }
            ))
        }

        adapter.submitList(items)
    }

    private fun showPermissionDialog() {
        MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(R.string.hotspot_permission_title)
            .setMessage(R.string.hotspot_permission_message)
            .setPositiveButton(R.string.open_settings) { dialog, _ ->
                val intent = Intent(SystemSettings.ACTION_MANAGE_WRITE_SETTINGS).apply {
                    data = Uri.parse("package:${requireContext().packageName}")
                }
                startActivity(intent)
                dialog.dismiss()
            }
            .setNegativeButton(R.string.cancel) { _, _ ->
                pendingAutoEnableHotspot = false
                checkChanges()
                updateSettingsList()
            }
            .show()
    }

    private fun showExperimentalWarning() {
        MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(R.string.hotspot_warning_title)
            .setMessage(R.string.hotspot_warning_message)
            .setPositiveButton(android.R.string.ok) { dialog, _ ->
                pendingAutoEnableHotspot = true
                checkChanges()
                updateSettingsList()
                dialog.dismiss()
            }
            .setNegativeButton(android.R.string.cancel) { _, _ ->
                pendingAutoEnableHotspot = false
                checkChanges()
                updateSettingsList()
            }
            .show()
    }

    private fun checkChanges() {
        val anyChange = pendingWifiConnectionMode != settings.wifiConnectionMode ||
                        pendingAutoEnableHotspot != settings.autoEnableHotspot ||
                        pendingWaitForWifi != settings.waitForWifiBeforeWifiDirect ||
                        pendingWaitForWifiTimeout != settings.waitForWifiTimeout

        if (hasChanges != anyChange) {
            hasChanges = anyChange
            updateSaveButtonState()
            updateSettingsList()
        }
    }

    private fun saveSettings() {
        val oldMode = settings.wifiConnectionMode
        settings.wifiConnectionMode = pendingWifiConnectionMode!!
        settings.autoEnableHotspot = pendingAutoEnableHotspot!!
        settings.waitForWifiBeforeWifiDirect = pendingWaitForWifi!!
        settings.waitForWifiTimeout = pendingWaitForWifiTimeout!!

        settings.commit()

        if (oldMode != settings.wifiConnectionMode) {
            val intent = Intent(requireContext(), AapService::class.java).apply {
                val mode = settings.wifiConnectionMode
                action = if (mode == 1 || mode == 2 || mode == 3)
                    AapService.ACTION_START_WIRELESS else AapService.ACTION_STOP_WIRELESS
            }
            requireContext().startService(intent)
        }

        hasChanges = false
        updateSaveButtonState()
        updateSettingsList()
        Toast.makeText(context, R.string.settings_saved, Toast.LENGTH_SHORT).show()
    }

    private fun handleBack() {
        if (hasChanges) {
            MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                .setTitle(R.string.unsaved_changes)
                .setMessage(R.string.unsaved_changes_message)
                .setPositiveButton(R.string.discard) { _, _ -> findNavController().popBackStack() }
                .setNegativeButton(R.string.cancel, null)
                .show()
        } else {
            findNavController().popBackStack()
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/UsbListFragment.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.content.Context
import android.content.Intent
import android.hardware.usb.UsbManager
import android.os.Bundle
import android.os.SystemClock
import android.text.Html
import android.view.LayoutInflater
import android.view.View
import android.view.View.GONE
import android.view.View.VISIBLE
import android.view.ViewGroup
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Observer
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.aap.AapProjectionActivity
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.connection.UsbAccessoryMode
import com.andrerinas.headunitrevived.connection.UsbDeviceCompat
import com.andrerinas.headunitrevived.connection.UsbReceiver
import com.andrerinas.headunitrevived.utils.Settings
import com.google.android.material.appbar.MaterialToolbar

class UsbListFragment : Fragment() {
    private lateinit var adapter: DeviceAdapter
    private lateinit var settings: Settings
    private lateinit var noUsbDeviceTextView: TextView
    private lateinit var recyclerView: RecyclerView
    private lateinit var toolbar: MaterialToolbar

    private val mainViewModel: MainViewModel by activityViewModels()

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        val view = inflater.inflate(R.layout.fragment_list, container, false)
        recyclerView = view.findViewById(android.R.id.list)
        noUsbDeviceTextView = view.findViewById(R.id.no_usb_device_text)
        toolbar = view.findViewById(R.id.toolbar)

        settings = Settings(requireContext())
        adapter = DeviceAdapter(requireContext(), settings)
        recyclerView.layoutManager = LinearLayoutManager(requireContext())
        recyclerView.adapter = adapter

        // Add padding
        val padding = resources.getDimensionPixelSize(R.dimen.list_padding)
        recyclerView.setPadding(padding, padding, padding, padding)
        recyclerView.clipToPadding = false

        return view
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        toolbar.title = getString(R.string.usb)
        toolbar.setNavigationOnClickListener {
            findNavController().popBackStack()
        }

        mainViewModel.usbDevices.observe(viewLifecycleOwner, Observer {
            val allowDevices = settings.allowedDevices
            adapter.setData(it, allowDevices)

            if (it.isEmpty()) {
                noUsbDeviceTextView.visibility = VISIBLE
                recyclerView.visibility = GONE
            } else {
                noUsbDeviceTextView.visibility = GONE
                recyclerView.visibility = VISIBLE
            }
        })
    }

    override fun onPause() {
        super.onPause()
        settings.commit()
    }

    private class DeviceViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        val allowButton = itemView.findViewById<Button>(android.R.id.button1)
        val startButton = itemView.findViewById<Button>(android.R.id.button2)
    }

    private class DeviceAdapter(private val mContext: Context, private val mSettings: Settings) : RecyclerView.Adapter<DeviceViewHolder>(), View.OnClickListener {
        private var allowedDevices: MutableSet<String> = mutableSetOf()
        private var deviceList: List<UsbDeviceCompat> = listOf()
        private var lastClickTime: Long = 0

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): DeviceViewHolder {
            val view = LayoutInflater.from(mContext).inflate(R.layout.list_item_device, parent, false)
            return DeviceViewHolder(view)
        }

        override fun onBindViewHolder(holder: DeviceViewHolder, position: Int) {
            val device = deviceList[position]

            // Background styling logic
            val isTop = position == 0
            val isBottom = position == itemCount - 1
            val bgRes = when {
                isTop && isBottom -> R.drawable.bg_setting_single
                isTop -> R.drawable.bg_setting_top
                isBottom -> R.drawable.bg_setting_bottom
                else -> R.drawable.bg_setting_middle
            }
            holder.itemView.setBackgroundResource(bgRes)

            holder.startButton.text = Html.fromHtml(String.format(
                    java.util.Locale.US, "<b>%1\$s</b><br/>%2\$s",
                    device.uniqueName, device.deviceName
            ))
            holder.startButton.tag = position
            holder.startButton.setOnClickListener(this)

            if (device.isInAccessoryMode) {
                holder.allowButton.setText(R.string.allowed)
                holder.allowButton.setTextColor(ContextCompat.getColor(mContext, R.color.material_green_700))
                holder.allowButton.isEnabled = false
            } else {
                if (allowedDevices.contains(device.uniqueName)) {
                    holder.allowButton.setText(R.string.allowed)
                    holder.allowButton.setTextColor(ContextCompat.getColor(mContext, R.color.material_green_700))
                } else {
                    holder.allowButton.setText(R.string.ignored)
                    holder.allowButton.setTextColor(ContextCompat.getColor(mContext, R.color.material_orange_700))
                }
                holder.allowButton.tag = position
                holder.allowButton.isEnabled = true
                holder.allowButton.setOnClickListener(this)
            }
        }

        override fun getItemCount(): Int {
            return deviceList.size
        }

        override fun onClick(v: View) {
            // Debounce clicks (prevent double tap)
            if (SystemClock.elapsedRealtime() - lastClickTime < 1000) {
                return
            }
            lastClickTime = SystemClock.elapsedRealtime()

            val device = deviceList.get(v.tag as Int)
            if (v.id == android.R.id.button1) {
                if (allowedDevices.contains(device.uniqueName)) {
                    allowedDevices.remove(device.uniqueName)
                } else {
                    allowedDevices.add(device.uniqueName)
                }
                mSettings.allowedDevices = allowedDevices
                notifyDataSetChanged()
            } else {
                if (App.provide(mContext).commManager.isConnected) {

                    // Already connected -> bring existing projection to front
                    val aapIntent = AapProjectionActivity.intent(mContext).apply {
                        putExtra(AapProjectionActivity.EXTRA_FOCUS, true)
                        addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
                    }
                    mContext.startActivity(aapIntent)
                } else if (device.isInAccessoryMode) {
                    // Device is in Accessory Mode but we are NOT connected.
                    // Start connection immediately.
                    Toast.makeText(mContext, R.string.android_auto_starting, Toast.LENGTH_SHORT).show()
                    ContextCompat.startForegroundService(mContext, Intent(mContext, AapService::class.java).apply {
                        action = AapService.ACTION_CHECK_USB
                    })
                } else {
                    // Standard connection flow
                    val usbManager = mContext.getSystemService(Context.USB_SERVICE) as UsbManager
                    if (usbManager.hasPermission(device.wrappedDevice)) {
                        val usbMode = UsbAccessoryMode(usbManager)
                        if (usbMode.connectAndSwitch(device.wrappedDevice)) {
                            Toast.makeText(mContext, R.string.switching_to_android_auto, Toast.LENGTH_SHORT).show()
                        } else {
                            Toast.makeText(mContext, R.string.switch_failed, Toast.LENGTH_SHORT).show()
                        }
                        notifyDataSetChanged()
                    } else {
                        Toast.makeText(mContext, R.string.requesting_usb_permission, Toast.LENGTH_SHORT).show()
                        ContextCompat.startForegroundService(mContext, Intent(mContext, AapService::class.java))
                        usbManager.requestPermission(
                            device.wrappedDevice,
                            UsbReceiver.createPermissionPendingIntent(mContext)
                        )
                    }
                }
            }
        }

        fun setData(deviceList: List<UsbDeviceCompat>, allowedDevices: Set<String>) {
            this.allowedDevices = allowedDevices.toMutableSet()
            this.deviceList = deviceList
            notifyDataSetChanged()
        }
    }

}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/MainActivity.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.KeyEvent
import android.view.View
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.aap.AapProjectionActivity
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.app.BaseActivity
import androidx.lifecycle.lifecycleScope
import com.andrerinas.headunitrevived.utils.AppLog
import android.content.res.Configuration
import com.andrerinas.headunitrevived.utils.Settings
import android.os.SystemClock
import com.andrerinas.headunitrevived.utils.SetupWizard
import com.andrerinas.headunitrevived.utils.SystemUI
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

class MainActivity : BaseActivity() {

    private var lastBackPressTime: Long = 0
    var keyListener: KeyListener? = null

    private val viewModel: MainViewModel by viewModels()

    private val finishReceiver = object : android.content.BroadcastReceiver() {
        override fun onReceive(context: android.content.Context, intent: Intent) {
            if (intent.action == "com.andrerinas.headunitrevived.ACTION_FINISH_ACTIVITIES") {
                AppLog.i("MainActivity: Received finish request. Closing.")
                finishAffinity()
            }
        }
    }

    interface KeyListener {
        fun onKeyEvent(event: KeyEvent?): Boolean
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        logLaunchSource()

        // If an Android Auto session is active, bring the projection activity to front
        if (App.provide(this).commManager.isConnected) {
            AppLog.i("MainActivity: Active session detected in onCreate, bringing projection to front")
            val aapIntent = AapProjectionActivity.intent(this).apply {
                putExtra(AapProjectionActivity.EXTRA_FOCUS, true)
                addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT or Intent.FLAG_ACTIVITY_SINGLE_TOP)
            }
            startActivity(aapIntent)

            // If we are auto-forwarding, hide the splash immediately to avoid flashing it twice
            if (savedInstanceState == null) {
                findViewById<View>(R.id.splash_overlay)?.visibility = View.GONE
            }
        }

        setTheme(R.style.AppTheme)
        val mainSettings = Settings(this)
        val isNightActive = (resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES
        if (mainSettings.appTheme == Settings.AppTheme.EXTREME_DARK ||
            (mainSettings.useExtremeDarkMode && isNightActive)) {
            theme.applyStyle(R.style.ThemeOverlay_ExtremeDark, true)
        } else if (mainSettings.useGradientBackground) {
            theme.applyStyle(R.style.ThemeOverlay_GradientBackground, true)
        }
        enableEdgeToEdge()
        setContentView(R.layout.activity_main)

        val appSettings = Settings(this)
        requestedOrientation = appSettings.screenOrientation.androidOrientation

        // Sync UsbAttachedActivity component state with the listen for USB devices setting.
        // This covers first install, app updates (manifest may reset component state),
        // and ensures the USB system modal only appears when the user has opted in to listen for ALL USB devices.
        lifecycleScope.launch(Dispatchers.IO) {
            Settings.setUsbAttachedActivityEnabled(applicationContext, appSettings.listenForUsbDevices)
        }

        // Start main service immediately to handle connections and wireless server
        val serviceIntent = Intent(this, AapService::class.java)
        ContextCompat.startForegroundService(this, serviceIntent)

        setFullscreen()

        val navHostFragment = supportFragmentManager.findFragmentById(R.id.main_content) as androidx.navigation.fragment.NavHostFragment
        val navController = navHostFragment.navController

        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                if (navController.navigateUp()) {
                    return
                } else if (System.currentTimeMillis() - lastBackPressTime < 2000) {
                    finish()
                } else {
                    lastBackPressTime = System.currentTimeMillis()
                    Toast.makeText(this@MainActivity, R.string.press_back_again_to_exit, Toast.LENGTH_SHORT).show()
                }
            }
        })

        if (savedInstanceState == null) {
            val elapsedSinceStart = SystemClock.elapsedRealtime() - App.appStartTime
            val targetTotalDuration = 1200L
            val actualDelay = (targetTotalDuration - elapsedSinceStart).coerceAtLeast(0L)

            showSplashWithDelay(actualDelay)
        } else {
            findViewById<View>(R.id.splash_overlay)?.visibility = View.GONE
        }

        requestPermissions()
        viewModel.register()
        handleLaunchIntent(intent)
        setupWifiDirectInfo()

        ContextCompat.registerReceiver(
            this, finishReceiver,
            android.content.IntentFilter("com.andrerinas.headunitrevived.ACTION_FINISH_ACTIVITIES"),
            ContextCompat.RECEIVER_NOT_EXPORTED
        )
    }

    private fun showSplashWithDelay(delayMs: Long) {
        val overlay = findViewById<View>(R.id.splash_overlay) ?: return
        lifecycleScope.launch(Dispatchers.Main) {
            if (delayMs > 0) {
                delay(delayMs)
            }
            overlay.animate()
                .alpha(0f)
                .setDuration(300)
                .withEndAction {
                    overlay.visibility = View.GONE
                }
                .start()
        }
    }

    private fun setupWifiDirectInfo() {
        val tvInfo = findViewById<android.widget.TextView>(R.id.wifi_direct_info)
        val settings = Settings(this)

        lifecycleScope.launch {
            AapService.wifiDirectName.collectLatest { name ->
                val isHelperMode = settings.wifiConnectionMode == 2
                if (isHelperMode && name != null) {
                    tvInfo.text = "WiFi Direct: $name"
                    tvInfo.visibility = View.VISIBLE
                } else {
                    tvInfo.visibility = View.GONE
                }
            }
        }
    }

    override fun onStart() {
        super.onStart()
    }

    override fun onStop() {
        super.onStop()
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleLaunchIntent(intent)
    }

    private fun logLaunchSource() {
        val source = intent?.getStringExtra(EXTRA_LAUNCH_SOURCE)
        if (source != null) {
            AppLog.i("App launched via: $source")
            return
        }

        val referrer = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP_MR1) {
            referrer?.toString()
        } else null

        val isLauncherTap = intent?.action == Intent.ACTION_MAIN &&
                intent.hasCategory(Intent.CATEGORY_LAUNCHER)

        if (isLauncherTap) {
            AppLog.i("App launched by user tap (referrer: ${referrer ?: "none"})")
        } else if (referrer != null) {
            AppLog.i("App launched by third party: $referrer (action: ${intent?.action})")
        } else {
            AppLog.i("App launched, source unknown (action: ${intent?.action})")
        }
    }

    private fun handleLaunchIntent(intent: Intent?) {
        if (intent == null) return

        AppLog.i("MainActivity: Processing launch intent: ${intent.action}, data: ${intent.data}")

        val intentData = intent.data
        val intentAction = intent.action

        if (intentAction == "com.andrerinas.headunitrevived.ACTION_EXIT") {
            AppLog.i("MainActivity: Received exit action")
            val exitIntent = Intent(this, AapService::class.java).apply {
                this.action = AapService.ACTION_STOP_SERVICE
            }
            ContextCompat.startForegroundService(this, exitIntent)
            finishAffinity()
            return
        }

        if (intentAction == AapService.ACTION_START_SELF_MODE ||
           (intentData?.scheme == "headunit" && intentData.host == "selfmode")) {
            AppLog.i("MainActivity: Forced self-mode start requested")
            HomeFragment.forceSelfModeLaunch = true
            val selfModeIntent = Intent(this, AapService::class.java).apply {
                this.action = AapService.ACTION_START_SELF_MODE
            }
            ContextCompat.startForegroundService(this, selfModeIntent)
        }

        if (intent.action == Intent.ACTION_VIEW) {
            if (intentData?.scheme == "headunit" && intentData.host == "connect") {
                val ip = intentData.getQueryParameter("ip")
                if (!ip.isNullOrEmpty()) {
                    AppLog.i("Received connect intent for IP: $ip")
                    ContextCompat.startForegroundService(this, Intent(this, AapService::class.java).apply {
                        action = AapService.ACTION_CONNECT_SOCKET
                    })
                    lifecycleScope.launch(Dispatchers.IO) { App.provide(this@MainActivity).commManager.connect(ip, 5277) }
                } else {
                    AppLog.i("Received connect intent without IP -> triggering last session auto-connect")
                    val autoIntent = Intent(this, AapService::class.java).apply {
                        action = AapService.ACTION_CHECK_USB
                    }
                    ContextCompat.startForegroundService(this, autoIntent)
                }
            } else if (intentData?.scheme == "headunit" && intentData.host == "disconnect") {
                AppLog.i("Received disconnect intent")
                val stopIntent = Intent(this, AapService::class.java).apply {
                    action = AapService.ACTION_DISCONNECT
                }
                ContextCompat.startForegroundService(this, stopIntent)
            } else if (intentData?.scheme == "headunit" && intentData.host == "exit") {
                AppLog.i("Received full exit intent via deep link")
                val exitIntent = Intent(this, AapService::class.java).apply {
                    action = AapService.ACTION_STOP_SERVICE
                }
                ContextCompat.startForegroundService(this, exitIntent)
                finishAffinity()
            }
        }
    }

    private fun requestPermissions() {
        val requiredPermissions = mutableListOf(
            Manifest.permission.RECORD_AUDIO,
            Manifest.permission.ACCESS_FINE_LOCATION,
            Manifest.permission.ACCESS_COARSE_LOCATION
        )

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            requiredPermissions.add(Manifest.permission.BLUETOOTH_ADVERTISE)
            requiredPermissions.add(Manifest.permission.BLUETOOTH_CONNECT)
            requiredPermissions.add(Manifest.permission.BLUETOOTH_SCAN)
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            requiredPermissions.add(Manifest.permission.NEARBY_WIFI_DEVICES)
            requiredPermissions.add(Manifest.permission.POST_NOTIFICATIONS)
        }

        // Filter out permissions that are already granted
        val permissionsToRequest = requiredPermissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }

        if (permissionsToRequest.isNotEmpty()) {
            AppLog.i("Requesting missing permissions: $permissionsToRequest")
            ActivityCompat.requestPermissions(
                this,
                permissionsToRequest.toTypedArray(),
                permissionRequestCode
            )
        } else {
            AppLog.d("All required permissions already granted.")
        }
    }

    private fun setFullscreen() {
        val root = findViewById<View>(R.id.root)
        val appSettings = Settings(this)
        SystemUI.apply(window, root, appSettings.fullscreenMode)
    }

    override fun onResume() {
        super.onResume()
        setFullscreen()

        checkSetupFlow()

        // If an Android Auto session is active, bring the projection activity to front
        if (App.provide(this).commManager.isConnected) {
            AppLog.i("MainActivity: Active session detected, bringing projection to front")
            val aapIntent = AapProjectionActivity.intent(this).apply {
                putExtra(AapProjectionActivity.EXTRA_FOCUS, true)
                addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT or Intent.FLAG_ACTIVITY_SINGLE_TOP)
            }
            startActivity(aapIntent)
        }
    }

    fun checkSetupFlow() {
        val appSettings = Settings(this)
        if (!appSettings.hasAcceptedDisclaimer) {
            SafetyDisclaimerDialog.show(supportFragmentManager)
        } else if (!appSettings.hasCompletedSetupWizard) {
            SetupWizard(this) {
                // Refresh activity after setup
                recreate()
            }.start()
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            setFullscreen()
        }
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        AppLog.i("dispatchKeyEvent: keyCode=%d, action=%d", event.keyCode, event.action)

        // Always give the KeymapFragment (if active) a chance to see the key
        val handled = keyListener?.onKeyEvent(event) ?: false

        // If the key was handled by our listener (e.g. in KeymapFragment), stop here
        if (handled) return true

        // Otherwise continue with standard handling
        return super.dispatchKeyEvent(event)
    }

    override fun onDestroy() {
        super.onDestroy()
        try { unregisterReceiver(finishReceiver) } catch (e: Exception) {}
        if (isFinishing) {
            AppLog.i("MainActivity finishing, resetting auto-start flag.")
            HomeFragment.resetAutoStart()
        }
    }

    companion object {
        private const val permissionRequestCode = 97
        const val EXTRA_LAUNCH_SOURCE = "launch_source"
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/HomeFragment.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.content.Context
import android.content.Intent
import android.hardware.usb.UsbManager
import android.os.Bundle
import android.graphics.Color
import android.content.res.ColorStateList
import android.widget.*
import android.view.View
import android.view.ViewGroup
import android.view.LayoutInflater
import android.net.VpnService
import androidx.activity.result.contract.ActivityResultContracts
import android.net.ConnectivityManager
import android.os.Build
import androidx.fragment.app.Fragment
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.navigation.fragment.findNavController
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.aap.AapProjectionActivity
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.connection.NearbyManager
import com.andrerinas.headunitrevived.connection.UsbDeviceCompat
import android.content.res.Configuration
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import com.andrerinas.headunitrevived.utils.AppLog
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.collect
import com.andrerinas.headunitrevived.utils.Settings
import com.andrerinas.headunitrevived.utils.VpnControl

class HomeFragment : Fragment() {

    private val commManager get() = App.provide(requireContext()).commManager

    private val vpnPermissionLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == android.app.Activity.RESULT_OK) {
            AppLog.i("VPN permission granted. Starting DummyVpnService and Self Mode.")
            VpnControl.startVpn(requireContext());
            startSelfModeInternal()
        } else {
            AppLog.w("VPN permission denied. Offline Self Mode might fail.")
            Toast.makeText(requireContext(), getString(R.string.failed_start_android_auto), Toast.LENGTH_LONG).show()
        }
    }

    private lateinit var self_mode_button: Button
    private lateinit var usb: Button
    private lateinit var settings: Button
    private lateinit var wifi: Button
    private lateinit var wifi_text_view: TextView
    private lateinit var exitButton: Button
    private lateinit var self_mode_text: TextView
    private var hasAttemptedAutoConnect = false
    private var hasAttemptedSingleUsbAutoConnect = false
    private var activeDialog: androidx.appcompat.app.AlertDialog? = null

    private fun updateWifiButtonFeedback(scanning: Boolean) {
        if (scanning) {
            wifi_text_view.text = getString(R.string.searching)
            wifi.alpha = 0.6f
        } else {
            wifi_text_view.text = getString(R.string.wifi)
            wifi.alpha = 1.0f
        }
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View? {
        return inflater.inflate(R.layout.fragment_home, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        self_mode_button = view.findViewById(R.id.self_mode_button)
        usb = view.findViewById(R.id.usb_button)
        settings = view.findViewById(R.id.settings_button)
        wifi = view.findViewById(R.id.wifi_button)
        wifi_text_view = view.findViewById(R.id.wifi_text)
        exitButton = view.findViewById(R.id.exit_button)
        self_mode_text = view.findViewById(R.id.self_mode_text)

        setupListeners()
        updateProjectionButtonText()

        viewLifecycleOwner.lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                commManager.connectionState.collect { updateProjectionButtonText() }
            }
        }

        viewLifecycleOwner.lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                AapService.scanningState.collect { updateWifiButtonFeedback(it) }
            }
        }

        val appSettings = App.provide(requireContext()).settings

        if (appSettings.autoStartOnScreenOn || appSettings.autoStartOnBoot) {
            ContextCompat.startForegroundService(requireContext(),
                Intent(requireContext(), AapService::class.java))
        }

        for (methodId in appSettings.autoConnectPriorityOrder) {
            if (commManager.isConnected) break
            when (methodId) {
                Settings.AUTO_CONNECT_LAST_SESSION -> {
                    if (appSettings.autoConnectLastSession && !hasAttemptedAutoConnect && !commManager.isConnected) {
                        hasAttemptedAutoConnect = true
                        attemptAutoConnect()
                    }
                }
                Settings.AUTO_CONNECT_SELF_MODE -> {
                    if ((appSettings.autoStartSelfMode || forceSelfModeLaunch) && !hasAutoStarted && !commManager.isConnected) {
                        hasAutoStarted = true
                        forceSelfModeLaunch = false // Reset once processed
                        startSelfMode()
                    }
                }
                Settings.AUTO_CONNECT_SINGLE_USB -> {
                    if (appSettings.autoConnectSingleUsbDevice && !hasAttemptedSingleUsbAutoConnect && !commManager.isConnected) {
                        hasAttemptedSingleUsbAutoConnect = true
                        attemptSingleUsbAutoConnect()
                    }
                }
            }
        }
    }

    private fun startSelfModeInternal() {
        AapService.selfMode = true
        val intent = Intent(requireContext(), AapService::class.java)
        intent.action = AapService.ACTION_START_SELF_MODE
        ContextCompat.startForegroundService(requireContext(), intent)
        AppLog.i("Auto start selfmode")
    }

    private fun startSelfMode() {
        val connectivityManager = requireContext().getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val activeNetwork = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            connectivityManager.activeNetwork
        } else null

        if (activeNetwork == null && VpnControl.isVpnAvailable()) {
            AppLog.i("Device is offline. Preparing Dummy VPN for Self Mode.")
            val vpnIntent = VpnService.prepare(requireContext())
            if (vpnIntent != null) {
                vpnPermissionLauncher.launch(vpnIntent)
                return
            } else {
                AppLog.i("VPN permission already granted. Starting VPN service.")
                VpnControl.startVpn(requireContext());
            }
        } else if (activeNetwork == null) {
            AppLog.i("Device is offline and VPN is not available in this build. Self Mode may fail.")
        }
        startSelfModeInternal()
    }

    private fun attemptAutoConnect() {
        val appSettings = App.provide(requireContext()).settings

        if (!appSettings.autoConnectLastSession ||
            !appSettings.hasAcceptedDisclaimer ||
            commManager.isConnected) {
            return
        }

        val connectionType = appSettings.lastConnectionType
        if (connectionType.isEmpty()) {
            AppLog.i("Auto-connect: No last session to reconnect to")
            return
        }

        when (connectionType) {
            Settings.CONNECTION_TYPE_WIFI -> {
                val ip = appSettings.lastConnectionIp
                if (ip.isNotEmpty()) {
                    AppLog.i("Auto-connect: Attempting WiFi connection to $ip")
                    Toast.makeText(requireContext(), getString(R.string.auto_connecting_to, ip), Toast.LENGTH_SHORT).show()
                    val ctx = requireContext()
                    lifecycleScope.launch(Dispatchers.IO) { App.provide(ctx).commManager.connect(ip, 5277) }
                    ContextCompat.startForegroundService(requireContext(), Intent(requireContext(), AapService::class.java).apply {
                        action = AapService.ACTION_CONNECT_SOCKET
                    })
                }
            }
            Settings.CONNECTION_TYPE_USB -> {
                val lastUsbDevice = appSettings.lastConnectionUsbDevice
                if (lastUsbDevice.isNotEmpty()) {
                    val usbManager = requireContext().getSystemService(Context.USB_SERVICE) as UsbManager
                    val matchingDevice = usbManager.deviceList.values.find { device ->
                        UsbDeviceCompat.getUniqueName(device) == lastUsbDevice
                    }
                    if (matchingDevice != null && usbManager.hasPermission(matchingDevice)) {
                        AppLog.i("Auto-connect: Attempting USB connection to $lastUsbDevice")
                        Toast.makeText(requireContext(), getString(R.string.auto_connecting_usb), Toast.LENGTH_SHORT).show()
                        ContextCompat.startForegroundService(requireContext(), Intent(requireContext(), AapService::class.java).apply {
                            action = AapService.ACTION_CHECK_USB
                        })
                    } else {
                        AppLog.i("Auto-connect: USB device $lastUsbDevice not found or no permission")
                    }
                }
            }
            Settings.CONNECTION_TYPE_NEARBY -> {
                AppLog.i("Auto-connect: Last session was via Google Nearby. AapService will handle discovery.")
                // No manual connect(ip) needed, NearbyManager in AapService manages this automatically on start.
            }
        }
    }

    private fun attemptSingleUsbAutoConnect() {
        val appSettings = App.provide(requireContext()).settings
        if (!appSettings.autoConnectSingleUsbDevice ||
            !appSettings.hasAcceptedDisclaimer ||
            commManager.isConnected) return

        AppLog.i("HomeFragment: Requesting single-USB auto-connect via AapService")
        ContextCompat.startForegroundService(requireContext(),
            Intent(requireContext(), AapService::class.java).apply {
                action = AapService.ACTION_CHECK_USB
            })
    }

    private val originalBackgrounds = mapOf(
        R.id.self_mode_button to R.drawable.gradient_blue,
        R.id.usb_button to R.drawable.gradient_orange,
        R.id.wifi_button to R.drawable.gradient_purple,
        R.id.settings_button to R.drawable.gradient_darkblue
    )

    private fun applyMonochromeStyle() {
        val monochromeBackground = ContextCompat.getDrawable(requireContext(), R.drawable.gradient_monochrome)
        val grayTint = ColorStateList.valueOf(0xFF808080.toInt())
        listOf(self_mode_button, usb, wifi, settings).forEach { button ->
            button.background = monochromeBackground?.constantState?.newDrawable()?.mutate()
            (button as? com.google.android.material.button.MaterialButton)?.iconTint = grayTint
        }
    }

    private fun restoreOriginalStyle() {
        val whiteTint = ColorStateList.valueOf(0xFFFFFFFF.toInt())
        val buttons = listOf(self_mode_button, usb, wifi, settings)
        val ids = listOf(R.id.self_mode_button, R.id.usb_button, R.id.wifi_button, R.id.settings_button)
        buttons.zip(ids).forEach { (button, id) ->
            originalBackgrounds[id]?.let { drawableRes ->
                button.background = ContextCompat.getDrawable(requireContext(), drawableRes)
            }
            (button as? com.google.android.material.button.MaterialButton)?.iconTint = whiteTint
        }
    }

    private fun updateButtonStyle() {
        val appSettings = App.provide(requireContext()).settings
        val isNightActive = (resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES
        val isDarkTheme = appSettings.appTheme == Settings.AppTheme.DARK ||
                          appSettings.appTheme == Settings.AppTheme.EXTREME_DARK ||
                          isNightActive
        if (isDarkTheme && appSettings.monochromeIcons) {
            applyMonochromeStyle()
        } else {
            restoreOriginalStyle()
        }
    }

    private fun setupListeners() {
        exitButton.setOnClickListener {
            val appSettings = App.provide(requireContext()).settings
            val keepServiceAlive = appSettings.autoStartOnBoot ||
                appSettings.autoStartOnScreenOn ||
                (appSettings.autoStartOnUsb && appSettings.reopenOnReconnection)
            if (keepServiceAlive) {
                val disconnectIntent = Intent(requireContext(), AapService::class.java).apply {
                    action = AapService.ACTION_DISCONNECT
                }
                ContextCompat.startForegroundService(requireContext(), disconnectIntent)
            } else {
                val stopServiceIntent = Intent(requireContext(), AapService::class.java).apply {
                    action = AapService.ACTION_STOP_SERVICE
                }
                ContextCompat.startForegroundService(requireContext(), stopServiceIntent)
            }
            requireActivity().finishAffinity()
        }

        self_mode_button.setOnClickListener {
            if (commManager.isConnected) {
                val aapIntent = Intent(requireContext(), AapProjectionActivity::class.java)
                aapIntent.putExtra(AapProjectionActivity.EXTRA_FOCUS, true)
                startActivity(aapIntent)
            } else {
                startSelfMode()
            }
        }

        usb.setOnClickListener {
            val controller = findNavController()
            if (controller.currentDestination?.id == R.id.homeFragment) {
                controller.navigate(R.id.action_homeFragment_to_usbListFragment)
            }
        }

        settings.setOnClickListener {
            val intent = Intent(requireContext(), SettingsActivity::class.java)
            startActivity(intent)
        }

        wifi.setOnClickListener {
            val mode = App.provide(requireContext()).settings.wifiConnectionMode
            when (mode) {
                1 -> { // Auto (Headunit Server) - One-Shot Scan
                    if (commManager.isConnected) {
                        // Already connected
                    } else if (AapService.scanningState.value) {
                        Toast.makeText(requireContext(), getString(R.string.already_scanning), Toast.LENGTH_SHORT).show()
                    } else {
                        Toast.makeText(requireContext(), getString(R.string.searching_headunit_server), Toast.LENGTH_SHORT).show()
                        val intent = Intent(requireContext(), AapService::class.java).apply {
                            action = AapService.ACTION_START_WIRELESS_SCAN
                        }
                        ContextCompat.startForegroundService(requireContext(), intent)
                    }
                }
                2 -> { // Helper (Wireless Launcher)
                    if (commManager.isConnected) {
                        // Already connected
                    } else {
                        val strategy = App.provide(requireContext()).settings.helperConnectionStrategy
                        if (strategy == 2) {
                            // Nearby Devices — show live discovery dialog
                            showNearbyDeviceSelector()
                        } else if (AapService.scanningState.value) {
                            Toast.makeText(requireContext(), getString(R.string.already_searching_phone), Toast.LENGTH_SHORT).show()
                        } else {
                            Toast.makeText(requireContext(), getString(R.string.searching_phone), Toast.LENGTH_SHORT).show()
                            val intent = Intent(requireContext(), AapService::class.java).apply {
                                action = AapService.ACTION_START_WIRELESS_SCAN
                            }
                            ContextCompat.startForegroundService(requireContext(), intent)
                        }
                    }
                }
                3 -> { // Native AA
                    showNativeAaDeviceSelector()
                }
                else -> { // Manual (0) -> Open List
                    val controller = findNavController()
                    if (controller.currentDestination?.id == R.id.homeFragment) {
                        controller.navigate(R.id.action_homeFragment_to_networkListFragment)
                    }
                }
            }
        }

        wifi.setOnLongClickListener {
            val controller = findNavController()
            if (controller.currentDestination?.id == R.id.homeFragment) {
                controller.navigate(R.id.action_homeFragment_to_networkListFragment)
            }
            true
        }
    }

    private fun updateProjectionButtonText() {
        if (commManager.isConnected) {
            self_mode_text.text = getString(R.string.to_android_auto)
        } else {
            self_mode_text.text = getString(R.string.self_mode)
        }
    }

    override fun onResume() {
        super.onResume()
        AppLog.i("HomeFragment: onResume. isConnected=${commManager.isConnected}")
        updateProjectionButtonText()
        updateButtonStyle()
        updateTextColors()
    }

    override fun onPause() {
        super.onPause()
        activeDialog?.dismiss()
        activeDialog = null
    }

    private fun showNativeAaDeviceSelector() {
        val adapter = if (Build.VERSION.SDK_INT >= 18) {
            (requireContext().getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
        } else {
            @Suppress("DEPRECATION")
            BluetoothAdapter.getDefaultAdapter()
        }

        if (adapter == null || !adapter.isEnabled) {
            Toast.makeText(requireContext(), getString(R.string.bt_not_enabled), Toast.LENGTH_SHORT).show()
            return
        }

        val bondedDevices = adapter.bondedDevices?.toList() ?: emptyList()
        if (bondedDevices.isEmpty()) {
            Toast.makeText(requireContext(), "No paired Bluetooth devices found", Toast.LENGTH_SHORT).show()
            return
        }

        val deviceNames = bondedDevices.map { it.name ?: "Unknown Device" }.toTypedArray()


        activeDialog = MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(R.string.select_bt_device)
            .setItems(deviceNames) { _, which ->
                val device = bondedDevices[which]
                AppLog.i("HomeFragment: Manually selected ${device.name} for Native-AA poke")

                val intent = Intent(requireContext(), AapService::class.java).apply {
                    action = AapService.ACTION_NATIVE_AA_POKE
                    putExtra(AapService.EXTRA_MAC, device.address)
                }
                ContextCompat.startForegroundService(requireContext(), intent)
                Toast.makeText(requireContext(), "Searching for ${device.name}...", Toast.LENGTH_SHORT).show()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showNearbyDeviceSelector() {
        // Ensure NearbyManager discovery is running via AapService
        ContextCompat.startForegroundService(requireContext(),
            Intent(requireContext(), AapService::class.java).apply {
                action = AapService.ACTION_START_WIRELESS_SCAN
            })

        val dialogView = LayoutInflater.from(requireContext()).inflate(R.layout.dialog_nearby_selection, null)
        val listContainer = dialogView.findViewById<View>(R.id.listContainer)
        val deviceListView = dialogView.findViewById<ListView>(R.id.deviceList)
        val searchingText = dialogView.findViewById<TextView>(R.id.searchingText)
        val connectingContainer = dialogView.findViewById<View>(R.id.connectingContainer)
        val connectingText = dialogView.findViewById<TextView>(R.id.connectingText)
        val connectionProgress = dialogView.findViewById<ProgressBar>(R.id.connectionProgress)

        // Ensure the loading spinner is visible in both Light and Dark modes by forcing our brand color.
        val brandTeal = ContextCompat.getColor(requireContext(), R.color.brand_teal)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            connectionProgress.indeterminateTintList = ColorStateList.valueOf(brandTeal)
            connectionProgress.indeterminateTintMode = android.graphics.PorterDuff.Mode.SRC_IN
        } else {
            @Suppress("DEPRECATION")
            connectionProgress.indeterminateDrawable?.setColorFilter(brandTeal, android.graphics.PorterDuff.Mode.SRC_IN)
        }

        // Custom adapter to handle rounded backgrounds like in USB/Network lists
        val listAdapter = object : ArrayAdapter<NearbyManager.DiscoveredEndpoint>(requireContext(), R.layout.list_item_nearby) {
            override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
                val view = convertView ?: LayoutInflater.from(context).inflate(R.layout.list_item_nearby, parent, false)
                val endpoint = getItem(position)
                view.findViewById<TextView>(R.id.deviceName).text = endpoint?.name ?: "Unknown"

                // Apply rounded backgrounds based on position
                val isTop = position == 0
                val isBottom = position == count - 1
                val bgRes = when {
                    isTop && isBottom -> R.drawable.bg_setting_single
                    isTop -> R.drawable.bg_setting_top
                    isBottom -> R.drawable.bg_setting_bottom
                    else -> R.drawable.bg_setting_middle
                }
                view.setBackgroundResource(bgRes)
                return view
            }
        }
        deviceListView.adapter = listAdapter

        var collectJob: Job? = null

        val dialog = MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(getString(R.string.searching)) // Initial title
            .setView(dialogView)
            .setNegativeButton(R.string.cancel, null)
            .setOnDismissListener {
                collectJob?.cancel()
                if (activeDialog == it) activeDialog = null
            }
            .create()

        activeDialog = dialog

        deviceListView.setOnItemClickListener { _, _, which, _ ->
            val endpoints = NearbyManager.discoveredEndpoints.value
            if (which < endpoints.size) {
                val endpoint = endpoints[which]
                AppLog.i("HomeFragment: Selected Nearby device: ${endpoint.name} (${endpoint.id})")

                // UI Switch: Hide list, show connecting spinner
                listContainer.visibility = View.GONE
                connectingContainer.visibility = View.VISIBLE
                connectingText.text = getString(R.string.connecting_to_nearby, endpoint.name)

                // Allow the user to see the progress
                dialog.setCancelable(false)

                val intent = Intent(requireContext(), AapService::class.java).apply {
                    action = AapService.ACTION_NEARBY_CONNECT
                    putExtra(AapService.EXTRA_ENDPOINT_ID, endpoint.id)
                }
                ContextCompat.startForegroundService(requireContext(), intent)
            }
        }

        dialog.show()

        // Live-update the dialog list as endpoints are discovered
        collectJob = viewLifecycleOwner.lifecycleScope.launch {
            NearbyManager.discoveredEndpoints.collect { endpoints ->
                listAdapter.clear()
                listAdapter.addAll(endpoints)
                listAdapter.notifyDataSetChanged()

                if (endpoints.isEmpty()) {
                    dialog.setTitle(getString(R.string.searching))
                    searchingText.visibility = View.GONE
                } else {
                    dialog.setTitle(getString(R.string.nearby_device_found))
                    searchingText.visibility = View.VISIBLE
                    searchingText.text = getString(R.string.select_nearby_device) + " (${endpoints.size})"
                }
            }
        }
    }

    private fun updateTextColors() {
        val appSettings = App.provide(requireContext()).settings
        val nightModeFlags = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
        val isLightMode = nightModeFlags != Configuration.UI_MODE_NIGHT_YES

        val labelViews = listOf(self_mode_text, wifi_text_view,
            view?.findViewById<TextView>(R.id.usb_text),
            view?.findViewById<TextView>(R.id.settings_text))

        if (appSettings.useGradientBackground && isLightMode) {
            val darkColor = Color.parseColor("#1a1a1a")
            labelViews.filterNotNull().forEach { tv ->
                tv.setTextColor(darkColor)
                tv.setShadowLayer(2f, 0f, 0f, Color.WHITE)
            }
        } else {
            val lightColor = Color.parseColor("#f7f7f7")
            labelViews.filterNotNull().forEach { tv ->
                tv.setTextColor(lightColor)
                tv.setShadowLayer(0f, 0f, 0f, Color.TRANSPARENT)
            }
        }

        exitButton.setTextColor(Color.WHITE)
    }

    companion object {
        private var hasAutoStarted = false
        var forceSelfModeLaunch = false
        fun resetAutoStart() {
            hasAutoStarted = false
        }
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/main/settings/SettingsAdapter.kt`:

```kt
package com.andrerinas.headunitrevived.main.settings

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Switch
import android.widget.TextView
import android.os.Build
import androidx.annotation.StringRes
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.andrerinas.headunitrevived.R
import com.google.android.material.slider.Slider

// Sealed class to represent different types of items in the settings list
sealed class SettingItem {
    abstract val stableId: String // Unique ID for DiffUtil

    data class SettingEntry(
        override val stableId: String, // Unique ID for the setting (e.g., "gpsNavigation")
        @StringRes val nameResId: Int,
        var value: String, // Current display value of the setting
        val onClick: (settingId: String) -> Unit // Callback when the setting is clicked
    ) : SettingItem()

    data class ToggleSettingEntry(
        override val stableId: String,
        @StringRes val nameResId: Int,
        @StringRes val descriptionResId: Int,
        var isChecked: Boolean,
        val isEnabled: Boolean = true,
        val nameOverride: String? = null,
        val onCheckedChanged: (Boolean) -> Unit
    ) : SettingItem()

    data class SliderSettingEntry(
        override val stableId: String,
        @StringRes val nameResId: Int,
        var value: String,
        var sliderValue: Float,
        val valueFrom: Float,
        val valueTo: Float,
        val stepSize: Float,
        val onValueChanged: (Float) -> Unit
    ) : SettingItem()

    data class CategoryHeader(override val stableId: String, @StringRes val titleResId: Int) : SettingItem()

    data class InfoBanner(override val stableId: String, @StringRes val textResId: Int) : SettingItem()

    data class ActionButton(
        override val stableId: String,
        @StringRes val textResId: Int,
        val isEnabled: Boolean = true,
        val onClick: () -> Unit
    ) : SettingItem()

    data class SegmentedButtonSettingEntry(
        override val stableId: String,
        @StringRes val nameResId: Int,
        val options: List<String>, // Exactly 3 options for now as per layout
        var selectedIndex: Int,
        val onOptionSelected: (Int) -> Unit
    ) : SettingItem()
}

class SettingsAdapter : ListAdapter<SettingItem, RecyclerView.ViewHolder>(SettingsDiffCallback()) { // Inherit from ListAdapter

    // Define View Types
    companion object {
        private const val VIEW_TYPE_HEADER = 0
        private const val VIEW_TYPE_SETTING = 1
        private const val VIEW_TYPE_TOGGLE = 3
        private const val VIEW_TYPE_SLIDER = 4
        private const val VIEW_TYPE_INFO_BANNER = 5
        private const val VIEW_TYPE_ACTION_BUTTON = 6
        private const val VIEW_TYPE_SEGMENTED = 7
    }

    override fun getItemViewType(position: Int): Int {
        return when (getItem(position)) { // Use getItem() from ListAdapter
            is SettingItem.CategoryHeader -> VIEW_TYPE_HEADER
            is SettingItem.SettingEntry -> VIEW_TYPE_SETTING
            is SettingItem.ToggleSettingEntry -> VIEW_TYPE_TOGGLE
            is SettingItem.SliderSettingEntry -> VIEW_TYPE_SLIDER
            is SettingItem.InfoBanner -> VIEW_TYPE_INFO_BANNER
            is SettingItem.ActionButton -> VIEW_TYPE_ACTION_BUTTON
            is SettingItem.SegmentedButtonSettingEntry -> VIEW_TYPE_SEGMENTED
        }
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
        val inflater = LayoutInflater.from(parent.context)
        return when (viewType) {
            VIEW_TYPE_HEADER -> HeaderViewHolder(inflater.inflate(R.layout.layout_category_header, parent, false))
            VIEW_TYPE_SETTING -> SettingViewHolder(inflater.inflate(R.layout.layout_setting_item, parent, false))
            VIEW_TYPE_TOGGLE -> ToggleSettingViewHolder(inflater.inflate(R.layout.layout_setting_item_toggle, parent, false))
            VIEW_TYPE_SLIDER -> SliderSettingViewHolder(inflater.inflate(R.layout.layout_setting_item_slider, parent, false))
            VIEW_TYPE_INFO_BANNER -> InfoBannerViewHolder(inflater.inflate(R.layout.layout_setting_info_banner, parent, false))
            VIEW_TYPE_ACTION_BUTTON -> ActionButtonViewHolder(inflater.inflate(R.layout.layout_setting_action_button, parent, false))
            VIEW_TYPE_SEGMENTED -> SegmentedButtonViewHolder(inflater.inflate(R.layout.layout_setting_item_segmented, parent, false))
            else -> throw IllegalArgumentException("Unknown view type: $viewType")
        }
    }

    override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
        val item = getItem(position)

        if (holder is SettingViewHolder || holder is ToggleSettingViewHolder || holder is SliderSettingViewHolder || holder is ActionButtonViewHolder || holder is SegmentedButtonViewHolder) {
            updateItemVisuals(holder.itemView, position)
        }

        when (item) {
            is SettingItem.CategoryHeader -> (holder as HeaderViewHolder).bind(item)
            is SettingItem.SettingEntry -> (holder as SettingViewHolder).bind(item)
            is SettingItem.ToggleSettingEntry -> (holder as ToggleSettingViewHolder).bind(item)
            is SettingItem.SliderSettingEntry -> (holder as SliderSettingViewHolder).bind(item)
            is SettingItem.InfoBanner -> (holder as InfoBannerViewHolder).bind(item)
            is SettingItem.ActionButton -> (holder as ActionButtonViewHolder).bind(item)
            is SettingItem.SegmentedButtonSettingEntry -> (holder as SegmentedButtonViewHolder).bind(item)
        }
    }

    private fun updateItemVisuals(view: View, position: Int) {
        val prev = if (position > 0) getItem(position - 1) else null
        val next = if (position < itemCount - 1) getItem(position + 1) else null

        val isTop = prev is SettingItem.CategoryHeader || prev == null
        val isBottom = next is SettingItem.CategoryHeader || next == null

        val bgRes = when {
            isTop && isBottom -> R.drawable.bg_setting_single
            isTop -> R.drawable.bg_setting_top
            isBottom -> R.drawable.bg_setting_bottom
            else -> R.drawable.bg_setting_middle
        }
        view.setBackgroundResource(bgRes)
    }

    // --- ViewHolder implementations ---

    class HeaderViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val title: TextView = itemView.findViewById(R.id.categoryTitle)
        fun bind(header: SettingItem.CategoryHeader) {
            title.setText(header.titleResId)
        }
    }

    class SettingViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val settingName: TextView = itemView.findViewById(R.id.settingName)
        private val settingValue: TextView = itemView.findViewById(R.id.settingValue)

        fun bind(setting: SettingItem.SettingEntry) {
            settingName.setText(setting.nameResId)
            settingValue.text = setting.value
            itemView.setOnClickListener { setting.onClick(setting.stableId) }
        }
    }

    class ToggleSettingViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val settingName: TextView = itemView.findViewById(R.id.settingName)
        private val settingDescription: TextView = itemView.findViewById(R.id.settingDescription)
        private val settingSwitch: Switch = itemView.findViewById(R.id.settingSwitch)

        fun bind(setting: SettingItem.ToggleSettingEntry) {
            if (setting.nameOverride != null) settingName.text = setting.nameOverride
            else settingName.setText(setting.nameResId)
            settingDescription.setText(setting.descriptionResId)
            settingSwitch.setOnCheckedChangeListener(null)
            settingSwitch.isChecked = setting.isChecked
            settingSwitch.isEnabled = setting.isEnabled
            itemView.alpha = if (setting.isEnabled) 1.0f else 0.5f
            itemView.isClickable = setting.isEnabled
            settingSwitch.setOnCheckedChangeListener { _, isChecked ->
                setting.onCheckedChanged(isChecked)
            }
            itemView.setOnClickListener {
                if (setting.isEnabled) settingSwitch.toggle()
            }
        }
    }

    class InfoBannerViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val infoText: TextView = itemView.findViewById(R.id.infoText)
        fun bind(item: SettingItem.InfoBanner) {
            infoText.setText(item.textResId)
        }
    }

    class ActionButtonViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val button: com.google.android.material.button.MaterialButton = itemView.findViewById(R.id.action_button)
        fun bind(item: SettingItem.ActionButton) {
            button.setText(item.textResId)
            button.isEnabled = item.isEnabled
            button.setOnClickListener { item.onClick() }
        }
    }

    class SliderSettingViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val settingName: TextView = itemView.findViewById(R.id.settingName)
        private val settingValue: TextView = itemView.findViewById(R.id.settingValue)
        private val settingSlider: Slider = itemView.findViewById(R.id.settingSlider)

        fun bind(setting: SettingItem.SliderSettingEntry) {
            settingName.setText(setting.nameResId)
            settingValue.text = setting.value
            settingSlider.clearOnChangeListeners()
            settingSlider.valueFrom = setting.valueFrom
            settingSlider.valueTo = setting.valueTo
            settingSlider.stepSize = setting.stepSize
            settingSlider.value = setting.sliderValue
            settingSlider.addOnChangeListener { _, value, fromUser ->
                if (fromUser) {
                    setting.onValueChanged(value)
                }
            }
        }
    }

    class SegmentedButtonViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView) {
        private val settingName: TextView = itemView.findViewById(R.id.settingName)
        private val btn1: com.google.android.material.button.MaterialButton = itemView.findViewById(R.id.btnOption1)
        private val btn2: com.google.android.material.button.MaterialButton = itemView.findViewById(R.id.btnOption2)
        private val btn3: com.google.android.material.button.MaterialButton = itemView.findViewById(R.id.btnOption3)

        fun bind(setting: SettingItem.SegmentedButtonSettingEntry) {
            settingName.setText(setting.nameResId)

            val buttons = listOf(btn1, btn2, btn3)
            val visibleButtons = buttons.filterIndexed { index, _ -> index < setting.options.size }

            buttons.forEachIndexed { index, button ->
                if (index < setting.options.size) {
                    button.text = setting.options[index]
                    button.visibility = View.VISIBLE
                } else {
                    button.visibility = View.GONE
                }
            }

            // Fixed 16dp radius like cards/save button
            val radius = 16f * itemView.resources.displayMetrics.density

            visibleButtons.forEachIndexed { index, button ->
                val shapeModel = when (index) {
                    0 -> com.google.android.material.shape.ShapeAppearanceModel.builder()
                        .setTopLeftCornerSize(radius).setBottomLeftCornerSize(radius)
                        .setTopRightCornerSize(0f).setBottomRightCornerSize(0f)
                        .build()
                    visibleButtons.size - 1 -> com.google.android.material.shape.ShapeAppearanceModel.builder()
                        .setTopRightCornerSize(radius).setBottomRightCornerSize(radius)
                        .setTopLeftCornerSize(0f).setBottomLeftCornerSize(0f)
                        .build()
                    else -> com.google.android.material.shape.ShapeAppearanceModel.builder()
                        .setAllCornerSizes(0f)
                        .build()
                }
                button.shapeAppearanceModel = shapeModel

                // Manual selection state handling
                val isSelected = index == setting.selectedIndex
                button.isChecked = isSelected

                // Bring active button to front so its 2dp border wins the overlap
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    button.elevation = if (isSelected) 2f else 0f
                    button.stateListAnimator = null // Disable default elevation animations
                }

                // Ensure stroke is 2dp and themed
                button.strokeWidth = (2 * itemView.resources.displayMetrics.density).toInt()

                button.setOnClickListener {
                    if (index != setting.selectedIndex) {
                        // Instant UI feedback: uncheck all, check this one
                        visibleButtons.forEach {
                            it.isChecked = false
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) it.elevation = 0f
                        }
                        button.isChecked = true
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) button.elevation = 2f

                        setting.onOptionSelected(index)
                    }
                }
            }
        }
    }

    // DiffUtil.ItemCallback implementation
    private class SettingsDiffCallback : DiffUtil.ItemCallback<SettingItem>() {
        override fun areItemsTheSame(oldItem: SettingItem, newItem: SettingItem): Boolean {
            return oldItem.stableId == newItem.stableId
        }

        override fun areContentsTheSame(oldItem: SettingItem, newItem: SettingItem): Boolean {
            return when {
                oldItem is SettingItem.SettingEntry && newItem is SettingItem.SettingEntry ->
                    oldItem.nameResId == newItem.nameResId && oldItem.value == newItem.value
                oldItem is SettingItem.ToggleSettingEntry && newItem is SettingItem.ToggleSettingEntry ->
                    oldItem.nameResId == newItem.nameResId && oldItem.descriptionResId == newItem.descriptionResId && oldItem.isChecked == newItem.isChecked && oldItem.isEnabled == newItem.isEnabled && oldItem.nameOverride == newItem.nameOverride
                oldItem is SettingItem.SliderSettingEntry && newItem is SettingItem.SliderSettingEntry ->
                    oldItem.nameResId == newItem.nameResId && oldItem.value == newItem.value && oldItem.sliderValue == newItem.sliderValue
                oldItem is SettingItem.CategoryHeader && newItem is SettingItem.CategoryHeader ->
                    oldItem.titleResId == newItem.titleResId
                oldItem is SettingItem.InfoBanner && newItem is SettingItem.InfoBanner ->
                    oldItem.textResId == newItem.textResId
                oldItem is SettingItem.ActionButton && newItem is SettingItem.ActionButton ->
                    oldItem.textResId == newItem.textResId && oldItem.isEnabled == newItem.isEnabled
                oldItem is SettingItem.SegmentedButtonSettingEntry && newItem is SettingItem.SegmentedButtonSettingEntry ->
                    oldItem.nameResId == newItem.nameResId && oldItem.options == newItem.options && oldItem.selectedIndex == newItem.selectedIndex
                else -> false
            }
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/settings/AutoConnectAdapter.kt`:

```kt
package com.andrerinas.headunitrevived.main.settings

import android.annotation.SuppressLint
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.Switch
import android.widget.TextView
import androidx.recyclerview.widget.ItemTouchHelper
import androidx.recyclerview.widget.RecyclerView
import com.andrerinas.headunitrevived.R

data class AutoConnectMethod(
    val id: String,
    val nameResId: Int,
    val descriptionResId: Int,
    var isEnabled: Boolean
)

class AutoConnectAdapter(
    private val items: MutableList<AutoConnectMethod>,
    private val onChanged: () -> Unit
) : RecyclerView.Adapter<AutoConnectAdapter.ViewHolder>() {

    var itemTouchHelper: ItemTouchHelper? = null

    inner class ViewHolder(view: View) : RecyclerView.ViewHolder(view) {
        val dragHandle: ImageView = view.findViewById(R.id.drag_handle)
        val priorityNumber: TextView = view.findViewById(R.id.priority_number)
        val methodName: TextView = view.findViewById(R.id.method_name)
        val methodDescription: TextView = view.findViewById(R.id.method_description)
        val btnMoveUp: ImageButton = view.findViewById(R.id.btn_move_up)
        val btnMoveDown: ImageButton = view.findViewById(R.id.btn_move_down)
        val methodToggle: Switch = view.findViewById(R.id.method_toggle)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        val view = LayoutInflater.from(parent.context).inflate(R.layout.item_auto_connect, parent, false)
        return ViewHolder(view)
    }

    @SuppressLint("ClickableViewAccessibility")
    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        val item = items[position]

        holder.priorityNumber.text = "#${position + 1}"
        holder.methodName.setText(item.nameResId)
        holder.methodDescription.setText(item.descriptionResId)

        // Arrow visibility
        holder.btnMoveUp.visibility = if (position == 0) View.INVISIBLE else View.VISIBLE
        holder.btnMoveDown.visibility = if (position == items.size - 1) View.INVISIBLE else View.VISIBLE

        // Toggle
        holder.methodToggle.setOnCheckedChangeListener(null)
        holder.methodToggle.isChecked = item.isEnabled
        holder.methodToggle.setOnCheckedChangeListener { _, isChecked ->
            item.isEnabled = isChecked
            onChanged()
        }

        // Arrow click listeners
        holder.btnMoveUp.setOnClickListener {
            val pos = holder.bindingAdapterPosition
            if (pos > 0) {
                swapItems(pos, pos - 1)
            }
        }

        holder.btnMoveDown.setOnClickListener {
            val pos = holder.bindingAdapterPosition
            if (pos < items.size - 1) {
                swapItems(pos, pos + 1)
            }
        }

        // Drag handle touch listener
        holder.dragHandle.setOnTouchListener { _, event ->
            if (event.actionMasked == MotionEvent.ACTION_DOWN) {
                itemTouchHelper?.startDrag(holder)
            }
            false
        }

        // Background based on position
        val bgRes = when {
            items.size == 1 -> R.drawable.bg_setting_single
            position == 0 -> R.drawable.bg_setting_top
            position == items.size - 1 -> R.drawable.bg_setting_bottom
            else -> R.drawable.bg_setting_middle
        }
        holder.itemView.setBackgroundResource(bgRes)
    }

    override fun getItemCount() = items.size

    fun swapItems(from: Int, to: Int) {
        val item = items.removeAt(from)
        items.add(to, item)
        notifyItemMoved(from, to)
        // Rebind both items to update priority numbers and arrow visibility
        notifyItemChanged(from)
        notifyItemChanged(to)
        onChanged()
    }

    fun getOrderedIds(): List<String> = items.map { it.id }

    fun getEnabledStates(): Map<String, Boolean> = items.associate { it.id to it.isEnabled }
}

class AutoConnectTouchCallback(
    private val adapter: AutoConnectAdapter
) : ItemTouchHelper.Callback() {

    override fun getMovementFlags(recyclerView: RecyclerView, viewHolder: RecyclerView.ViewHolder): Int {
        val dragFlags = ItemTouchHelper.UP or ItemTouchHelper.DOWN
        return makeMovementFlags(dragFlags, 0)
    }

    override fun onMove(recyclerView: RecyclerView, viewHolder: RecyclerView.ViewHolder, target: RecyclerView.ViewHolder): Boolean {
        adapter.swapItems(viewHolder.bindingAdapterPosition, target.bindingAdapterPosition)
        return true
    }

    override fun onSwiped(viewHolder: RecyclerView.ViewHolder, direction: Int) {
        // No swipe
    }

    override fun isLongPressDragEnabled() = false
}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/AutoConnectFragment.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.app.AlertDialog
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.ItemTouchHelper
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.main.settings.AutoConnectAdapter
import com.andrerinas.headunitrevived.main.settings.AutoConnectMethod
import com.andrerinas.headunitrevived.main.settings.AutoConnectTouchCallback
import com.andrerinas.headunitrevived.utils.Settings
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton

class AutoConnectFragment : Fragment() {

    private lateinit var settings: Settings
    private lateinit var toolbar: MaterialToolbar
    private lateinit var recyclerView: RecyclerView
    private lateinit var adapter: AutoConnectAdapter
    private var saveButton: MaterialButton? = null

    // Snapshot of initial state for change detection
    private lateinit var initialOrder: List<String>
    private lateinit var initialEnabled: Map<String, Boolean>

    private var hasChanges = false
    private val SAVE_ITEM_ID = 1001

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return inflater.inflate(R.layout.fragment_auto_connect, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        settings = App.provide(requireContext()).settings

        // Snapshot initial state
        initialOrder = settings.autoConnectPriorityOrder.toList()
        initialEnabled = mapOf(
            Settings.AUTO_CONNECT_LAST_SESSION to settings.autoConnectLastSession,
            Settings.AUTO_CONNECT_SELF_MODE to settings.autoStartSelfMode,
            Settings.AUTO_CONNECT_SINGLE_USB to settings.autoConnectSingleUsbDevice
        )

        // Build method list in priority order
        val methods = initialOrder.mapNotNull { id ->
            methodDefinition(id)?.let { (nameRes, descRes) ->
                AutoConnectMethod(id, nameRes, descRes, initialEnabled[id] ?: false)
            }
        }.toMutableList()

        adapter = AutoConnectAdapter(methods) { checkChanges() }

        val touchCallback = AutoConnectTouchCallback(adapter)
        val itemTouchHelper = ItemTouchHelper(touchCallback)
        adapter.itemTouchHelper = itemTouchHelper

        toolbar = view.findViewById(R.id.toolbar)
        recyclerView = view.findViewById(R.id.recycler_view)
        recyclerView.layoutManager = LinearLayoutManager(requireContext())
        recyclerView.adapter = adapter
        itemTouchHelper.attachToRecyclerView(recyclerView)

        setupToolbar()

        // Intercept system back button
        requireActivity().onBackPressedDispatcher.addCallback(viewLifecycleOwner, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                handleBackPress()
            }
        })
    }

    private fun methodDefinition(id: String): Pair<Int, Int>? {
        return when (id) {
            Settings.AUTO_CONNECT_LAST_SESSION -> R.string.auto_connect_last_session to R.string.auto_connect_last_session_description
            Settings.AUTO_CONNECT_SELF_MODE -> R.string.auto_start_self_mode to R.string.auto_start_self_mode_description
            Settings.AUTO_CONNECT_SINGLE_USB -> R.string.auto_connect_single_usb to R.string.auto_connect_single_usb_description
            else -> null
        }
    }

    private fun setupToolbar() {
        toolbar.setNavigationOnClickListener {
            handleBackPress()
        }

        val saveItem = toolbar.menu.add(0, SAVE_ITEM_ID, 0, getString(R.string.save))
        saveItem.setShowAsAction(android.view.MenuItem.SHOW_AS_ACTION_ALWAYS)
        saveItem.setActionView(R.layout.layout_save_button)

        saveButton = saveItem.actionView?.findViewById(R.id.save_button_widget)
        saveButton?.setOnClickListener {
            saveSettings()
        }

        updateSaveButtonState()
    }

    private fun handleBackPress() {
        if (hasChanges) {
            AlertDialog.Builder(requireContext())
                .setTitle(R.string.unsaved_changes)
                .setMessage(R.string.unsaved_changes_message)
                .setPositiveButton(R.string.discard) { _, _ ->
                    navigateBack()
                }
                .setNegativeButton(R.string.cancel, null)
                .show()
        } else {
            navigateBack()
        }
    }

    private fun navigateBack() {
        try {
            val navController = findNavController()
            if (!navController.navigateUp()) {
                requireActivity().finish()
            }
        } catch (e: Exception) {
            requireActivity().finish()
        }
    }

    private fun checkChanges() {
        val currentOrder = adapter.getOrderedIds()
        val currentEnabled = adapter.getEnabledStates()

        hasChanges = currentOrder != initialOrder ||
            currentEnabled != initialEnabled
        updateSaveButtonState()
    }

    private fun updateSaveButtonState() {
        saveButton?.isEnabled = hasChanges
        saveButton?.text = getString(R.string.save)
    }

    private fun saveSettings() {
        val orderedIds = adapter.getOrderedIds()
        val enabledStates = adapter.getEnabledStates()

        // Persist order
        settings.autoConnectPriorityOrder = orderedIds

        // Persist individual toggles
        enabledStates[Settings.AUTO_CONNECT_LAST_SESSION]?.let { settings.autoConnectLastSession = it }
        enabledStates[Settings.AUTO_CONNECT_SELF_MODE]?.let { settings.autoStartSelfMode = it }
        enabledStates[Settings.AUTO_CONNECT_SINGLE_USB]?.let { settings.autoConnectSingleUsbDevice = it }

        // Update snapshot
        initialOrder = orderedIds.toList()
        initialEnabled = enabledStates.toMap()

        hasChanges = false
        updateSaveButtonState()

        Toast.makeText(context, getString(R.string.settings_saved), Toast.LENGTH_SHORT).show()
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/BackgroundNotification.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.app.PendingIntent
import android.content.Context
import android.graphics.Bitmap
import android.os.Build
import android.view.KeyEvent
import androidx.core.app.NotificationCompat
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.aap.AapProjectionActivity
import com.andrerinas.headunitrevived.aap.protocol.proto.MediaPlayback
import com.andrerinas.headunitrevived.contract.MediaKeyIntent
import com.andrerinas.headunitrevived.utils.protoUint32ToLong

class BackgroundNotification(private val context: Context) {

    companion object {
        private const val NOTIFICATION_MEDIA = 2
        const val mediaChannel = "media_v2"
    }

    fun notify(
        metadata: MediaPlayback.MediaMetaData,
        playbackSeconds: Long = 0L,
        isPlaying: Boolean = false,
        albumArtBitmap: Bitmap? = null
    ) {

        val playPauseKey = KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE)
        val nextKey = KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_MEDIA_NEXT)
        val prevKey = KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_MEDIA_PREVIOUS)
        val broadcastFlags =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            } else {
                PendingIntent.FLAG_UPDATE_CURRENT
            }

        val playPause = PendingIntent.getBroadcast(context, 1, MediaKeyIntent(playPauseKey), broadcastFlags)
        val next = PendingIntent.getBroadcast(context, 2, MediaKeyIntent(nextKey), broadcastFlags)
        val prev = PendingIntent.getBroadcast(context, 3, MediaKeyIntent(prevKey), broadcastFlags)
        val durationSeconds = if (metadata.hasDurationSeconds()) metadata.durationSeconds.protoUint32ToLong() else 0L
        val clampedPlayback = playbackSeconds.coerceAtLeast(0L).coerceAtMost(durationSeconds.takeIf { it > 0 } ?: playbackSeconds.coerceAtLeast(0L))
        val progressText = if (durationSeconds > 0) {
            "${formatAsMmSs(clampedPlayback)} / ${formatAsMmSs(durationSeconds)}"
        } else {
            formatAsMmSs(clampedPlayback)
        }

        val notification = NotificationCompat.Builder(context, mediaChannel)
                .setContentTitle(metadata.song)
                .setAutoCancel(false)
                .setOngoing(true)
                .setContentText(metadata.artist)
                .setSubText(progressText)
                .setSmallIcon(R.drawable.ic_stat_aa)
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .setContentIntent(PendingIntent.getActivity(context, 0, AapProjectionActivity.intent(context),
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE else PendingIntent.FLAG_UPDATE_CURRENT))
                .addAction(R.drawable.ic_skip_previous_black_24dp, context.getString(R.string.media_action_previous), prev)
                .addAction(
                    R.drawable.ic_play_arrow_black_24dp,
                    context.getString(if (isPlaying) R.string.media_action_pause else R.string.media_action_play),
                    playPause
                )
                .addAction(R.drawable.ic_skip_next_black_24dp, context.getString(R.string.media_action_next), next)


        if (albumArtBitmap != null) {
            notification
                .setStyle(NotificationCompat.BigPictureStyle().bigPicture(albumArtBitmap))
                .setLargeIcon(albumArtBitmap)
        }
        App.provide(context).notificationManager.notify(NOTIFICATION_MEDIA, notification.build())
    }

    fun cancel() {
        App.provide(context).notificationManager.cancel(NOTIFICATION_MEDIA)
    }

    private fun formatAsMmSs(totalSeconds: Long): String {
        val clamped = totalSeconds.coerceAtLeast(0L)
        val minutes = clamped / 60
        val seconds = clamped % 60
        return String.format("%02d:%02d", minutes, seconds)
    }

}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/VehicleInfoFragment.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.main.settings.SettingItem
import com.andrerinas.headunitrevived.main.settings.SettingsAdapter
import com.andrerinas.headunitrevived.utils.Settings
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder

class VehicleInfoFragment : Fragment() {
    private lateinit var settings: Settings
    private lateinit var recyclerView: RecyclerView
    private lateinit var settingsAdapter: SettingsAdapter
    private lateinit var toolbar: MaterialToolbar
    private var saveButton: MaterialButton? = null

    private var pendingVehicleDisplayName: String? = null
    private var pendingVehicleMake: String? = null
    private var pendingVehicleModel: String? = null
    private var pendingVehicleYear: String? = null
    private var pendingVehicleId: String? = null
    private var pendingRightHandDrive: Boolean? = null
    private var pendingHeadUnitMake: String? = null
    private var pendingHeadUnitModel: String? = null

    private var hasChanges = false
    private val SAVE_ITEM_ID = 1001

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return inflater.inflate(R.layout.fragment_vehicle_info, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        settings = App.provide(requireContext()).settings

        pendingVehicleDisplayName = settings.vehicleDisplayName
        pendingVehicleMake = settings.vehicleMake
        pendingVehicleModel = settings.vehicleModel
        pendingVehicleYear = settings.vehicleYear
        pendingVehicleId = settings.vehicleId
        pendingRightHandDrive = settings.rightHandDrive
        pendingHeadUnitMake = settings.headUnitMake
        pendingHeadUnitModel = settings.headUnitModel

        requireActivity().onBackPressedDispatcher.addCallback(viewLifecycleOwner, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                handleBackPress()
            }
        })

        toolbar = view.findViewById(R.id.toolbar)
        settingsAdapter = SettingsAdapter()
        recyclerView = view.findViewById(R.id.recycler_view)
        recyclerView.layoutManager = LinearLayoutManager(requireContext())
        recyclerView.adapter = settingsAdapter

        updateSettingsList()
        setupToolbar()
    }

    private fun setupToolbar() {
        toolbar.setNavigationOnClickListener {
            handleBackPress()
        }

        val saveItem = toolbar.menu.add(0, SAVE_ITEM_ID, 0, getString(R.string.save))
        saveItem.setShowAsAction(android.view.MenuItem.SHOW_AS_ACTION_ALWAYS)
        saveItem.setActionView(R.layout.layout_save_button)

        saveButton = saveItem.actionView?.findViewById(R.id.save_button_widget)
        saveButton?.setOnClickListener {
            saveSettings()
        }

        updateSaveButtonState()
    }

    private fun handleBackPress() {
        if (hasChanges) {
            MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                .setTitle(R.string.unsaved_changes)
                .setMessage(R.string.unsaved_changes_message)
                .setPositiveButton(R.string.discard) { _, _ ->
                    navigateBack()
                }
                .setNegativeButton(R.string.cancel, null)
                .show()
        } else {
            navigateBack()
        }
    }

    private fun navigateBack() {
        try {
            val navController = findNavController()
            if (!navController.navigateUp()) {
                requireActivity().finish()
            }
        } catch (e: Exception) {
            requireActivity().finish()
        }
    }

    private fun updateSaveButtonState() {
        saveButton?.isEnabled = hasChanges
        saveButton?.text = getString(R.string.save)
    }

    private fun saveSettings() {
        pendingVehicleDisplayName?.let { settings.vehicleDisplayName = it }
        pendingVehicleMake?.let { settings.vehicleMake = it }
        pendingVehicleModel?.let { settings.vehicleModel = it }
        pendingVehicleYear?.let { settings.vehicleYear = it }
        pendingVehicleId?.let { settings.vehicleId = it }
        pendingRightHandDrive?.let { settings.rightHandDrive = it }
        pendingHeadUnitMake?.let { settings.headUnitMake = it }
        pendingHeadUnitModel?.let { settings.headUnitModel = it }

        hasChanges = false
        updateSaveButtonState()

        Toast.makeText(context, getString(R.string.settings_saved), Toast.LENGTH_SHORT).show()
    }

    private fun checkChanges() {
        hasChanges = pendingVehicleDisplayName != settings.vehicleDisplayName ||
                pendingVehicleMake != settings.vehicleMake ||
                pendingVehicleModel != settings.vehicleModel ||
                pendingVehicleYear != settings.vehicleYear ||
                pendingVehicleId != settings.vehicleId ||
                pendingRightHandDrive != settings.rightHandDrive ||
                pendingHeadUnitMake != settings.headUnitMake ||
                pendingHeadUnitModel != settings.headUnitModel

        updateSaveButtonState()
    }

    private fun updateSettingsList() {
        val scrollState = recyclerView.layoutManager?.onSaveInstanceState()
        val items = mutableListOf<SettingItem>()

        items.add(SettingItem.InfoBanner(
            stableId = "vehicleInfoRestartNote",
            textResId = R.string.vehicle_info_restart_note
        ))

        items.add(SettingItem.CategoryHeader("vehicle", R.string.category_vehicle))

        items.add(SettingItem.SettingEntry(
            stableId = "vehicleDisplayName",
            nameResId = R.string.vehicle_display_name_label,
            value = pendingVehicleDisplayName ?: "",
            onClick = {
                showTextInputDialog(R.string.vehicle_display_name_label, pendingVehicleDisplayName ?: "") { value ->
                    pendingVehicleDisplayName = value
                    checkChanges()
                    updateSettingsList()
                }
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "vehicleMake",
            nameResId = R.string.vehicle_make_label,
            value = pendingVehicleMake ?: "",
            onClick = {
                showTextInputDialog(R.string.vehicle_make_label, pendingVehicleMake ?: "") { value ->
                    pendingVehicleMake = value
                    checkChanges()
                    updateSettingsList()
                }
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "vehicleModel",
            nameResId = R.string.vehicle_model_label,
            value = pendingVehicleModel ?: "",
            onClick = {
                showTextInputDialog(R.string.vehicle_model_label, pendingVehicleModel ?: "") { value ->
                    pendingVehicleModel = value
                    checkChanges()
                    updateSettingsList()
                }
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "vehicleYear",
            nameResId = R.string.vehicle_year_label,
            value = pendingVehicleYear ?: "",
            onClick = {
                showYearInputDialog(pendingVehicleYear ?: "") { value ->
                    pendingVehicleYear = value
                    checkChanges()
                    updateSettingsList()
                }
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "vehicleId",
            nameResId = R.string.vehicle_id_label,
            value = pendingVehicleId ?: "",
            onClick = {
                showTextInputDialogWithMessage(
                    R.string.vehicle_id_label,
                    R.string.vehicle_id_description,
                    pendingVehicleId ?: ""
                ) { value ->
                    pendingVehicleId = value
                    checkChanges()
                    updateSettingsList()
                }
            }
        ))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "rightHandDrive",
            nameResId = R.string.right_hand_drive,
            descriptionResId = R.string.right_hand_drive_description,
            isChecked = pendingRightHandDrive!!,
            onCheckedChanged = { isChecked ->
                pendingRightHandDrive = isChecked
                checkChanges()
            }
        ))

        items.add(SettingItem.CategoryHeader("headUnit", R.string.category_head_unit))

        items.add(SettingItem.SettingEntry(
            stableId = "headUnitMake",
            nameResId = R.string.head_unit_make_label,
            value = pendingHeadUnitMake ?: "",
            onClick = {
                showTextInputDialogWithMessage(
                    R.string.head_unit_make_label,
                    R.string.head_unit_make_hint,
                    pendingHeadUnitMake ?: ""
                ) { value ->
                    pendingHeadUnitMake = value
                    checkChanges()
                    updateSettingsList()
                }
            }
        ))

        items.add(SettingItem.SettingEntry(
            stableId = "headUnitModel",
            nameResId = R.string.head_unit_model_label,
            value = pendingHeadUnitModel ?: "",
            onClick = {
                showTextInputDialog(R.string.head_unit_model_label, pendingHeadUnitModel ?: "") { value ->
                    pendingHeadUnitModel = value
                    checkChanges()
                    updateSettingsList()
                }
            }
        ))

        settingsAdapter.submitList(items) {
            scrollState?.let { recyclerView.layoutManager?.onRestoreInstanceState(it) }
        }
    }

    private fun showTextInputDialog(titleResId: Int, currentValue: String, onResult: (String) -> Unit) {
        showTextInputDialogWithMessage(titleResId, null, currentValue, onResult)
    }

    private fun showTextInputDialogWithMessage(titleResId: Int, messageResId: Int?, currentValue: String, onResult: (String) -> Unit) {
        val container = android.widget.LinearLayout(requireContext()).apply {
            orientation = android.widget.LinearLayout.VERTICAL
            setPadding(48, 16, 48, 0)
        }

        if (messageResId != null) {
            val messageText = android.widget.TextView(requireContext()).apply {
                setText(messageResId)
                val textColorAttr = android.util.TypedValue()
                context.theme.resolveAttribute(android.R.attr.textColorSecondary, textColorAttr, true)
                setTextColor(context.resources.getColor(textColorAttr.resourceId, context.theme))
                textSize = 13f
                setPadding(0, 0, 0, 24)
            }
            container.addView(messageText)
        }

        val editText = android.widget.EditText(requireContext()).apply {
            setText(currentValue)
            setSelection(text.length)
        }
        container.addView(editText)

        MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(titleResId)
            .setView(container)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val value = editText.text.toString().trim()
                if (value.isNotEmpty()) {
                    onResult(value)
                }
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun showYearInputDialog(currentValue: String, onResult: (String) -> Unit) {
        val maxYear = java.util.Calendar.getInstance().get(java.util.Calendar.YEAR) + 2

        val editText = android.widget.EditText(requireContext()).apply {
            setText(currentValue)
            setSelection(text.length)
            setPadding(48, 32, 48, 16)
            inputType = android.text.InputType.TYPE_CLASS_NUMBER
            filters = arrayOf(android.text.InputFilter.LengthFilter(4))
        }

        val dialog = MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(R.string.vehicle_year_label)
            .setView(editText)
            .setPositiveButton(android.R.string.ok, null)
            .setNegativeButton(android.R.string.cancel, null)
            .show()

        dialog.getButton(android.app.AlertDialog.BUTTON_POSITIVE).setOnClickListener {
            val text = editText.text.toString().trim()
            val year = text.toIntOrNull()
            when {
                text.isEmpty() || year == null -> {
                    editText.error = getString(R.string.vehicle_year_error_invalid)
                }
                year > maxYear -> {
                    editText.error = getString(R.string.vehicle_year_error_max, maxYear)
                }
                year < 1900 -> {
                    editText.error = getString(R.string.vehicle_year_error_invalid)
                }
                else -> {
                    onResult(text)
                    dialog.dismiss()
                }
            }
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/AutoStartFragment.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.main.settings.SettingItem
import com.andrerinas.headunitrevived.main.settings.SettingsAdapter
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.Settings
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder

class AutoStartFragment : Fragment() {
    private lateinit var settings: Settings
    private lateinit var recyclerView: RecyclerView
    private lateinit var settingsAdapter: SettingsAdapter
    private lateinit var toolbar: MaterialToolbar
    private var saveButton: MaterialButton? = null

    private var pendingAutoStartOnBoot: Boolean? = null
    private var pendingAutoStartOnScreenOn: Boolean? = null
    private var pendingListenForUsbDevices: Boolean? = null
    private var pendingAutoStartOnUsb: Boolean? = null
    private var pendingAutoStartBtName: String? = null
    private var pendingAutoStartBtMac: String? = null
    private var pendingReopenOnReconnection: Boolean? = null

    private var hasChanges = false
    private val SAVE_ITEM_ID = 1001

    private val bluetoothPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) {
            showBluetoothDeviceSelector()
        } else {
            showBluetoothPermissionDeniedDialog()
        }
    }

    private val bluetoothEnableLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == android.app.Activity.RESULT_OK) {
            showBluetoothDeviceSelector()
        }
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return inflater.inflate(R.layout.fragment_auto_start, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        settings = App.provide(requireContext()).settings

        pendingAutoStartOnBoot = settings.autoStartOnBoot
        pendingAutoStartOnScreenOn = settings.autoStartOnScreenOn
        pendingListenForUsbDevices = settings.listenForUsbDevices
        pendingAutoStartOnUsb = settings.autoStartOnUsb
        pendingAutoStartBtName = settings.autoStartBluetoothDeviceName
        pendingAutoStartBtMac = settings.autoStartBluetoothDeviceMac
        pendingReopenOnReconnection = settings.reopenOnReconnection

        requireActivity().onBackPressedDispatcher.addCallback(viewLifecycleOwner, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                handleBackPress()
            }
        })

        toolbar = view.findViewById(R.id.toolbar)
        settingsAdapter = SettingsAdapter()
        recyclerView = view.findViewById(R.id.recycler_view)
        recyclerView.layoutManager = LinearLayoutManager(requireContext())
        recyclerView.adapter = settingsAdapter

        updateSettingsList()
        setupToolbar()
    }

    private fun setupToolbar() {
        toolbar.setNavigationOnClickListener {
            handleBackPress()
        }

        val saveItem = toolbar.menu.add(0, SAVE_ITEM_ID, 0, getString(R.string.save))
        saveItem.setShowAsAction(android.view.MenuItem.SHOW_AS_ACTION_ALWAYS)
        saveItem.setActionView(R.layout.layout_save_button)

        saveButton = saveItem.actionView?.findViewById(R.id.save_button_widget)
        saveButton?.setOnClickListener {
            saveSettings()
        }

        updateSaveButtonState()
    }

    private fun handleBackPress() {
        if (hasChanges) {
            MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                .setTitle(R.string.unsaved_changes)
                .setMessage(R.string.unsaved_changes_message)
                .setPositiveButton(R.string.discard) { _, _ ->
                    navigateBack()
                }
                .setNegativeButton(R.string.cancel, null)
                .show()
        } else {
            navigateBack()
        }
    }

    private fun navigateBack() {
        try {
            val navController = findNavController()
            if (!navController.navigateUp()) {
                requireActivity().finish()
            }
        } catch (e: Exception) {
            requireActivity().finish()
        }
    }

    private fun updateSaveButtonState() {
        saveButton?.isEnabled = hasChanges
        saveButton?.text = getString(R.string.save)
    }

    private fun saveSettings() {
        pendingAutoStartOnBoot?.let {
            settings.autoStartOnBoot = it
            Settings.syncAutoStartOnBootToDeviceStorage(requireContext(), it)
        }
        pendingAutoStartOnScreenOn?.let {
            settings.autoStartOnScreenOn = it
            Settings.syncAutoStartOnScreenOnToDeviceStorage(requireContext(), it)
        }
        pendingListenForUsbDevices?.let {
            settings.listenForUsbDevices = it
            Settings.syncListenForUsbDevicesToDeviceStorage(requireContext(), it)
            Settings.setUsbAttachedActivityEnabled(requireContext(), it)
        }
        pendingAutoStartOnUsb?.let {
            settings.autoStartOnUsb = it
            Settings.syncAutoStartOnUsbToDeviceStorage(requireContext(), it)
        }
        pendingAutoStartBtName?.let { settings.autoStartBluetoothDeviceName = it }
        pendingAutoStartBtMac?.let {
            settings.autoStartBluetoothDeviceMac = it
            Settings.syncAutoStartBtMacToDeviceStorage(requireContext(), it)
        }
        pendingReopenOnReconnection?.let { settings.reopenOnReconnection = it }

        // Check for Overlay permission if any auto-start is configured
        if ((!pendingAutoStartBtMac.isNullOrEmpty() || pendingAutoStartOnUsb == true || pendingAutoStartOnBoot == true || pendingAutoStartOnScreenOn == true) && Build.VERSION.SDK_INT >= 23) {
            if (!android.provider.Settings.canDrawOverlays(requireContext())) {
                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                    .setTitle(R.string.overlay_permission_title)
                    .setMessage(R.string.overlay_permission_description)
                    .setPositiveButton(R.string.open_settings) { _, _ ->
                        val intent = Intent(
                            android.provider.Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                            android.net.Uri.parse("package:${requireContext().packageName}")
                        )
                        startActivity(intent)
                    }
                    .setNegativeButton(R.string.cancel, null)
                    .show()
            }
        }

        // Start the foreground service immediately when wake-detection settings
        // are enabled so it can register the dynamic SCREEN_ON receiver.
        if (settings.autoStartOnScreenOn || settings.autoStartOnBoot) {
            ContextCompat.startForegroundService(requireContext(),
                Intent(requireContext(), AapService::class.java))
        }

        hasChanges = false
        updateSaveButtonState()

        Toast.makeText(context, getString(R.string.settings_saved), Toast.LENGTH_SHORT).show()
    }

    private fun checkChanges() {
        hasChanges = pendingAutoStartOnBoot != settings.autoStartOnBoot ||
                pendingAutoStartOnScreenOn != settings.autoStartOnScreenOn ||
                pendingListenForUsbDevices != settings.listenForUsbDevices ||
                pendingAutoStartOnUsb != settings.autoStartOnUsb ||
                pendingAutoStartBtMac != settings.autoStartBluetoothDeviceMac ||
                pendingReopenOnReconnection != settings.reopenOnReconnection

        updateSaveButtonState()
    }

    private fun updateSettingsList() {
        val scrollState = recyclerView.layoutManager?.onSaveInstanceState()
        val items = mutableListOf<SettingItem>()

        items.add(SettingItem.CategoryHeader("autoStart", R.string.auto_start_settings))

        items.add(SettingItem.InfoBanner(
            stableId = "autoStartOemWarning",
            textResId = R.string.auto_start_oem_warning
        ))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "autoStartOnBoot",
            nameResId = R.string.auto_start_on_boot_label,
            descriptionResId = R.string.auto_start_on_boot_description,
            isChecked = pendingAutoStartOnBoot!!,
            onCheckedChanged = { isChecked ->
                pendingAutoStartOnBoot = isChecked
                checkChanges()
                updateSettingsList()
            }
        ))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "autoStartOnScreenOn",
            nameResId = R.string.auto_start_screen_on_label,
            descriptionResId = R.string.auto_start_screen_on_description,
            isChecked = pendingAutoStartOnScreenOn!!,
            onCheckedChanged = { isChecked ->
                pendingAutoStartOnScreenOn = isChecked
                checkChanges()
                updateSettingsList()
            }
        ))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "listenForUsbDevices",
            nameResId = R.string.listen_for_usb_devices_label,
            descriptionResId = R.string.listen_for_usb_devices_description,
            isChecked = pendingListenForUsbDevices!!,
            onCheckedChanged = { isChecked ->
                pendingListenForUsbDevices = isChecked
                checkChanges()
                updateSettingsList()
            }
        ))

        items.add(SettingItem.ToggleSettingEntry(
            stableId = "autoStartUsb",
            nameResId = R.string.auto_start_usb_label,
            descriptionResId = R.string.auto_start_usb_description,
            isChecked = pendingAutoStartOnUsb!!,
            onCheckedChanged = { isChecked ->
                pendingAutoStartOnUsb = isChecked
                checkChanges()
                updateSettingsList()
            }
        ))

        if (pendingAutoStartOnUsb == true) {
            items.add(SettingItem.ToggleSettingEntry(
                stableId = "reopenOnReconnection",
                nameResId = R.string.reopen_on_reconnection_label,
                descriptionResId = R.string.reopen_on_reconnection_description,
                isChecked = pendingReopenOnReconnection!!,
                onCheckedChanged = { isChecked ->
                    pendingReopenOnReconnection = isChecked
                    checkChanges()
                }
            ))
        }

        items.add(SettingItem.SettingEntry(
            stableId = "autoStartBt",
            nameResId = R.string.auto_start_bt_label,
            value = if (pendingAutoStartBtName.isNullOrEmpty()) getString(R.string.bt_device_not_set) else pendingAutoStartBtName!!,
            onClick = {
                showBluetoothDeviceSelector()
            }
        ))

        settingsAdapter.submitList(items) {
            scrollState?.let { recyclerView.layoutManager?.onRestoreInstanceState(it) }
        }
    }

    override fun onResume() {
        super.onResume()
        // Re-check overlay permission. If the user returned from system settings
        // without granting it, disable auto-start settings that require it.
        if (Build.VERSION.SDK_INT >= 23 && !android.provider.Settings.canDrawOverlays(requireContext())) {
            var disabled = false
            if (settings.autoStartOnBoot) {
                settings.autoStartOnBoot = false
                pendingAutoStartOnBoot = false
                disabled = true
            }
            if (settings.autoStartOnScreenOn) {
                settings.autoStartOnScreenOn = false
                Settings.syncAutoStartOnScreenOnToDeviceStorage(requireContext(), false)
                pendingAutoStartOnScreenOn = false
                disabled = true
            }
            if (settings.autoStartOnUsb) {
                settings.autoStartOnUsb = false
                pendingAutoStartOnUsb = false
                disabled = true
            }
            if (!settings.autoStartBluetoothDeviceMac.isNullOrEmpty()) {
                settings.autoStartBluetoothDeviceMac = ""
                settings.autoStartBluetoothDeviceName = ""
                pendingAutoStartBtMac = ""
                pendingAutoStartBtName = ""
                disabled = true
            }
            if (disabled) {
                AppLog.w("Overlay permission not granted, disabling auto-start settings")
                Toast.makeText(requireContext(), getString(R.string.overlay_permission_denied_auto_start_disabled), Toast.LENGTH_LONG).show()
                checkChanges()
                updateSettingsList()
            }
        }
    }

    private fun showBluetoothDeviceSelector() {
        if (Build.VERSION.SDK_INT >= 31 && ContextCompat.checkSelfPermission(requireContext(), android.Manifest.permission.BLUETOOTH_CONNECT) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            bluetoothPermissionLauncher.launch(android.Manifest.permission.BLUETOOTH_CONNECT)
            return
        }

        val adapter = if (Build.VERSION.SDK_INT >= 18) {
            (requireContext().getSystemService(Context.BLUETOOTH_SERVICE) as android.bluetooth.BluetoothManager).adapter
        } else {
            @Suppress("DEPRECATION")
            android.bluetooth.BluetoothAdapter.getDefaultAdapter()
        }

        if (adapter == null || !adapter.isEnabled) {
            val enableIntent = Intent(android.bluetooth.BluetoothAdapter.ACTION_REQUEST_ENABLE)
            bluetoothEnableLauncher.launch(enableIntent)
            return
        }

        val bondedDevices = adapter.bondedDevices.toList()

        if (bondedDevices.isEmpty()) {
            Toast.makeText(requireContext(), "No paired Bluetooth devices found", Toast.LENGTH_LONG).show()
            return
        }

        val deviceNames = bondedDevices.map { it.name ?: "Unknown Device" }.toTypedArray()

        MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(R.string.select_bt_device)
            .setItems(deviceNames) { _, which ->
                val device = bondedDevices[which]
                pendingAutoStartBtMac = device.address
                pendingAutoStartBtName = device.name
                checkChanges()
                updateSettingsList()
            }
            .setNeutralButton(R.string.remove) { _, _ ->
                pendingAutoStartBtMac = ""
                pendingAutoStartBtName = ""
                checkChanges()
                updateSettingsList()
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    private fun showBluetoothPermissionDeniedDialog() {
        MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(R.string.bt_permission_denied_title)
            .setMessage(R.string.bt_permission_denied_message)
            .setPositiveButton(R.string.open_settings) { _, _ ->
                val intent = Intent(
                    android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                    android.net.Uri.parse("package:${requireContext().packageName}")
                )
                startActivity(intent)
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/NetworkListFragment.kt`:

```kt
package com.andrerinas.headunitrevived.main

import com.google.android.material.dialog.MaterialAlertDialogBuilder
import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Build
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.ProgressBar
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentManager
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import android.content.Intent
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.andrerinas.headunitrevived.App
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.connection.NetworkDiscovery
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.Settings
import com.andrerinas.headunitrevived.utils.changeLastBit
import com.andrerinas.headunitrevived.utils.toInetAddress
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import java.net.Inet4Address
import java.net.InetAddress

class NetworkListFragment : Fragment(), NetworkDiscovery.Listener {
    private lateinit var adapter: AddressAdapter
    private lateinit var connectivityManager: ConnectivityManager
    private lateinit var toolbar: MaterialToolbar
    private lateinit var networkDiscovery: NetworkDiscovery

    private var networkCallback: ConnectivityManager.NetworkCallback? = null
    private val ADD_ITEM_ID = 1002
    private val SCAN_ITEM_ID = 1003
    private var scanDialog: androidx.appcompat.app.AlertDialog? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        networkDiscovery = NetworkDiscovery(requireContext(), this)
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        val view = inflater.inflate(R.layout.fragment_list, container, false)
        val recyclerView = view.findViewById<RecyclerView>(android.R.id.list)
        toolbar = view.findViewById(R.id.toolbar)

        connectivityManager = requireContext().getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            networkCallback = object : ConnectivityManager.NetworkCallback() {
                override fun onAvailable(network: Network) {
                    updateCurrentAddress()
                }

                override fun onLost(network: Network) {
                    updateCurrentAddress()
                }
            }
        }

        adapter = AddressAdapter(requireContext(), childFragmentManager, viewLifecycleOwner.lifecycleScope)
        recyclerView.layoutManager = LinearLayoutManager(context)
        recyclerView.adapter = adapter

        recyclerView.setPadding(
            resources.getDimensionPixelSize(R.dimen.list_padding),
            resources.getDimensionPixelSize(R.dimen.list_padding),
            resources.getDimensionPixelSize(R.dimen.list_padding),
            resources.getDimensionPixelSize(R.dimen.list_padding)
        )
        recyclerView.clipToPadding = false

        return view
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        toolbar.title = getString(R.string.wifi)
        toolbar.setNavigationOnClickListener {
            findNavController().popBackStack()
        }
        toolbar.navigationIcon = androidx.core.content.ContextCompat.getDrawable(requireContext(), R.drawable.ic_arrow_back_white)

        setupToolbarMenu()
    }

    private fun setupToolbarMenu() {
        // Scan Button (Custom Layout)
        val scanItem = toolbar.menu.add(0, SCAN_ITEM_ID, 0, getString(R.string.scan))
        scanItem.setShowAsAction(android.view.MenuItem.SHOW_AS_ACTION_ALWAYS)
        scanItem.setActionView(R.layout.layout_scan_button)

        val scanButton = scanItem.actionView?.findViewById<MaterialButton>(R.id.scan_button_widget)
        scanButton?.setOnClickListener {
            startScan()
        }

        // Add Button (Custom Layout)
        val addItem = toolbar.menu.add(0, ADD_ITEM_ID, 0, getString(R.string.add_new))
        addItem.setShowAsAction(android.view.MenuItem.SHOW_AS_ACTION_ALWAYS)
        addItem.setActionView(R.layout.layout_add_button)

        val addButton = addItem.actionView?.findViewById<MaterialButton>(R.id.add_button_widget)
        addButton?.setOnClickListener {
            showAddAddressDialog()
        }
    }

    private fun startScan() {
        showScanDialog()
        networkDiscovery.startScan()
    }

    private fun showScanDialog() {
        val builder = MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
        val progressBar = ProgressBar(requireContext())
        progressBar.setPadding(32, 32, 32, 32)
        builder.setView(progressBar)
        builder.setTitle(R.string.scanning_network)
        builder.setNegativeButton(R.string.cancel) { _, _ ->
             networkDiscovery.stop()
        }
        builder.setCancelable(false)
        scanDialog = builder.show()

        scanDialog?.window?.decorView?.apply {
            @Suppress("DEPRECATION")
            systemUiVisibility = (View.SYSTEM_UI_FLAG_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN)
        }
    }

    override fun onServiceFound(ip: String, port: Int, socket: java.net.Socket?) {
        if (port != 5277) {
            // Only interested in Headunit Server (5277) for manual connection list
            // Ignore WifiLauncher (5289) here as that is for auto-triggering
            try { socket?.close() } catch (e: Exception) {}
            return
        }
        activity?.runOnUiThread {
            // Save immediately so it stays in the list permanently
            try {
                AppLog.i("Found Infotainment Device. Try to connect to $ip:$port")
                adapter.addNewAddress(InetAddress.getByName(ip))
            } catch (e: Exception) {
                AppLog.e("Failed to add discovered address", e)
            }

            // Auto-connect to the first found device during a manual scan
            if (scanDialog?.isShowing == true) {
                scanDialog?.dismiss()
                Toast.makeText(context, getString(R.string.found_connecting, ip), Toast.LENGTH_SHORT).show()

                val ctx = context ?: return@runOnUiThread
                lifecycleScope.launch(Dispatchers.IO) {
                    if (socket != null && socket.isConnected)
                        App.provide(ctx).commManager.connect(socket)
                    else
                        App.provide(ctx).commManager.connect(ip, 5277)
                    ContextCompat.startForegroundService(ctx, Intent(ctx, AapService::class.java).apply {
                        action = AapService.ACTION_CONNECT_SOCKET
                    })
                }
            } else {
                // If not in auto-connect dialog, close the probe socket
                try { socket?.close() } catch (e: Exception) {}
            }
        }
    }

    override fun onScanFinished() {
        activity?.runOnUiThread {
            if (scanDialog?.isShowing == true) {
                scanDialog?.dismiss()
                if (adapter.addressList.size <= 2) { // Only localhost and current IP
                    Toast.makeText(context, getString(R.string.no_devices_found), Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun showAddAddressDialog() {
        var ip: InetAddress? = null
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            val activeNetwork = connectivityManager.activeNetwork
            val linkProperties = connectivityManager.getLinkProperties(activeNetwork)
            ip = linkProperties?.linkAddresses?.find { it.address is Inet4Address }?.address
        } else {
            val wifiManager = App.provide(requireContext()).wifiManager
            @Suppress("DEPRECATION")
            val currentIp = wifiManager.connectionInfo.ipAddress
            if (currentIp != 0) {
                ip = currentIp.toInetAddress()
            }
        }
        com.andrerinas.headunitrevived.main.AddNetworkAddressDialog.show(ip, childFragmentManager)
    }

    override fun onResume() {
        super.onResume()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            val request = NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .build()
            networkCallback?.let {
                connectivityManager.registerNetworkCallback(request, it)
            }
        }
        updateCurrentAddress()
    }

    override fun onPause() {
        super.onPause()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            networkCallback?.let {
                connectivityManager.unregisterNetworkCallback(it)
            }
        }
    }

    private fun updateCurrentAddress() {
        var ipAddress: InetAddress? = null
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) { // API 23+ (for getActiveNetwork)
            val activeNetwork = connectivityManager.activeNetwork
            val linkProperties = connectivityManager.getLinkProperties(activeNetwork)
            ipAddress = linkProperties?.linkAddresses?.find { it.address is Inet4Address }?.address
        } else { // API 19, 20, 21, 22
            val wifiManager = App.provide(requireContext()).wifiManager
            @Suppress("DEPRECATION")
            val currentIp = wifiManager.connectionInfo.ipAddress
            if (currentIp != 0) {
                ipAddress = currentIp.toInetAddress()
            }
        }

        activity?.runOnUiThread {
            adapter.currentAddress = ipAddress?.changeLastBit(1)?.hostAddress ?: ""
            adapter.loadAddresses()
        }
    }

    fun addAddress(ip: InetAddress) {
        adapter.addNewAddress(ip)
    }

    private class DeviceViewHolder internal constructor(itemView: View) : RecyclerView.ViewHolder(itemView) {
        internal val removeButton = itemView.findViewById<Button>(android.R.id.button1)
        internal val startButton = itemView.findViewById<Button>(android.R.id.button2)
    }

    private class AddressAdapter(
        private val context: Context,
        private val fragmentManager: FragmentManager,
        private val scope: kotlinx.coroutines.CoroutineScope
    ) : RecyclerView.Adapter<DeviceViewHolder>(), View.OnClickListener {

        val addressList = ArrayList<String>()
        var currentAddress: String = ""
        private val settings: Settings = Settings(context)

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): DeviceViewHolder {
            val view = LayoutInflater.from(context).inflate(R.layout.list_item_device, parent, false)
            val holder = DeviceViewHolder(view)

            holder.startButton.setOnClickListener(this)
            holder.removeButton.setOnClickListener(this)
            holder.removeButton.setText(R.string.remove)
            return holder
        }

        override fun onBindViewHolder(holder: DeviceViewHolder, position: Int) {
            val ipAddress = addressList[position]

            val isTop = position == 0
            val isBottom = position == itemCount - 1

            val bgRes = when {
                isTop && isBottom -> R.drawable.bg_setting_single
                isTop -> R.drawable.bg_setting_top
                isBottom -> R.drawable.bg_setting_bottom
                else -> R.drawable.bg_setting_middle
            }
            holder.itemView.setBackgroundResource(bgRes)


            val line1: String = ipAddress
            holder.removeButton.visibility = if (ipAddress == "127.0.0.1" || (currentAddress.isNotEmpty() && ipAddress == currentAddress)) View.GONE else View.VISIBLE

            holder.startButton.setTag(R.integer.key_position, position)
            holder.startButton.text = line1
            holder.startButton.setTag(R.integer.key_data, ipAddress)
            holder.removeButton.setTag(R.integer.key_data, ipAddress)
        }

        override fun getItemCount(): Int {
            return addressList.size
        }

        override fun onClick(v: View) {
            if (v.id == android.R.id.button2) {
                val ip = v.getTag(R.integer.key_data) as String
                ContextCompat.startForegroundService(context, Intent(context, AapService::class.java).apply {
                    action = AapService.ACTION_CONNECT_SOCKET
                })
                scope.launch(Dispatchers.IO) { App.provide(context).commManager.connect(ip, 5277) }
            } else {
                this.removeAddress(v.getTag(R.integer.key_data) as String)
            }
        }

        internal fun addNewAddress(ip: InetAddress) {
            val newAddrs = HashSet(settings.networkAddresses)
            if (newAddrs.add(ip.hostAddress)) {
                settings.networkAddresses = newAddrs
                loadAddresses()
            }
        }

        internal fun loadAddresses() {
            set(settings.networkAddresses)
        }

        private fun set(addrs: Collection<String>) {
            addressList.clear()
            addressList.add("127.0.0.1")
            if (currentAddress.isNotEmpty()) {
                if (!addressList.contains(currentAddress)) {
                    addressList.add(currentAddress)
                }
            }
            addressList.addAll(addrs.filterNotNull())

            // Deduplicate
            val uniqueList = addressList.distinct()
            addressList.clear()
            addressList.addAll(uniqueList)

            notifyDataSetChanged()
        }

        private fun removeAddress(ipAddress: String) {
            val newAddrs = HashSet(settings.networkAddresses)
            if (newAddrs.remove(ipAddress)) {
                settings.networkAddresses = newAddrs
            }
            set(newAddrs)
        }
    }

    companion object {
        const val TAG = "NetworkListFragment"
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/SafetyDisclaimerDialog.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.os.Build
import android.os.Bundle
import android.text.Html
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.TextView
import androidx.fragment.app.DialogFragment
import androidx.fragment.app.FragmentManager
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.R

class SafetyDisclaimerDialog : DialogFragment() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // Use custom theme for transparent background (CardView handles background)
        setStyle(STYLE_NO_TITLE, R.style.SafetyDialogTheme)
        isCancelable = false // Prevent back button close
    }

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        val view = inflater.inflate(R.layout.dialog_safety_disclaimer, container, false)

        val messageView = view.findViewById<TextView>(R.id.disclaimer_message)
        val btnAccept = view.findViewById<Button>(R.id.btn_accept)

        // Load and format text
        val rawText = getString(R.string.disclaimer_text)
        messageView.text = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
            Html.fromHtml(rawText, Html.FROM_HTML_MODE_LEGACY)
        } else {
            @Suppress("DEPRECATION")
            Html.fromHtml(rawText)
        }

        btnAccept.setOnClickListener {
            // Save setting
            App.provide(requireContext()).settings.hasAcceptedDisclaimer = true
            dismiss()
            (activity as? MainActivity)?.checkSetupFlow()
        }

        return view
    }

    override fun onStart() {
        super.onStart()
        // Make dialog 90% width on phones, less on tablets?
        // Or just let layout handle it?
        // DialogFragment often defaults to WRAP_CONTENT but limited width.
        // Let's set a min width.
        dialog?.window?.setLayout(
            (resources.displayMetrics.widthPixels * 0.9).toInt(),
            ViewGroup.LayoutParams.WRAP_CONTENT
        )
    }

    companion object {
        const val TAG = "SafetyDisclaimerDialog"

        fun show(manager: FragmentManager) {
            if (manager.findFragmentByTag(TAG) == null) {
                SafetyDisclaimerDialog().show(manager, TAG)
            }
        }
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/main/AutomationActivity.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.os.Bundle
import android.content.Intent
import android.os.Build
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.lifecycle.lifecycleScope
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.Settings
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

/**
 * A transparent activity that handles App Shortcuts and Deep Links.
 * It translates incoming intents into service actions for AapService.
 */
class AutomationActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Invisible activity
        window.setBackgroundDrawableResource(android.R.color.transparent)

        val data = intent.data
        val action = intent.action

        AppLog.i("AutomationActivity: Received intent. Action: $action, Data: $data")

        if (data?.scheme == "headunit") {
            handleUri(data)
        } else {
            val state = intent.getStringExtra("state")
            handleAction(action, state)
        }

        finish()
    }

    private fun handleUri(data: android.net.Uri) {
        when (data.host) {
            "connect" -> {
                val ip = data.getQueryParameter("ip")
                if (!ip.isNullOrEmpty()) {
                    ContextCompat.startForegroundService(this, Intent(this, AapService::class.java).apply {
                        action = AapService.ACTION_CONNECT_SOCKET
                    })
                    lifecycleScope.launch(Dispatchers.IO) { App.provide(this@AutomationActivity).commManager.connect(ip, 5277) }
                } else {
                    val autoIntent = Intent(this, AapService::class.java).apply {
                        this.action = AapService.ACTION_CHECK_USB
                    }
                    ContextCompat.startForegroundService(this, autoIntent)
                }
            }
            "disconnect" -> {
                val stopIntent = Intent(this, AapService::class.java).apply {
                    this.action = AapService.ACTION_DISCONNECT
                }
                ContextCompat.startForegroundService(this, stopIntent)
            }
            "exit" -> {
                val exitIntent = Intent(this, AapService::class.java).apply {
                    this.action = AapService.ACTION_STOP_SERVICE
                }
                ContextCompat.startForegroundService(this, exitIntent)
                // Broadcast a finish request to close MainActivity if it's open
                sendBroadcast(Intent("com.andrerinas.headunitrevived.ACTION_FINISH_ACTIVITIES"))
            }
            "nightmode" -> {
                val state = data.getQueryParameter("state")
                applyNightMode(state)
            }
        }
    }

    private fun handleAction(incomingAction: String?, incomingState: String?) {
        when (incomingAction) {
            "com.andrerinas.headunitrevived.ACTION_SET_NIGHT_MODE" -> applyNightMode(incomingState)
            "com.andrerinas.headunitrevived.ACTION_CONNECT" -> {
                val autoIntent = Intent(this, AapService::class.java).apply {
                    this.action = AapService.ACTION_CHECK_USB
                }
                ContextCompat.startForegroundService(this, autoIntent)
            }
            "com.andrerinas.headunitrevived.ACTION_DISCONNECT" -> {
                val stopIntent = Intent(this, AapService::class.java).apply {
                    this.action = AapService.ACTION_DISCONNECT
                }
                ContextCompat.startForegroundService(this, stopIntent)
            }
            "com.andrerinas.headunitrevived.ACTION_START_SELF_MODE" -> {
                val selfIntent = Intent(this, AapService::class.java).apply {
                    this.action = AapService.ACTION_START_SELF_MODE
                }
                ContextCompat.startForegroundService(this, selfIntent)
            }
            "com.andrerinas.headunitrevived.ACTION_STOP_SERVICE",
            "com.andrerinas.headunitrevived.ACTION_EXIT" -> {
                val exitIntent = Intent(this, AapService::class.java).apply {
                    this.action = AapService.ACTION_STOP_SERVICE
                }
                ContextCompat.startForegroundService(this, exitIntent)
                sendBroadcast(Intent("com.andrerinas.headunitrevived.ACTION_FINISH_ACTIVITIES").apply {
                    setPackage(packageName)
                })
            }
        }
    }

    private fun applyNightMode(state: String?) {
        val appSettings = App.provide(this).settings
        when (state?.lowercase()) {
            "day" -> appSettings.nightMode = Settings.NightMode.DAY
            "night" -> appSettings.nightMode = Settings.NightMode.NIGHT
            "auto" -> appSettings.nightMode = Settings.NightMode.AUTO
        }
        val updateIntent = Intent(this, AapService::class.java).apply {
            this.action = AapService.ACTION_REQUEST_NIGHT_MODE_UPDATE
        }
        ContextCompat.startForegroundService(this, updateIntent)
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/KeymapFragment.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.app.Activity
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.KeyEvent
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import android.widget.Toast
import androidx.fragment.app.Fragment
import androidx.core.content.ContextCompat
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.contract.KeyIntent
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.IntentFilters
import com.andrerinas.headunitrevived.utils.Settings
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder

class KeymapFragment : Fragment(), MainActivity.KeyListener {

    private lateinit var toolbar: MaterialToolbar
    private lateinit var recyclerView: RecyclerView
    private lateinit var keypressDebuggerTextView: TextView
    private lateinit var adapter: KeymapAdapter
    private lateinit var settings: Settings
    private val RESET_ITEM_ID = 1003

    private var assignTargetCode = KeyEvent.KEYCODE_UNKNOWN
    private var assignDialog: androidx.appcompat.app.AlertDialog? = null

    data class KeymapItem(val nameResId: Int, val keyCode: Int)

    private val keyList = listOf(
        KeymapItem(R.string.key_soft_left, KeyEvent.KEYCODE_SOFT_LEFT),
        KeymapItem(R.string.key_soft_right, KeyEvent.KEYCODE_SOFT_RIGHT),
        KeymapItem(R.string.key_dpad_up, KeyEvent.KEYCODE_DPAD_UP),
        KeymapItem(R.string.key_dpad_down, KeyEvent.KEYCODE_DPAD_DOWN),
        KeymapItem(R.string.key_dpad_left, KeyEvent.KEYCODE_DPAD_LEFT),
        KeymapItem(R.string.key_dpad_right, KeyEvent.KEYCODE_DPAD_RIGHT),
        KeymapItem(R.string.key_dpad_center, KeyEvent.KEYCODE_DPAD_CENTER),
        KeymapItem(R.string.key_media_play, KeyEvent.KEYCODE_MEDIA_PLAY),
        KeymapItem(R.string.key_media_pause, KeyEvent.KEYCODE_MEDIA_PAUSE),
        KeymapItem(R.string.key_media_play_pause, KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE),
        KeymapItem(R.string.key_media_next, KeyEvent.KEYCODE_MEDIA_NEXT),
        KeymapItem(R.string.key_media_prev, KeyEvent.KEYCODE_MEDIA_PREVIOUS),
        KeymapItem(R.string.key_search, KeyEvent.KEYCODE_SEARCH),
        KeymapItem(R.string.key_call, KeyEvent.KEYCODE_CALL),
        KeymapItem(R.string.key_endcall, KeyEvent.KEYCODE_ENDCALL),
        KeymapItem(R.string.key_music, KeyEvent.KEYCODE_MUSIC),
        KeymapItem(R.string.key_nav, KeyEvent.KEYCODE_GUIDE),
        KeymapItem(R.string.key_night, KeyEvent.KEYCODE_N)
    )

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View? {
        return inflater.inflate(R.layout.fragment_keymap, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        settings = Settings(requireContext())

        keypressDebuggerTextView = view.findViewById(R.id.keypress_debugger_text)
        toolbar = view.findViewById(R.id.toolbar)
        recyclerView = view.findViewById(R.id.recycler_view)

        recyclerView.layoutManager = LinearLayoutManager(requireContext())
        adapter = KeymapAdapter(keyList, settings.keyCodes) { item ->
            showAssignDialog(item)
        }
        recyclerView.adapter = adapter

        setupToolbar()

        // Ensure fragment can receive key events
        view.isFocusableInTouchMode = true
        view.requestFocus()
        view.setOnKeyListener { _, _, event ->
            onKeyEvent(event)
        }
    }

    private fun setupToolbar() {
        toolbar.setNavigationOnClickListener {
            findNavController().popBackStack()
        }

        val resetItem = toolbar.menu.add(0, RESET_ITEM_ID, 0, getString(R.string.reset))
        resetItem.setShowAsAction(android.view.MenuItem.SHOW_AS_ACTION_ALWAYS)
        resetItem.setActionView(R.layout.layout_reset_button)

        val resetButton = resetItem.actionView?.findViewById<MaterialButton>(R.id.reset_button_widget)
        resetButton?.setOnClickListener {
            settings.keyCodes = mutableMapOf()
            adapter.updateCodes(settings.keyCodes)
            Toast.makeText(requireContext(), getString(R.string.key_mappings_reset), Toast.LENGTH_SHORT).show()
        }
    }

    private fun showAssignDialog(item: KeymapItem) {
        assignTargetCode = item.keyCode
        val name = getString(item.nameResId)

        // Using MaterialAlertDialogBuilder with DarkAlertDialog style for consistency and compatibility
        assignDialog = MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
            .setTitle(name)
            .setMessage(getString(R.string.press_key_to_assign, name))
            .setNegativeButton(R.string.cancel) { dialog, _ ->
                assignTargetCode = KeyEvent.KEYCODE_UNKNOWN
                dialog.dismiss()
            }
            .setOnDismissListener {
                assignTargetCode = KeyEvent.KEYCODE_UNKNOWN
                assignDialog = null
            }
            .create()

        // Important: We need to set the listener on the dialog to catch keys like ENTER
        assignDialog?.setOnKeyListener { _, _, event ->
            onKeyEvent(event)
        }

        assignDialog?.show()
    }

    private val keyCodeReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == "com.andrerinas.headunitrevived.DEBUG_KEY") {
                val action = (intent.getStringExtra("action") ?: "unknown").replace("com.andrerinas.headunitrevived.", "")

                // Try to find a keycode in various common extras
                val extractedCode = intent.getIntExtra("keyCode", -1).takeIf { it != -1 }
                    ?: intent.getByteExtra("extra_key_value", 0).toInt().takeIf { it != 0 }
                    ?: intent.getIntExtra("CLICK_KEY", -1).takeIf { it != -1 }
                    ?: intent.getIntExtra("com.winca.service.Setting.KEY_ACTION_EXTRA", -1).takeIf { it != -1 }

                val extras = intent.extras?.keySet()
                    ?.filter { it != "action" && it != "keyCode" && it != "extra_key_value" }
                    ?.joinToString { "$it=${intent.extras?.get(it)}" } ?: ""

                val codeText = if (extractedCode != null) "CODE: $extractedCode" else "NO CODE"
                val displayText = "Action: $action\n$codeText\n$extras"

                keypressDebuggerTextView.text = displayText
                keypressDebuggerTextView.setTextColor(ContextCompat.getColor(requireContext(), R.color.brand_teal))
                return
            }

            val event: KeyEvent? = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableExtra(KeyIntent.extraEvent, KeyEvent::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(KeyIntent.extraEvent)
            }
            onKeyEvent(event)
        }
    }

    override fun onResume() {
        super.onResume()
        val filter = IntentFilters.keyEvent
        filter.addAction("com.andrerinas.headunitrevived.DEBUG_KEY")
        ContextCompat.registerReceiver(requireContext(), keyCodeReceiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED)
    }

    override fun onPause() {
        super.onPause()
        context?.unregisterReceiver(keyCodeReceiver)
    }

    override fun onAttach(context: Context) {
        super.onAttach(context)
        (activity as? MainActivity)?.let {
            it.setDefaultKeyMode(Activity.DEFAULT_KEYS_DISABLE)
            it.keyListener = this
        }
    }

    override fun onDetach() {
        (activity as? MainActivity)?.let {
            it.setDefaultKeyMode(Activity.DEFAULT_KEYS_SHORTCUT)
            it.keyListener = null
        }
        super.onDetach()
    }

    override fun onKeyEvent(event: KeyEvent?): Boolean {
        if (event == null) return false

        val keyCode = event.keyCode
        // Only ignore BACK if we are NOT in assignment mode
        if (keyCode == KeyEvent.KEYCODE_BACK && assignTargetCode == KeyEvent.KEYCODE_UNKNOWN) return false

        val keyName = try { KeyEvent.keyCodeToString(keyCode).replace("KEYCODE_", "") } catch (e: Exception) { "UNKNOWN" }
        val actionName = if (event.action == KeyEvent.ACTION_DOWN) "DOWN" else "UP"

        AppLog.i("KeymapFragment: Captured $keyName ($keyCode) $actionName")

        keypressDebuggerTextView.text = "Key: $keyName ($keyCode) - $actionName"
        keypressDebuggerTextView.setTextColor(ContextCompat.getColor(requireContext(), R.color.brand_teal))

        if (assignTargetCode != KeyEvent.KEYCODE_UNKNOWN) {
            // Only finalize assignment on ACTION_UP to avoid accidental multiple mappings
            if (event.action == KeyEvent.ACTION_UP) {
                val codesMap = settings.keyCodes

                // Map: Logical (AA) -> Physical (HW)
                codesMap[assignTargetCode] = keyCode

                settings.keyCodes = codesMap
                adapter.updateCodes(codesMap)

                val targetName = getString(keyList.find { it.keyCode == assignTargetCode }?.nameResId ?: R.string.keymap)
                Toast.makeText(requireContext(), getString(R.string.key_assigned, keyName, targetName), Toast.LENGTH_SHORT).show()

                assignDialog?.dismiss()
            }
            return true
        }

        return false
    }

    inner class KeymapAdapter(
        private val items: List<KeymapItem>,
        private var codesMap: Map<Int, Int>,
        private val onClick: (KeymapItem) -> Unit
    ) : RecyclerView.Adapter<KeymapAdapter.ViewHolder>() {

        inner class ViewHolder(view: View) : RecyclerView.ViewHolder(view) {
            val nameText: TextView = view.findViewById(R.id.key_name)
            val valueText: TextView = view.findViewById(R.id.key_value)
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
            val view = LayoutInflater.from(parent.context).inflate(R.layout.item_keymap, parent, false)
            return ViewHolder(view)
        }

        override fun onBindViewHolder(holder: ViewHolder, position: Int) {
            val item = items[position]
            holder.nameText.text = getString(item.nameResId)

            // Map: Logical -> Physical
            val physicalKey = codesMap[item.keyCode]

            if (physicalKey != null) {
                holder.valueText.text = KeyEvent.keyCodeToString(physicalKey).replace("KEYCODE_", "")
            } else {
                holder.valueText.text = getString(R.string.not_set)
            }

            holder.itemView.setOnClickListener { onClick(item) }
        }

        override fun getItemCount() = items.size

        fun updateCodes(newMap: Map<Int, Int>) {
            codesMap = newMap
            notifyDataSetChanged()
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/main/DarkModeFragment.kt`:

```kt
package com.andrerinas.headunitrevived.main

import android.app.AlertDialog
import android.app.TimePickerDialog
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.core.content.ContextCompat
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.andrerinas.headunitrevived.App
import com.andrerinas.headunitrevived.R
import com.andrerinas.headunitrevived.aap.AapService
import com.andrerinas.headunitrevived.main.settings.SettingItem
import com.andrerinas.headunitrevived.main.settings.SettingsAdapter
import com.andrerinas.headunitrevived.utils.AppThemeManager
import com.andrerinas.headunitrevived.utils.Settings
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Handler
import android.os.Looper
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.button.MaterialButton
import com.google.android.material.dialog.MaterialAlertDialogBuilder

class DarkModeFragment : Fragment(), SensorEventListener {
    private lateinit var settings: Settings
    private lateinit var recyclerView: RecyclerView
    private lateinit var settingsAdapter: SettingsAdapter
    private lateinit var toolbar: MaterialToolbar
    private var saveButton: MaterialButton? = null

    // Pending dark mode settings
    private var pendingAppTheme: Settings.AppTheme? = null
    private var pendingAppThemeThresholdLux: Int? = null
    private var pendingAppThemeThresholdBrightness: Int? = null
    private var pendingAppThemeManualStart: Int? = null
    private var pendingAppThemeManualEnd: Int? = null
    private var pendingMonochromeIcons: Boolean? = null
    private var pendingUseExtremeDarkMode: Boolean? = null
    private var pendingUseGradientBackground: Boolean? = null

    // Pending night mode settings (Android Auto)
    private var pendingNightMode: Settings.NightMode? = null
    private var pendingThresholdLux: Int? = null
    private var pendingThresholdBrightness: Int? = null
    private var pendingManualStart: Int? = null
    private var pendingManualEnd: Int? = null

    // Pending AA monochrome settings
    private var pendingAaMonochromeEnabled: Boolean? = null
    private var pendingAaDesaturationLevel: Int? = null

    // View mode (needed for GLES dialog)
    private var pendingViewMode: Settings.ViewMode? = null

    // Sensor for live lux reading
    private var cachedLux: Float = -1f
    private var sensorManager: SensorManager? = null
    private val refreshHandler = Handler(Looper.getMainLooper())
    private val refreshRunnable = Runnable {
        if (isAdded && ::settingsAdapter.isInitialized) {
            updateSettingsList()
        }
    }

    private var requiresRestart = false
    private var hasChanges = false
    private val SAVE_ITEM_ID = 1001

    override fun onCreateView(inflater: LayoutInflater, container: ViewGroup?, savedInstanceState: Bundle?): View {
        return inflater.inflate(R.layout.fragment_dark_mode, container, false)
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        settings = App.provide(requireContext()).settings

        // Initialize pending state from current values
        pendingAppTheme = settings.appTheme
        pendingAppThemeThresholdLux = settings.appThemeThresholdLux
        pendingAppThemeThresholdBrightness = settings.appThemeThresholdBrightness
        pendingAppThemeManualStart = settings.appThemeManualStart
        pendingAppThemeManualEnd = settings.appThemeManualEnd
        pendingMonochromeIcons = settings.monochromeIcons
        pendingUseExtremeDarkMode = settings.useExtremeDarkMode
        pendingUseGradientBackground = settings.useGradientBackground

        pendingNightMode = settings.nightMode
        pendingThresholdLux = settings.nightModeThresholdLux
        pendingThresholdBrightness = settings.nightModeThresholdBrightness
        pendingManualStart = settings.nightModeManualStart
        pendingManualEnd = settings.nightModeManualEnd

        pendingAaMonochromeEnabled = settings.aaMonochromeEnabled
        pendingAaDesaturationLevel = settings.aaDesaturationLevel

        pendingViewMode = settings.viewMode

        // Intercept system back button
        requireActivity().onBackPressedDispatcher.addCallback(viewLifecycleOwner, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                handleBackPress()
            }
        })

        toolbar = view.findViewById(R.id.toolbar)
        settingsAdapter = SettingsAdapter()
        recyclerView = view.findViewById(R.id.recycler_view)
        recyclerView.layoutManager = LinearLayoutManager(requireContext())
        recyclerView.adapter = settingsAdapter

        updateSettingsList()
        setupToolbar()
    }

    override fun onSensorChanged(event: SensorEvent) {
        if (event.sensor.type == Sensor.TYPE_LIGHT) {
            val newLux = event.values[0]
            if (kotlin.math.abs(newLux - cachedLux) >= 1.0f || cachedLux < 0f) {
                cachedLux = newLux
                scheduleListRefresh()
            }
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}

    private fun setupToolbar() {
        toolbar.setNavigationOnClickListener {
            handleBackPress()
        }

        val saveItem = toolbar.menu.add(0, SAVE_ITEM_ID, 0, getString(R.string.save))
        saveItem.setShowAsAction(android.view.MenuItem.SHOW_AS_ACTION_ALWAYS)
        saveItem.setActionView(R.layout.layout_save_button)

        saveButton = saveItem.actionView?.findViewById(R.id.save_button_widget)
        saveButton?.setOnClickListener {
            saveSettings()
        }

        updateSaveButtonState()
    }

    private fun handleBackPress() {
        if (hasChanges) {
            MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                .setTitle(R.string.unsaved_changes)
                .setMessage(R.string.unsaved_changes_message)
                .setPositiveButton(R.string.discard) { _, _ ->
                    navigateBack()
                }
                .setNegativeButton(R.string.cancel, null)
                .show()
        } else {
            navigateBack()
        }
    }

    private fun navigateBack() {
        try {
            val navController = findNavController()
            if (!navController.navigateUp()) {
                requireActivity().finish()
            }
        } catch (e: Exception) {
            requireActivity().finish()
        }
    }

    private fun updateSaveButtonState() {
        saveButton?.isEnabled = hasChanges
        saveButton?.text = if (requiresRestart) getString(R.string.save_and_restart) else getString(R.string.save)
    }

    private fun saveSettings() {
        // Detect changes BEFORE saving values to SharedPreferences
        val themeChanged = pendingAppTheme != settings.appTheme
        val appThemeThresholdChanged = pendingAppThemeThresholdLux != settings.appThemeThresholdLux ||
                pendingAppThemeThresholdBrightness != settings.appThemeThresholdBrightness ||
                pendingAppThemeManualStart != settings.appThemeManualStart ||
                pendingAppThemeManualEnd != settings.appThemeManualEnd
        val gradientChanged = pendingUseGradientBackground != settings.useGradientBackground
        val extremeDarkChanged = pendingUseExtremeDarkMode != settings.useExtremeDarkMode
        val monochromeIconsChanged = pendingMonochromeIcons != settings.monochromeIcons
        val viewModeChanged = pendingViewMode != settings.viewMode

        // Save night mode settings
        pendingNightMode?.let { settings.nightMode = it }
        pendingThresholdLux?.let { settings.nightModeThresholdLux = it }
        pendingThresholdBrightness?.let { settings.nightModeThresholdBrightness = it }
        pendingManualStart?.let { settings.nightModeManualStart = it }
        pendingManualEnd?.let { settings.nightModeManualEnd = it }

        // Save app theme settings
        pendingAppThemeThresholdLux?.let { settings.appThemeThresholdLux = it }
        pendingAppThemeThresholdBrightness?.let { settings.appThemeThresholdBrightness = it }
        pendingAppThemeManualStart?.let { settings.appThemeManualStart = it }
        pendingAppThemeManualEnd?.let { settings.appThemeManualEnd = it }
        pendingMonochromeIcons?.let { settings.monochromeIcons = it }
        pendingUseExtremeDarkMode?.let { settings.useExtremeDarkMode = it }
        pendingUseGradientBackground?.let { settings.useGradientBackground = it }

        // Save AA monochrome settings
        pendingAaMonochromeEnabled?.let { settings.aaMonochromeEnabled = it }
        pendingAaDesaturationLevel?.let { settings.aaDesaturationLevel = it }

        // Save view mode if changed (from GLES dialog)
        if (viewModeChanged) {
            pendingViewMode?.let { settings.viewMode = it }
        }

        pendingAppTheme?.let { newTheme ->
            settings.appTheme = newTheme
            if (themeChanged || (appThemeThresholdChanged && !AppThemeManager.isStaticMode(newTheme))) {
                // Stop existing auto theme manager
                App.appThemeManager?.stop()
                App.appThemeManager = null

                if (AppThemeManager.isStaticMode(newTheme)) {
                    AppThemeManager.applyStaticTheme(settings)
                } else {
                    val manager = AppThemeManager(requireContext().applicationContext, settings)
                    App.appThemeManager = manager
                    manager.start()
                }
            }
        }

        // Notify Service about Night Mode changes immediately
        val nightModeUpdateIntent = Intent(AapService.ACTION_REQUEST_NIGHT_MODE_UPDATE)
        nightModeUpdateIntent.setPackage(requireContext().packageName)
        requireContext().sendBroadcast(nightModeUpdateIntent)

        if (requiresRestart) {
            if (App.provide(requireContext()).commManager.isConnected) {
                Toast.makeText(context, getString(R.string.stopping_service), Toast.LENGTH_SHORT).show()
                val stopServiceIntent = Intent(requireContext(), AapService::class.java).apply {
                    action = AapService.ACTION_STOP_SERVICE
                }
                ContextCompat.startForegroundService(requireContext(), stopServiceIntent)
            }
        }

        // Reset change tracking
        hasChanges = false
        requiresRestart = false
        updateSaveButtonState()

        Toast.makeText(context, getString(R.string.settings_saved), Toast.LENGTH_SHORT).show()

        // Signal visual change so all activities (including MainActivity) pick up changes
        if (gradientChanged || extremeDarkChanged || monochromeIconsChanged || themeChanged ||
            (appThemeThresholdChanged && pendingAppTheme?.let { !AppThemeManager.isStaticMode(it) } == true)) {
            AppThemeManager.signalVisualChange()
        }
    }

    private fun checkChanges() {
        val anyChange = pendingAppTheme != settings.appTheme ||
                pendingAppThemeThresholdLux != settings.appThemeThresholdLux ||
                pendingAppThemeThresholdBrightness != settings.appThemeThresholdBrightness ||
                pendingAppThemeManualStart != settings.appThemeManualStart ||
                pendingAppThemeManualEnd != settings.appThemeManualEnd ||
                pendingMonochromeIcons != settings.monochromeIcons ||
                pendingUseExtremeDarkMode != settings.useExtremeDarkMode ||
                pendingUseGradientBackground != settings.useGradientBackground ||
                pendingNightMode != settings.nightMode ||
                pendingThresholdLux != settings.nightModeThresholdLux ||
                pendingThresholdBrightness != settings.nightModeThresholdBrightness ||
                pendingManualStart != settings.nightModeManualStart ||
                pendingManualEnd != settings.nightModeManualEnd ||
                pendingAaMonochromeEnabled != settings.aaMonochromeEnabled ||
                pendingAaDesaturationLevel != settings.aaDesaturationLevel ||
                pendingViewMode != settings.viewMode

        hasChanges = anyChange

        // View mode change requires restart
        requiresRestart = pendingViewMode != settings.viewMode

        updateSaveButtonState()
    }

    private fun updateSettingsList() {
        val scrollState = recyclerView.layoutManager?.onSaveInstanceState()
        val items = mutableListOf<SettingItem>()

        // --- App Theme ---
        items.add(SettingItem.CategoryHeader("appTheme", R.string.app_theme))

        val appThemeTitles = resources.getStringArray(R.array.app_theme)
        items.add(SettingItem.SettingEntry(
            stableId = "appTheme",
            nameResId = R.string.app_theme,
            value = appThemeTitles[pendingAppTheme!!.value],
            onClick = { _ ->
                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                    .setTitle(R.string.change_app_theme)
                    .setSingleChoiceItems(appThemeTitles, pendingAppTheme!!.value) { dialog, which ->
                        pendingAppTheme = Settings.AppTheme.fromInt(which)
                        if (pendingAppTheme == Settings.AppTheme.EXTREME_DARK) {
                            pendingMonochromeIcons = true
                        } else if (pendingAppTheme == Settings.AppTheme.CLEAR) {
                            pendingMonochromeIcons = false
                        }
                        // Reset useExtremeDarkMode for static modes
                        if (pendingAppTheme == Settings.AppTheme.CLEAR ||
                            pendingAppTheme == Settings.AppTheme.DARK ||
                            pendingAppTheme == Settings.AppTheme.EXTREME_DARK) {
                            pendingUseExtremeDarkMode = false
                        }
                        checkChanges()
                        dialog.dismiss()
                        updateSettingsList()
                    }
                    .show()
            }
        ))

        // "Use Extreme Dark" toggle for auto modes
        if (pendingAppTheme != Settings.AppTheme.CLEAR &&
            pendingAppTheme != Settings.AppTheme.DARK &&
            pendingAppTheme != Settings.AppTheme.EXTREME_DARK) {
            items.add(SettingItem.ToggleSettingEntry(
                stableId = "useExtremeDarkMode",
                nameResId = R.string.use_extreme_dark,
                descriptionResId = R.string.use_extreme_dark_description,
                isChecked = pendingUseExtremeDarkMode!!,
                onCheckedChanged = { isChecked ->
                    pendingUseExtremeDarkMode = isChecked
                    if (isChecked) pendingUseGradientBackground = false
                    checkChanges()
                    updateSettingsList()
                }
            ))
        }

        // Monochrome icons toggle (for all dark-capable modes)
        if (pendingAppTheme != Settings.AppTheme.CLEAR) {
            items.add(SettingItem.ToggleSettingEntry(
                stableId = "monochromeIcons",
                nameResId = R.string.monochrome_icons,
                descriptionResId = R.string.monochrome_icons_description,
                isChecked = pendingMonochromeIcons!!,
                onCheckedChanged = { isChecked ->
                    pendingMonochromeIcons = isChecked
                    checkChanges()
                    updateSettingsList()
                }
            ))
        }

        // Gradient background toggle (hidden for Extreme Dark)
        if (pendingAppTheme != Settings.AppTheme.EXTREME_DARK) {
            val isAutoMode = pendingAppTheme != Settings.AppTheme.CLEAR &&
                             pendingAppTheme != Settings.AppTheme.DARK
            val gradientEnabled = !isAutoMode || pendingUseExtremeDarkMode != true
            val isGradientOn = pendingUseGradientBackground == true
            val descResId = when {
                isGradientOn && isAutoMode -> R.string.use_gradient_background_description_on_auto
                isGradientOn -> R.string.use_gradient_background_description_on
                isAutoMode -> R.string.use_gradient_background_description_off_auto
                else -> R.string.use_gradient_background_description_off
            }

            val gradientName = if (pendingAppTheme == Settings.AppTheme.CLEAR) {
                getString(R.string.use_white_background)
            } else null

            items.add(SettingItem.ToggleSettingEntry(
                stableId = "useGradientBackground",
                nameResId = R.string.use_gradient_background,
                descriptionResId = descResId,
                isChecked = pendingUseGradientBackground!!,
                isEnabled = gradientEnabled,
                nameOverride = gradientName,
                onCheckedChanged = { isChecked ->
                    pendingUseGradientBackground = isChecked
                    checkChanges()
                    updateSettingsList()
                }
            ))
        }

        // App theme sub-options: threshold slider for Light Sensor / Screen Brightness
        if (pendingAppTheme == Settings.AppTheme.LIGHT_SENSOR || pendingAppTheme == Settings.AppTheme.SCREEN_BRIGHTNESS) {
            val isSensor = pendingAppTheme == Settings.AppTheme.LIGHT_SENSOR
            val currentValue = if (isSensor) pendingAppThemeThresholdLux else pendingAppThemeThresholdBrightness
            val title = getString(if (isSensor) R.string.threshold_light_title else R.string.threshold_brightness_title)
            val hint = getString(if (isSensor) R.string.threshold_light_hint else R.string.threshold_brightness_hint)
            val currentReading = if (isSensor) {
                if (cachedLux >= 0) getString(R.string.current_light_reading, cachedLux.toInt()) else ""
            } else { "" }
            val displayValue = if (isSensor) {
                val base = "${currentValue ?: 0} Lux"
                if (currentReading.isNotEmpty()) "$base ($currentReading)" else base
            } else {
                "${currentValue ?: 0} / 255"
            }

            items.add(SettingItem.SettingEntry(
                stableId = "appThemeThreshold",
                nameResId = if (isSensor) R.string.threshold_light_title else R.string.threshold_brightness_title,
                value = displayValue,
                onClick = { _ ->
                    if (isSensor) {
                        showLuxSliderDialog(
                            title = title,
                            message = hint,
                            initialLux = currentValue ?: 0,
                            currentReading = currentReading,
                            onConfirm = { newLux ->
                                pendingAppThemeThresholdLux = newLux
                                checkChanges()
                                updateSettingsList()
                            }
                        )
                    } else {
                        showSliderDialog(
                            title = title,
                            message = hint,
                            initialPercentage = currentValue ?: 100,
                            minLabel = "0",
                            maxLabel = "255",
                            formatValue = { v -> "$v" },
                            currentReading = "",
                            sliderMax = 255,
                            onConfirm = { newVal ->
                                pendingAppThemeThresholdBrightness = newVal
                                checkChanges()
                                updateSettingsList()
                            }
                        )
                    }
                }
            ))
        }

        // App theme sub-options: time pickers for Manual Time
        if (pendingAppTheme == Settings.AppTheme.MANUAL_TIME) {
            val formatTime = { minutes: Int -> "%02d:%02d".format(minutes / 60, minutes % 60) }

            items.add(SettingItem.SettingEntry(
                stableId = "appThemeStart",
                nameResId = R.string.night_mode_start,
                value = formatTime(pendingAppThemeManualStart!!),
                onClick = { _ ->
                    TimePickerDialog(requireContext(), { _, hour, minute ->
                        pendingAppThemeManualStart = hour * 60 + minute
                        checkChanges()
                        updateSettingsList()
                    }, pendingAppThemeManualStart!! / 60, pendingAppThemeManualStart!! % 60, true).show()
                }
            ))

            items.add(SettingItem.SettingEntry(
                stableId = "appThemeEnd",
                nameResId = R.string.night_mode_end,
                value = formatTime(pendingAppThemeManualEnd!!),
                onClick = { _ ->
                    TimePickerDialog(requireContext(), { _, hour, minute ->
                        pendingAppThemeManualEnd = hour * 60 + minute
                        checkChanges()
                        updateSettingsList()
                    }, pendingAppThemeManualEnd!! / 60, pendingAppThemeManualEnd!! % 60, true).show()
                }
            ))
        }

        // --- Android Auto Night Mode ---
        items.add(SettingItem.CategoryHeader("aaNightMode", R.string.night_mode))

        // Night Mode (Android Auto)
        items.add(SettingItem.SettingEntry(
            stableId = "nightMode",
            nameResId = R.string.night_mode,
            value = run {
                val base = resources.getStringArray(R.array.night_mode)[pendingNightMode!!.value]
                if (pendingNightMode == Settings.NightMode.AUTO) {
                    val info = com.andrerinas.headunitrevived.utils.NightMode(settings, true).getCalculationInfo()
                    "$base ($info)"
                } else {
                    base
                }
            },
            onClick = { _ ->
                val nightModeTitles = resources.getStringArray(R.array.night_mode)

                MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                    .setTitle(R.string.night_mode)
                    .setSingleChoiceItems(nightModeTitles, pendingNightMode!!.value) { dialog, which ->
                        pendingNightMode = Settings.NightMode.fromInt(which)!!
                        checkChanges()
                        dialog.dismiss()
                        updateSettingsList()
                    }
                    .show()
            }
        ))

        // Night mode sub-options: threshold slider for Light Sensor / Screen Brightness
        if (pendingNightMode == Settings.NightMode.LIGHT_SENSOR || pendingNightMode == Settings.NightMode.SCREEN_BRIGHTNESS) {
            val isSensor = pendingNightMode == Settings.NightMode.LIGHT_SENSOR
            val currentValue = if (isSensor) pendingThresholdLux else pendingThresholdBrightness
            val title = getString(if (isSensor) R.string.threshold_light_title else R.string.threshold_brightness_title)
            val hint = getString(if (isSensor) R.string.threshold_light_hint else R.string.threshold_brightness_hint)
            val nmCurrentReading = if (isSensor) {
                if (cachedLux >= 0) getString(R.string.current_light_reading, cachedLux.toInt()) else ""
            } else { "" }
            val displayValue = if (isSensor) {
                val base = "${currentValue ?: 0} Lux"
                if (nmCurrentReading.isNotEmpty()) "$base ($nmCurrentReading)" else base
            } else {
                "${currentValue ?: 0} / 255"
            }

            items.add(SettingItem.SettingEntry(
                stableId = "nightModeThreshold",
                nameResId = if (isSensor) R.string.threshold_light_title else R.string.threshold_brightness_title,
                value = displayValue,
                onClick = { _ ->
                    if (isSensor) {
                        showLuxSliderDialog(
                            title = title,
                            message = hint,
                            initialLux = currentValue ?: 0,
                            currentReading = nmCurrentReading,
                            onConfirm = { newLux ->
                                pendingThresholdLux = newLux
                                checkChanges()
                                updateSettingsList()
                            }
                        )
                    } else {
                        showSliderDialog(
                            title = title,
                            message = hint,
                            initialPercentage = currentValue ?: 100,
                            minLabel = "0",
                            maxLabel = "255",
                            formatValue = { v -> "$v" },
                            currentReading = "",
                            sliderMax = 255,
                            onConfirm = { newVal ->
                                pendingThresholdBrightness = newVal
                                checkChanges()
                                updateSettingsList()
                            }
                        )
                    }
                }
            ))
        }

        // Night mode sub-options: time pickers for Manual Time
        if (pendingNightMode == Settings.NightMode.MANUAL_TIME) {
            val formatTime = { minutes: Int -> "%02d:%02d".format(minutes / 60, minutes % 60) }

            items.add(SettingItem.SettingEntry(
                stableId = "nightModeStart",
                nameResId = R.string.night_mode_start,
                value = formatTime(pendingManualStart!!),
                onClick = { _ ->
                    TimePickerDialog(requireContext(), { _, hour, minute ->
                        pendingManualStart = hour * 60 + minute
                        checkChanges()
                        updateSettingsList()
                    }, pendingManualStart!! / 60, pendingManualStart!! % 60, true).show()
                }
            ))

            items.add(SettingItem.SettingEntry(
                stableId = "nightModeEnd",
                nameResId = R.string.night_mode_end,
                value = formatTime(pendingManualEnd!!),
                onClick = { _ ->
                    TimePickerDialog(requireContext(), { _, hour, minute ->
                        pendingManualEnd = hour * 60 + minute
                        checkChanges()
                        updateSettingsList()
                    }, pendingManualEnd!! / 60, pendingManualEnd!! % 60, true).show()
                }
            ))
        }

        // AA Monochrome toggle — hidden when Night Mode is DAY
        if (pendingNightMode != Settings.NightMode.DAY) {
            items.add(SettingItem.ToggleSettingEntry(
                stableId = "aaMonochrome",
                nameResId = R.string.aa_monochrome,
                descriptionResId = R.string.aa_monochrome_description,
                isChecked = pendingAaMonochromeEnabled!!,
                onCheckedChanged = { isChecked ->
                    if (isChecked && pendingViewMode != Settings.ViewMode.GLES) {
                        // Set to true so the submitted list matches the visual switch state.
                        // This way DiffUtil can detect the revert to false on cancel.
                        pendingAaMonochromeEnabled = true
                        updateSettingsList()
                        // Show GLES required dialog
                        MaterialAlertDialogBuilder(requireContext(), R.style.DarkAlertDialog)
                            .setTitle(R.string.gles_required_title)
                            .setMessage(R.string.gles_required_message)
                            .setPositiveButton(R.string.enable_gles) { _, _ ->
                                pendingViewMode = Settings.ViewMode.GLES
                                pendingAaMonochromeEnabled = true
                                checkChanges()
                                updateSettingsList()
                            }
                            .setNegativeButton(R.string.cancel) { _, _ ->
                                pendingAaMonochromeEnabled = false
                                checkChanges()
                                updateSettingsList()
                            }
                            .setOnCancelListener {
                                pendingAaMonochromeEnabled = false
                                checkChanges()
                                updateSettingsList()
                            }
                            .show()
                    } else {
                        pendingAaMonochromeEnabled = isChecked
                        checkChanges()
                        updateSettingsList()
                    }
                }
            ))

            // Desaturation slider — only visible when AA monochrome is ON
            if (pendingAaMonochromeEnabled == true) {
                items.add(SettingItem.SliderSettingEntry(
                    stableId = "aaDesaturation",
                    nameResId = R.string.aa_desaturation,
                    value = "${pendingAaDesaturationLevel}%",
                    sliderValue = pendingAaDesaturationLevel!!.toFloat(),
                    valueFrom = 0f,
                    valueTo = 100f,
                    stepSize = 0f,
                    onValueChanged = { value ->
                        pendingAaDesaturationLevel = value.toInt()
                        checkChanges()
                        updateSettingsList()
                    }
                ))
            }
        }

        settingsAdapter.submitList(items) {
            scrollState?.let { recyclerView.layoutManager?.onRestoreInstanceState(it) }
        }
    }

    override fun onResume() {
        super.onResume()
        sensorManager = requireContext().getSystemService(Context.SENSOR_SERVICE) as? SensorManager
        val lightSensor = sensorManager?.getDefaultSensor(Sensor.TYPE_LIGHT)
        if (lightSensor != null) {
            sensorManager?.registerListener(this, lightSensor, SensorManager.SENSOR_DELAY_NORMAL)
        }
    }

    override fun onPause() {
        super.onPause()
        sensorManager?.unregisterListener(this)
        refreshHandler.removeCallbacks(refreshRunnable)
    }

    private fun scheduleListRefresh() {
        refreshHandler.removeCallbacks(refreshRunnable)
        refreshHandler.postDelayed(refreshRunnable, 500)
    }

    private fun showSliderDialog(
        title: String,
        message: String,
        initialPercentage: Int,
        minLabel: String,
        maxLabel: String,
        formatValue: (Int) -> String,
        currentReading: String = "",
        sliderMax: Int = 100,
        onConfirm: (Int) -> Unit
    ) {
        val context = requireContext()
        val density = context.resources.displayMetrics.density
        val padding = (24 * density).toInt()

        val layout = android.widget.LinearLayout(context).apply {
            orientation = android.widget.LinearLayout.VERTICAL
            setPadding(padding, (8 * density).toInt(), padding, 0)
        }

        val hint = android.widget.TextView(context).apply {
            text = message
            textSize = 14f
            setTextColor(context.resources.getColor(android.R.color.darker_gray, null))
        }
        layout.addView(hint)

        val label = android.widget.TextView(context).apply {
            text = formatValue(initialPercentage.coerceIn(0, sliderMax))
            textSize = 24f
            gravity = android.view.Gravity.CENTER
            val topMargin = (16 * density).toInt()
            val lp = android.widget.LinearLayout.LayoutParams(
                android.widget.LinearLayout.LayoutParams.MATCH_PARENT,
                android.widget.LinearLayout.LayoutParams.WRAP_CONTENT
            )
            lp.topMargin = topMargin
            layoutParams = lp
        }
        layout.addView(label)

        val seekBar = android.widget.SeekBar(context).apply {
            max = sliderMax
            progress = initialPercentage.coerceIn(0, sliderMax)
            setOnSeekBarChangeListener(object : android.widget.SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(seekBar: android.widget.SeekBar?, progress: Int, fromUser: Boolean) {
                    label.text = formatValue(progress)
                }
                override fun onStartTrackingTouch(seekBar: android.widget.SeekBar?) {}
                override fun onStopTrackingTouch(seekBar: android.widget.SeekBar?) {}
            })
        }
        layout.addView(seekBar)

        val rangeRow = android.widget.LinearLayout(context).apply {
            orientation = android.widget.LinearLayout.HORIZONTAL
            val lp = android.widget.LinearLayout.LayoutParams(
                android.widget.LinearLayout.LayoutParams.MATCH_PARENT,
                android.widget.LinearLayout.LayoutParams.WRAP_CONTENT
            )
            layoutParams = lp
        }
        val minText = android.widget.TextView(context).apply {
            text = minLabel
            textSize = 12f
            setTextColor(context.resources.getColor(android.R.color.darker_gray, null))
            val lp = android.widget.LinearLayout.LayoutParams(0, android.widget.LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            layoutParams = lp
        }
        val maxText = android.widget.TextView(context).apply {
            text = maxLabel
            textSize = 12f
            gravity = android.view.Gravity.END
            setTextColor(context.resources.getColor(android.R.color.darker_gray, null))
            val lp = android.widget.LinearLayout.LayoutParams(0, android.widget.LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            layoutParams = lp
        }
        rangeRow.addView(minText)
        rangeRow.addView(maxText)
        layout.addView(rangeRow)

        if (currentReading.isNotEmpty()) {
            val readingLabel = android.widget.TextView(context).apply {
                text = currentReading
                textSize = 16f
                gravity = android.view.Gravity.CENTER
                setTextColor(context.resources.getColor(android.R.color.darker_gray, null))
                val lp = android.widget.LinearLayout.LayoutParams(
                    android.widget.LinearLayout.LayoutParams.MATCH_PARENT,
                    android.widget.LinearLayout.LayoutParams.WRAP_CONTENT
                )
                lp.topMargin = (16 * density).toInt()
                layoutParams = lp
            }
            layout.addView(readingLabel)
        }

        MaterialAlertDialogBuilder(context, R.style.DarkAlertDialog)
            .setTitle(title)
            .setView(layout)
            .setPositiveButton(android.R.string.ok) { dialog, _ ->
                onConfirm(seekBar.progress)
                dialog.dismiss()
            }
            .setNegativeButton(android.R.string.cancel) { dialog, _ ->
                dialog.cancel()
            }
            .show()
    }

    private fun showLuxSliderDialog(
        title: String,
        message: String,
        initialLux: Int,
        currentReading: String = "",
        onConfirm: (Int) -> Unit
    ) {
        val context = requireContext()
        val density = context.resources.displayMetrics.density
        val padding = (24 * density).toInt()

        var currentMax = if (initialLux <= LUX_MAX_FINE) LUX_MAX_FINE else LUX_MAX

        val layout = android.widget.LinearLayout(context).apply {
            orientation = android.widget.LinearLayout.VERTICAL
            setPadding(padding, (8 * density).toInt(), padding, 0)
        }

        val hint = android.widget.TextView(context).apply {
            text = message
            textSize = 14f
            setTextColor(context.resources.getColor(android.R.color.darker_gray, null))
        }
        layout.addView(hint)

        val label = android.widget.TextView(context).apply {
            text = "$initialLux Lux"
            textSize = 24f
            gravity = android.view.Gravity.CENTER
            val lp = android.widget.LinearLayout.LayoutParams(
                android.widget.LinearLayout.LayoutParams.MATCH_PARENT,
                android.widget.LinearLayout.LayoutParams.WRAP_CONTENT
            )
            lp.topMargin = (16 * density).toInt()
            layoutParams = lp
        }
        layout.addView(label)

        val seekBar = android.widget.SeekBar(context).apply {
            max = currentMax
            progress = initialLux.coerceIn(0, currentMax)
        }

        val minText = android.widget.TextView(context).apply {
            text = "0 Lux"
            textSize = 12f
            setTextColor(context.resources.getColor(android.R.color.darker_gray, null))
            layoutParams = android.widget.LinearLayout.LayoutParams(0, android.widget.LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        }
        val maxText = android.widget.TextView(context).apply {
            text = "${currentMax} Lux"
            textSize = 12f
            gravity = android.view.Gravity.END
            setTextColor(context.resources.getColor(android.R.color.darker_gray, null))
            layoutParams = android.widget.LinearLayout.LayoutParams(0, android.widget.LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        }

        seekBar.setOnSeekBarChangeListener(object : android.widget.SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: android.widget.SeekBar?, progress: Int, fromUser: Boolean) {
                label.text = "$progress Lux"
            }
            override fun onStartTrackingTouch(sb: android.widget.SeekBar?) {}
            override fun onStopTrackingTouch(sb: android.widget.SeekBar?) {}
        })
        layout.addView(seekBar)

        val rangeRow = android.widget.LinearLayout(context).apply {
            orientation = android.widget.LinearLayout.HORIZONTAL
        }
        rangeRow.addView(minText)
        rangeRow.addView(maxText)
        layout.addView(rangeRow)

        val checkBox = android.widget.CheckBox(context).apply {
            text = getString(R.string.enable_fine_lux_control)
            isChecked = currentMax == LUX_MAX_FINE
            val lp = android.widget.LinearLayout.LayoutParams(
                android.widget.LinearLayout.LayoutParams.MATCH_PARENT,
                android.widget.LinearLayout.LayoutParams.WRAP_CONTENT
            )
            lp.topMargin = (12 * density).toInt()
            layoutParams = lp
        }
        checkBox.setOnCheckedChangeListener { _, isChecked ->
            val oldProgress = seekBar.progress
            currentMax = if (isChecked) LUX_MAX_FINE else LUX_MAX
            seekBar.max = currentMax
            seekBar.progress = oldProgress.coerceIn(0, currentMax)
            maxText.text = "${currentMax} Lux"
            label.text = "${seekBar.progress} Lux"
        }
        layout.addView(checkBox)

        if (currentReading.isNotEmpty()) {
            val readingLabel = android.widget.TextView(context).apply {
                text = currentReading
                textSize = 16f
                gravity = android.view.Gravity.CENTER
                setTextColor(context.resources.getColor(android.R.color.darker_gray, null))
                val lp = android.widget.LinearLayout.LayoutParams(
                    android.widget.LinearLayout.LayoutParams.MATCH_PARENT,
                    android.widget.LinearLayout.LayoutParams.WRAP_CONTENT
                )
                lp.topMargin = (16 * density).toInt()
                layoutParams = lp
            }
            layout.addView(readingLabel)
        }

        MaterialAlertDialogBuilder(context, R.style.DarkAlertDialog)
            .setTitle(title)
            .setView(layout)
            .setPositiveButton(android.R.string.ok) { dialog, _ ->
                onConfirm(seekBar.progress)
                dialog.dismiss()
            }
            .setNegativeButton(android.R.string.cancel) { dialog, _ ->
                dialog.cancel()
            }
            .show()
    }

    companion object {
        private const val LUX_MAX = 10000
        private const val LUX_MAX_FINE = 100
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/AapBroadcastReceiver.kt`:

```kt
package com.andrerinas.headunitrevived

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.Intent.FLAG_ACTIVITY_NEW_TASK
import android.content.IntentFilter
import android.view.KeyEvent
import com.andrerinas.headunitrevived.aap.AapProjectionActivity
import com.andrerinas.headunitrevived.aap.protocol.messages.LocationUpdateEvent
import com.andrerinas.headunitrevived.connection.CommManager
import com.andrerinas.headunitrevived.contract.KeyIntent
import com.andrerinas.headunitrevived.contract.LocationUpdateIntent
import com.andrerinas.headunitrevived.contract.MediaKeyIntent
import com.andrerinas.headunitrevived.contract.ProjectionActivityRequest

class AapBroadcastReceiver : BroadcastReceiver() {

    companion object {
        val filter: IntentFilter by lazy {
            val filter = IntentFilter()
            filter.addAction(LocationUpdateIntent.action)
            filter.addAction(MediaKeyIntent.action)
            filter.addAction(ProjectionActivityRequest.action)
            filter
        }
    }

    override fun onReceive(context: Context, intent: Intent) {
        val component = App.provide(context)
        if (intent.action == LocationUpdateIntent.action) {
            val location = LocationUpdateIntent.extractLocation(intent)

            // Apply Fake Speed if enabled
            if (component.settings.fakeSpeed) {
                location.speed = 0.5f // 0.5 m/s corresponds to 500 mm/s in Emil's logic
            }

            if (component.settings.useGpsForNavigation) {
                component.commManager.send(LocationUpdateEvent(location))
            }

            if (location.latitude != 0.0 && location.longitude != 0.0) {
                component.settings.lastKnownLocation = location
            }
        } else if (intent.action == MediaKeyIntent.action) {
            val event: KeyEvent? = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
                intent.getParcelableExtra(KeyIntent.extraEvent, KeyEvent::class.java)
            } else {
                @Suppress("DEPRECATION")
                intent.getParcelableExtra(KeyIntent.extraEvent)
            }
            event?.let {
                component.commManager.send(it.keyCode, it.action == KeyEvent.ACTION_DOWN)
            }
        } else if (intent.action == ProjectionActivityRequest.action){
            if (component.commManager.connectionState.value is CommManager.ConnectionState.TransportStarted) {
                val aapIntent = Intent(context, AapProjectionActivity::class.java)
                aapIntent.putExtra(AapProjectionActivity.EXTRA_FOCUS, true)
                aapIntent.flags = FLAG_ACTIVITY_NEW_TASK
                context.startActivity(aapIntent)
            }
        }
    }
}


```

`app/src/main/java/com/andrerinas/headunitrevived/App.kt`:

```kt
package com.andrerinas.headunitrevived

import android.app.Application
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.os.Build
import androidx.core.content.ContextCompat
import androidx.multidex.MultiDex
import com.andrerinas.headunitrevived.main.BackgroundNotification
import com.andrerinas.headunitrevived.aap.AapNavigation
import com.andrerinas.headunitrevived.ssl.ConscryptInitializer
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.AppThemeManager
import com.andrerinas.headunitrevived.utils.Settings
import android.os.SystemClock
import java.io.File

class App : Application() {

    private val component: AppComponent by lazy {
        AppComponent(this)
    }

    override fun attachBaseContext(base: Context?) {
        super.attachBaseContext(base)
        MultiDex.install(this)
    }

    override fun onCreate() {
        super.onCreate()

        if (ConscryptInitializer.isNeededForTls12()) {
            ConscryptInitializer.initialize()
        }

        val settings = Settings(this) // Create a Settings instance
        AppLog.init(settings) // Initialize AppLog with settings for conditional logging

        // Sync auto-start settings to device-protected storage so that
        // BootCompleteReceiver, UsbAttachedActivity, and AutoStartReceiver
        // can read them during locked boot (before user unlock)
        Settings.syncAutoStartOnBootToDeviceStorage(this, settings.autoStartOnBoot)
        Settings.syncAutoStartOnUsbToDeviceStorage(this, settings.autoStartOnUsb)
        Settings.syncAutoStartBtMacToDeviceStorage(this, settings.autoStartBluetoothDeviceMac)

        // Apply app theme
        if (AppThemeManager.isStaticMode(settings.appTheme)) {
            AppThemeManager.applyStaticTheme(settings)
        } else {
            appThemeManager = AppThemeManager(this, settings)
            appThemeManager?.start()
        }

        if (ConscryptInitializer.isAvailable()) {
            AppLog.i("Conscrypt security provider is active")
        } else if (ConscryptInitializer.isNeededForTls12()) {
            AppLog.w("Conscrypt not available - TLS 1.2 may not work on this device")
        }

        AppLog.d( "native library dir ${applicationInfo.nativeLibraryDir}")

        File(applicationInfo.nativeLibraryDir).listFiles()?.forEach { file ->
            AppLog.d( "   ${file.name}")
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val serviceChannel = NotificationChannel(defaultChannel, "Headunit Service", NotificationManager.IMPORTANCE_LOW)
            serviceChannel.description = "Persistent service notification"
            serviceChannel.setShowBadge(false)
            component.notificationManager.createNotificationChannel(serviceChannel)

            val mediaChannel = NotificationChannel(BackgroundNotification.mediaChannel, "Media Playback", NotificationManager.IMPORTANCE_LOW)
            mediaChannel.setSound(null, null)
            mediaChannel.setShowBadge(false)
            component.notificationManager.createNotificationChannel(mediaChannel)

            AapNavigation.createNotificationChannel(this)

            val bootChannel = NotificationChannel(bootStartChannel, "Boot Auto-Start", NotificationManager.IMPORTANCE_HIGH)
            bootChannel.description = "Shown once after boot to open the app"
            bootChannel.setShowBadge(false)
            component.notificationManager.createNotificationChannel(bootChannel)
        }

        // Register the main broadcast receiver safely for Android 14+ using ContextCompat
        ContextCompat.registerReceiver(this, AapBroadcastReceiver(), AapBroadcastReceiver.filter, ContextCompat.RECEIVER_NOT_EXPORTED)
    }

    companion object {
        const val defaultChannel = "headunit_service_v2"
        const val bootStartChannel = "headunit_boot_start"
        val appStartTime = SystemClock.elapsedRealtime()
        var appThemeManager: AppThemeManager? = null

        fun get(context: Context): App {
            return context.applicationContext as App
        }
        fun provide(context: Context): AppComponent {
            return get(context).component
        }
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/decoder/MicRecorder.kt`:

```kt
package com.andrerinas.headunitrevived.decoder

import android.Manifest
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.MediaRecorder
import android.media.audiofx.AcousticEchoCanceler
import android.media.audiofx.AutomaticGainControl
import android.media.audiofx.NoiseSuppressor
import androidx.core.content.ContextCompat
import androidx.core.content.PermissionChecker
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.Settings

class MicRecorder(private val micSampleRate: Int, private val context: Context) {

    private var audioRecord: AudioRecord? = null
    private var aec: AcousticEchoCanceler? = null
    private var ns: NoiseSuppressor? = null
    private var agc: AutomaticGainControl? = null
    private val settings = Settings(context)

    private val micBufferSize: Int
    private var micAudioBuf: ByteArray

    // Indicates whether mic recording is available on this device
    val isAvailable: Boolean

    init {
        val minSize = AudioRecord.getMinBufferSize(micSampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
        if (minSize <= 0) {
            // Device doesn't support the requested audio config (common on API 16)
            AppLog.w("MicRecorder: getMinBufferSize returned $minSize, mic recording unavailable")
            micBufferSize = 0
            micAudioBuf = ByteArray(0)
            isAvailable = false
        } else {
            micBufferSize = minSize
            micAudioBuf = ByteArray(minSize)
            isAvailable = true
        }
    }

    private var threadMicAudioActive = false
    private var threadMicAudio: Thread? = null
    var listener: Listener? = null

    // Tracks whether this instance started Bluetooth SCO so we can clean it up
    private var bluetoothScoStarted = false
    private var scoReceiver: BroadcastReceiver? = null

    companion object {
        // Sentinel value stored in settings to indicate Bluetooth SCO mode
        const val SOURCE_BLUETOOTH_SCO = 100
    }

    interface Listener {
        fun onMicDataAvailable(mic_buf: ByteArray, mic_audio_len: Int)
    }

    fun stop() {
        AppLog.i("MicRecorder: Stopping. Active: $threadMicAudioActive")

        threadMicAudioActive = false
        threadMicAudio?.interrupt()
        threadMicAudio = null

        audioRecord?.apply {
            try {
                stop()
                release()
            } catch (e: Exception) {
                AppLog.e("MicRecorder: Error releasing AudioRecord", e)
            }
        }
        audioRecord = null

        try {
            aec?.release()
            ns?.release()
            agc?.release()
        } catch (e: Exception) {
            AppLog.e("MicRecorder: Error releasing AudioFX", e)
        }
        aec = null
        ns = null
        agc = null

        if (bluetoothScoStarted) {
            cleanupSco()
        }
    }

    private fun cleanupSco() {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
        try {
            scoReceiver?.let { context.unregisterReceiver(it) }
        } catch (e: Exception) {}
        scoReceiver = null

        audioManager.stopBluetoothSco()
        @Suppress("DEPRECATION")
        audioManager.isBluetoothScoOn = false
        bluetoothScoStarted = false
        AppLog.i("MicRecorder: Bluetooth SCO stopped")
    }

    private fun micAudioRead(aud_buf: ByteArray, max_len: Int): Int {
        val currentAudioRecord = audioRecord ?: return 0
        val currentListener = listener ?: return 0

        val len = currentAudioRecord.read(aud_buf, 0, max_len)
        if (len <= 0) {
            if (len == AudioRecord.ERROR_INVALID_OPERATION && threadMicAudioActive) {
                AppLog.e("MicRecorder: Unexpected interruption error: $len")
            }
            return len
        }

        currentListener.onMicDataAvailable(aud_buf, len)
        return len
    }

    fun start(): Int {
        if (!isAvailable) {
            AppLog.w("MicRecorder: Cannot start, mic not available on this device")
            return -4
        }

        if (PermissionChecker.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) != PermissionChecker.PERMISSION_GRANTED) {
            AppLog.e("MicRecorder: No RECORD_AUDIO permission")
            return -3
        }

        val configuredSource = settings.micInputSource

        if (configuredSource == SOURCE_BLUETOOTH_SCO) {
            startScoAndRecord()
        } else {
            startRecording(configuredSource)
        }

        return 0
    }

    private fun startScoAndRecord() {
        val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager

        // 1. Listen for SCO connection state
        scoReceiver = object : BroadcastReceiver() {
            override fun onReceive(context: Context, intent: Intent) {
                val state = intent.getIntExtra(AudioManager.EXTRA_SCO_AUDIO_STATE, -1)
                AppLog.d("MicRecorder: SCO State change: $state")

                if (state == AudioManager.SCO_AUDIO_STATE_CONNECTED) {
                    AppLog.i("MicRecorder: SCO Connected. Starting AudioRecord.")
                    // On many devices, even with SCO, we should use MIC or DEFAULT
                    // as VOICE_COMMUNICATION might try to use the device's own noise cancellation.
                    startRecording(MediaRecorder.AudioSource.MIC)
                } else if (state == AudioManager.SCO_AUDIO_STATE_DISCONNECTED && bluetoothScoStarted) {
                    AppLog.w("MicRecorder: SCO Disconnected unexpectedly.")
                    stop()
                }
            }
        }

        ContextCompat.registerReceiver(context, scoReceiver, IntentFilter(AudioManager.ACTION_SCO_AUDIO_STATE_UPDATED), ContextCompat.RECEIVER_EXPORTED)

        // 2. Start SCO
        AppLog.i("MicRecorder: Starting Bluetooth SCO...")
        audioManager.startBluetoothSco()
        @Suppress("DEPRECATION")
        audioManager.isBluetoothScoOn = true
        bluetoothScoStarted = true
    }

    private fun startRecording(source: Int) {
        try {
            if (audioRecord != null) return // Already recording

            AppLog.i("MicRecorder: Initializing AudioRecord with source: ${getAudioSourceName(source)} ($source), SampleRate: $micSampleRate, BufferSize: $micBufferSize")
            audioRecord = AudioRecord(source, micSampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, micBufferSize)

            if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                AppLog.e("MicRecorder: Failed to initialize AudioRecord")
                audioRecord = null
                return
            }

            val audioSessionId = audioRecord?.audioSessionId ?: 0
            if (audioSessionId != 0) {
                try {
                    if (NoiseSuppressor.isAvailable()) {
                        ns = NoiseSuppressor.create(audioSessionId)
                        ns?.enabled = true
                        AppLog.i("MicRecorder: NoiseSuppressor: ${if (ns?.enabled == true) "ON" else "failed"}")
                    } else {
                        AppLog.i("MicRecorder: NoiseSuppressor: Unsupported on this device")
                    }

                    if (AutomaticGainControl.isAvailable()) {
                        agc = AutomaticGainControl.create(audioSessionId)
                        agc?.enabled = true
                        AppLog.i("MicRecorder: AutomaticGainControl: ${if (agc?.enabled == true) "ON" else "failed"}")
                    } else {
                        AppLog.i("MicRecorder: AutomaticGainControl: Unsupported on this device")
                    }

                    if (AcousticEchoCanceler.isAvailable()) {
                        aec = AcousticEchoCanceler.create(audioSessionId)
                        aec?.enabled = true
                        AppLog.i("MicRecorder: AcousticEchoCanceler: ${if (aec?.enabled == true) "ON" else "failed"}")
                    } else {
                        AppLog.i("MicRecorder: AcousticEchoCanceler: Unsupported on this device")
                    }
                } catch (e: Exception) {
                    AppLog.e("MicRecorder: Error initializing AudioFX", e)
                }
            }

            audioRecord?.startRecording()

            threadMicAudioActive = true
            threadMicAudio = Thread({
                while (threadMicAudioActive) {
                    micAudioRead(micAudioBuf, micBufferSize)
                }
            }, "mic_audio").apply { start() }

        } catch (e: Exception) {
            AppLog.e("MicRecorder: Error during startRecording", e)
            audioRecord = null
        }
    }

    private fun getAudioSourceName(source: Int): String {
        return when (source) {
            MediaRecorder.AudioSource.DEFAULT -> "DEFAULT"
            MediaRecorder.AudioSource.MIC -> "MIC"
            MediaRecorder.AudioSource.VOICE_UPLINK -> "VOICE_UPLINK"
            MediaRecorder.AudioSource.VOICE_DOWNLINK -> "VOICE_DOWNLINK"
            MediaRecorder.AudioSource.VOICE_CALL -> "VOICE_CALL"
            MediaRecorder.AudioSource.CAMCORDER -> "CAMCORDER"
            MediaRecorder.AudioSource.VOICE_RECOGNITION -> "VOICE_RECOGNITION"
            MediaRecorder.AudioSource.VOICE_COMMUNICATION -> "VOICE_COMMUNICATION"
            MediaRecorder.AudioSource.REMOTE_SUBMIX -> "REMOTE_SUBMIX"
            MediaRecorder.AudioSource.UNPROCESSED -> "UNPROCESSED"
            SOURCE_BLUETOOTH_SCO -> "BLUETOOTH_SCO"
            else -> "UNKNOWN ($source)"
        }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/decoder/VideoDecoder.kt`:

```kt
package com.andrerinas.headunitrevived.decoder

import android.media.MediaCodec
import android.media.MediaCodecList
import android.media.MediaFormat
import android.os.Build
import android.view.Surface
import com.andrerinas.headunitrevived.utils.AppLog
import com.andrerinas.headunitrevived.utils.Settings
import com.andrerinas.headunitrevived.utils.HeadUnitScreenConfig
import android.os.SystemClock
import java.nio.ByteBuffer
import java.util.Locale

interface VideoDimensionsListener {
    fun onVideoDimensionsChanged(width: Int, height: Int)
}

/**
 * Main video decoding engine.
 * Handles H.264/H.265 streams via MediaCodec.
 */
class VideoDecoder(private val settings: Settings) {
    companion object {
        private const val TIMEOUT_US = 10000L

        /**
         * Checks if H.265 (HEVC) hardware decoding is supported on the current device.
         */
        fun isHevcSupported(): Boolean {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) return false
            val codecList = MediaCodecList(MediaCodecList.ALL_CODECS)
            return codecList.codecInfos.any { !it.isEncoder && it.supportedTypes.any { t -> t.equals("video/hevc", true) } }
        }
    }

    private var codec: MediaCodec? = null
    private var codecBufferInfo: MediaCodec.BufferInfo? = null
    private var mSurface: Surface? = null
    private var outputThread: Thread? = null
    @Volatile private var running = false
    private var startTime = 0L

    private var mWidth = 0
    private var mHeight = 0
    private var vps: ByteArray? = null
    private var sps: ByteArray? = null
    private var pps: ByteArray? = null
    private var codecConfigured = false
    private var currentCodecType = CodecType.H264
    private var currentCodecName: String? = null

    // Reuse buffers for older API levels to minimize GC pressure
    private var inputBuffers: Array<ByteBuffer>? = null
    private var legacyFrameBuffer: ByteArray? = null

    var dimensionsListener: VideoDimensionsListener? = null
    var onFpsChanged: ((Int) -> Unit)? = null
    private var frameCount = 0
    private var lastFpsLogTime = 0L
    @Volatile var onFirstFrameListener: (() -> Unit)? = null
    @Volatile var lastFrameRenderedMs: Long = 0L

    val videoWidth: Int get() = mWidth
    val videoHeight: Int get() = mHeight

    enum class CodecType(val mimeType: String, val displayName: String) {
        H264("video/avc", "H.264/AVC"),
        H265("video/hevc", "H.265/HEVC")
    }

    /**
     * Handles dynamic video dimension changes during the session.
     */
    private fun handleOutputFormatChange(format: MediaFormat) {
        AppLog.i("Output Format Changed: $format")
        val newWidth = try { format.getInteger(MediaFormat.KEY_WIDTH) } catch (e: Exception) { mWidth }
        val newHeight = try { format.getInteger(MediaFormat.KEY_HEIGHT) } catch (e: Exception) { mHeight }
        if (mWidth != newWidth || mHeight != newHeight) {
            AppLog.i("Video dimensions changed via format: ${newWidth}x$newHeight")
            mWidth = newWidth
            mHeight = newHeight
            dimensionsListener?.onVideoDimensionsChanged(mWidth, mHeight)
        }
        try {
            codec?.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT)
        } catch (e: Exception) {}
    }

    /**
     * Sets the rendering surface and restarts the decoder if necessary.
     */
    fun setSurface(surface: Surface?) {
        synchronized(this) {
            if (mSurface === surface) return

            AppLog.i("New surface set: $surface")
            if (codec != null) {
                stop("New surface")
            }
            mSurface = surface
            lastFrameRenderedMs = 0L
        }
    }

    /**
     * Stops the decoder, terminates the output thread, and releases hardware resources.
     */
    fun stop(reason: String = "unknown") {
        synchronized(this) {
            running = false
            try {
                // If calling from output thread, don't join itself to avoid deadlock
                if (outputThread != null && outputThread != Thread.currentThread()) {
                    outputThread?.interrupt()
                    outputThread?.join(500)
                }
            } catch (e: Exception) {}
            outputThread = null

            try {
                codec?.stop()
            } catch (e: Exception) {}
            try {
                codec?.release()
            } catch (e: Exception) {
                AppLog.e("Error releasing decoder", e)
            }

            codec = null
            inputBuffers = null
            legacyFrameBuffer = null
            codecBufferInfo = null
            codecConfigured = false
            // Keep VPS/SPS/PPS cached so we can re-inject them on restart
            lastFrameRenderedMs = 0L
            AppLog.i("Decoder stopped: $reason")
        }
    }

    /**
     * Main entry point for decoding a video/control packet.
     */
    fun decode(buffer: ByteArray, offset: Int, size: Int, forceSoftware: Boolean, codecName: String) {
        synchronized(this) {
            // Buffer management for backward compatibility
            // Modern devices (API 21+) use the original buffer with offset/size to avoid GC pressure.
            val frameData: ByteArray
            val frameOffset: Int
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
                if (legacyFrameBuffer == null || legacyFrameBuffer!!.size < size) {
                    legacyFrameBuffer = ByteArray(size + 1024)
                }
                System.arraycopy(buffer, offset, legacyFrameBuffer!!, 0, size)
                frameData = legacyFrameBuffer!!
                frameOffset = 0
            } else {
                frameData = buffer
                frameOffset = offset
            }

            // Initialization phase: detect codec and configuration (SPS/PPS)
            if (codec == null) {
                val detectedType = detectCodecType(frameData, frameOffset, size)
                val typeToUse = detectedType ?: if (codecName.contains("265")) CodecType.H265 else CodecType.H264
                currentCodecType = typeToUse

                if (!codecConfigured) {
                    scanAndApplyConfig(frameData, frameOffset, size, typeToUse)

                    if (mWidth == 0) {
                         // Fallback dimensions if SPS/PPS parsing fails or is missing
                         val negotiatedW = HeadUnitScreenConfig.getNegotiatedWidth()
                         val negotiatedH = HeadUnitScreenConfig.getNegotiatedHeight()
                         if (negotiatedW > 0 && negotiatedH > 0) {
                             AppLog.i("Fallback to negotiated dimensions: ${negotiatedW}x${negotiatedH}")
                             mWidth = negotiatedW
                             mHeight = negotiatedH
                             dimensionsListener?.onVideoDimensionsChanged(mWidth, mHeight)
                         }
                    }
                }

                if (mSurface == null || !mSurface!!.isValid) return
                if (mWidth == 0 || mHeight == 0) return

                start(typeToUse.mimeType, settings.forceSoftwareDecoding || forceSoftware, mWidth, mHeight)
            }

            if (codec == null) return

            // Feed frame data into MediaCodec input buffers
            val buf = ByteBuffer.wrap(frameData, frameOffset, size)
            while (buf.hasRemaining()) {
                if (!feedInputBuffer(buf)) {
                    return
                }
            }
        }
    }

    private fun detectCodecType(buffer: ByteArray, offset: Int, size: Int): CodecType? {
        if (size < 5) return null
        val limit = offset + size
        // Need at least 5 bytes visible from position i: [0, 0, 0/1, 1, NAL_HEADER]
        for (i in offset until limit - 4) {
            if (buffer[i].toInt() == 0 && buffer[i+1].toInt() == 0) {
                val headerPos: Int
                if (buffer[i+2].toInt() == 0 && buffer[i+3].toInt() == 1) {
                    headerPos = i + 4
                } else if (buffer[i+2].toInt() == 1) {
                    headerPos = i + 3
                } else continue
                if (headerPos >= limit) return null
                val b = buffer[headerPos].toInt()
                val hevcType = (b and 0x7E) shr 1
                if (hevcType in 32..34) return CodecType.H265
                val avcType = b and 0x1F
                if (avcType == 7 || avcType == 8) return CodecType.H264
            }
            // Only scan the first ~100 bytes for performance
            if (i - offset >= 96) break
        }
        return null
    }

    /**
     * Splits a combined packet into multiple NAL units and normalizes start codes.
     */
    private fun forEachNalUnit(buffer: ByteArray, offset: Int, size: Int, callback: (ByteArray, Int) -> Unit) {
        var currentPos = offset
        val limit = offset + size

        while (currentPos < limit - 3) {
            var nalStart = -1
            var startCodeLen = 0

            for (i in currentPos until limit - 3) {
                if (buffer[i].toInt() == 0 && buffer[i+1].toInt() == 0) {
                    if (buffer[i+2].toInt() == 0 && buffer[i+3].toInt() == 1) {
                        nalStart = i; startCodeLen = 4; break
                    } else if (buffer[i+2].toInt() == 1) {
                        nalStart = i; startCodeLen = 3; break
                    }
                }
            }

            if (nalStart != -1) {
                var nalEnd = limit
                for (j in (nalStart + startCodeLen) until limit - 3) {
                    if (buffer[j].toInt() == 0 && buffer[j+1].toInt() == 0 &&
                        (buffer[j+2].toInt() == 1 || (buffer[j+2].toInt() == 0 && buffer[j+3].toInt() == 1))) {
                        nalEnd = j; break
                    }
                }

                val rawNal = buffer.copyOfRange(nalStart, nalEnd)
                val fixedNal = if (startCodeLen == 3) {
                    // Normalize to 4-byte start codes for better decoder compatibility
                    ByteArray(rawNal.size + 1).apply {
                        this[0] = 0; System.arraycopy(rawNal, 0, this, 1, rawNal.size)
                    }
                } else rawNal

                callback(fixedNal, if (startCodeLen == 3) 4 else 4)
                currentPos = nalEnd
            } else break
        }
    }

    /**
     * Extracts SPS/PPS/VPS data for the decoder configuration (CSD).
     */
    private fun scanAndApplyConfig(buffer: ByteArray, offset: Int, size: Int, type: CodecType) {
        forEachNalUnit(buffer, offset, size) { nalData, headerLen ->
            val nalFirstByte = nalData[headerLen].toInt()
            if (type == CodecType.H264) {
                val nalType = nalFirstByte and 0x1F
                if (nalType == 7) { // SPS
                    sps = nalData
                    try {
                        val offsetInNal = if (sps!![2].toInt() == 1) 3 else 4
                        SpsParser.parse(sps!!, offsetInNal, sps!!.size - offsetInNal)?.let {
                            if (mWidth != it.width || mHeight != it.height) {
                                AppLog.i("H.264 SPS parsed: ${it.width}x${it.height}")
                                mWidth = it.width; mHeight = it.height
                                dimensionsListener?.onVideoDimensionsChanged(mWidth, mHeight)
                            }
                        }
                    } catch (e: Exception) { AppLog.e("Failed to parse SPS data", e) }
                } else if (nalType == 8) pps = nalData // PPS

                // H.264 requires at least SPS to start
                if (sps != null) codecConfigured = true
            } else {
                val nalType = (nalFirstByte and 0x7E) shr 1
                if (nalType == 32) vps = nalData
                else if (nalType == 33) sps = nalData
                else if (nalType == 34) pps = nalData

                // H.265 requires VPS and SPS to start reliably
                if (vps != null && sps != null) codecConfigured = true
            }
        }
    }

    /**
     * Configures and starts the native MediaCodec.
     */
    private fun start(mimeType: String, forceSoftware: Boolean, width: Int, height: Int) {
        try {
            startTime = System.nanoTime()
            val bestCodec = findBestCodec(mimeType, !forceSoftware)
                ?: throw IllegalStateException("No decoder available for $mimeType")
            this.currentCodecName = bestCodec

            codec = MediaCodec.createByCodecName(bestCodec)
            codecBufferInfo = MediaCodec.BufferInfo()

            val format = MediaFormat.createVideoFormat(mimeType, width, height)

            // Apply Codec Specific Data (CSD) from parsed SPS/PPS/VPS
            if (mimeType == CodecType.H265.mimeType) {
                val combined = (vps ?: byteArrayOf()) + (sps ?: byteArrayOf()) + (pps ?: byteArrayOf())
                if (combined.isNotEmpty()) {
                    format.setByteBuffer("csd-0", ByteBuffer.wrap(combined))
                }
                format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 8 * 1024 * 1024)
            } else {
                if (sps != null) format.setByteBuffer("csd-0", ByteBuffer.wrap(sps!!))
                if (pps != null) format.setByteBuffer("csd-1", ByteBuffer.wrap(pps!!))
                format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 2 * 1024 * 1024)
            }

            if (!mSurface!!.isValid) throw IllegalStateException("Surface not valid")

            AppLog.i("Configuring decoder: $bestCodec for ${width}x${height}")
            codec?.configure(format, mSurface, null, 0)
            try { codec?.setVideoScalingMode(MediaCodec.VIDEO_SCALING_MODE_SCALE_TO_FIT) } catch (e: Exception) {}
            codec?.start()

            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) {
                @Suppress("DEPRECATION") inputBuffers = codec?.inputBuffers
            }

            running = true
            outputThread = Thread {
                android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_DISPLAY)
                com.andrerinas.headunitrevived.utils.LegacyOptimizer.setHighPriority()
                outputThreadLoop()
            }.apply { name = "VideoDecoder-Output"; start() }

            AppLog.i("Codec initialized: $bestCodec")
        } catch (e: Exception) {
            AppLog.e("Failed to start decoder", e)
            codec = null; running = false
        }
    }

    /**
     * Logic to identify chipsets that require constant flagging
     */
    private fun shouldAlwaysFlagConfig(): Boolean {
        val name = currentCodecName?.lowercase(Locale.ROOT) ?: return false
        return name.contains(".rk.") ||       // Rockchip
                name.contains("allwinner") ||
                name.contains(".tcc.")      // Telechips
    }

    /**
     * Checks if the data contains SPS/PPS/VPS configuration data.
     */
    private fun isCodecConfigData(data: ByteArray, offset: Int, size: Int): Boolean {
        if (size < 5) return false
        for (i in offset until (offset + size - 4).coerceAtMost(offset + 32)) {
            if (data[i].toInt() == 0 && data[i + 1].toInt() == 0) {
                val headerPos: Int
                if (data[i + 2].toInt() == 0 && data[i + 3].toInt() == 1) {
                    headerPos = i + 4
                } else if (data[i + 2].toInt() == 1) {
                    headerPos = i + 3
                } else continue
                if (headerPos >= offset + size) return false
                val b = data[headerPos].toInt()
                if (currentCodecType == CodecType.H265) {
                    val nalType = (b and 0x7E) shr 1
                    return nalType in 32..34
                } else {
                    val nalType = b and 0x1F
                    return nalType == 7 || nalType == 8
                }
            }
        }
        return false
    }

    /**
     * Feeds the raw byte stream into the decoder buffer.
     */
    private fun feedInputBuffer(buffer: ByteBuffer): Boolean {
        val currentCodec = codec ?: return false
        try {
            var inputIndex = -1
            var attempts = 0
            while (attempts < 30) {
                inputIndex = currentCodec.dequeueInputBuffer(TIMEOUT_US)
                if (inputIndex >= 0) break
                attempts++
            }

            if (inputIndex < 0) {
                AppLog.e("Input buffer feed failed (full)")
                return false
            }

            val inputBuffer = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                currentCodec.getInputBuffer(inputIndex)
            } else {
                @Suppress("DEPRECATION") inputBuffers?.get(inputIndex)
            }

            if (inputBuffer == null) return false
            inputBuffer.clear()

            val capacity = inputBuffer.capacity()

            // Always set BUFFER_FLAG_CODEC_CONFIG for config data (VPS/SPS/PPS).
            // Some decoders (Rockchip/Allwinner) require this flag for every config packet
            // even after the stream has already started.
            val isConfig = buffer.hasArray() && isCodecConfigData(buffer.array(), buffer.position(), buffer.remaining())
            val flags = if (isConfig && (shouldAlwaysFlagConfig() || !codecConfigured)) {
                MediaCodec.BUFFER_FLAG_CODEC_CONFIG
            } else 0

            if (buffer.remaining() <= capacity) {
                inputBuffer.put(buffer)
            } else {
                AppLog.w("Frame too large: ${buffer.remaining()} > $capacity. Truncating!")
                val limit = buffer.limit()
                buffer.limit(buffer.position() + capacity)
                inputBuffer.put(buffer)
                buffer.limit(limit)
            }

            inputBuffer.flip()
            val pts = (System.nanoTime() - startTime) / 1000
            currentCodec.queueInputBuffer(inputIndex, 0, inputBuffer.limit(), pts, flags)
            return true
        } catch (e: Exception) {
            AppLog.e("Error feeding input buffer", e)
            return false
        }
    }

    /**
     * Dedicated thread to pull decoded frames and render them to the surface.
     */
    private fun outputThreadLoop() {
        AppLog.i("Output thread started")
        while (running) {
            val currentCodec = codec
            val bufferInfo = codecBufferInfo
            if (currentCodec == null || bufferInfo == null) {
                try { Thread.sleep(10) } catch (e: InterruptedException) { break }
                continue
            }

            try {
                val outputIndex = currentCodec.dequeueOutputBuffer(bufferInfo, 10000L)
                if (outputIndex >= 0) {
                    currentCodec.releaseOutputBuffer(outputIndex, true)
                    lastFrameRenderedMs = SystemClock.elapsedRealtime()
                    onFirstFrameListener?.let { it(); onFirstFrameListener = null }

                    frameCount++
                    val now = System.currentTimeMillis()
                    val elapsed = now - lastFpsLogTime
                    if (elapsed >= 1000) {
                        if (lastFpsLogTime != 0L) {
                            val fps = (frameCount * 1000 / elapsed).toInt()
                            onFpsChanged?.invoke(fps)
                        }
                        frameCount = 0
                        lastFpsLogTime = now
                    }
                } else if (outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    handleOutputFormatChange(currentCodec.outputFormat)
                }
            } catch (e: Exception) {
                if (running) {
                    AppLog.w("Codec exception in output thread: ${e.message}")
                    try { Thread.sleep(50) } catch (ignore: Exception) {}
                }
            }
        }
        AppLog.i("Output thread stopped")
    }

    /**
     * Resolves the best available hardware or software decoder for the given mime type.
     */
    private fun findBestCodec(mimeType: String, preferHardware: Boolean): String? {
        val codecInfos = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            MediaCodecList(MediaCodecList.ALL_CODECS).codecInfos.toList()
        } else {
            @Suppress("DEPRECATION")
            val count = MediaCodecList.getCodecCount()
            (0 until count).map { MediaCodecList.getCodecInfoAt(it) }
        }

        val infos = codecInfos.filter { !it.isEncoder && it.supportedTypes.any { t -> t.equals(mimeType, true) } }
        val hw = infos.find { isHardwareAccelerated(it.name) }
        val sw = infos.find { !isHardwareAccelerated(it.name) }
        return if (preferHardware && hw != null) hw.name else sw?.name ?: hw?.name
    }

    private fun isHardwareAccelerated(name: String): Boolean {
        val lower = name.lowercase(Locale.ROOT)
        return !(lower.startsWith("omx.google.") || lower.startsWith("c2.android.") || lower.contains(".sw.") || lower.contains("software"))
    }
}

/**
 * Helper to parse Bitstreams for SPS data.
 */
private class BitReader(private val buffer: ByteArray, private val offset: Int, private val size: Int) {
    private var bitPosition = offset * 8
    private val bitLimit = (offset + size) * 8

    fun readBit(): Int {
        if (bitPosition >= bitLimit) return 0
        return (buffer[bitPosition / 8].toInt() shr (7 - (bitPosition++ % 8))) and 1
    }

    fun readBits(count: Int): Int {
        var res = 0
        repeat(count) { res = (res shl 1) or readBit() }
        return res
    }

    fun readUE(): Int {
        var zeros = 0
        while (readBit() == 0 && bitPosition < bitLimit) zeros++
        return if (zeros == 0) 0 else (1 shl zeros) - 1 + readBits(zeros)
    }
}

data class SpsData(val width: Int, val height: Int)

/**
 * Parses AVC/H.264 Sequence Parameter Sets to extract video dimensions.
 */
private object SpsParser {
    fun parse(sps: ByteArray, offset: Int, size: Int): SpsData? {
        try {
            val reader = BitReader(sps, offset, size)
            reader.readBits(8)
            val profileIdc = reader.readBits(8)
            reader.readBits(16)
            reader.readUE()
            if (profileIdc in listOf(100, 110, 122, 244, 44, 83, 86, 118, 128)) {
                val chroma = reader.readUE()
                if (chroma == 3) reader.readBit()
                reader.readUE(); reader.readUE(); reader.readBit()
                if (reader.readBit() == 1) {
                    repeat(if (chroma != 3) 8 else 12) {
                        if (reader.readBit() == 1) {
                            var last = 8; var next = 8
                            repeat(if (it < 6) 16 else 64) {
                                if (next != 0) next = (last + reader.readUE() + 256) % 256
                                if (next != 0) last = next
                            }
                        }
                    }
                }
            }
            reader.readUE()
            if (reader.readUE() == 0) reader.readUE()
            reader.readUE(); reader.readBit()
            val w = (reader.readUE() + 1) * 16
            val hMap = reader.readUE()
            val mbs = reader.readBit()
            var h = (2 - mbs) * (hMap + 1) * 16
            if (mbs == 0) reader.readBit()
            reader.readBit()
            if (reader.readBit() == 1) {
                val l = reader.readUE(); val r = reader.readUE()
                val t = reader.readUE(); val b = reader.readUE()
                return SpsData(w - (l + r) * 2, h - (t + b) * 2)
            }
            return SpsData(w, h)
        } catch (e: Exception) { return null }
    }
}

```

`app/src/main/java/com/andrerinas/headunitrevived/decoder/AudioTrackWrapper.kt`:

```kt
package com.andrerinas.headunitrevived.decoder

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.Process
import com.andrerinas.headunitrevived.utils.AppLog
import java.util.concurrent.Executors
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

class AudioTrackWrapper(
    stream: Int,
    sampleRateInHz: Int,
    bitDepth: Int,
    channelCount: Int,
    private val isAac: Boolean = false,
    gain: Float,
    private val audioLatencyMultiplier: Int = 8,
    private val audioQueueCapacity: Int = 0
) : Thread() {

    private val audioTrack: AudioTrack
    private var decoder: MediaCodec? = null
    private var codecHandlerThread: HandlerThread? = null
    private val freeInputBuffers = LinkedBlockingQueue<Int>()
    private val writeExecutor = Executors.newSingleThreadExecutor()

    // Limit queue capacity to provide backpressure to the network thread if audio playback is slow
    private val dataQueue = if (audioQueueCapacity > 0) LinkedBlockingQueue<ByteArray>(audioQueueCapacity) else LinkedBlockingQueue<ByteArray>()
    @Volatile
    private var isRunning = true

    private var currentGain: Float = gain

    // Track frames written for better draining
    private var framesWritten: Long = 0
    private val bytesPerFrame: Int = channelCount * (if (bitDepth == 16) 2 else 1)

    init {
        this.name = "AudioPlaybackThread"
        audioTrack = createAudioTrack(stream, sampleRateInHz, bitDepth, channelCount, audioLatencyMultiplier)
        audioTrack.play()

        if (isAac) {
            initDecoder(sampleRateInHz, channelCount)
        }

        this.start()
    }

    private fun initDecoder(sampleRate: Int, channels: Int) {
        try {
            val mime = "audio/mp4a-latm"
            val format = MediaFormat.createAudioFormat(mime, sampleRate, channels)
            format.setInteger(
                MediaFormat.KEY_AAC_PROFILE,
                MediaCodecInfo.CodecProfileLevel.AACObjectLC
            )
            format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 16384)

            // CSD enabled for RAW AAC (MEDIA_CODEC_AUDIO_AAC_LC)
            val csd = makeAacCsd(sampleRate, channels)
            format.setByteBuffer("csd-0", java.nio.ByteBuffer.wrap(csd))

            decoder = MediaCodec.createDecoderByType(mime)

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                codecHandlerThread = HandlerThread("AacCodecThread")
                codecHandlerThread!!.start()

                val callback = object : MediaCodec.Callback() {
                    override fun onInputBufferAvailable(codec: MediaCodec, index: Int) {
                        freeInputBuffers.offer(index)
                    }

                    override fun onOutputBufferAvailable(
                        codec: MediaCodec,
                        index: Int,
                        info: MediaCodec.BufferInfo
                    ) {
                        try {
                            val outputBuffer = codec.getOutputBuffer(index)
                            if (outputBuffer != null) {
                                val chunk = ByteArray(info.size)
                                outputBuffer.position(info.offset)
                                outputBuffer.get(chunk)
                                outputBuffer.clear()

                                // Write to AudioTrack using executor
                                writeExecutor.submit {
                                    try {
                                        writeToTrack(chunk)
                                    } catch (e: Exception) {
                                        AppLog.e("Error writing decoded AAC to AudioTrack", e)
                                    }
                                }
                            }
                            codec.releaseOutputBuffer(index, false)
                        } catch (e: Exception) {
                            AppLog.e("Error processing AAC output", e)
                        }
                    }

                    override fun onError(codec: MediaCodec, e: MediaCodec.CodecException) {
                        AppLog.e("AAC Codec Error", e)
                    }

                    override fun onOutputFormatChanged(codec: MediaCodec, format: MediaFormat) {
                        AppLog.i("AAC Output Format Changed: $format")
                    }
                }

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    val handler = Handler(codecHandlerThread!!.looper)
                    decoder!!.setCallback(callback, handler)
                } else {
                    decoder!!.setCallback(callback)
                }
            }

            decoder?.configure(format, null, null, 0)
            decoder?.start()
            AppLog.i("AAC Decoder started for $sampleRate Hz, $channels channels with is-adts (Async)")
        } catch (e: Exception) {
            AppLog.e("Failed to init AAC decoder", e)
        }
    }

    private fun applyGain(buffer: ByteArray) {
        if (currentGain == 1.0f) return
        for (i in 0 until buffer.size - 1 step 2) {
            val low = buffer[i].toInt() and 0xFF
            val high = buffer[i + 1].toInt() // High byte handles sign
            val sample = (high shl 8) or low
            val modifiedSample = (sample * currentGain).toInt().coerceIn(-32768, 32767)
            buffer[i] = (modifiedSample and 0xFF).toByte()
            buffer[i + 1] = (modifiedSample shr 8).toByte()
        }
    }

    private fun writeToTrack(buffer: ByteArray) {
        applyGain(buffer)
        val result = audioTrack.write(buffer, 0, buffer.size)
        if (result > 0) {
            framesWritten += result / bytesPerFrame
        }
    }

    override fun run() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)

        // Drain the queue even after isRunning is set to false
        while (isRunning || dataQueue.isNotEmpty()) {
            try {
                // Use poll to avoid blocking indefinitely if isRunning becomes false
                val buffer = dataQueue.poll(200, TimeUnit.MILLISECONDS)
                if (buffer != null) {
                    if (isAac && decoder != null) {
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                            queueInput(buffer)
                        } else {
                            decodeSync(buffer)
                        }
                    } else {
                        // PCM path - direct write in this high-priority thread
                        writeToTrack(buffer)
                    }
                }
            } catch (e: InterruptedException) {
                // If interrupted, check if we should still drain or exit
                if (!isRunning && dataQueue.isEmpty()) break
            } catch (e: Exception) {
                AppLog.e("Error in AudioTrackWrapper run loop", e)
                isRunning = false
            }
        }
        cleanup()
        AppLog.i("AudioTrackWrapper thread finished.")
    }

    @Suppress("DEPRECATION")
    private fun decodeSync(inputData: ByteArray) {
        try {
            val dec = this.decoder ?: return
            val inputIndex = dec.dequeueInputBuffer(200000)
            if (inputIndex >= 0) {
                val inputBuffer = dec.inputBuffers[inputIndex]
                inputBuffer.clear()
                inputBuffer.put(inputData)
                dec.queueInputBuffer(inputIndex, 0, inputData.size, 0, 0)
            }

            val info = MediaCodec.BufferInfo()
            var outputIndex = dec.dequeueOutputBuffer(info, 0)
            while (outputIndex >= 0) {
                val outputBuffer = dec.outputBuffers[outputIndex]
                val chunk = ByteArray(info.size)
                outputBuffer.position(info.offset)
                outputBuffer.get(chunk)
                writeToTrack(chunk)
                dec.releaseOutputBuffer(outputIndex, false)
                outputIndex = dec.dequeueOutputBuffer(info, 0)
            }
        } catch (e: Exception) {
            AppLog.e("Error in decodeSync", e)
        }
    }

    private fun queueInput(inputData: ByteArray) {
        try {
            // Wait for input buffer (with timeout to avoid deadlock if codec dies)
            val inputIndex = freeInputBuffers.poll(200, TimeUnit.MILLISECONDS)

            if (inputIndex != null && inputIndex >= 0) {
                val inputBuffer = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    decoder?.getInputBuffer(inputIndex)
                } else {
                    @Suppress("DEPRECATION")
                    decoder?.inputBuffers?.get(inputIndex)
                }

                inputBuffer?.clear()
                inputBuffer?.put(inputData)
                decoder?.queueInputBuffer(inputIndex, 0, inputData.size, 0, 0)
            } else {
                AppLog.w("AAC Input Buffer timeout - dropping frame")
            }
        } catch (e: Exception) {
            AppLog.e("Error queuing AAC input", e)
        }
    }

    private fun makeAacCsd(sampleRate: Int, channelCount: Int): ByteArray {
        val sampleRateIndex = getFrequencyIndex(sampleRate)
        val audioObjectType = 2 // AAC-LC

        // AudioSpecificConfig: 5 bits AOT, 4 bits Frequency Index, 4 bits Channel Config
        val config = (audioObjectType shl 11) or (sampleRateIndex shl 7) or (channelCount shl 3)
        val csd = ByteArray(2)
        csd[0] = ((config shr 8) and 0xFF).toByte()
        csd[1] = (config and 0xFF).toByte()
        return csd
    }

    private fun getFrequencyIndex(sampleRate: Int): Int {
        return when (sampleRate) {
            96000 -> 0
            88200 -> 1
            64000 -> 2
            48000 -> 3
            44100 -> 4
            32000 -> 5
            24000 -> 6
            22050 -> 7
            16000 -> 8
            12000 -> 9
            11025 -> 10
            8000 -> 11
            7350 -> 12
            else -> 4 // Default 44100
        }
    }

    private fun createAudioTrack(
        stream: Int,
        sampleRateInHz: Int,
        bitDepth: Int,
        channelCount: Int,
        multiplier: Int
    ): AudioTrack {
        val channelConfig =
            if (channelCount == 2) AudioFormat.CHANNEL_OUT_STEREO else AudioFormat.CHANNEL_OUT_MONO
        val dataFormat =
            if (bitDepth == 16) AudioFormat.ENCODING_PCM_16BIT else AudioFormat.ENCODING_PCM_8BIT

        val minBufferSize = AudioTrack.getMinBufferSize(sampleRateInHz, channelConfig, dataFormat)
        // Adjust buffer size based on user preference to balance latency and stutter
        val bufferSize = if (minBufferSize > 0) minBufferSize * multiplier else minBufferSize

        AppLog.i("Audio stream: $stream buffer size: $bufferSize (min: $minBufferSize) sampleRateInHz: $sampleRateInHz channelCount: $channelCount")

        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            val audioAttributes = AudioAttributes.Builder()
                .setLegacyStreamType(stream)
                .build()

            val audioFormat = AudioFormat.Builder()
                .setSampleRate(sampleRateInHz)
                .setChannelMask(channelConfig)
                .setEncoding(dataFormat)
                .build()

            AudioTrack.Builder()
                .setAudioAttributes(audioAttributes)
                .setAudioFormat(audioFormat)
                .setBufferSizeInBytes(bufferSize)
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build()
        } else {
            @Suppress("DEPRECATION")
            AudioTrack(
                stream,
                sampleRateInHz,
                channelConfig,
                dataFormat,
                bufferSize,
                AudioTrack.MODE_STREAM
            )
        }
    }

    fun write(buffer: ByteArray, offset: Int, size: Int) {
        if (!isRunning) return

        try {
            // offer() doesn't block if the queue is full. This prevents the network thread from blocking.
            val success = dataQueue.offer(buffer.copyOfRange(offset, offset + size), 5, TimeUnit.MILLISECONDS)
            if (!success) {
                AppLog.w("Audio queue is full, dropping audio frame to prevent stalling")
            }
        } catch (e: InterruptedException) {
            AppLog.w("Interrupted while offering audio data to queue")
        }
    }

    fun stopPlayback() {
        isRunning = false
        this.interrupt()
    }

    private fun cleanup() {
        // 1. Stop the decoder first if it's AAC to stop producing new output buffers
        try {
            decoder?.stop()
            decoder?.release()
            decoder = null
        } catch (e: Exception) {
            AppLog.e("Error releasing audio decoder", e)
        }

        // 2. Wait for AAC writes that were already submitted to the executor
        writeExecutor.shutdown()
        try {
            if (!writeExecutor.awaitTermination(1, TimeUnit.SECONDS)) {
                AppLog.w("Audio write executor did not terminate in time")
            }
        } catch (e: InterruptedException) {
            AppLog.w("Audio write executor interrupted during shutdown")
        }

        // 3. Gracefully stop the AudioTrack and wait for its internal buffer to drain.
        // Using stop() instead of pause()/flush() ensures pending data is played.
        if (audioTrack.playState == AudioTrack.PLAYSTATE_PLAYING) {
            try {
                audioTrack.stop()

                // Graceful wait for the AudioTrack buffer to drain,
                // especially important on older versions like KitKat.
                var lastPos = -1
                var stagnantCount = 0
                val startTime = System.currentTimeMillis()
                while (System.currentTimeMillis() - startTime < 2500) {
                    val pos = audioTrack.playbackHeadPosition
                    // If we know exactly how many frames we wrote, we can wait until they are all played.
                    if (pos >= framesWritten && framesWritten > 0) break

                    // If pos hasn't changed, it might be done or stalled
                    if (pos == lastPos && pos > 0) {
                        stagnantCount++
                        if (stagnantCount >= 3) break // Stagnant for 300ms, assume finished
                    } else {
                        lastPos = pos
                        stagnantCount = 0
                    }
                    Thread.sleep(100)
                }
            } catch (e: Exception) {
                AppLog.e("Error during audio track cleanup", e)
            }
        }

        // 4. Finally release the track
        try {
            audioTrack.release()
        } catch (e: Exception) {
            AppLog.e("Error releasing audio track", e)
        }

        // 5. Cleanup the codec thread
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR2) {
                codecHandlerThread?.quitSafely()
            } else {
                codecHandlerThread?.quit()
            }
            codecHandlerThread = null
        } catch (e: Exception) {
            AppLog.e("Error quitting codec thread", e)
        }
    }
}
```

`app/src/main/java/com/andrerinas/headunitrevived/decoder/AudioDecoder.kt`:

```kt
package com.andrerinas.headunitrevived.decoder

import android.util.SparseArray

class AudioDecoder {

    private val audioTracks = SparseArray<AudioTrackWrapper>(3)

    fun getTrack(channel: Int): AudioTrackWrapper? {
        return audioTracks.get(channel)
    }

    fun decode(channel: Int, buffer: ByteArray, offset: Int, size: Int) {
        val audioTrack = audioTracks.get(channel)
        audioTrack?.write(buffer, offset, size)
    }

    fun stop() {
        for (i in 0..audioTracks.size() - 1) {
            stop(audioTracks.keyAt(i))
        }
    }

    fun stop(chan: Int) {
        val audioTrack = audioTracks.get(chan)
        audioTrack?.stopPlayback()
        audioTracks.put(chan, null)
    }

    fun start(channel: Int, stream: Int, sampleRate: Int, numberOfBits: Int, numberOfChannels: Int, isAac: Boolean = false, gain: Float = 1.0f, audioLatencyMultiplier: Int = 8, audioQueueCapacity: Int = 0) {
        val thread = AudioTrackWrapper(stream, sampleRate, numberOfBits, numberOfChannels, isAac, gain, audioLatencyMultiplier, audioQueueCapacity)
        audioTracks.put(channel, thread)
    }

    companion object {
        const val SAMPLE_RATE_HZ_48 = 48000
        const val SAMPLE_RATE_HZ_16 = 16000
    }
}

```

`./app/src/main/res/raw/privkey`:

```/app/src/main/res/raw/privkey
-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCpqQmvoDW/XsRE
oj20dRcMqJGWh8RlUoHB8CpBpsoqV4nAuvNngkyrdpCf1yg0fVAp2Ugj5eOtzbiN
6BxoNHpPgiZ64pc+JRlwjmyHpssDaHzP+zHZM7acwMcroNVyynSzpiydEDyx/KPt
Ez5AsKi7c7AYYEtnCmAnK/waN1RT5KdZ9f97D9NeF7Ljdk+IKFROJh7Nv/YGiv9G
dPZh/ezSm2qhD3gzdh9PYs2cu0u+N17PYpSYB7vXPcYa/gmIVipIJ5RuMQVBWrCg
tfzwKPqbnJQVykm8LnysK+8RCgmPLN3uhsZx6Whax2TVXb1q68DoiaFPhvMfPr2i
/9IKaC69AgMBAAECggEAbBoW3963IG6jpA+0PW11+EzYJw/u5ZiCsS3z3s0Fd6E7
VqBIQyXU8FOlpxMSvQ8zqtaVjroGLlIsS88feo4leM+28Qm70I8W/I7jPDPcmxlS
nbqycnDu5EY5IeVi27eAUI+LUbBs3APb900Rl2p4uKfoBkAlC0yjI5J1GcczZhf7
RDh1wGgFWZI+ljiSrfpdiA4XmcZ9c7FlO5+NTotZzYeNx1iZprajV1/dlDy8UWEk
woWtppeGzUf3HHgl8yay62ub2vo5I1Z7Z98Roq8KC1o7k2IXOrHztCl3X03gMwlI
F4WQ6Fx5LZDU9dfaPhzkutekVgbtO9SzHgb3NXCZwQKBgQDcSS/OLll18ssjBwc7
PsdaIFIPlF428Tk8qezEnDmHS6xeztkGnpOlilk9jYSsVUbQmq8MwBSjfMVH95B0
w0yyfOYqjgTocg4lRCoPuBdnuBY/lU1Lws4FoGsGMNFkHWjHzl622mavkJiDzWA+
CORPUllS/DnPKJnZk2n0zZRKaQKBgQDFKqvePMx/a/ayQ09UZYxov0vwRyNkHevm
wEGQjOiHKozWvLqWhCvFtwo+VqHqmCw95cYUpg1GvppB6Lnw2uHgWAWxr3ugDjaR
YSqG/L7FG6FDF+1sPvBuxNpBmto59TI1fBFmU9VBGLDnr1M27qH3KTWlA3lCsovV
6Dbk7D+vNQKBgE6GgFYdS6KyFBu+a6OA84t7LgWDvDoVr3Oil1ZW4mMKZL2/OroT
WUqPkNRSWFMeawn9uhzvc+v7lE/dPk+BNxwBTgMpcTJzRfue2ueTljRQ+Q1daZpy
LQLwdnZUfLAVk752IGlKXYSEJPoHAiHbBZgJIPJmGy1vqbhXxlOP3SbRAoGBAJoA
Q2/5gy0/sdf5FRxxmOM0D+dkWTNY36pDnrJ+LR1uUcVkckUghWQQHRMl7aBkLaJH
N5lnPdV1CN3UHnAPNwBZIFFyJJiWoW6aO3JmNceVVjcmmE7FNlz+qw81GaDNcOMv
vhN0BYyr8Xl1iwTMDXwVFw6FkRBUjz6L+1yBXxjFAoGAJZcU+tEM1+gHPCqHK2bP
kfYOCyEAro4zY/VWXZKHgCoPau8Uc9+vFu2QVMb5kVyLTdyRLQKpooR6f8En6utS
/G15YuqRYqzSTrMBzpRrqIwbgKI9RHNPAvhtVAmXnwsYDPIQ1rrELK6WzTjUySRd
7gyCoq+DlY7ZKDa7FUz05Ek=
-----END PRIVATE KEY-----
```

`./app/src/main/res/raw/cert`:

```/app/src/main/res/raw/cert
-----BEGIN CERTIFICATE-----
MIIDJTCCAg0CAnZTMA0GCSqGSIb3DQEBCwUAMFsxCzAJBgNVBAYTAlVTMRMwEQYD
VQQIDApDYWxpZm9ybmlhMRYwFAYDVQQHDA1Nb3VudGFpbiBWaWV3MR8wHQYDVQQK
DBZHb29nbGUgQXV0b21vdGl2ZSBMaW5rMB4XDTE0MDcwODIyNDkxOFoXDTQ0MDcw
NzIyNDkxOFowVTELMAkGA1UEBhMCVVMxCzAJBgNVBAgMAkNBMRYwFAYDVQQHDA1N
b3VudGFpbiBWaWV3MSEwHwYDVQQKDBhHb29nbGUtQW5kcm9pZC1SZWZlcmVuY2Uw
ggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQCpqQmvoDW/XsREoj20dRcM
qJGWh8RlUoHB8CpBpsoqV4nAuvNngkyrdpCf1yg0fVAp2Ugj5eOtzbiN6BxoNHpP
giZ64pc+JRlwjmyHpssDaHzP+zHZM7acwMcroNVyynSzpiydEDyx/KPtEz5AsKi7
c7AYYEtnCmAnK/waN1RT5KdZ9f97D9NeF7Ljdk+IKFROJh7Nv/YGiv9GdPZh/ezS
m2qhD3gzdh9PYs2cu0u+N17PYpSYB7vXPcYa/gmIVipIJ5RuMQVBWrCgtfzwKPqb
nJQVykm8LnysK+8RCgmPLN3uhsZx6Whax2TVXb1q68DoiaFPhvMfPr2i/9IKaC69
AgMBAAEwDQYJKoZIhvcNAQELBQADggEBAIpfjQriEtbpUyWLoOOfJsjFN04+ajq9
1XALCPd+2ixWHZIBJiucrrf0H7OgY7eFnNbU0cRqiDZHI8BtvzFxNi/JgXqCmSHR
rlaoIsITfqo8KHwcAMs4qWTeLQmkTXBZYz0M3HwC7N1vOGjAJJN5qENIm1Jq+/3c
fxVg2zhHPKY8qtdgl73YIXb9Xx3WmPCBeRBCKJncj0Rq14uaOjWXRyBgbmdzMXJz
FGPHx3wN04JqGyfPFlDazXExFQwuAryjoYBRdxPxGufeQCp3am4xxI2oxNIzR+4L
nOcDhgU1B7sbkVzbKj5gjdOQAmxnKCfBtUNB63a7yzGPYGPIwlBsm54=
-----END CERTIFICATE-----
```
