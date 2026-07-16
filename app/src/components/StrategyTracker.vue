<template>
    <div class="strategy-tracker">
        <!-- 顶部策略选择器 -->
        <div class="strategy-selector">
            <label>追踪策略：</label>
            <select v-model="selectedStrategy">
                <option value="">-- 选择策略 --</option>
                <option v-for="s in strategyList" :key="s.name" :value="s.name">
                    {{ s.name }}
                    <span v-if="s.running" style="color: #4ade80;">● 运行中</span>
                    <span v-else style="color: #f87171;">○ 已停止</span>
                </option>
            </select>
        </div>

        <!-- Tab 切换 -->
        <div class="tabs">
            <button
                :class="{ active: activeTab === 'realtime' }"
                @click="activeTab = 'realtime'; viewingLogs = false"
            >
                <i class="fas fa-bolt"></i> 实时监控
            </button>
            <button
                :class="{ active: activeTab === 'review' }"
                @click="activeTab = 'review'"
            >
                <i class="fas fa-history"></i> 历史复盘
            </button>
            <button
                :class="{ active: activeTab === 'signals' }"
                @click="activeTab = 'signals'; viewingLogs = false"
            >
                <i class="fas fa-satellite-dish"></i> 信号监控
            </button>
        </div>

        <!-- 实时监控面板 -->
        <div v-if="activeTab === 'realtime'" class="tab-content realtime-content">
            <!-- 策略列表视图 -->
            <div v-if="!viewingLogs" class="strategy-list-view">
                <div v-if="strategyList.length === 0" class="empty-state">
                    <i class="fas fa-inbox"></i>
                    <p>暂无策略</p>
                    <span>请先在策略工厂中创建策略</span>
                </div>
                <div v-else class="strategy-table">
                    <table>
                        <thead>
                            <tr>
                                <th style="width: 60px;">状态</th>
                                <th>策略名称</th>
                                <th style="width: 120px;">分配资金</th>
                                <th style="width: 120px;">持仓市值</th>
                                <th style="width: 100px;">Epoch</th>
                                <th style="width: 120px;">最后执行</th>
                                <th style="width: 120px;">最后心跳</th>
                                <th style="width: 250px;">操作</th>
                            </tr>
                        </thead>
                        <tbody>
                            <tr v-for="s in strategyList" :key="s.name">
                                <td>
                                    <span class="status-dot" :class="{ running: s.running }"></span>
                                </td>
                                <td class="strategy-name" :title="s.name">{{ s.name }}</td>
                                <td class="capital-cell">{{ formatCapital(s.allocatedCapital) }}</td>
                                <td class="capital-cell">{{ formatCapital(s.usedCapital) }}</td>
                                <td class="epoch-count">{{ s.epochCount ?? 0 }}</td>
                                <td>
                                    <span v-if="s.lastEvoke" class="heartbeat">{{ formatHeartbeat(s.lastEvoke) }}</span>
                                    <span v-else class="heartbeat unknown">--</span>
                                </td>
                                <td>
                                    <span v-if="s.lastHeartbeat" class="heartbeat">{{ formatHeartbeat(s.lastHeartbeat) }}</span>
                                    <span v-else class="heartbeat unknown">--</span>
                                </td>
                                <td>
                                    <div class="action-buttons">
                                        <button
                                            v-if="s.running"
                                            class="btn btn-stop"
                                            @click="stopStrategy(s.name)"
                                            :disabled="isOperating(s.name)"
                                        >
                                            <i class="fas fa-stop"></i> 停止
                                        </button>
                                        <button
                                            v-else
                                            class="btn btn-start"
                                            @click="startStrategy(s.name)"
                                            :disabled="isOperating(s.name)"
                                        >
                                            <i class="fas fa-play"></i> 启动
                                        </button>
                                        <button
                                            class="btn btn-log"
                                            @click="viewLogs(s.name)"
                                        >
                                            <i class="fas fa-file-alt"></i> 日志
                                        </button>
                                        <button
                                            class="btn btn-delete"
                                            @click="deleteStrategy(s.name)"
                                            :disabled="isOperating(s.name)"
                                        >
                                            <i class="fas fa-trash"></i>
                                        </button>
                                    </div>
                                </td>
                            </tr>
                        </tbody>
                    </table>
                </div>
            </div>

            <!-- 日志查看视图 -->
            <div v-else class="log-view-container">
                <div class="log-view-header">
                    <button class="btn btn-back" @click="backToList">
                        <i class="fas fa-arrow-left"></i> 返回策略列表
                    </button>
                    <!-- 策略选择下拉框 -->
                    <select v-model="viewLogStrategy" class="log-strategy-select">
                        <option value="">全部策略</option>
                        <option v-for="name in strategyNames" :key="name" :value="name">
                            {{ name }}
                        </option>
                    </select>
                </div>
                <div class="log-view-content">
                    <StrategySelectPanel
                        :strategy-names="strategyNames"
                        :selected-strategy="viewLogStrategy"
                    />
                </div>
            </div>
        </div>

        <!-- 历史复盘面板 -->
        <div v-if="activeTab === 'review'" class="tab-content">
            <!-- 未选择策略时的提示 -->
            <div v-if="!selectedStrategy" class="empty-state">
                <i class="fas fa-chart-line"></i>
                <p>请选择策略</p>
                <span>选择策略后可查看完整复盘分析</span>
            </div>

            <!-- 复盘面板 -->
            <ReviewPanel
                v-else
                ref="reviewPanelRef"
                :strategy-id="selectedStrategy"
            />
        </div>

        <!-- 信号监控面板 -->
        <div v-if="activeTab === 'signals'" class="tab-content">
            <SignalMonitor />
        </div>
    </div>
</template>

<script setup>
import { ref, computed, inject, watch, onMounted } from 'vue'
import axios from 'axios'
import { message } from '../tool'
import ReviewPanel from './review/ReviewPanel.vue'
import SignalMonitor from './SignalMonitor.vue'
import StrategySelectPanel from './StrategySelectPanel.vue'

const activeTab = ref('realtime')
const selectedStrategy = ref('')

// 日志查看视图切换
const viewingLogs = ref(false)
const viewLogStrategy = ref('')

// 复用 App.vue 共享的策略状态（10s 轮询）
const serverStrategies = inject('serverStrategies', ref([]))
const fetchServerStrategies = inject('fetchServerStrategies', () => {})

const strategyNames = computed(() => serverStrategies.value.map(s => s.name))

const strategyList = serverStrategies

// ReviewPanel 引用
const reviewPanelRef = ref(null)

// 操作中的策略名（防止重复点击）
const operatingStrategies = ref(new Set())

/** 策略是否正在执行操作中 */
const isOperating = (name) => operatingStrategies.value.has(name)

/** 查看策略日志 */
const viewLogs = (name) => {
  viewLogStrategy.value = name
  viewingLogs.value = true
}

/** 从日志视图返回策略列表 */
const backToList = () => {
  viewingLogs.value = false
  viewLogStrategy.value = ''
}

/** 格式化心跳时间：距离 lastHeartbeat 的时间差 */
const formatHeartbeat = (lastHeartbeat) => {
    // lastHeartbeat 是 Unix 时间戳（秒），服务端返回 0 表示无心跳
    if (!lastHeartbeat) return '--'
    const elapsed = Math.floor(Date.now() / 1000) - lastHeartbeat
    if (elapsed < 0) return '刚刚'
    if (elapsed < 60) return `${elapsed}s前`
    if (elapsed < 3600) return `${Math.floor(elapsed / 60)}m前`
    return `${Math.floor(elapsed / 3600)}h${Math.floor((elapsed % 3600) / 60)}m前`
}

/** 格式化资金数值 */
const formatCapital = (value) => {
    if (value === undefined || value === null || value === 0) return '--'
    return `¥${Number(value).toLocaleString('zh-CN', { minimumFractionDigits: 2, maximumFractionDigits: 2 })}`
}

/** 停止策略 */
const stopStrategy = async (name) => {
    operatingStrategies.value.add(name)
    try {
        await axios.post('/v0/strategy', { mode: 2, name })
        message.success(`策略 "${name}" 已停止`)
        await fetchServerStrategies()
    } catch (e) {
        message.error('停止失败: ' + (e.response?.data?.message || e.message))
    } finally {
        operatingStrategies.value.delete(name)
    }
}

/** 启动策略 */
const startStrategy = async (name) => {
    operatingStrategies.value.add(name)
    try {
        await axios.post('/v0/strategy', { mode: 1, name })
        message.success(`策略 "${name}" 已启动`)
        await fetchServerStrategies()
    } catch (e) {
        message.error('启动失败: ' + (e.response?.data?.message || e.message))
    } finally {
        operatingStrategies.value.delete(name)
    }
}

/** 删除策略 */
const deleteStrategy = async (name) => {
    const s = strategyList.value.find(x => x.name === name)
    let confirmMsg = `确定要删除策略 "${name}" 吗？此操作不可恢复。`
    if (s && s.running) {
        confirmMsg = `策略 "${name}" 正在运行中，删除前会先停止它。确定继续吗？`
    }
    if (!confirm(confirmMsg)) return

    operatingStrategies.value.add(name)
    try {
        await axios.delete('/v0/strategy', { data: { name } })
        message.success(`策略 "${name}" 已删除`)
        // 如果当前选中的策略被删除，清空选择
        if (selectedStrategy.value === name) {
            selectedStrategy.value = ''
        }
        await fetchServerStrategies()
    } catch (e) {
        message.error('删除失败: ' + (e.response?.data?.message || e.message))
    } finally {
        operatingStrategies.value.delete(name)
    }
}

// 监听策略选择和 Tab 切换，自动加载复盘数据
watch([selectedStrategy, activeTab], ([newStrategy, newTab]) => {
    if (newStrategy && newTab === 'review' && reviewPanelRef.value) {
        reviewPanelRef.value.loadData()
    }
})

// 挂载时获取服务端策略信息
onMounted(async () => {
    await fetchServerStrategies()
})
</script>

<style scoped>
.strategy-tracker {
    height: 100%;
    display: flex;
    flex-direction: column;
    padding: 16px;
    gap: 16px;
}

.strategy-selector {
    display: flex;
    align-items: center;
    gap: 12px;
}

.strategy-selector label {
    font-weight: 600;
    color: #94a3b8;
}

.strategy-selector select {
    padding: 6px 12px;
    background: rgba(15, 23, 42, 0.7);
    border: 1px solid rgba(74, 158, 255, 0.3);
    border-radius: 8px;
    color: #e2e8f0;
    font-size: 14px;
    min-width: 200px;
}

.tabs {
    display: flex;
    gap: 4px;
    border-bottom: 1px solid rgba(74, 158, 255, 0.2);
}

.tabs button {
    padding: 8px 20px;
    background: transparent;
    border: none;
    color: #94a3b8;
    font-size: 14px;
    font-weight: 500;
    cursor: pointer;
    border-bottom: 2px solid transparent;
    transition: all 0.2s;
}

.tabs button:hover {
    color: #e2e8f0;
}

.tabs button.active {
    color: #60a5fa;
    border-bottom-color: #60a5fa;
}

.tab-content {
    flex: 1;
    display: flex;
    flex-direction: column;
    overflow: hidden;
    min-height: 0;
}

.empty-state {
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    padding: 60px 20px;
    color: #64748b;
    flex: 1;
}

.empty-state i {
    font-size: 48px;
    margin-bottom: 16px;
    opacity: 0.4;
}

.empty-state p {
    font-size: 18px;
    margin: 0 0 8px;
    color: #94a3b8;
}

.empty-state span {
    font-size: 13px;
}

/* 策略表格样式 */
.realtime-content {
  gap: 16px;
  overflow-y: auto;
}

.strategy-table {
    flex: 0 0 auto;
    overflow: auto;
}

.strategy-table table {
    width: 100%;
    border-collapse: collapse;
    color: #e2e8f0;
}

.strategy-table thead {
    position: sticky;
    top: 0;
    z-index: 1;
}

.strategy-table th {
    padding: 10px 12px;
    text-align: left;
    font-weight: 600;
    color: #94a3b8;
    font-size: 12px;
    text-transform: uppercase;
    letter-spacing: 0.05em;
    background: rgba(15, 23, 42, 0.9);
    border-bottom: 1px solid rgba(74, 158, 255, 0.2);
    border-right: 1px solid rgba(74, 158, 255, 0.1);
}

.strategy-table th:last-child {
    border-right: none;
}

.strategy-table td {
    padding: 10px 12px;
    border-bottom: 1px solid rgba(74, 158, 255, 0.08);
    border-right: 1px solid rgba(74, 158, 255, 0.08);
    font-size: 14px;
    vertical-align: middle;
}

.strategy-table td:last-child {
    border-right: none;
}

.strategy-table tbody tr {
    background: rgba(15, 23, 42, 0.5);
    transition: background 0.15s;
}

.strategy-table tbody tr:hover {
    background: rgba(30, 41, 59, 0.8);
}

/* 状态圆点 */
.status-dot {
    display: inline-block;
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background: #475569;
}

.status-dot.running {
    background: #4ade80;
    box-shadow: 0 0 6px rgba(74, 222, 128, 0.4);
}

/* 策略名称 */
.strategy-name {
    max-width: 200px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

/* Epoch 计数 */
.epoch-count {
    color: #94a3b8;
    font-family: 'SF Mono', 'Consolas', monospace;
    font-size: 13px;
}

/* 资金列 */
.capital-cell {
    font-family: 'SF Mono', 'Consolas', monospace;
    font-size: 13px;
    text-align: right;
    white-space: nowrap;
}

/* 心跳时间 */
.heartbeat {
    color: #94a3b8;
    font-family: 'SF Mono', 'Consolas', monospace;
    font-size: 13px;
}

.heartbeat.unknown {
    color: #475569;
}

/* 操作按钮 */
.action-buttons {
    display: flex;
    gap: 6px;
    align-items: center;
}

.btn {
    padding: 4px 10px;
    border: none;
    border-radius: 4px;
    font-size: 12px;
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    gap: 4px;
    transition: all 0.15s;
    color: #e2e8f0;
}

.btn:disabled {
    opacity: 0.4;
    cursor: not-allowed;
}

.btn-stop {
    background: rgba(251, 146, 60, 0.2);
    border: 1px solid rgba(251, 146, 60, 0.4);
}

.btn-stop:hover:not(:disabled) {
    background: rgba(251, 146, 60, 0.35);
}

.btn-start {
    background: rgba(74, 222, 128, 0.2);
    border: 1px solid rgba(74, 222, 128, 0.4);
}

.btn-start:hover:not(:disabled) {
    background: rgba(74, 222, 128, 0.35);
}

.btn-delete {
    background: rgba(248, 113, 113, 0.15);
    border: 1px solid rgba(248, 113, 113, 0.3);
    padding: 4px 8px;
    color: #f87171;
}

.btn-delete:hover:not(:disabled) {
    background: rgba(248, 113, 113, 0.3);
}

/* ── 策略列表视图 ── */
.strategy-list-view {
  display: flex;
  flex-direction: column;
  gap: 16px;
  overflow-y: auto;
}

/* ── 日志查看视图 ── */
.log-view-container {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 0;
  min-height: 0;
}

/* 日志容器滚动条（thin 风格） */
.log-view-content {
  flex: 1;
  overflow-y: auto;
  min-height: 0;
}

.log-view-content::-webkit-scrollbar {
  width: 6px;
}

.log-view-content::-webkit-scrollbar-track {
  background: transparent;
}

.log-view-content::-webkit-scrollbar-thumb {
  background: rgba(100, 116, 139, 0.4);
  border-radius: 3px;
}

.log-view-content::-webkit-scrollbar-thumb:hover {
  background: rgba(100, 116, 139, 0.6);
}

.log-view-header {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 12px 16px;
  background: rgba(15, 23, 42, 0.8);
  border: 1px solid rgba(74, 158, 255, 0.2);
  border-radius: 8px;
  margin-bottom: 12px;
}

.log-strategy-select {
  padding: 6px 12px;
  background: rgba(15, 23, 42, 0.9);
  border: 1px solid rgba(74, 158, 255, 0.3);
  border-radius: 6px;
  color: #e2e8f0;
  font-size: 13px;
  cursor: pointer;
  min-width: 150px;
}

/* ── 按钮样式 ── */
.btn-back {
  background: rgba(96, 165, 250, 0.15);
  border: 1px solid rgba(96, 165, 250, 0.3);
  color: #60a5fa;
  padding: 6px 12px;
}

.btn-back:hover {
  background: rgba(96, 165, 250, 0.3);
}

.btn-log {
  background: rgba(96, 165, 250, 0.15);
  border: 1px solid rgba(96, 165, 250, 0.3);
}

.btn-log:hover:not(:disabled) {
  background: rgba(96, 165, 250, 0.3);
}

/* 日志区块（保留给 StrategySelectPanel 内部样式） */
</style>
