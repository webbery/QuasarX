<script setup lang="ts">
import { ref, reactive, computed, watch, onMounted, nextTick } from 'vue'
import axios from 'axios'
import sseService from '@/ts/SSEService'
import PromptDialog from '../PromptDialog.vue'
import StrategyPoolSelect from './StrategyPoolSelect.vue'
import { isAShareStock } from '@/lib/symbol'
import SymbolCode from '../SymbolCode.vue'
import { useRemoteStrategies } from './composables/useRemoteStrategies'
import { useIsLoggedIn } from './composables/useSession'

// ── 基本面数据状态 ──
const { remoteStrategies, remoteFetched, remoteReachable, fetchRemoteStrategies } = useRemoteStrategies()
const isLoggedIn = useIsLoggedIn()

// 每页条数（与行情面板独立）
const pageSize = ref(20)

// 当前选中的行（用于高亮）
const selectedDetailKey = ref<string | null>(null)

// PromptDialog 引用
const promptDialogRef = ref<{ confirm: (opts: { title: string; message: string }) => Promise<boolean> } | null>(null)

// 远程策略的标的池按需补拉（service 首次只返回策略清单，不含 symbols）
const loadRemotePoolSymbols = async (name: string): Promise<string[] | null> => {
    const remote = remoteStrategies.value.find(s => s.name === name)
    if (!remote || remote.failed) return null
    if (remote.symbols === null) {
        await fetchRemoteStrategies(true)
        const reloaded = remoteStrategies.value.find(s => s.name === name)
        if (reloaded) remote.symbols = reloaded.symbols ?? []
    }
    return remote.symbols ?? []
}

const financeCode = ref('')
const financeCategory = ref('')
const financeTables = ref<any[]>([])
const financeStatus = ref('')
const financeDownloading = ref(false)
const financeDeleting = ref(false)
const financeLoading = ref(false)
const financeLoaded = ref(false)
const financePage = ref(1)
const selectedFinance = ref(new Set<string>())
const categoryNameMap: Record<string, string> = {
    profit: '盈利能力', operation: '营运能力', growth: '成长能力',
    balance: '偿债能力', cashflow: '现金流量', dupont: '杜邦分析'
}
// 详情弹窗
const financeDetailVisible = ref(false)
const financeDetailTitle = ref('')
const financeDetailData = ref<any[]>([])
const financeDetailKeys = ref<string[]>([])

// 排序
//  基本面数据管理
// ═══════════════════════════════════════════════════════════

const financeFlatItems = computed(() => {
    const result = []
    for (const table of financeTables.value) {
        for (const sym of (table.symbols || [])) {
            result.push({
                category: table.category,
                symbol: typeof sym === 'string' ? sym : sym.symbol,
            })
        }
    }
    return result
})

const pagedFinanceItems = computed(() => {
    const start = (financePage.value - 1) * pageSize.value
    return financeFlatItems.value.slice(start, start + pageSize.value)
})

const financeTotalPages = computed(() => Math.ceil(financeFlatItems.value.length / pageSize.value))

const financeItemKey = (item: any) => `${item.category}|${item.symbol}`

// 点击行 → 高亮 + 通知右侧详情面板
const onFinanceRowClick = (item: any) => {
    // 基本面无 freq 概念，默认 1d
    selectedDetailKey.value = `finance|${item.category}|${item.symbol}`
    window.dispatchEvent(new CustomEvent('datacenter-symbol-selected', {
        detail: { symbol: item.symbol, freq: '1d' }
    }))
}
const isFinanceSelected = (item: any) => selectedFinance.value.has(financeItemKey(item))

const isFinanceAllSelected = computed(() => {
    return pagedFinanceItems.value.length > 0 && pagedFinanceItems.value.every(item => isFinanceSelected(item))
})

const toggleFinanceSelect = (item: any) => {
    const key = financeItemKey(item)
    if (selectedFinance.value.has(key)) selectedFinance.value.delete(key)
    else selectedFinance.value.add(key)
    selectedFinance.value = new Set(selectedFinance.value)
}

const toggleFinanceSelectAll = () => {
    if (isFinanceAllSelected.value) {
        for (const item of pagedFinanceItems.value) selectedFinance.value.delete(financeItemKey(item))
    } else {
        for (const item of pagedFinanceItems.value) selectedFinance.value.add(financeItemKey(item))
    }
    selectedFinance.value = new Set(selectedFinance.value)
}


// ── 基本面「按策略下载」──
const financeStrategy = ref('')
const financeStrategySymbols = ref('')       // A 股代码，逗号分隔
const financeStrategyDownloading = ref(false)
const financeTaskId = ref('')                // 当前任务 id，用于过滤并发任务的 SSE 事件
const financeProgress = reactive({ downloaded: 0, total: 0, failed: 0 })
const financeLogs = ref<{ time: string; text: string; type: string }[]>([])
const financeLogRef = ref<HTMLElement | null>(null)
const currentYear = new Date().getFullYear()

const financeStartYear = ref(String(currentYear - 5))
const financeEndYear = ref(String(currentYear))

const financeStrategySymbolCount = computed(() => {
    if (!financeStrategySymbols.value) return 0
    return financeStrategySymbols.value.split(',').filter(s => s.trim()).length
})

const financeCategoryLabel = computed(() =>
    financeCategory.value ? (categoryNameMap[financeCategory.value] || financeCategory.value) : '全部类别')

// 下载任务的 SSE 兜底超时：后端可能因网络/进程异常不再发 done，按钮不能永远卡住
const FINANCE_DOWNLOAD_TIMEOUT_MS = 60 * 60 * 1000
let financeTimer: number | null = null

const stopFinanceWatchdog = () => {
    if (financeTimer !== null) { clearTimeout(financeTimer); financeTimer = null }
}

// 两个下载入口共用一个看门狗：超时后停止等待并提示（服务端任务可能仍在后台跑）
const startFinanceWatchdog = () => {
    stopFinanceWatchdog()
    financeTimer = window.setTimeout(() => {
        financeTimer = null
        if (!financeStrategyDownloading.value && !financeDownloading.value) return
        financeStrategyDownloading.value = false
        financeDownloading.value = false
        sseService.off('finance_download', onFinanceDownloadEvent)
        financeStatus.value = '下载超时（60 分钟），已停止等待。服务端任务可能仍在后台运行。'
        addFinanceLog('下载超时，停止等待', 'error')
    }, FINANCE_DOWNLOAD_TIMEOUT_MS)
}

const addFinanceLog = (text: string, type: string = 'info') => {
    const now = new Date()
    const time = `${String(now.getHours()).padStart(2,'0')}:${String(now.getMinutes()).padStart(2,'0')}:${String(now.getSeconds()).padStart(2,'0')}`
    financeLogs.value.push({ time, text, type })
    nextTick(() => {
        if (financeLogRef.value) financeLogRef.value.scrollTop = financeLogRef.value.scrollHeight
    })
}

// 基本面下载 SSE：按 task_id 过滤，避免与其它财务任务的事件串台
const onFinanceDownloadEvent = (msg: any) => {
    const d = msg.data || {}
    if (financeTaskId.value && d.task_id && d.task_id !== financeTaskId.value) return

    switch (d.status) {
        case 'started':
            if (d.total) financeProgress.total = Number(d.total)
            addFinanceLog(`开始下载 ${financeProgress.total} 个标的`)
            break
        case 'symbol_downloaded':
            financeProgress.downloaded = Number(d.downloaded) || financeProgress.downloaded + 1
            addFinanceLog(`${d.code}: ${d.rows} 行 (${financeProgress.downloaded}/${financeProgress.total})`, 'done')
            break
        case 'symbol_failed':
            financeProgress.downloaded = Number(d.downloaded) || financeProgress.downloaded + 1
            financeProgress.failed++
            addFinanceLog(`${d.code} 下载失败`, 'error')
            break
        case 'done':
            financeStrategyDownloading.value = false
            financeDownloading.value = false
            sseService.off('finance_download', onFinanceDownloadEvent)
            stopFinanceWatchdog()
            financeStatus.value = `下载完成：${financeProgress.downloaded - financeProgress.failed}/${financeProgress.total} 个标的，共 ${d.total_rows || 0} 行`
            addFinanceLog(`全部完成，共 ${d.total_rows || 0} 行`, 'done')
            onLoadFinanceData()
            break
    }
}

const onDownloadFinanceByStrategy = async () => {
    if (!isLoggedIn.value) {
        financeStatus.value = '请先登录'
        return
    }
    if (!financeStrategy.value) {
        financeStatus.value = '请先选择策略'
        return
    }
    if (!financeStrategySymbols.value) {
        financeStatus.value = '所选策略没有 A 股标的（基本面数据仅覆盖 A 股股票）'
        return
    }

    financeStrategyDownloading.value = true
    financeProgress.downloaded = 0
    financeProgress.total = 0
    financeProgress.failed = 0
    financeLogs.value = []
    financeTaskId.value = ''

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    sseService.on('finance_download', onFinanceDownloadEvent)

    try {
        const resp = await axios.post(`https://${server}/v0/finance`, {
            symbols: financeStrategySymbols.value,
            category: financeCategory.value || 'all',
            start: financeStartYear.value,
            end: financeEndYear.value,
        }, { headers: { 'Authorization': token || '' } })

        financeTaskId.value = resp.data?.task_id || ''
        financeProgress.total = Number(resp.data?.symbol_count) || financeStrategySymbolCount.value
        financeStatus.value = `已提交：${financeProgress.total} 个 A 股标的，后台下载中...`
        addFinanceLog(`已提交 ${financeProgress.total} 个标的（${financeCategoryLabel.value} · ${financeStartYear.value}-${financeEndYear.value}）`)

        startFinanceWatchdog()
    } catch (err: any) {
        sseService.off('finance_download', onFinanceDownloadEvent)
        financeStrategyDownloading.value = false
        stopFinanceWatchdog()
        financeStatus.value = `提交失败: ${err.response?.data?.message || err.message}`
        addFinanceLog(`提交失败: ${err.message}`, 'error')
    }
}

onMounted(() => {
    if (isLoggedIn.value) onLoadFinanceData()
})

const onLoadFinanceData = async () => {
    if (!isLoggedIn.value) return
    financeLoading.value = true
    financeStatus.value = ''
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    try {
        const params: Record<string, string> = {}
        if (financeCategory.value) params.category = financeCategory.value
        if (financeCode.value.trim()) params.code = financeCode.value.trim()
        const resp = await axios.get(`https://${server}/v0/finance`, {
            params, headers: { 'Authorization': token || '' }
        })
        financeTables.value = resp.data.tables || []
        financeStatus.value = `查询完成，共 ${financeFlatItems.value.length} 条记录`
    } catch (err: any) {
        financeStatus.value = `查询失败: ${err.response?.data?.message || err.message}`
    } finally {
        financeLoading.value = false
        financeLoaded.value = true
    }
}

const onDownloadFinance = async () => {
    const code = financeCode.value.trim()
    if (!code) {
        financeStatus.value = '请输入标的代码'
        return
    }
    financeDownloading.value = true
    financeStatus.value = '正在下载...'
    financeProgress.downloaded = 0
    financeProgress.total = 0
    financeProgress.failed = 0
    financeLogs.value = []
    financeTaskId.value = ''

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    // 与「按策略下载」共用同一条 SSE 通道：完成后由 done 事件刷新下方列表
    sseService.on('finance_download', onFinanceDownloadEvent)

    try {
        const resp = await axios.post(`https://${server}/v0/finance`, {
            code,
            category: financeCategory.value || 'all'
        }, { headers: { 'Authorization': token || '' } })
        financeTaskId.value = resp.data?.task_id || ''
        financeStatus.value = '下载任务已提交，后台执行中...'
        addFinanceLog(`已提交 ${code}（${financeCategoryLabel.value}）`)
        startFinanceWatchdog()
    } catch (err: any) {
        sseService.off('finance_download', onFinanceDownloadEvent)
        financeDownloading.value = false
        stopFinanceWatchdog()
        financeStatus.value = `下载失败: ${err.response?.data?.message || err.message}`
        addFinanceLog(`提交失败: ${err.message}`, 'error')
    }
}

const onViewFinanceDetail = async (item: any) => {
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    try {
        const resp = await axios.get(`https://${server}/v0/finance`, {
            params: { category: item.category, code: item.symbol },
            headers: { 'Authorization': token || '' }
        })
        financeDetailData.value = resp.data.data || []
        financeDetailKeys.value = financeDetailData.value.length > 0 ? Object.keys(financeDetailData.value[0]) : []
        financeDetailTitle.value = `${item.symbol} - ${categoryNameMap[item.category] || item.category}`
        financeDetailVisible.value = true
    } catch (err: any) {
        financeStatus.value = `查询详情失败: ${err.response?.data?.message || err.message}`
    }
}

const onDeleteFinanceSymbol = async (item: any) => {
    const confirmed = await promptDialogRef.value?.confirm({
        title: '确认删除',
        message: `确定要删除 ${item.symbol} 的${categoryNameMap[item.category] || item.category}数据吗？`
    })
    if (!confirmed) return
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    try {
        await axios.delete(`https://${server}/v0/finance`, {
            params: { category: item.category, code: item.symbol },
            headers: { 'Authorization': token || '' }
        })
        financeStatus.value = `${item.symbol} 已删除`
        await onLoadFinanceData()
    } catch (err: any) {
        financeStatus.value = `删除失败: ${err.response?.data?.message || err.message}`
    }
}

const onDeleteAllFinance = async () => {
    const confirmed = await promptDialogRef.value?.confirm({
        title: '确认删除',
        message: '确定要清空所有基本面数据吗？此操作不可恢复！'
    })
    if (!confirmed) return
    financeDeleting.value = true
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    try {
        await axios.delete(`https://${server}/v0/finance`, {
            headers: { 'Authorization': token || '' }
        })
        financeStatus.value = '所有基本面数据已清空'
        financeTables.value = []
    } catch (err: any) {
        financeStatus.value = `删除失败: ${err.response?.data?.message || err.message}`
    } finally {
        financeDeleting.value = false
    }
}

const onBatchDeleteFinance = async () => {
    const confirmed = await promptDialogRef.value?.confirm({
        title: '确认批量删除',
        message: `确定要删除选中的 ${selectedFinance.value.size} 条财务数据吗？`
    })
    if (!confirmed) return
    let successCount = 0, failCount = 0
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    for (const key of selectedFinance.value) {
        const [category, symbol] = key.split('|')
        try {
            await axios.delete(`https://${server}/v0/finance`, {
                params: { category, code: symbol },
                headers: { 'Authorization': token || '' }
            })
            successCount++
        } catch { failCount++ }
    }
    selectedFinance.value = new Set()
    financeStatus.value = `批量删除完成：成功 ${successCount}，失败 ${failCount}`
    await onLoadFinanceData()
}

const onBatchUpdateFinance = async () => {
    const confirmed = await promptDialogRef.value?.confirm({
        title: '确认批量更新',
        message: `确定要重新下载选中的 ${selectedFinance.value.size} 条财务数据吗？`
    })
    if (!confirmed) return
    financeDownloading.value = true
    let successCount = 0, failCount = 0
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    for (const key of selectedFinance.value) {
        const [category, symbol] = key.split('|')
        try {
            await axios.post(`https://${server}/v0/finance`, {
                code: symbol, category
            }, { headers: { 'Authorization': token || '' } })
            successCount++
        } catch { failCount++ }
    }
    selectedFinance.value = new Set()
    financeStatus.value = `批量更新已提交：成功 ${successCount}，失败 ${failCount}`
    financeDownloading.value = false
    setTimeout(onLoadFinanceData, 3000)
}

// ═══════════════════════════════════════════════════════════
</script>

<template>
    <div class="tab-content">
        <div class="section-title">
            <i class="fas fa-file-invoice-dollar"></i> 基本面数据管理
        </div>

        <!-- 类别筛选：同时作用于两个下载入口与下方列表 -->
        <div class="input-row">
            <div class="input-group">
                <label>财务类别</label>
                <select v-model="financeCategory">
                    <option value="">全部</option>
                    <option value="profit">盈利能力</option>
                    <option value="operation">营运能力</option>
                    <option value="growth">成长能力</option>
                    <option value="balance">偿债能力</option>
                    <option value="cashflow">现金流量</option>
                    <option value="dupont">杜邦分析</option>
                </select>
            </div>
        </div>

        <!-- 下载区：单标的 / 按策略 两列 -->
        <div class="download-cards-grid finance-download-grid">
            <!-- 单标的下载 -->
            <div class="download-card">
                <div class="card-title"><i class="fas fa-crosshairs"></i> 单标的下载</div>
                <div class="input-group">
                    <label>标的代码</label>
                    <input type="text" placeholder="如 600519.SH" v-model="financeCode" />
                </div>
                <small class="field-hint">留空则查询会列出全部标的</small>
                <div class="button-row">
                    <button class="btn btn-sm" @click="onLoadFinanceData">
                        <i class="fas fa-search"></i> 查询
                    </button>
                    <button class="btn btn-primary btn-sm" @click="onDownloadFinance"
                            :disabled="financeDownloading || !financeCode.trim()">
                        <i class="fas fa-download"></i>
                        {{ financeDownloading ? '下载中...' : '下载财务数据' }}
                    </button>
                    <button class="btn-danger btn-sm" @click="onDeleteAllFinance" :disabled="financeDeleting">
                        <i class="fas fa-trash"></i> 清空所有
                    </button>
                </div>
            </div>

            <!-- 按策略下载 -->
            <div class="download-card">
                <div class="card-title">
                    <i class="fas fa-project-diagram"></i> 按策略下载
                    <span class="remote-status-dot"
                          :class="{ online: remoteReachable, offline: !remoteReachable }"
                          :title="remoteReachable
                            ? `service 在线 · ${remoteStrategies.length} 个远程策略`
                            : 'service 未连接或超时'"></span>
                </div>
                <StrategyPoolSelect
                    v-model="financeStrategy"
                    v-model:symbols="financeStrategySymbols"
                    :remote-strategies="remoteStrategies"
                    :remote-fetched="remoteFetched"
                    :load-remote-pools="loadRemotePoolSymbols"
                    :filter="isAShareStock"
                    unit-label="个 A 股"
                    skipped-label="已跳过"
                    select-class="quote-select"
                />
                <div class="input-row">
                    <div class="input-group">
                        <label>起始年份</label>
                        <input type="number" v-model.number="financeStartYear" min="1990" :max="financeEndYear" />
                    </div>
                    <div class="input-group">
                        <label>结束年份</label>
                        <input type="number" v-model.number="financeEndYear" :min="financeStartYear" :max="currentYear" />
                    </div>
                </div>
                <small class="field-hint">
                    下载范围：{{ financeCategoryLabel }} · {{ financeStartYear }}–{{ financeEndYear }}
                </small>
                <div class="button-row">
                    <button class="btn btn-primary btn-sm" @click="onDownloadFinanceByStrategy"
                            :disabled="financeStrategyDownloading || !isLoggedIn || !financeStrategy || financeStrategySymbolCount === 0">
                        <i class="fas fa-download"></i>
                        {{ financeStrategyDownloading ? '下载中...' : '下载策略标的池' }}
                    </button>
                </div>
            </div>
        </div>


        <!-- 下载进度 + 日志 -->
        <div v-if="financeProgress.total > 0" class="quote-progress">
            <div class="quote-progress-header">
                <span class="quote-progress-label">
                    <i class="fas fa-cloud-download-alt"></i> 下载中
                </span>
                <span class="quote-progress-count">
                    {{ financeProgress.downloaded }} / {{ financeProgress.total }} 标的
                    <template v-if="financeProgress.failed > 0">（失败 {{ financeProgress.failed }}）</template>
                </span>
            </div>
            <div class="quote-progress-bar">
                <div class="quote-progress-fill"
                     :style="{ width: financeProgress.total ? (financeProgress.downloaded / financeProgress.total * 100) + '%' : '0%' }">
                </div>
            </div>
        </div>
        <div v-if="financeLogs.length" class="quote-log-box">
            <div class="quote-log-title">下载日志</div>
            <div class="quote-log-content" ref="financeLogRef">
                <div v-for="(log, i) in financeLogs" :key="i" class="quote-log-line"
                     :class="{ 'log-error': log.type === 'error', 'log-success': log.type === 'done' }">
                    <span class="log-time">{{ log.time }}</span> {{ log.text }}
                </div>
            </div>
        </div>

        <!-- 基本面数据列表 -->
        <div class="manage-section">
            <div class="section-title">
                <i class="fas fa-database"></i> 基本面数据列表
            </div>

            <div v-if="financeStatus" class="status-text" :class="{ 'status-error': financeStatus.includes('失败') }">
                {{ financeStatus }}
            </div>

            <div v-if="financeTables.length > 0" class="quote-data-table">
            <div class="table-scroll">
                <table class="data-table">
                    <thead class="sticky-header">
                        <tr>
                            <th class="col-checkbox">
                                <input type="checkbox" :checked="isFinanceAllSelected" @change="toggleFinanceSelectAll" />
                            </th>
                            <th>类别</th>
                            <th>标的代码</th>
                            <th>操作</th>
                        </tr>
                    </thead>
                    <tbody>
                        <tr v-for="item in pagedFinanceItems" :key="`${item.category}-${item.symbol}`"
                            :class="{ 'row-selected': selectedDetailKey === `finance|${item.category}|${item.symbol}` }">
                            <td class="col-checkbox">
                                <input type="checkbox" :checked="isFinanceSelected(item)" @change="toggleFinanceSelect(item)" />
                            </td>
                            <td>{{ categoryNameMap[item.category] || item.category }}</td>
                            <td class="symbol-code clickable" @click="onFinanceRowClick(item)" :title="`点击查看 ${item.symbol} 详情`">
                                <SymbolCode :symbol="item.symbol" />
                            </td>
                            <td class="actions">
                                <button class="btn-info btn-xs" @click="onViewFinanceDetail(item)">详情</button>
                                <button class="btn-danger btn-xs" @click="onDeleteFinanceSymbol(item)">删除</button>
                            </td>
                        </tr>
                    </tbody>
                </table>
            </div>
            <div class="pagination" v-if="financeFlatItems.length > 0">
                <div class="pagination-left">
                    <button class="btn-warning btn-sm" @click="onBatchDeleteFinance" :disabled="selectedFinance.size === 0">
                        <i class="fas fa-trash"></i> 批量删除 ({{ selectedFinance.size }})
                    </button>
                    <button class="btn-info btn-sm" @click="onBatchUpdateFinance" :disabled="selectedFinance.size === 0">
                        <i class="fas fa-sync-alt"></i> 批量更新 ({{ selectedFinance.size }})
                    </button>
                </div>
                <div class="pagination-center" v-if="financeTotalPages > 1">
                    <button class="page-btn" :disabled="financePage === 1" @click="financePage--">
                        <i class="fas fa-angle-left"></i>
                    </button>
                    <span class="page-info">第 {{ financePage }} / {{ financeTotalPages }} 页（共 {{ financeFlatItems.length }} 条）</span>
                    <button class="page-btn" :disabled="financePage === financeTotalPages" @click="financePage++">
                        <i class="fas fa-angle-right"></i>
                    </button>
                </div>
            </div>
        </div>

            <div v-if="financeTables.length === 0 && !financeLoading && financeLoaded" class="empty-text">
                <i class="fas fa-info-circle"></i> 暂无基本面数据，请先下载
            </div>
        </div>

        <!-- 财务数据详情弹窗 -->
        <div v-if="financeDetailVisible" class="finance-detail-overlay" @click.self="financeDetailVisible = false">
            <div class="finance-detail-dialog">
                <div class="finance-detail-header">
                    <span>{{ financeDetailTitle }}</span>
                    <button class="finance-detail-close" @click="financeDetailVisible = false">&times;</button>
                </div>
                <div class="finance-detail-body">
                    <div class="table-scroll" style="max-height: 500px;">
                        <table class="data-table" v-if="financeDetailData.length > 0">
                            <thead class="sticky-header">
                                <tr>
                                    <th v-for="key in financeDetailKeys" :key="key">{{ key }}</th>
                                </tr>
                            </thead>
                            <tbody>
                                <tr v-for="(row, i) in financeDetailData" :key="i">
                                    <td v-for="key in financeDetailKeys" :key="key">
                                        {{ typeof row[key] === 'number' ? (Number.isInteger(row[key]) ? row[key] : row[key].toFixed(4)) : (row[key] || '-') }}
                                    </td>
                                </tr>
                            </tbody>
                        </table>
                    </div>
                </div>
            </div>
        </div>
        <PromptDialog ref="promptDialogRef" />
    </div>
</template>

<style scoped>
@import './datacenter.scss';
</style>
