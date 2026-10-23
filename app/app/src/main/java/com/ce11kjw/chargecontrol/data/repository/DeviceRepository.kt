package com.ce11kjw.chargecontrol.data.repository

import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import androidx.lifecycle.MutableLiveData
import com.ce11kjw.chargecontrol.AppSettings
import com.ce11kjw.chargecontrol.RefreshPolicy
import com.ce11kjw.chargecontrol.data.model.*
import com.ce11kjw.chargecontrol.data.source.*
import com.ce11kjw.chargecontrol.data.module.ChargeControlApi
import com.ce11kjw.chargecontrol.data.module.ModuleDataAdapter
import kotlinx.coroutines.*
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.*

/** Core data repository using Kotlin coroutines and SharedFlow */
class DeviceRepository(context: Context) {
    private val appContext: Context = context.applicationContext

    companion object {
        const val TAG = "DeviceRepo"
    }

    // ═══════ 提取的独立类 ═══════
    val healthTracker = HealthTracker()
    val sourceHealth get() = healthTracker.liveData  // 向后兼容
    /* ★ v1.16.3: AuxiliaryCollector 已删除（GPS/WiFi/网络页面早已移除）
       只保留存储（ExportHelper 导出诊断包时要用） */
    val storageLiveData = MutableLiveData<StorageInfo>()

    // ═══════ DataSources (核心 5 模块 + 静态) ═══════
    /* v1.15.2: cpuDataSource 已移除 */
    /* v1.15.2: gpuDataSource 已移除 */
    private val batteryDataSource = BatteryDataSource(appContext)
    /* v1.15.2: memoryDataSource 已移除 */
    /* v1.15.2: sensorDataSource 已移除（传感器页删除） */
    /* v1.15.2: systemDataSource 已移除 */
    /* v1.15.2: oemDataSource 已移除 */
    // ═══════ CpuCache ═══════
    private val cachedChip: CpuCache.KnownChip? by lazy {
        // ★ Android 12+ (API31) 官方 SoC 字段优先：Build.SOC_MODEL 直接给出型号(如 mt6989)与厂商(MediaTek)
        //   仅在 API31+ 可达时使用，作为比 ro.board.platform 更高优先级的候选；
        //   minSdk=21，故旧路径 (ro.board.platform → ro.hardware.chipname) 必须保留为降级链。
        val socModel = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            Build.SOC_MODEL.trim().lowercase()
        } else ""
        val platform = if (socModel.isNotEmpty()) socModel
        else SysFsReader.readProp("ro.board.platform")
        // ★ #11c: 删除恒等冗余 `resolved` 中间变量 (其 else 分支重复读同一 prop, 值与 platform 恒等)
        //   注意: 仅删变量, cachedChip 字段保留 — collectCpuBlock/collectGpuBlock 仍依赖它注入芯片信息
        val finalPlatform = if (platform.isNotEmpty()) platform
        else SysFsReader.readProp("ro.hardware.chipname")
        CpuCache.lookup(finalPlatform)
    }

    // ═══════ History + 静态数据 ═══════
    val historyCache = HistoryCache()

    // ═══════ SharedFlow 输出 (悬浮窗消费) ═══════
    // replay=1 ensures new subscribers get latest value; DROP_OLDEST prevents backpressure
    // 注: SharedFlow 当前仅被 FloatingWindowService 消费 (悬浮窗实时刷新);
    //     UI 主屏经下方 LiveData 消费 (P2#8 见说明)。
    private val _cpuFlow = MutableSharedFlow<CpuInfo>(replay = 1, onBufferOverflow = BufferOverflow.DROP_OLDEST)
    val cpuFlow: SharedFlow<CpuInfo> = _cpuFlow.asSharedFlow()

    private val _gpuFlow = MutableSharedFlow<GpuInfo>(replay = 1, onBufferOverflow = BufferOverflow.DROP_OLDEST)
    val gpuFlow: SharedFlow<GpuInfo> = _gpuFlow.asSharedFlow()

    private val _memoryFlow = MutableSharedFlow<MemoryInfo>(replay = 1, onBufferOverflow = BufferOverflow.DROP_OLDEST)
    val memoryFlow: SharedFlow<MemoryInfo> = _memoryFlow.asSharedFlow()

    private val _batteryFlow = MutableSharedFlow<BatteryInfo>(replay = 1, onBufferOverflow = BufferOverflow.DROP_OLDEST)
    val batteryFlow: SharedFlow<BatteryInfo> = _batteryFlow.asSharedFlow()

    // ═══════ LiveData (UI 主用 — 暂保留) ═══════
    // 全部 ViewModel (Dashboard/Cpu/Gpu/Memory/Battery) + ExportHelper 经此消费,
    // 故为当前 UI 主数据管道; SharedFlow 并行 emit 以服务悬浮窗。
    // P2#8: 完整迁移 (LiveData → SharedFlow + collectAsStateWithLifecycle) 属大型 UI 改动,
    //       需严格审查 + 真机验证, 当前保留双管道 (collect 块内双写在后续去重)。
    val cpuLiveData = MutableLiveData<CpuInfo>()
    val gpuLiveData = MutableLiveData<GpuInfo>()
    val memoryLiveData = MutableLiveData<MemoryInfo>()
    val batteryLiveData = MutableLiveData<BatteryInfo>()


    // 静态模块 LiveData
    /* v1.15.2: sensorsLiveData 已移除 —— 传感器页删除后无任何订阅者 */
    val systemLiveData = MutableLiveData<SystemInfo>()
    val oemLiveData = MutableLiveData<OemInfo>()

    // 传感器
    val sensorLiveData = MutableLiveData<SensorLiveData>()
    val sensorHistoryData = MutableLiveData<Map<String, List<HistoryDataPoint>>>(emptyMap())

    // 聚合历史数据
    val historyData = MutableLiveData<Map<String, List<HistoryDataPoint>>>(emptyMap())

    // ═══════ 协程调度 ═══════
    /* ★ v1.9.0 纯模块取数：模块在线时全部数据来自 /api/status
       ★ v1.11.2: 加时间戳缓存 + 互斥。
       4 个采集 Block 是独立协程并发跑的，原先各自调 refreshModuleStatus()，
       一个轮询周期会发 4 次 HTTP（实际只需 1 次）。
       现在 500ms 内的并发调用共享同一次请求结果。 */
    private var moduleStatus: org.json.JSONObject? = null
    private var moduleStatusAt = 0L
    private val moduleFetchLock = kotlinx.coroutines.sync.Mutex()
    /** 模块最近一次拉取是否成功 —— 供 UI 显示「不可用」 */
    @Volatile var moduleOnline: Boolean = false
        private set

    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())

    @Volatile private var monitoring = false
    @Volatile private var intervalMs: Long = RefreshPolicy.Tier.NORMAL.defaultMs
    /** ★ 悬浮窗提频窗 — >0 时 policy 推流对每模块 min(eff, override) 只提速; 详见 setPaceOverride */
    @Volatile private var paceOverrideMs = 0L
    private var batteryJob: Job? = null
    private var policyObserverJob: Job? = null

    private val cpuIntervalFlow = MutableStateFlow(RefreshPolicy.Tier.NORMAL.defaultMs)
    private val gpuIntervalFlow = MutableStateFlow(RefreshPolicy.Tier.NORMAL.defaultMs)
    private val memIntervalFlow = MutableStateFlow(RefreshPolicy.Tier.NORMAL.defaultMs)
    private val batIntervalFlow = MutableStateFlow(RefreshPolicy.Tier.NORMAL.defaultMs)

    // ═══════ 生命周期 ═══════

    fun startMonitoring(intervalMs: Long = RefreshPolicy.Tier.NORMAL.defaultMs) {
        if (monitoring) return
        monitoring = true
        this.intervalMs = intervalMs
        pushAllIntervalFlows(intervalMs)

        /* ★ v1.14.0: 只保留电池采集
           CPU / GPU / 内存页面已移除，其轮询是纯浪费（每 3-5 秒一次 HTTP）。
           collectCpuBlock/collectGpuBlock/collectMemoryBlock 函数保留，
           若将来恢复页面可直接接回。 */
        batteryJob = PollingFlow.launchModulePolling("Battery", batIntervalFlow, scope) { collectBatteryBlock() }

        /* ★ v1.14.0: Aux 通道已移除。
           它原本为 CPU/GPU/内存页构建 15 组历史快照（每 tick ≈1200 个对象拷贝），
           这些页面已删除 → 纯浪费。电池的历史由 collectBatteryBlock 自己写入 historyCache。 */

        // 观察刷新策略 (省电模式降频，前后台不降频 — 仅动画暂停)
        policyObserverJob = scope.launch {
            combine(
                RefreshPolicy.state,
                RefreshPolicy.powerSaveModeFlow
            ) { state, powerSave ->
                Pair(state, powerSave)
            }.collect { (state, powerSave) ->
                Log.d(TAG, "RefreshPolicy state=${state.name}, powerSave=$powerSave, pushing per-module intervals")
                pushPolicyAdjustedIntervals()
            }
        }
    }

    fun stopMonitoring() {
        monitoring = false
        batteryJob?.cancel()   /* v1.14.0: 只保留电池 */
        policyObserverJob?.cancel()
        /* v1.15.2: disableSensor() 已移除（传感器页删除） */
        disableStepCounter()
    }

    fun shutdown() {
        stopMonitoring()
        historyCache.shutdown()
    }

    fun loadStaticData() {
        scope.launch(Dispatchers.IO) { // ★ 修复(N3): 阻塞 I/O 跑 IO 线程池 (原 Default 占 CPU 线程)
            /* ★ v1.9.0: 静态数据也走模块（纯模块策略）
               ★ v1.11.2: force=true —— 静态数据是页面首次加载的关键路径，
               若恰好复用采集 Block 的旧缓存（可能是模块刚启动时的空值），
               会导致设备详情空白。强制刷新。 */
            val ms = refreshModuleStatus(force = true)

            runCatching {
                val info = if (ms != null) {
                    com.ce11kjw.chargecontrol.data.model.SystemInfo().also {
                        ModuleDataAdapter.fillSystemInfo(it, ms)
                    }
                } else com.ce11kjw.chargecontrol.data.model.SystemInfo()
                systemLiveData.postValue(info)
                healthTracker.mark(HealthTracker.SourceHealth.Health.OK, "system")
            }.onFailure { e -> Log.w(TAG, "系统信息采集失败", e); healthTracker.mark(HealthTracker.SourceHealth.Health.ERROR, "system") }

            runCatching {
                val st = if (ms != null) ModuleDataAdapter.toStorageInfo(ms)
                         else com.ce11kjw.chargecontrol.data.model.StorageInfo()
                storageLiveData.postValue(st)
            }.onFailure { e -> Log.w(TAG, "存储信息采集失败", e) }
            /* ★ v1.15.2: 传感器页已删除 → 不再采集（原来每次 loadStaticData 都遍历全部传感器） */
            runCatching {
                val oem = if (ms != null) {
                    com.ce11kjw.chargecontrol.data.model.OemInfo().also {
                        ModuleDataAdapter.fillOemInfo(it, ms)
                    }
                } else com.ce11kjw.chargecontrol.data.model.OemInfo()
                oemLiveData.postValue(oem)
                healthTracker.mark(HealthTracker.SourceHealth.Health.OK, "oem")
            }.onFailure { e -> Log.w(TAG, "OEM信息采集失败", e); healthTracker.mark(HealthTracker.SourceHealth.Health.ERROR, "oem") }
        }
    }


    // ═══════ 核心采集块 (写入 SharedFlow + LiveData + History) ═══════

    /**
     * ★ v1.9.0: 拉取模块状态。
     * 所有采集 Block 都先调它 —— 一次 HTTP 拿到全部数据，
     * 避免每个 Block 各请求一遍。
     */
    /**
     * 拉取模块状态（带 500ms 缓存 + 互斥）。
     *
     * 4 个采集 Block 并发调用时，只有第一个真正发 HTTP，
     * 其余等待并复用结果 —— 一个轮询周期最多 1 次请求。
     *
     * @param force true 时忽略缓存（手动刷新用）
     */
    private suspend fun refreshModuleStatus(force: Boolean = false): org.json.JSONObject? {
        val now = System.currentTimeMillis()
        if (!force && moduleStatus != null && now - moduleStatusAt < 500) {
            return if (moduleOnline) moduleStatus else null
        }
        moduleFetchLock.withLock {
            val t2 = System.currentTimeMillis()
            if (!force && moduleStatus != null && t2 - moduleStatusAt < 500) {
                return if (moduleOnline) moduleStatus else null
            }
            val st = try { ChargeControlApi.fetchStatus() } catch (e: Throwable) { null }
            moduleStatus = st
            moduleStatusAt = System.currentTimeMillis()
            moduleOnline = ModuleDataAdapter.isUsable(st)
            return if (moduleOnline) st else null
        }
    }

    /* v1.15.2: collectCpuBlock 已移除（对应页面删除，无调用者） */

    /* v1.15.2: collectGpuBlock 已移除（对应页面删除，无调用者） */

    /* v1.15.2: collectMemoryBlock 已移除（对应页面删除，无调用者） */

    private suspend fun collectBatteryBlock() {
        try {
            val ms = refreshModuleStatus()
            val bat = if (ms != null) ModuleDataAdapter.toBatteryInfo(ms)
                      else com.ce11kjw.chargecontrol.data.model.BatteryInfo()
            _batteryFlow.emit(bat); batteryLiveData.postValue(bat)
            RefreshPolicy.isPowerSaveMode = bat.isPowerSaveMode
            if (!bat.temperatureCelsius.isNaN()) historyCache.addPoint("battery_temp", bat.temperatureCelsius)
            val batPowerMw = if (bat.isCharging) bat.chargingPowerMw else bat.dischargingPowerMw
            if (batPowerMw >= 0) historyCache.addPoint("battery_power", batPowerMw.toFloat())
            if (bat.levelPercent >= 0) historyCache.addPoint("battery_level", bat.levelPercent.toFloat())
            if (!bat.wattageNow.isNaN() && bat.wattageNow > 0) historyCache.addPoint("battery_wattage", bat.wattageNow.toFloat())
            if (bat.chargeFullMAh > 0 && bat.chargeFullDesignMAh > 0) {
                historyCache.addPoint("battery_soh", bat.chargeFullMAh.toFloat() / bat.chargeFullDesignMAh.toFloat() * 100f)
                historyCache.addPoint("battery_charge_full", bat.chargeFullMAh.toFloat())
            }
            if (!bat.internalResistanceMOhm.isNaN() && bat.internalResistanceMOhm > 0)
                historyCache.addPoint("battery_resistance", bat.internalResistanceMOhm)
            healthTracker.mark(HealthTracker.SourceHealth.Health.OK, "battery")
        } catch (e: Throwable) {
            Log.w(TAG, "电池采集失败", e)
            healthTracker.mark(HealthTracker.SourceHealth.Health.ERROR, "battery")
        }
    }

    /** 强制刷新电池数据 — 供 batteryPulseFlow 插拔事件消费，不等轮询 tick */
    fun forceRefreshBattery() {
        scope.launch { collectBatteryBlock() }
    }

    // ═══════ GPS / 传感器 ═══════


    @Volatile private var sensorListening = false

    // ═══════ 步数传感器独立监听（设备详情页卡片用）═══════
    private val stepSensorManager: SensorManager? by lazy {
        appContext.getSystemService(Context.SENSOR_SERVICE) as? SensorManager
    }
    @Volatile private var stepCounterListening = false
    // F-04: @Volatile — 主线程写 (enableStepCounter), 传感器/主线程读, 保证可见性
    @Volatile private var stepCounterCallback: ((Long) -> Unit)? = null
    private val stepCounterListener = object : SensorEventListener {
        override fun onSensorChanged(event: SensorEvent?) {
            event ?: return
            if (event.sensor.type != Sensor.TYPE_STEP_COUNTER) return
            stepCounterCallback?.invoke(event.values[0].toLong().coerceAtLeast(0L))
        }
        override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}
    }

    /* v1.15.2: enableSensor/disableSensor 已移除（传感器页删除） */

    /** 设备是否支持 STEP_COUNTER（API 29+ 未授权时返回 false） */
    fun hasStepCounter(): Boolean = try {
        val sm = stepSensorManager ?: return false
        sm.getDefaultSensor(Sensor.TYPE_STEP_COUNTER) != null
            || sm.getDefaultSensor(Sensor.TYPE_STEP_COUNTER, true) != null
            || sm.getSensorList(Sensor.TYPE_STEP_COUNTER).isNotEmpty()
    } catch (_: Throwable) { false }

    /** 启动 STEP_COUNTER 监听，回调参数为硬件自开机累积的原始读数 */
    fun enableStepCounter(onReading: (rawSinceBoot: Long) -> Unit) {
        if (stepCounterListening) disableStepCounter()
        val sm = stepSensorManager ?: return
        val sensor = try {
            sm.getDefaultSensor(Sensor.TYPE_STEP_COUNTER)
                ?: sm.getDefaultSensor(Sensor.TYPE_STEP_COUNTER, true)
                ?: sm.getSensorList(Sensor.TYPE_STEP_COUNTER).firstOrNull()
        } catch (e: Throwable) { null } ?: return
        stepCounterCallback = onReading
        stepCounterListening = true
        try {
            // F-04: 回调投递到主线程, 与 DeviceViewModel.pushStepUi (操作 ArrayDeque) 全程主线程,
            // 消除传感器线程直调导致的竞态; SensorDetailViewModel 走 enableSensor 不经此路径。
            sm.registerListener(
                stepCounterListener, sensor,
                SensorManager.SENSOR_DELAY_NORMAL,
                Handler(Looper.getMainLooper())
            )
        } catch (e: Throwable) {
            // F-13: 注册失败关键日志 (此前静默, 问题难回溯)
            Log.w(TAG, "STEP_COUNTER registerListener failed", e)
            stepCounterListening = false
            stepCounterCallback = null
        }
    }

    fun disableStepCounter() {
        if (!stepCounterListening) return
        stepCounterListening = false
        stepCounterCallback = null
        try { stepSensorManager?.unregisterListener(stepCounterListener) } catch (_: Throwable) {}
    }

    fun pushSensorHistory(sensorType: Int) {
        val meta = SensorTypeMeta.fromTypeId(sensorType) ?: return
        val prefix = "sensor_${sensorType}"
        val labels = meta.axisLabelResIds.map { appContext.getString(it) }
        val map = mutableMapOf<String, List<HistoryDataPoint>>()
        // ★ #3: 全量 getSeries(300点) → getRecentSeries(80点), 详情页每秒减少 ≈1.4 万次对象拷贝
        for (label in labels) map["${prefix}_${label}"] = historyCache.getRecentSeries("${prefix}_${label}", 80)
        sensorHistoryData.postValue(map)
    }

    // ═══════ 分模块间隔控制 ═══════

    private fun pushAllIntervalFlows(baseMs: Long) {
        // ★ v3.4.1 (四轮审查): 统一结算 — 原 start 路径直接推 settings 原值, 绕过
        //   paceOverride min 与省电封顶。旋转/主题变更等 Activity 重建 (onDispose 停 repo →
        //   新组合 startMonitoring) 会把提频覆盖回慢节奏, 此前仅靠两处 DisposableEffect 的
        //   源码书写顺序兜底 (脆弱未声明)。所有 interval writer 走同一结算 → 真正顺序无关。
        //   baseMs 形参保留兼容既有调用点 (原实现本就未消费)。
        pushPolicyAdjustedIntervals()
    }

    // Applies RefreshPolicy (省电模式封装，前后台不降频)
    private fun pushPolicyAdjustedIntervals() {
        val settings = AppSettings.getInstance(appContext)
        val tier = RefreshPolicy.Tier.NORMAL
        // ★ paceOverride 结算: 每模块 min(eff, override) 只提速; 省电封顶不突破
        //   (powerSave 时 eff ≥ BACKGROUND_CAP_MS, max 把 min 结果托回封顶线)
        val powerSaveFloor = if (RefreshPolicy.powerSaveModeFlow.value) RefreshPolicy.BACKGROUND_CAP_MS else 0L
        fun effMs(moduleMs: Int): Long {
            val v = RefreshPolicy.effectiveMs(settings.effectiveRefreshMs(moduleMs).toLong(), tier)
            return if (paceOverrideMs > 0L) maxOf(minOf(v, paceOverrideMs), powerSaveFloor) else v
        }
        cpuIntervalFlow.value = effMs(settings.cpuRefreshMs)
        gpuIntervalFlow.value = effMs(settings.gpuRefreshMs)
        memIntervalFlow.value = effMs(settings.memoryRefreshMs)
        batIntervalFlow.value = effMs(settings.batteryRefreshMs)
        intervalMs = cpuIntervalFlow.value  // 字段对齐 cpu 实际值, getIntervalMs() 语义保鲜
    }

    fun setIntervalMs(ms: Long) {
        if (ms == this.intervalMs) return
        this.intervalMs = ms
        cpuIntervalFlow.value = ms
        gpuIntervalFlow.value = ms
        memIntervalFlow.value = ms
        batIntervalFlow.value = ms
    }

    /**
     * ★ 悬浮窗提频窗 (2026-09-03 三轮审查) — 提频下沉到 policy 推流层统一结算:
     * setPaceOverride(baseTick) 后, 每次 pushPolicyAdjustedIntervals 对每模块
     * min(settings×policy 真值, override) 只提速不降速; 省电封顶不突破。
     *
     * 为什么不用"提频时 setIntervalMs 统一推流 + 归还时回填":
     *   1. startMonitoring 从停止态启动会 pushAllIntervalFlows(settings 原值) 覆盖
     *      调用方刚写入的提频值 (磁贴先开/STICKY 重启/后台重显路径慢一拍复发的根因),
     *      且 observer 异步初始推送还会再覆盖一次; 下沉后 observer 推送与
     *      setPaceOverride 推送同值, 竞态免疫, 顺序无关。
     *   2. 统一值会抹平用户 per-module 差异 (更快的模块被降速); min 语义只提速。
     *   3. 悬浮窗期间省电开关变化 → policy 事件自动重结算 (含 min), 无需悬浮窗干预。
     */
    fun setPaceOverride(ms: Long) {
        paceOverrideMs = ms
        pushPolicyAdjustedIntervals()
    }

    /** 悬浮窗隐藏/销毁时归还 — 清 override 并按当前活配置重推四模块 */
    fun clearPaceOverride() {
        paceOverrideMs = 0L
        pushPolicyAdjustedIntervals()
    }

    fun getIntervalMs(): Long = intervalMs

    // ═══════ 电池脉冲事件 ═══════
    fun batteryPulseFlow(): Flow<BatteryDataSource.BatteryPulseEvent> =
        batteryDataSource.monitorBatteryPulses()

    // ═══════ 分模块刷新间隔 getter/setter ═══════
    fun setCpuRefreshMs(ms: Long) {
        AppSettings.getInstance(appContext).cpuRefreshMs = ms.toInt()
        // ★ v3.4.1: 统一结算 — 单 flow 裸推会绕过 paceOverride min/省电封顶
        //   (悬浮窗显示中改模块间隔时, 悬浮窗节奏应仍不低于 override)
        pushPolicyAdjustedIntervals()
    }
    fun getCpuRefreshMs(): Long = AppSettings.getInstance(appContext).cpuRefreshMs.toLong()
    fun setGpuRefreshMs(ms: Long) {
        AppSettings.getInstance(appContext).gpuRefreshMs = ms.toInt()
        // ★ v3.4.1: 统一结算 — 单 flow 裸推会绕过 paceOverride min/省电封顶
        //   (悬浮窗显示中改模块间隔时, 悬浮窗节奏应仍不低于 override)
        pushPolicyAdjustedIntervals()
    }
    fun getGpuRefreshMs(): Long = AppSettings.getInstance(appContext).gpuRefreshMs.toLong()
    fun setMemoryRefreshMs(ms: Long) {
        AppSettings.getInstance(appContext).memoryRefreshMs = ms.toInt()
        // ★ v3.4.1: 统一结算 — 单 flow 裸推会绕过 paceOverride min/省电封顶
        //   (悬浮窗显示中改模块间隔时, 悬浮窗节奏应仍不低于 override)
        pushPolicyAdjustedIntervals()
    }
    fun getMemoryRefreshMs(): Long = AppSettings.getInstance(appContext).memoryRefreshMs.toLong()
    fun setBatteryRefreshMs(ms: Long) {
        AppSettings.getInstance(appContext).batteryRefreshMs = ms.toInt()
        // ★ v3.4.1: 统一结算 — 单 flow 裸推会绕过 paceOverride min/省电封顶
        //   (悬浮窗显示中改模块间隔时, 悬浮窗节奏应仍不低于 override)
        pushPolicyAdjustedIntervals()
    }
    fun getBatteryRefreshMs(): Long = AppSettings.getInstance(appContext).batteryRefreshMs.toLong()
}
