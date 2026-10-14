package com.rb.cybermonitorpro.ui.theme

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color

// ============================================
//  ChargeControl — 可变主题色系统
//  支持运行时切换配色（设置页 → 主题色）
//  所有 val 改为 var：367 处引用无需改动
// ============================================

/** 可选主题（设置页展示） */
enum class AccentTheme(val id: String, val label: String, val primary: Long, val bright: Long, val deep: Long) {
    CYAN   ("cyan",    "青色",  0xFF06B6D4, 0xFF22D3EE, 0xFF0E7490),
    PURPLE ("purple",  "紫色",  0xFF7C3AED, 0xFFA78BFA, 0xFF4C1D95),
    GREEN  ("green",   "翠绿",  0xFF10B981, 0xFF34D399, 0xFF047857),
    AMBER  ("amber",   "琥珀",  0xFFF59E0B, 0xFFFBBF24, 0xFFB45309),
    ROSE   ("rose",    "玫红",  0xFFF43F5E, 0xFFFB7185, 0xFFBE123C),
    BLUE   ("blue",    "深蓝",  0xFF3B82F6, 0xFF60A5FA, 0xFF1D4ED8),
    STEEL  ("steel",   "钢灰",  0xFF64748B, 0xFF94A3B8, 0xFF334155);

    companion object {
        fun of(id: String?): AccentTheme = entries.firstOrNull { it.id == id } ?: CYAN
    }
}

// ── 背景与表面（不随主题变）──
val CyberBackground     = Color(0xFF080B14)
val CyberCardStart      = Color(0xFF101624)
val CyberPill           = Color(0xFF121E30)
val CyberElevated       = Color(0xFF141D2E)

// ── 主题色（可变，由 CyberThemeApplier 在启动/切换时更新）──
var NeonPurple          by mutableStateOf(Color(0xFF06B6D4))
    private set
var NeonPurpleBright    by mutableStateOf(Color(0xFF22D3EE))
    private set
var NeonPurpleDeep      by mutableStateOf(Color(0xFF0E7490))
    private set
var NeonPurplePale      by mutableStateOf(Color(0xFFE8EDF5))
    private set

/** 派生色（随主题联动） */
val PurpleGlow: Color        get() = NeonPurple.copy(alpha = 0.15f)
val PurpleGlowLight: Color   get() = NeonPurple.copy(alpha = 0.10f)
val PurpleGlowStrong: Color  get() = NeonPurpleBright.copy(alpha = 0.25f)
val ChartLinePurple: Color   get() = NeonPurple
val ChartAreaPurple: Color   get() = NeonPurple.copy(alpha = 0.19f)
val ChartGlow: Color         get() = NeonPurpleBright.copy(alpha = 0.31f)
val TextValue: Color         get() = NeonPurpleBright
val DividerCyber: Color      get() = NeonPurpleDeep
val ProgressTrack: Color     get() = NeonPurpleDeep.copy(alpha = 0.25f)
val CyberCardEnd: Color      get() = NeonPurpleDeep.copy(alpha = 0.35f)

// ── 辅助色（不随主题变）──
val CyberMuted          = Color(0xFF1A2436)
val NeonSteelBlue       = Color(0xFF3D70B8)
val NeonCyan            = Color(0xFF22D3EE)
val NeonMagenta         = Color(0xFFF87171)
val NeonMagentaPurple   = Color(0xFF38BDF8)

// ── 功能色（不随主题变）──
val SuccessNeon         = Color(0xFF10D9A0)
val WarningNeon         = Color(0xFFFBBF24)
val ErrorNeon           = Color(0xFFF87171)
val TitaniumGold        = Color(0xFFC9A86A)
val DeepRedAlert        = Color(0xFF9B1C1C)
val NeonDeepPink        = Color(0xFF0E7490)

// ── 文字色 ──
val TextPrimary         = Color(0xFFE8EDF5)
val TextSecondary       = Color(0xFF8B98B0)

/** 当前主题的 onPrimary 文字色（深色主题下用黑字） */
val TextOnPrimary: Color get() = Color(0xFF000000)

/** ★ 应用主题色（设置页切换后调用） */
fun applyAccentTheme(theme: AccentTheme) {
    NeonPurple       = Color(theme.primary)
    NeonPurpleBright = Color(theme.bright)
    NeonPurpleDeep   = Color(theme.deep)
}
