<script setup lang="ts">
import { ref, reactive, computed, watch, onMounted, onUnmounted, nextTick } from 'vue'
import { ipcRenderer } from 'electron'
import axios from 'axios'
import sseService from '@/ts/SSEService'
import PromptDialog from '../PromptDialog.vue'
import StrategyPoolSelect from './StrategyPoolSelect.vue'
import { symbolMatches } from '@/lib/symbol'
import SymbolCode from '../SymbolCode.vue'
import { useRemoteStrategies } from './composables/useRemoteStrategies'
import { useIsLoggedIn } from './composables/useSession'

const { remoteStrategies, remoteFetched, remoteReachable, remoteLoading, fetchRemoteStrategies } = useRemoteStrategies()
const isLoggedIn = useIsLoggedIn()

// 数据下载区折叠状态
const downloadExpanded = ref(true)

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

// Tick 下载状态
const tickDbPath = ref('')
const tickSymbol = ref('')
const tickStartDate = ref('')
const tickEndDate = ref('')
const tickDownloading = ref(false)
const tickProgress = ref(0)
const tickCount = ref(0)
const tickDownloadStatus = ref('')

// 删除状态
const deleting = ref(false)
const deleteStatus = ref('')

// 行情下载状态
const quoteFreq = ref('5m')
const quoteSymbols = ref('')
const quoteStartDate = ref('')
const quoteEndDate = ref('')
const quoteDownloading = ref(false)
const quoteStatus = ref('')
const quoteLogs = ref<any[]>([])
const quoteLogRef = ref<HTMLElement | null>(null)
const quoteProgress = reactive({ downloaded: 0, total: 0, phase: '' as '' | 'downloading' | 'importing' })

// SSE 进度是按「分组」下发的，每组各自从 0 重新计数，所以要按组记录再求和。
// 「更新到最新」/「批量更新」会对每个 (table, startDate) 各发一次 POST，于是同一
// asset_type 会有多个先后相继的分组 —— 用递增序号区分，并记住每种 asset_type
// 当前活跃的那个分组，symbol_* 事件才知道该记到哪一组头上
const quoteGroupProgress = reactive<Record<string, { downloaded: number; total: number }>>({})
const quoteActiveGroup = new Map<string, string>()
let quoteGroupSeq = 0

const sumGroupProgress = (key: 'downloaded' | 'total') =>
    Object.values(quoteGroupProgress).reduce((sum, g) => sum + (g[key] || 0), 0)

const resetQuoteProgress = () => {
    Object.keys(quoteGroupProgress).forEach(k => { delete quoteGroupProgress[k] })
    quoteActiveGroup.clear()
    quoteProgress.downloaded = 0
    quoteProgress.total = 0
    quoteProgress.phase = ''
}

// 总数只认 POST /v0/quote 响应里的 groups —— 服务端返回的是本次请求实际受理的标的清单，
// 是权威值。SSE 事件可能因重连被重复投递，拿它累加分母会让 42 显示成 252
const applyGroupsTotal = (groups: any) => {
    if (!Array.isArray(groups)) return
    const n = groups.reduce((sum, g) =>
        sum + String(g?.symbols ?? '').split(',').filter(s => s.trim()).length, 0)
    if (n > 0) quoteProgress.total += n
}

const openQuoteGroup = (d: any) => {
    const at = d.asset_type || 'default'
    const key = `${at}#${++quoteGroupSeq}`
    quoteGroupProgress[key] = { downloaded: 0, total: Number(d.total) || 0 }
    quoteActiveGroup.set(at, key)
}

// 服务端已下发权威的 downloaded，直接赋值而不是 ++：
// 同一事件被重复投递时结果不变，进度不会成倍虚高
const applyGroupDownloaded = (d: any) => {
    const at = d.asset_type || 'default'
    let key = quoteActiveGroup.get(at)
    if (!key) {
        // 漏了 started（重连中途接入等），就地补一个分组，别让这批计数丢失
        openQuoteGroup({ asset_type: at, total: d.total })
        key = quoteActiveGroup.get(at)!
    }
    const g = quoteGroupProgress[key]
    if (!g) return
    g.downloaded = Number(d.downloaded) || 0
    quoteProgress.downloaded = sumGroupProgress('downloaded')
}

// 按策略下载行情（与 dividend tab 同样思路：取最新版本 flowData → 提取 symbols）
const quoteStrategy = ref('')
const quoteStrategySymbols = ref('')
const quoteStrategyFreq = ref('1d')
const quoteStrategyStartDate = ref('')
const quoteStrategyEndDate = ref('')
const quoteStrategyDownloading = ref(false)
const quoteStrategySymbolCount = computed(() => {
    if (!quoteStrategySymbols.value) return 0
    return quoteStrategySymbols.value.split(',').filter(s => s.trim()).length
})

// 批量下载选中：可选起始日期（留空按各标的 end_time+1）
const quoteBatchStartDate = ref('')

// 服务端数据列表状态
const quoteDataList = ref<any[]>([])
const loadingQuoteData = ref(false)
const deletingQuote = ref(false)
const deletingSymbol = ref(false)
const updatingSymbol = ref(false)
const isUpdatingLatest = ref(false)

// 标记当前 /v0/quote POST 由哪个按钮发起的（让 onQuoteDownloadEvent 只重置对应按钮的标志位）
type QuoteDownloadScenario = 'idle' | 'single' | 'strategy' | 'updateLatest'
const activeDownloadScenario = ref<QuoteDownloadScenario>('idle')

// "更新到最新"任务的 SSE done 等待超时（兜底，避免某个组 done 丢失导致按钮永久卡住）
const UPDATE_LATEST_TIMEOUT_MS = 30 * 60 * 1000
const updateLatestTimer = ref<number | null>(null)

// 跟踪"更新到最新"任务的 SSE done 事件（等所有组完成后再刷新列表）
const expectedDones = ref(0)
const completedDones = ref(0)
const onUpdateLatestDone = (msg: any) => {
    if (msg.data.status !== 'done') return
    completedDones.value++
    if (completedDones.value >= expectedDones.value) {
        if (updateLatestTimer.value !== null) {
            clearTimeout(updateLatestTimer.value)
            updateLatestTimer.value = null
        }
        sseService.off('quote_download', onUpdateLatestDone)
        activeDownloadScenario.value = 'idle'
        addQuoteLog(`✓ 全部组下载完成，刷新列表`, 'done')
        loadQuoteData()
        isUpdatingLatest.value = false
    }
}

// "更新到最新"超时兜底：done 计数卡住时强制结束任务
const onUpdateLatestTimeout = () => {
    if (!isUpdatingLatest.value) return
    sseService.off('quote_download', onUpdateLatestDone)
    addQuoteLog(`⚠ 更新超时（${Math.round(UPDATE_LATEST_TIMEOUT_MS / 60000)} 分钟）：已完成 ${completedDones.value}/${expectedDones.value} 组，剩余组视为失败`, 'error')
    quoteStatus.value = `更新超时：${completedDones.value}/${expectedDones.value} 组完成`
    loadQuoteData()
    isUpdatingLatest.value = false
    activeDownloadScenario.value = 'idle'
    updateLatestTimer.value = null
}

// 批量更新分组类型
interface SymbolInfo {
    table: string
    symbol: string
    endTime: string
    startDate: string  // 增量起始日期
}

// 导出状态
const exportDir = ref(localStorage.getItem('quoteExportPath') || '')
const exporting = ref(false)
const downloadingCSV = ref(false)

// 分页
const currentPage = ref(1)
const pageSize = ref(20)

// 标的筛选
const symbolFilter = ref('')
const assetTypeFilter = ref('')
const freqFilter = ref('')

// 频率筛选项（value 为表名后缀，与后端 toString(BarFreq) 一致）
// 注意：下载接口的 "60m" 经 parseBarFreq 落到 BarFreq::Hour1，落表后缀是 1h，
// 即 60分钟 与 1小时 是同一批数据，这里只列 1小时
const FREQ_FILTER_OPTIONS = [
    { value: '', label: '全部' },
    { value: '1d', label: '日线' },
    { value: '5m', label: '5分钟' },
    { value: '15m', label: '15分钟' },
    { value: '30m', label: '30分钟' },
    { value: '1h', label: '1小时' },
]

// 表名后缀归一：日线在下载接口叫 daily，历史表可能残留 60m 后缀（等价 1h）
const FREQ_ALIASES: Record<string, string> = { daily: '1d', '60m': '1h' }
const normalizeFreq = (rawFreq: string) => FREQ_ALIASES[rawFreq] || rawFreq

const sortKey = ref('')
const sortOrder = ref('asc') // 'asc' or 'desc'


const handleSort = (key: string) => {
    if (sortKey.value === key) {
        // Toggle sort order
        sortOrder.value = sortOrder.value === 'asc' ? 'desc' : 'asc'
    } else {
        sortKey.value = key
        sortOrder.value = 'asc'
    }
    currentPage.value = 1 // Reset to first page
}

// 将所有标的展平为一维列表（供分页使用）
const allFlatSymbols = computed(() => {
    const freqMap: Record<string, string> = {
        '1d': '日线', '5m': '5分钟', '15m': '15分钟',
        '30m': '30分钟', '60m': '60分钟', '1h': '1小时',
        'daily': '日线'
    }
    const result = []
    for (const table of quoteDataList.value) {
        // 从表名解析类型和频率：如 "etf_5m" → "ETF", "5m"
        const parts = table.table.split('_')
        const assetType = parts[0] === 'etf' ? 'ETF' : 'Stock'
        const rawFreq = parts.length > 1 ? parts.slice(1).join('_') : '-'
        const freq = freqMap[rawFreq] || rawFreq

        for (const sym of table.symbols) {
            result.push({ table: table.table, assetType, freq, rawFreq, ...sym })
        }
    }
    return result
})

// 应用筛选和排序后的标的列表
const flatSymbols = computed(() => {
    let result = allFlatSymbols.value

    // 按标的代码筛选（支持 600000 / 600000.SH / sh.600000 / 沪）
    if (symbolFilter.value.trim()) {
        result = result.filter(item => symbolMatches(item.symbol, symbolFilter.value))
    }

    // 按资产类型筛选
    if (assetTypeFilter.value) {
        result = result.filter(item => item.assetType === assetTypeFilter.value)
    }

    // 按频率筛选
    if (freqFilter.value) {
        result = result.filter(item => normalizeFreq(item.rawFreq) === freqFilter.value)
    }

    // 排序
    if (sortKey.value) {
        result = [...result].sort((a, b) => {
            let aVal = a[sortKey.value]
            let bVal = b[sortKey.value]

            // 处理 null/undefined
            if (aVal == null) aVal = ''
            if (bVal == null) bVal = ''

            let comparison = 0
            if (sortKey.value === 'count') {
                // 数字排序
                comparison = aVal - bVal
            } else {
                // 字符串排序
                comparison = String(aVal).localeCompare(String(bVal))
            }

            return sortOrder.value === 'asc' ? comparison : -comparison
        })
    }

    return result
})

// 批量选择
const selectedSymbols = ref(new Set<string>())  // key: "table|symbol"

// 当前页的标的列表
const pagedSymbols = computed(() => {
    const start = (currentPage.value - 1) * pageSize.value
    return flatSymbols.value.slice(start, start + pageSize.value)
})

// 总页数
const totalPages = computed(() => Math.ceil(flatSymbols.value.length / pageSize.value))

// 当前页是否全选
const isAllSelected = computed(() => {
    return pagedSymbols.value.length > 0 && pagedSymbols.value.every(item => isSelected(item))
})

const itemKey = (item: any) => `${item.table}|${item.symbol}`

// ── 右侧详情面板：选中标的 ──
const selectedDetailKey = ref<string | null>(null)

const onSymbolRowClick = (item: any) => {
    const key = itemKey(item)
    // 切换选中：点击同一行再次切换（不实现，用户要求无需清除）
    selectedDetailKey.value = key
    // 把 table 名解析为 freq 名（兼容 stock_5m 等），rawFreq 已挂上 item
    const rawFreq = item.rawFreq || '1d'
    window.dispatchEvent(new CustomEvent('datacenter-symbol-selected', {
        detail: { symbol: item.symbol, freq: rawFreq, table: item.table }
    }))
}

const onFinanceRowClick = (item: any) => {
    // 基本面无 freq 概念，默认 1d
    selectedDetailKey.value = `finance|${item.category}|${item.symbol}`
    window.dispatchEvent(new CustomEvent('datacenter-symbol-selected', {
        detail: { symbol: item.symbol, freq: '1d' }
    }))
}

const isSelected = (item: any) => selectedSymbols.value.has(itemKey(item))

const toggleSelect = (item: any) => {
    const key = itemKey(item)
    if (selectedSymbols.value.has(key)) {
        selectedSymbols.value.delete(key)
    } else {
        selectedSymbols.value.add(key)
    }
    selectedSymbols.value = new Set(selectedSymbols.value) // trigger reactivity
}

const toggleSelectAll = () => {
    if (isAllSelected.value) {
        // 取消当前页全选
        for (const item of pagedSymbols.value) {
            selectedSymbols.value.delete(itemKey(item))
        }
    } else {
        // 当前页全选
        for (const item of pagedSymbols.value) {
            selectedSymbols.value.add(itemKey(item))
        }
    }
    selectedSymbols.value = new Set(selectedSymbols.value)
}

// 监听进度事件
const onTickProgress = (_: any, data: any) => {
    tickCount.value = data.count
}

// 行情下载 SSE 事件处理
const addQuoteLog = (text: string, type: string = 'info') => {
    const now = new Date()
    const time = `${String(now.getHours()).padStart(2,'0')}:${String(now.getMinutes()).padStart(2,'0')}:${String(now.getSeconds()).padStart(2,'0')}`
    quoteLogs.value.push({ time, text, type })
    nextTick(() => {
        if (quoteLogRef.value) quoteLogRef.value.scrollTop = quoteLogRef.value.scrollHeight
    })
}

const onQuoteDownloadEvent = (msg: any) => {
    const d = msg.data
    switch (d.status) {
        case 'started':
            // 只登记分组，不动全局 total —— total 由 POST 响应决定（见 applyGroupsTotal）
            openQuoteGroup(d)
            quoteProgress.phase = 'downloading'
            addQuoteLog(`📥 开始下载: ${d.total} 只标的 (${d.asset_type})`)
            break
        case 'symbol_downloaded':
            applyGroupDownloaded(d)
            addQuoteLog(`✅ ${d.symbol}: ${d.rows} 行 (${d.downloaded}/${d.total})`, 'success')
            break
        case 'symbol_failed':
            applyGroupDownloaded(d)
            addQuoteLog(`❌ ${d.symbol}: 下载失败 (${d.error})`, 'error')
            break
        case 'downloaded':
            addQuoteLog(`📥 下载完成: ${d.downloaded} 成功，${d.failed} 失败，开始导入...`)
            quoteProgress.phase = 'importing'
            break
        case 'download_failed':
            addQuoteLog('脚本执行失败', 'error')
            if (d.output) {
                const lines = d.output.split(/\r?\n/).filter((l: string) => l.trim())
                lines.forEach((line: string) => addQuoteLog(line, 'error'))
            }
            break
        case 'importing':
            addQuoteLog(`导入 ${d.table}: ${d.symbol} (${d.rows} 行)`, 'success')
            break
        case 'done':
            // 仅当本 SSE 事件来自当前激活场景时，才重置对应按钮的标志位
            // （避免「更新到最新」运行中，单/策略按钮被错误关闭）
            if (activeDownloadScenario.value === 'single') {
                quoteDownloading.value = false
                quoteProgress.phase = ''
            } else if (activeDownloadScenario.value === 'strategy') {
                quoteStrategyDownloading.value = false
                quoteProgress.phase = ''
            }
            // 'updateLatest' / 'idle' 不在这里重置标志位（各自有专门的 done 处理）
            if (d.success === 'true') {
                addQuoteLog(`✅ 完成: 共导入 ${d.total_rows} 行 → ${d.table}`, 'done')
                if (activeDownloadScenario.value === 'single' || activeDownloadScenario.value === 'strategy') {
                    quoteStatus.value = `下载完成，${d.total_rows} 行已导入 ${d.table}`
                }
            } else {
                addQuoteLog(`❌ ${d.table} 下载失败`, 'error')
                if (activeDownloadScenario.value === 'single' || activeDownloadScenario.value === 'strategy') {
                    quoteStatus.value = '下载失败'
                }
            }
            break
    }
}

onMounted(() => {
    // 只在首次挂载时注册 SSE handler（KeepAlive 不会触发重复注册）
    ipcRenderer.on('tick-download-progress', onTickProgress)
    sseService.on('quote_download', onQuoteDownloadEvent)

    // 加载已下载的行情数据列表
    if (isLoggedIn.value) loadQuoteData()
})

// 远程组卡片展开时按需补齐 pools（仅当有策略还没拉过 symbols）
watch(downloadExpanded, async (v) => {
    if (!v) return
    const needPools = remoteStrategies.value.some(s => s.symbols === null && !s.failed)
    if (needPools && !remoteLoading.value) {
        await fetchRemoteStrategies(true)
    }
})

onUnmounted(() => {
    // 组件真正销毁时才移除 handler
    ipcRenderer.removeListener('tick-download-progress', onTickProgress)
    sseService.off('quote_download', onQuoteDownloadEvent)
    sseService.off('quote_download', onUpdateLatestDone)
    if (updateLatestTimer.value !== null) {
        clearTimeout(updateLatestTimer.value)
        updateLatestTimer.value = null
    }
})

const onHandleTickDownload = async () => {
    tickDownloading.value = true
    tickCount.value = 0
    tickDownloadStatus.value = '正在连接服务端...'

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    const url = `https://${server}/v0/replay`
    const startTs = tickStartDate.value ? Math.floor(new Date(tickStartDate.value).getTime() / 1000) : 0
    const endTs = tickEndDate.value ? Math.floor(new Date(tickEndDate.value + 'T23:59:59').getTime() / 1000) : 0

    const dbPath = tickDbPath.value || 'tick_data.db'

    try {
        const result = await ipcRenderer.invoke('tick-sync-to-duckdb',
            url, token, tickSymbol.value || null, startTs, endTs, dbPath)

        if (result.success) {
            tickDownloadStatus.value = `下载完成，共 ${result.count} 条 tick → ${dbPath}`
        } else {
            tickDownloadStatus.value = `下载失败: ${result.error}`
        }
    } catch (err: any) {
        tickDownloadStatus.value = `下载失败: ${err.message}`
    } finally {
        tickDownloading.value = false
    }
}

const onHandleQuoteDownload = async () => {
    // 检查是否已登录
    if (!isLoggedIn.value) {
        quoteStatus.value = '请先登录后再下载行情数据'
        return
    }

    if (!quoteSymbols.value.trim()) {
        quoteStatus.value = '请输入标的代码'
        return
    }

    quoteDownloading.value = true
    activeDownloadScenario.value = 'single'
    quoteStatus.value = ''
    quoteLogs.value = []
    resetQuoteProgress()

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    try {
        const resp = await axios.post(`https://${server}/v0/quote`, {
            symbols: quoteSymbols.value.trim(),
            freq: quoteFreq.value,
            start: quoteStartDate.value || undefined,
            end: quoteEndDate.value || undefined,
        }, {
            headers: { 'Authorization': token || '' }
        })
        // 总数取响应里的 groups；SSE 只负责推进分子
        applyGroupsTotal(resp.data?.groups)
        // POST 立即返回，进度通过 SSE 推送
    } catch (err: any) {
        quoteDownloading.value = false
        activeDownloadScenario.value = 'idle'
        quoteStatus.value = `请求失败: ${err.response?.data?.message || err.message}`
        addQuoteLog(`请求失败: ${err.message}`, 'error')
    }
}

// 行情「批量下载选中」：复用 onBatchUpdateSymbols 的按 table+end_time 分组逻辑
const onBatchDownloadSelected = async () => {
    if (!isLoggedIn.value) {
        quoteStatus.value = '请先登录'
        return
    }
    if (selectedSymbols.value.size === 0) {
        quoteStatus.value = '请先在下方表格勾选标的'
        return
    }
    // 当前实现 = 增量到最新（每标的按 end_time+1 开始）
    // quoteBatchStartDate 作为预留：未来如需强制从某日开始重下可启用
    addQuoteLog(`开始批量下载选中: ${selectedSymbols.value.size} 只标的`)
    await onBatchUpdateSymbols()
}

// 行情「按策略下载」：取策略标的池 → POST /v0/quote
const onDownloadQuoteByStrategy = async () => {
    if (!isLoggedIn.value) {
        quoteStatus.value = '请先登录'
        return
    }
    if (!quoteStrategy.value) {
        quoteStatus.value = '请先选择策略'
        return
    }
    if (!quoteStrategySymbols.value) {
        quoteStatus.value = '所选策略没有可用标的'
        return
    }

    quoteStrategyDownloading.value = true
    activeDownloadScenario.value = 'strategy'
    quoteStatus.value = `正在从策略「${quoteStrategy.value}」下载 ${quoteStrategySymbolCount.value} 个标的...`
    quoteLogs.value = []
    resetQuoteProgress()

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    try {
        const resp = await axios.post(`https://${server}/v0/quote`, {
            symbols: quoteStrategySymbols.value,
            freq: quoteStrategyFreq.value,
            start: quoteStrategyStartDate.value || undefined,
            end: quoteStrategyEndDate.value || undefined,
        }, {
            headers: { 'Authorization': token || '' }
        })
        // 总数取响应里的 groups（服务端受理的标的清单），不用 SSE 反推
        applyGroupsTotal(resp.data?.groups)
        addQuoteLog(`✓ 已提交下载请求：${quoteStrategySymbolCount.value} 个标的 (${quoteStrategyFreq.value})`)
    } catch (err: any) {
        quoteStrategyDownloading.value = false
        activeDownloadScenario.value = 'idle'
        quoteStatus.value = `请求失败: ${err.response?.data?.message || err.message}`
        addQuoteLog(`请求失败: ${err.message}`, 'error')
    }
}

const onHandleDeleteServerTicks = async () => {
    const confirmed = await promptDialogRef.value?.confirm({
        title: '确认删除',
        message: '确定要删除服务端所有 Tick 数据吗？此操作不可恢复！'
    })
    if (!confirmed) return

    deleting.value = true
    deleteStatus.value = '正在删除...'

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    const url = `https://${server}/v0/replay`

    try {
        // before=0 表示删除全部数据
        const res = await ipcRenderer.invoke('delete-server-ticks', url, token, 0)
        if (res.deleted) {
            deleteStatus.value = '删除成功'
        } else if (res.error) {
            deleteStatus.value = `删除失败: ${res.error}`
        } else {
            deleteStatus.value = '删除完成'
        }
    } catch (err: any) {
        deleteStatus.value = `删除失败: ${err.message}`
    } finally {
        deleting.value = false
    }
}

// 加载已下载的行情数据列表
const loadQuoteData = async () => {
    if (!isLoggedIn.value) {
        quoteStatus.value = '请先登录后查看数据'
        return
    }

    loadingQuoteData.value = true
    quoteDataList.value = []
    currentPage.value = 1

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    try {
        const response = await axios.get(`https://${server}/v0/quote`, {
            headers: { 'Authorization': token || '' }
        })
        quoteDataList.value = response.data
    } catch (err: any) {
        quoteStatus.value = `加载数据列表失败: ${err.response?.data?.message || err.message}`
    } finally {
        loadingQuoteData.value = false
    }
}

// 删除单个表
const onDeleteTable = async (tableName: string) => {
    const confirmed = await promptDialogRef.value?.confirm({
        title: '确认删除',
        message: `确定要删除表 "${tableName}" 的所有数据吗？此操作不可恢复！`
    })
    if (!confirmed) return

    deletingQuote.value = true

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    try {
        await axios.delete(`https://${server}/v0/quote?table=${tableName}`, {
            headers: { 'Authorization': token || '' }
        })
        quoteStatus.value = `表 ${tableName} 已删除`
        // 刷新列表
        await loadQuoteData()
    } catch (err: any) {
        quoteStatus.value = `删除失败: ${err.response?.data?.message || err.message}`
    } finally {
        deletingQuote.value = false
    }
}

// 更新单个标的（重新下载）
const onUpdateSymbol = async (tableName: string, symbol: string) => {
    const confirmed = await promptDialogRef.value?.confirm({
        title: '确认更新',
        message: `确定要重新下载表 "${tableName}" 中标的 "${symbol}" 的数据吗？`
    })
    if (!confirmed) return

    updatingSymbol.value = true

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    try {
        // 从表名提取频率（如 "etf_1d" → "1d"）
        const rawFreq = tableName.includes('_') ? tableName.split('_').slice(1).join('_') : '5m'
        // 映射为 Python 脚本接受的频率格式
        const freqMap: Record<string, string> = {
            '1d': 'daily', 'daily': 'daily', '日线': 'daily',
            '5m': '5m', '5分钟': '5m',
            '15m': '15m', '15分钟': '15m',
            '30m': '30m', '30分钟': '30m',
            '60m': '60m', '1h': '60m', '60分钟': '60m', '1小时': '60m',
        }
        const freq = freqMap[rawFreq] || rawFreq

        // 转换 symbol 格式：sh.510300 → 510300.SH
        const normalizedSymbol = symbol.replace(/^([a-z]+)\.(\d+)$/, '$2.$1').toUpperCase()

        await axios.post(`https://${server}/v0/quote`, {
            symbols: normalizedSymbol,
            freq,
        }, {
            headers: { 'Authorization': token || '' }
        })
        quoteStatus.value = `标的 ${symbol} 更新任务已提交`
    } catch (err: any) {
        quoteStatus.value = `更新失败: ${err.response?.data?.message || err.message}`
    } finally {
        updatingSymbol.value = false
    }
}

// 删除单个标的
const onDeleteSymbol = async (tableName: string, symbol: string) => {
    const confirmed = await promptDialogRef.value?.confirm({
        title: '确认删除',
        message: `确定要删除表 "${tableName}" 中标的 "${symbol}" 的数据吗？此操作不可恢复！`
    })
    if (!confirmed) return

    deletingSymbol.value = true

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    try {
        await axios.delete(`https://${server}/v0/quote?table=${tableName}&symbol=${symbol}`, {
            headers: { 'Authorization': token || '' }
        })
        quoteStatus.value = `标的 ${symbol} 已删除`
        // 刷新列表（loadQuoteData 会重置 currentPage）
        await loadQuoteData()
    } catch (err: any) {
        quoteStatus.value = `删除失败: ${err.response?.data?.message || err.message}`
    } finally {
        deletingSymbol.value = false
    }
}

// 批量删除选中的标的
const onBatchDeleteSymbols = async () => {
    const confirmed = await promptDialogRef.value?.confirm({
        title: '确认批量删除',
        message: `确定要删除选中的 ${selectedSymbols.value.size} 个标的的数据吗？此操作不可恢复！`
    })
    if (!confirmed) return

    deletingSymbol.value = true

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    let successCount = 0
    let failCount = 0

    for (const key of selectedSymbols.value) {
        const [table, symbol] = key.split('|')
        try {
            await axios.delete(`https://${server}/v0/quote?table=${table}&symbol=${symbol}`, {
                headers: { 'Authorization': token || '' }
            })
            successCount++
        } catch (err: any) {
            failCount++
            console.error(`[DataCenter] Failed to delete ${symbol}:`, err)
        }
    }

    selectedSymbols.value = new Set()
    quoteStatus.value = `批量删除完成：成功 ${successCount}，失败 ${failCount}`
    await loadQuoteData()

    deletingSymbol.value = false
}

// 批量更新选中的标的（按起始时间分组合并，跳过已是最新日期的标的）
const onBatchUpdateSymbols = async () => {
    updatingSymbol.value = true
    resetQuoteProgress()

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    // 获取今天的日期（YYYY-MM-DD）
    const today = new Date()
    const todayStr = today.toISOString().split('T')[0]

    // === 第 1 步：构建标的信息映射，过滤已是最新日期的标的 ===
    const symbolInfos: SymbolInfo[] = []
    let skippedCount = 0
    const skippedSymbols: string[] = []

    for (const key of selectedSymbols.value) {
        const [table, symbol] = key.split('|')
        const item = allFlatSymbols.value.find(s => s.table === table && s.symbol === symbol)
        const endTime = item?.end_time || ''

        // 判断是否已是最新日期
        if (endTime) {
            const datePart = endTime.split(' ')[0]
            if (datePart === todayStr) {
                // 数据已是今天，无需再次下载
                skippedCount++
                skippedSymbols.push(symbol)
                continue
            }
        }

        // 计算增量 start
        let startDate = ''
        if (endTime) {
            const datePart = endTime.split(' ')[0]
            const nextDay = new Date(datePart)
            nextDay.setDate(nextDay.getDate() + 1)
            startDate = nextDay.toISOString().split('T')[0]
        }

        symbolInfos.push({ table, symbol, endTime, startDate })
    }

    // 报告跳过的标的
    if (skippedCount > 0) {
        console.log(`[DataCenter] Skipped ${skippedCount} symbols with up-to-date data:`, skippedSymbols)
    }

    // 如果所有标的都已是最细日期，直接返回
    if (symbolInfos.length === 0) {
        selectedSymbols.value = new Set()
        quoteStatus.value = `所有 ${skippedCount} 只标的已是最新数据，无需更新`
        updatingSymbol.value = false
        return
    }

    // === 第 2 步：按 (table, startDate) 分组 ===
    // key: "table|startDate" → SymbolInfo[]
    const groupMap = new Map<string, SymbolInfo[]>()
    for (const info of symbolInfos) {
        const groupKey = `${info.table}|${info.startDate}`
        if (!groupMap.has(groupKey)) {
            groupMap.set(groupKey, [])
        }
        const group = groupMap.get(groupKey)
        if (group) {
            group.push(info)
        }
    }

    // === 第 3 步：逐组发送批量请求 ===
    const freqMap: Record<string, string> = {
        '1d': 'daily', 'daily': 'daily', '日线': 'daily',
        '5m': '5m', '5分钟': '5m',
        '15m': '15m', '15分钟': '15m',
        '30m': '30m', '30分钟': '30m',
        '60m': '60m', '1h': '60m', '60分钟': '60m', '1小时': '60m',
    }

    let successCount = 0
    let failCount = 0

    for (const [groupKey, group] of groupMap) {
        const [table, startDate] = groupKey.split('|')
        const symbols = group.map(s => s.symbol)

        // 从表名提取频率
        const rawFreq = table.includes('_') ? table.split('_').slice(1).join('_') : '5m'
        const freq = freqMap[rawFreq] || rawFreq

        // 转换 symbol 格式：sh.510300 → 510300.SH
        const normalizedSymbols = symbols.map(s =>
            s.replace(/^([a-z]+)\.(\d+)$/, '$2.$1').toUpperCase()
        )

        const params = {
            symbols: normalizedSymbols.join(','),  // 逗号分隔的批量 symbols
            freq,
            ...(startDate && { start: startDate }),
        }

        try {
            const resp = await axios.post(`https://${server}/v0/quote`, params, {
                headers: { 'Authorization': token || '' }
            })
            applyGroupsTotal(resp.data?.groups)
            successCount += symbols.length  // 该组所有标的都算成功
        } catch (err: any) {
            failCount += symbols.length
            console.error(`[DataCenter] Failed to batch update group [${groupKey}]:`, err)
        }
    }

    selectedSymbols.value = new Set()
    let statusMsg = `批量更新完成：成功 ${successCount}，失败 ${failCount}`
    if (skippedCount > 0) {
        statusMsg += `，跳过 ${skippedCount} 只（已是最新数据）`
    }
    quoteStatus.value = statusMsg
    await loadQuoteData()

    updatingSymbol.value = false
}

// 更新当前过滤结果全量到最新（按 table+startDate 分组批量下载，跳过已是今天）
const onUpdateToLatest = async () => {
    if (isUpdatingLatest.value || !isLoggedIn.value) return
    if (flatSymbols.value.length === 0) return

    isUpdatingLatest.value = true
    activeDownloadScenario.value = 'updateLatest'
    quoteLogs.value = []
    quoteStatus.value = ''
    resetQuoteProgress()

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    // 今天的日期（YYYY-MM-DD）
    const todayStr = new Date().toISOString().split('T')[0]

    // === 第 1 步：构建标的信息映射，过滤已是最新日期的标的 ===
    const symbolInfos: SymbolInfo[] = []
    let skippedCount = 0
    const skippedSymbols: string[] = []

    for (const item of flatSymbols.value) {
        const endTime = item.end_time || ''
        const datePart = endTime.split(' ')[0]

        if (datePart && datePart >= todayStr) {
            skippedCount++
            skippedSymbols.push(item.symbol)
            continue
        }

        let startDate = ''
        if (datePart) {
            const nextDay = new Date(datePart)
            nextDay.setDate(nextDay.getDate() + 1)
            startDate = nextDay.toISOString().split('T')[0]
        }

        symbolInfos.push({ table: item.table, symbol: item.symbol, endTime, startDate })
    }

    if (skippedCount > 0) {
        addQuoteLog(`⏭ 跳过 ${skippedCount} 只已是最新数据的标的`)
    }

    if (symbolInfos.length === 0) {
        addQuoteLog(`✓ 所有 ${skippedCount} 只标的已是最新数据，无需更新`, 'done')
        quoteStatus.value = `所有标的已是最新数据`
        isUpdatingLatest.value = false
        activeDownloadScenario.value = 'idle'
        return
    }

    // === 第 2 步：按 (table, startDate) 分组 ===
    const groupMap = new Map<string, SymbolInfo[]>()
    for (const info of symbolInfos) {
        const groupKey = `${info.table}|${info.startDate}`
        const group = groupMap.get(groupKey)
        if (group) {
            group.push(info)
        } else {
            groupMap.set(groupKey, [info])
        }
    }

    // === 第 3 步：订阅 SSE done 事件，初始化计数器 + 启动超时兜底 ===
    expectedDones.value = 0
    completedDones.value = 0
    sseService.on('quote_download', onUpdateLatestDone)
    if (updateLatestTimer.value !== null) {
        clearTimeout(updateLatestTimer.value)
    }
    updateLatestTimer.value = window.setTimeout(onUpdateLatestTimeout, UPDATE_LATEST_TIMEOUT_MS)

    // === 第 4 步：逐组发送批量请求（失败跳过，继续下一组） ===
    const freqMap: Record<string, string> = {
        '1d': 'daily', 'daily': 'daily', '日线': 'daily',
        '5m': '5m', '5分钟': '5m',
        '15m': '15m', '15分钟': '15m',
        '30m': '30m', '30分钟': '30m',
        '60m': '60m', '1h': '60m', '60分钟': '60m', '1小时': '60m',
    }

    let groupIdx = 0
    const totalGroups = groupMap.size
    let successCount = 0
    let failCount = 0

    for (const [groupKey, group] of groupMap) {
        groupIdx++
        const [table, startDate] = groupKey.split('|')
        const symbols = group.map(s => s.symbol)

        const rawFreq = table.includes('_') ? table.split('_').slice(1).join('_') : '5m'
        const freq = freqMap[rawFreq] || rawFreq

        const normalizedSymbols = symbols.map(s =>
            s.replace(/^([a-z]+)\.(\d+)$/, '$2.$1').toUpperCase()
        )

        addQuoteLog(`📥 [${groupIdx}/${totalGroups}] ${table} start=${startDate} (${symbols.length} 个标的)`)

        const params = {
            symbols: normalizedSymbols.join(','),
            freq,
            ...(startDate && { start: startDate }),
        }

        try {
            const resp = await axios.post(`https://${server}/v0/quote`, params, {
                headers: { 'Authorization': token || '' }
            })
            applyGroupsTotal(resp.data?.groups)
            successCount += symbols.length
            expectedDones.value++  // POST 成功 = 期望收到 1 个 done 事件
        } catch (err: any) {
            failCount += symbols.length
            addQuoteLog(`❌ 组 [${groupKey}] 请求失败: ${err.message}`, 'error')
            // 失败跳过，不会触发 done 事件
        }
    }

    // 如果所有 POST 都失败，立刻清理
    if (expectedDones.value === 0) {
        if (updateLatestTimer.value !== null) {
            clearTimeout(updateLatestTimer.value)
            updateLatestTimer.value = null
        }
        sseService.off('quote_download', onUpdateLatestDone)
        addQuoteLog(`✗ 所有组请求失败：${failCount}`, 'error')
        quoteStatus.value = `更新失败：${failCount} 只标的`
        await loadQuoteData()
        isUpdatingLatest.value = false
        activeDownloadScenario.value = 'idle'
        return
    }

    // 否则等待 SSE done 事件（handler 会在全部完成后自动刷新并取消订阅）
    quoteStatus.value = `更新中：已发送 ${totalGroups} 组请求，等待完成...`
}

// 批量下载选中标的的 CSV
const onBatchDownloadCSV = async () => {
    if (selectedSymbols.value.size === 0) return

    // 检查导出目录
    if (!exportDir.value) {
        const result = await ipcRenderer.invoke('select-file', {
            title: '选择 CSV 导出目录',
            properties: ['openDirectory', 'createDirectory']
        })
        if (!result.success || !result.filePath) return
        exportDir.value = result.filePath
        localStorage.setItem('quoteExportPath', result.filePath)
    }

    downloadingCSV.value = true

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    let successCount = 0
    let failCount = 0

    for (const key of selectedSymbols.value) {
        const [table, symbol] = key.split('|')
        try {
            const resp = await axios.post(`https://${server}/v0/quote/data`, {
                action: 'export',
                table,
                symbol,
                format: 'csv'
            }, {
                headers: { 'Authorization': token || '' },
                responseType: 'text'
            })
            const fileName = `${symbol}_${table}.csv`
            const saveResult = await ipcRenderer.invoke('save-csv-to-dir', exportDir.value, fileName, resp.data)
            if (saveResult.success) {
                successCount++
            } else {
                failCount++
                console.error(`[DataCenter] Failed to save ${fileName}:`, saveResult.error)
            }
        } catch (err: any) {
            failCount++
            console.error(`[DataCenter] Failed to export ${symbol}:`, err)
        }
    }

    quoteStatus.value = `CSV 下载完成：成功 ${successCount}，失败 ${failCount}，目录: ${exportDir.value}`
    downloadingCSV.value = false
}

// 选择导出目录
const onSelectExportDir = async () => {
    const result = await ipcRenderer.invoke('select-file', {
        title: '选择 CSV 导出目录',
        properties: ['openDirectory', 'createDirectory']
    })
    if (result.success && result.filePath) {
        exportDir.value = result.filePath
        localStorage.setItem('quoteExportPath', result.filePath)
    }
}

// 批量导出选中标的的 CSV
const onBatchExport = async () => {
    if (selectedSymbols.value.size === 0) return

    // 检查导出目录
    if (!exportDir.value) {
        const result = await ipcRenderer.invoke('select-file', {
            title: '选择 CSV 导出目录',
            properties: ['openDirectory', 'createDirectory']
        })
        if (!result.success || !result.filePath) return
        exportDir.value = result.filePath
        localStorage.setItem('quoteExportPath', result.filePath)
    }

    exporting.value = true
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    let successCount = 0
    let failCount = 0

    for (const key of selectedSymbols.value) {
        const [table, symbol] = key.split('|')
        try {
            const resp = await axios.post(`https://${server}/v0/quote/data`, {
                action: 'export',
                table,
                symbol,
                format: 'csv'
            }, {
                headers: { 'Authorization': token || '' },
                responseType: 'text'
            })
            // 通过 IPC 写入文件
            const fileName = `${symbol}_${table}.csv`
            const saveResult = await ipcRenderer.invoke('save-csv-to-dir', exportDir.value, fileName, resp.data)
            if (saveResult.success) {
                successCount++
            } else {
                failCount++
                console.error(`[DataCenter] Failed to save ${fileName}:`, saveResult.error)
            }
        } catch (err: any) {
            failCount++
            console.error(`[DataCenter] Failed to export ${symbol}:`, err)
        }
    }

    quoteStatus.value = `导出完成：成功 ${successCount}，失败 ${failCount}，目录: ${exportDir.value}`
    exporting.value = false
}

// 清空所有行情数据
const onHandleDeleteAllQuoteData = async () => {
    const confirmed = await promptDialogRef.value?.confirm({
        title: '确认删除',
        message: '确定要清空服务端所有行情数据吗？此操作不可恢复！'
    })
    if (!confirmed) return

    deletingQuote.value = true

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    try {
        await axios.delete(`https://${server}/v0/quote`, {
            headers: { 'Authorization': token || '' }
        })
        quoteStatus.value = '所有行情数据已清空'
        quoteDataList.value = []
        currentPage.value = 1
    } catch (err: any) {
        quoteStatus.value = `删除失败: ${err.response?.data?.message || err.message}`
    } finally {
        deletingQuote.value = false
    }
}

// ═══════════════════════════════════════════════════════════
</script>

<template>
    <div class="tab-content">
        <!-- 下载区 -->
        <div class="download-section">
            <div class="section-header" @click="downloadExpanded = !downloadExpanded">
                <div class="section-title">
                    <i class="fas fa-download"></i> 数据下载
                </div>
                <i class="fas chevron-icon" :class="downloadExpanded ? 'fa-chevron-up' : 'fa-chevron-down'"></i>
            </div>
            <div v-show="downloadExpanded" class="download-body">
                <!-- 统一下载入口：按标的 / 按策略 / 批量选中 -->
                <div class="download-cards-grid">
                    <!-- 按标的下载 -->
                    <div class="download-card">
                        <div class="card-title"><i class="fas fa-crosshairs"></i> 按标的下载</div>
                        <div class="input-row">
                            <div class="input-group">
                                <label>频率</label>
                                <select v-model="quoteFreq" class="quote-select">
                                    <option value="daily">日线</option>
                                    <option value="5m">5分钟</option>
                                    <option value="15m">15分钟</option>
                                    <option value="30m">30分钟</option>
                                    <option value="60m">60分钟</option>
                                </select>
                            </div>
                            <div class="input-group">
                                <label>开始日期</label>
                                <input type="date" v-model="quoteStartDate" />
                            </div>
                            <div class="input-group">
                                <label>结束日期</label>
                                <input type="date" v-model="quoteEndDate" />
                            </div>
                        </div>
                        <div class="input-group">
                            <label>标的代码 (逗号分隔)</label>
                            <input type="text" placeholder="510300.SH, 510500.SH" v-model="quoteSymbols">
                        </div>
                        <div class="button-row">
                            <button class="btn btn-primary" @click="onHandleQuoteDownload" :disabled="quoteDownloading || !isLoggedIn">
                                <i class="fas fa-download"></i>
                                {{ quoteDownloading ? '下载中...' : (!isLoggedIn ? '请先登录' : '开始下载') }}
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
                            v-model="quoteStrategy"
                            v-model:symbols="quoteStrategySymbols"
                            :remote-strategies="remoteStrategies"
                            :remote-fetched="remoteFetched"
                            :load-remote-pools="loadRemotePoolSymbols"
                            select-class="quote-select"
                        />
                        <div class="input-row">
                            <div class="input-group">
                                <label>频率</label>
                                <select v-model="quoteStrategyFreq" class="quote-select">
                                    <option value="daily">日线</option>
                                    <option value="5m">5分钟</option>
                                    <option value="15m">15分钟</option>
                                    <option value="30m">30分钟</option>
                                    <option value="60m">60分钟</option>
                                </select>
                            </div>
                            <div class="input-group">
                                <label>开始日期</label>
                                <input type="date" v-model="quoteStrategyStartDate" />
                            </div>
                            <div class="input-group">
                                <label>结束日期</label>
                                <input type="date" v-model="quoteStrategyEndDate" />
                            </div>
                        </div>
                        <div class="button-row">
                            <button class="btn btn-primary" @click="onDownloadQuoteByStrategy"
                                    :disabled="quoteStrategyDownloading || !isLoggedIn || !quoteStrategy">
                                <i class="fas fa-download"></i>
                                {{ quoteStrategyDownloading ? '下载中...' : '下载策略标的池' }}
                            </button>
                        </div>
                    </div>

                    <!-- 批量下载选中（增量到最新） -->
                    <div class="download-card">
                        <div class="card-title"><i class="fas fa-tasks"></i> 批量下载选中</div>
                        <div class="pool-summary">
                            已选 <strong>{{ selectedSymbols.size }}</strong> 只标的
                            <span v-if="selectedSymbols.size === 0" class="hint">（在下方表格勾选）</span>
                        </div>
                        <div class="input-group">
                            <label>起始日期 (可选)</label>
                            <input type="date" v-model="quoteBatchStartDate" placeholder="留空按各标的 end_time+1" />
                            <small class="field-hint">留空则按各标的已有 end_time 增量下载到最新</small>
                        </div>
                        <div class="button-row">
                            <button class="btn btn-primary" @click="onBatchDownloadSelected"
                                    :disabled="updatingSymbol || !isLoggedIn || selectedSymbols.size === 0">
                                <i class="fas fa-cloud-download-alt"></i>
                                {{ updatingSymbol ? '下载中...' : '批量下载到最新' }}
                            </button>
                        </div>
                    </div>
                </div>

                <!-- 共享状态 + 进度 + 日志 -->
                <div v-if="quoteStatus" class="status-text" :class="{ 'status-error': quoteStatus.includes('失败') }">
                    {{ quoteStatus }}
                </div>
                <!-- 下载进度条 -->
                <div v-if="quoteProgress.total > 0 && quoteProgress.phase" class="quote-progress">
                    <div class="quote-progress-header">
                        <span class="quote-progress-label">
                            <template v-if="quoteProgress.phase === 'downloading'">
                                <i class="fas fa-cloud-download-alt"></i> 下载中
                            </template>
                            <template v-else-if="quoteProgress.phase === 'importing'">
                                <i class="fas fa-file-import"></i> 导入中
                            </template>
                        </span>
                        <span class="quote-progress-count">{{ quoteProgress.downloaded }} / {{ quoteProgress.total }} 标的</span>
                    </div>
                    <div class="quote-progress-bar">
                        <div class="quote-progress-fill"
                             :style="{ width: quoteProgress.total ? (quoteProgress.downloaded / quoteProgress.total * 100) + '%' : '0%' }">
                        </div>
                    </div>
                </div>
                <div v-if="quoteLogs.length" class="quote-log-box">
                    <div class="quote-log-title">下载日志</div>
                    <div class="quote-log-content" ref="quoteLogRef">
                        <div v-for="(log, i) in quoteLogs" :key="i" class="quote-log-line"
                             :class="{ 'log-error': log.type === 'error', 'log-success': log.type === 'done' }">
                            <span class="log-time">{{ log.time }}</span> {{ log.text }}
                        </div>
                    </div>
                </div>

                <!-- Tick 下载 -->
                <div class="download-card">
                    <div class="card-title"><i class="fas fa-chart-bar"></i> Tick 数据 (DuckDB)</div>
                    <div class="input-group">
                        <label>本地 DuckDB 路径</label>
                        <input type="text" placeholder="留空默认 tick_data.db" v-model="tickDbPath">
                    </div>
                    <div class="input-row">
                        <div class="input-group">
                            <label>标的代码 (留空下载全部)</label>
                            <input type="text" placeholder="如 600000.SH" v-model="tickSymbol">
                        </div>
                        <div class="input-group">
                            <label>开始日期</label>
                            <input type="date" v-model="tickStartDate" />
                        </div>
                        <div class="input-group">
                            <label>结束日期</label>
                            <input type="date" v-model="tickEndDate" />
                        </div>
                    </div>
                    <div class="button-row">
                        <button class="btn" @click="onHandleTickDownload" :disabled="tickDownloading">
                            <i class="fas fa-download"></i>
                            {{ tickDownloading ? '下载中...' : '下载 Tick 到 DuckDB' }}
                        </button>
                        <span v-if="tickDownloading && tickProgress > 0" class="progress-text">
                            已下载 {{ tickCount }} 条
                        </span>
                    </div>
                    <div v-if="tickDownloadStatus" class="status-text" :class="{ 'status-error': tickDownloadStatus.includes('失败') }">
                        {{ tickDownloadStatus }}
                    </div>
                </div>
            </div>
        </div>

        <!-- 数据管理区 -->
        <div class="manage-section">
            <div class="section-title">
                <i class="fas fa-database"></i> 已下载数据
            </div>

            <!-- 筛选行 -->
            <div class="symbol-search-row">
                <div class="search-item">
                    <span class="search-label">标的代码</span>
                    <input type="text" placeholder="如 600000.SH（留空显示全部）" v-model="symbolFilter" @input="currentPage = 1">
                </div>
                <div class="search-item">
                    <span class="search-label">资产类型</span>
                    <select v-model="assetTypeFilter" @change="currentPage = 1">
                        <option value="">全部</option>
                        <option value="Stock">Stock</option>
                        <option value="ETF">ETF</option>
                    </select>
                </div>
                <div class="search-item">
                    <span class="search-label">频率</span>
                    <select v-model="freqFilter" @change="currentPage = 1">
                        <option v-for="opt in FREQ_FILTER_OPTIONS" :key="opt.value" :value="opt.value">
                            {{ opt.label }}
                        </option>
                    </select>
                </div>
                <button class="btn btn-primary btn-sm" @click="onUpdateToLatest"
                        :disabled="isUpdatingLatest || quoteDataList.length === 0 || !isLoggedIn"
                        title="对当前过滤结果全量下载增量数据到最新日期">
                    <i class="fas fa-cloud-download-alt"></i>
                    {{ isUpdatingLatest ? '更新中...' : '更新到最新' }}
                </button>
            </div>

            <!-- 数据表格 -->
            <div v-if="allFlatSymbols.length > 0" class="quote-data-table">
                <div class="table-scroll">
                    <table class="data-table">
                        <thead class="sticky-header">
                            <tr>
                                <th class="col-checkbox">
                                    <input type="checkbox" :checked="isAllSelected" @change="toggleSelectAll">
                                </th>
                                <th>类型</th>
                                <th>频率</th>
                                <th class="sortable" @click="handleSort('symbol')">
                                    标的代码
                                    <span class="sort-icon" v-if="sortKey === 'symbol'">{{ sortOrder === 'asc' ? '↑' : '↓' }}</span>
                                </th>
                                <th class="sortable" @click="handleSort('start_time')">
                                    起始时间
                                    <span class="sort-icon" v-if="sortKey === 'start_time'">{{ sortOrder === 'asc' ? '↑' : '↓' }}</span>
                                </th>
                                <th class="sortable" @click="handleSort('end_time')">
                                    结束时间
                                    <span class="sort-icon" v-if="sortKey === 'end_time'">{{ sortOrder === 'asc' ? '↑' : '↓' }}</span>
                                </th>
                                <th class="sortable" @click="handleSort('count')">
                                    数据量
                                    <span class="sort-icon" v-if="sortKey === 'count'">{{ sortOrder === 'asc' ? '↑' : '↓' }}</span>
                                </th>
                            </tr>
                        </thead>
                        <tbody>
                            <tr v-for="(item, idx) in pagedSymbols" :key="`${item.table}-${item.symbol}`"
                                :class="{ 'row-selected': selectedDetailKey === itemKey(item) }">
                                <td class="col-checkbox">
                                    <input type="checkbox" :checked="isSelected(item)" @change="toggleSelect(item)">
                                </td>
                                <td class="asset-type">{{ item.assetType }}</td>
                                <td class="freq" :class="{ 'freq-daily': item.rawFreq === '1d' || item.rawFreq === 'daily' }">
                                    <i class="fas fa-clock freq-icon"></i>{{ item.freq }}
                                </td>
                                <td class="symbol-code clickable" @click="onSymbolRowClick(item)" :title="`点击查看 ${item.symbol} 详情`">
                                    <SymbolCode :symbol="item.symbol" />
                                </td>
                                <td class="time-range">{{ item.start_time || '-' }}</td>
                                <td class="time-range">{{ item.end_time || '-' }}</td>
                                <td class="symbol-count">{{ item.count.toLocaleString() }}</td>
                            </tr>
                        </tbody>
                    </table>
                </div>

                <!-- 分页 + 操作栏 -->
                <div class="pagination" v-if="allFlatSymbols.length > 0">
                    <div class="pagination-left">
                        <button class="btn-warning btn-sm" @click="onBatchDeleteSymbols" :disabled="deletingSymbol || selectedSymbols.size === 0">
                            <i class="fas fa-trash"></i>
                            {{ deletingSymbol ? '删除中...' : `批量删除 (${selectedSymbols.size})` }}
                        </button>
                        <button class="btn-info btn-sm" @click="onBatchUpdateSymbols" :disabled="updatingSymbol || selectedSymbols.size === 0">
                            <i class="fas fa-sync-alt"></i>
                            {{ updatingSymbol ? '更新中...' : `批量更新 (${selectedSymbols.size})` }}
                        </button>
                        <button class="btn-success btn-sm" @click="onBatchDownloadCSV" :disabled="downloadingCSV || selectedSymbols.size === 0">
                            <i class="fas fa-file-csv"></i>
                            {{ downloadingCSV ? '下载中...' : `下载CSV (${selectedSymbols.size})` }}
                        </button>
                        <button class="btn-sm btn-icon" @click="onSelectExportDir" :title="exportDir || '未设置导出目录'">
                            <i class="fas fa-folder-open"></i>
                            下载路径
                        </button>
                        <span v-if="exportDir" class="export-dir-hint" :title="exportDir">
                            <i class="fas fa-download"></i> {{ exportDir }}
                        </span>
                        <button class="btn-danger btn-sm" @click="onHandleDeleteAllQuoteData" :disabled="deletingQuote">
                            <i class="fas fa-trash"></i>
                            {{ deletingQuote ? '删除中...' : '清空所有数据' }}
                        </button>
                        <button class="btn-danger btn-sm" @click="onHandleDeleteServerTicks" :disabled="deleting">
                            <i class="fas fa-eraser"></i>
                            {{ deleting ? '删除中...' : '清空 Tick' }}
                        </button>
                    </div>
                    <div class="pagination-center" v-if="allFlatSymbols.length > 0">
                        <button class="page-btn" @click="loadQuoteData" :disabled="loadingQuoteData" title="刷新数据">
                            <i class="fas fa-sync-alt" :class="{ 'fa-spin': loadingQuoteData }"></i>
                        </button>
                        <button class="page-btn" :disabled="currentPage === 1" @click="currentPage = 1">
                            <i class="fas fa-angle-double-left"></i>
                        </button>
                        <button class="page-btn" :disabled="currentPage === 1" @click="currentPage--">
                            <i class="fas fa-angle-left"></i>
                        </button>
                        <span class="page-info">第 {{ currentPage }} / {{ totalPages }} 页（共 {{ flatSymbols.length }} 条，总计 {{ allFlatSymbols.length }} 条）</span>
                        <button class="page-btn" :disabled="currentPage === totalPages" @click="currentPage++">
                            <i class="fas fa-angle-right"></i>
                        </button>
                        <button class="page-btn" :disabled="currentPage === totalPages" @click="currentPage = totalPages">
                            <i class="fas fa-angle-double-right"></i>
                        </button>
                        <select class="page-size-select" v-model.number="pageSize" @change="currentPage = 1">
                            <option :value="10">10 条/页</option>
                            <option :value="20">20 条/页</option>
                            <option :value="50">50 条/页</option>
                            <option :value="100">100 条/页</option>
                        </select>
                    </div>
                </div>
            </div>

            <div v-if="allFlatSymbols.length === 0 && loadingQuoteData" class="loading-text">
                <i class="fas fa-spinner fa-spin"></i> 加载中...
            </div>
            <div v-if="allFlatSymbols.length === 0 && !loadingQuoteData && quoteDataList.length === 0" class="empty-text">
                <i class="fas fa-info-circle"></i> 暂无已导入的行情数据
            </div>
            <div v-if="allFlatSymbols.length === 0 && !loadingQuoteData && quoteDataList.length > 0 && (symbolFilter || assetTypeFilter || freqFilter)" class="empty-text">
                <i class="fas fa-search"></i> 无匹配的标的，请调整筛选条件
            </div>

            <div v-if="deleteStatus" class="status-text" :class="{ 'status-error': deleteStatus.includes('失败') }">
                {{ deleteStatus }}
            </div>
        </div>
        <PromptDialog ref="promptDialogRef" />
    </div>
</template>

<style scoped>
@import './datacenter.scss';
</style>
