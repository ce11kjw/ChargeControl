package com.ce11kjw.chargecontrol.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable

/**
 * ChargeControl 暗色方案 —— 主题色随设置动态变化
 * 读取 Color.kt 的 var（由 applyAccentTheme 更新）
 */
@Composable
fun DeviceInfoViewerTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    dynamicColor: Boolean = false,
    content: @Composable () -> Unit
) {
    // 每次重组读取当前主题色（var + mutableStateOf → 切换立即生效）
    val scheme = darkColorScheme(
        primary              = NeonPurple,
        onPrimary            = TextOnPrimary,
        primaryContainer     = NeonPurpleDeep,
        onPrimaryContainer   = NeonPurpleBright,

        secondary            = NeonSteelBlue,
        onSecondary          = TextOnPrimary,
        secondaryContainer   = CyberMuted,
        onSecondaryContainer = NeonCyan,

        tertiary             = NeonMagenta,
        onTertiary           = TextOnPrimary,
        tertiaryContainer    = CyberMuted,
        onTertiaryContainer  = NeonMagenta,

        error                = ErrorNeon,
        onError              = CyberBackground,
        errorContainer       = ErrorNeon.copy(alpha = 0.15f),
        onErrorContainer     = ErrorNeon,

        background           = CyberBackground,
        onBackground         = TextPrimary,
        surface              = CyberPill,
        onSurface            = TextPrimary,
        surfaceVariant       = CyberMuted,
        onSurfaceVariant     = TextSecondary,
        outline              = NeonPurpleDeep,
        outlineVariant       = NeonPurpleDeep.copy(alpha = 0.5f)
    )

    MaterialTheme(
        colorScheme = scheme,
        typography  = Typography,
        content     = content
    )
}
