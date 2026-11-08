<script setup lang="ts">
import { ref, computed, watch, onMounted, nextTick } from 'vue'
import axios from 'axios'
import sseService from '@/ts/SSEService'
import PromptDialog from '../PromptDialog.vue'
import { useHistoryStore } from '@/stores/history'
import { extractSecuritiesFromFlowData } from '@/lib/strategyPool'
import { useIsLoggedIn } from './composables/useSession'

// ── 期权数据状态 ──
const isLoggedIn = useIsLoggedIn()
const historyStore = useHistoryStore()

// PromptDialog 引用
const promptDialogRef = ref<{ confirm: (opts: { title: string; message: string }) => Promise<boolean> } | null>(null)

const optionExchange = ref<'CFFEX' | 'SSE' | 'SZSE'>('CFFEX')
const optionStartDate = ref('')
const optionEndDate = ref('')
const optionSelectedProducts = ref<string[]>([])

// 「按策略下载」期权（策略几乎不使用期权合约，提取为空时给用户明确提示）
const optionStrategy = ref('')
const optionStrategySecurities = ref<Array<{ code: string; name: string }>>([])
const optionDownloading = ref(false)
const optionStatus = ref('')
const optionLogs = ref<{ time: string; text: string; type: string }[]>([])
const optionLogRef = ref<HTMLElement | null>(null)

const optionContracts = ref<any[]>([])
const loadingOptionContracts = ref(false)
const optionDeleting = ref(false)
const optionFilterExchange = ref('')
const optionFilterProduct = ref('')
const optionPage = ref(1)
const optionPageSize = ref(20)
const optionJumpPage = ref<number>(1)
const selectedOptionContract = ref<any>(null)
const optionSortField = ref<'start_date' | 'end_date' | ''>('')
const optionSortAsc = ref(true)

const optionProductsByExchange: Record<string, { value: string; label: string }[]> = {
    CFFEX: [
        { value: 'IO', label: '沪深300股指期权 (IO)' },
        { value: 'HO', label: '上证50股指期权 (HO)' },
        { value: 'MO', label: '中证1000股指期权 (MO)' },
    ],
    SSE: [
        { value: '50ETF',     label: '50ETF期权' },
        { value: '300ETF',    label: '沪深300ETF期权' },
        { value: '500ETF',    label: '500ETF期权' },
        { value: 'STAR50ETF', label: '科创50ETF期权' },
    ],
    SZSE: [
        { value: '159919', label: '沪深300ETF期权' },
        { value: '159915', label: '创业板ETF期权' },
        { value: '159922', label: '中证500ETF期权' },
        { value: '159901', label: '深证100ETF期权' },
    ],
}
const optionProductsForExchange = computed(() =>
    optionProductsByExchange[optionExchange.value] || []
)

// 切换交易所时清空已选品种
watch(optionExchange, () => {
    optionSelectedProducts.value = []
})

// 期权「按策略下载」：选策略后从最新版本 flowData 提取 securities
watch(optionStrategy, async (name) => {
    optionStrategySecurities.value = []
    if (!name) return
    const strategy = historyStore.strategies.find(s => s.name === name)
    if (!strategy) return
    const versions = historyStore.getVersionsByStrategy(strategy.id)
    if (versions.length === 0) return
    const sorted = [...versions].sort((a, b) =>
        new Date(b.saveTime).getTime() - new Date(a.saveTime).getTime()
    )
    const flowData = await historyStore.loadVersionFlowData(sorted[0].id)
    if (flowData) {
        optionStrategySecurities.value = extractSecuritiesFromFlowData(flowData as any)
    }
})

const optionTotalPages = computed(() =>
    Math.max(1, Math.ceil(optionContracts.value.length / optionPageSize.value))
)
const sortedOptionContracts = computed(() => {
    const arr = [...optionContracts.value]
    if (!optionSortField.value) return arr
    const field = optionSortField.value
    const dir = optionSortAsc.value ? 1 : -1
    return arr.sort((a, b) =>
        dir * (a[field] || '').localeCompare(b[field] || '')
    )
})
const pagedOptionContracts = computed(() => {
    const start = (optionPage.value - 1) * optionPageSize.value
    return sortedOptionContracts.value.slice(start, start + optionPageSize.value)
})

const addOptionLog = (text: string, type: string = 'info') => {
    const now = new Date().toLocaleTimeString()
    optionLogs.value.push({ time: now, text, type })
    // 自动滚动到底部
    nextTick(() => {
        if (optionLogRef.value) {
            optionLogRef.value.scrollTop = optionLogRef.value.scrollHeight
        }
    })
}

// 期权下载 SSE done 事件
const onOptionDownloadDone = (msg: any) => {
    const d = msg.data
    if (d.status === 'done') {
        sseService.off('option_data_download', onOptionDownloadDone)
        optionDownloading.value = false
        const success = d.success_products || 0
        const fail = d.fail_products || 0
        const rows = d.total_rows || 0
        optionStatus.value = `下载完成: ${success} 个品种成功, ${fail} 失败, 共导入 ${rows} 行`
        addOptionLog(`✓ 完成: 成功 ${success}, 失败 ${fail}, 共导入 ${rows} 行`, 'done')
        onLoadOptionContracts()
    } else if (d.status === 'aborted') {
        sseService.off('option_data_download', onOptionDownloadDone)
        optionDownloading.value = false
        optionStatus.value = `下载中止: ${d.reason || '未知错误'}`
        addOptionLog(`✗ 中止: ${d.reason || '未知错误'}`, 'error')
    } else if (d.status === 'imported') {
        addOptionLog(`  [OK] ${d.exchange} ${d.product}: ${d.rows} 行`, 'success')
    } else if (d.status === 'product_failed') {
        addOptionLog(`  [WARN] ${d.exchange} ${d.product} 下载失败, 已跳过`, 'error')
    } else if (d.status === 'script_log') {
        addOptionLog(`  ${d.line}`)
    } else if (d.status === 'downloading') {
        addOptionLog(`→ 拉取 ${d.exchange} ${d.product}...`)
    } else if (d.status === 'started') {
        addOptionLog(`启动: ${d.exchange} ${d.products} 个品种, ${d.start || '-'} ~ ${d.end || '-'}`)
    }
}

//  期权数据 Handler (Tab 4)
// ═══════════════════════════════════════════════════════════

onMounted(() => {
    if (isLoggedIn.value) onLoadOptionContracts()
})

const onOptionDownload = async () => {
    if (!isLoggedIn.value) {
        optionStatus.value = '请先登录'
        return
    }
    if (optionSelectedProducts.value.length === 0) {
        optionStatus.value = '请至少选择一个品种'
        return
    }
    // 日期验证: 结束日期必须在起始日期之后
    if (optionStartDate.value && optionEndDate.value && optionEndDate.value < optionStartDate.value) {
        optionStatus.value = '结束日期必须在起始日期之后'
        return
    }

    optionDownloading.value = true
    optionLogs.value = []
    optionStatus.value = '正在提交下载任务...'

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    try {
        sseService.on('option_data_download', onOptionDownloadDone)
        await axios.post(`https://${server}/v0/option/data`, {
            exchange: optionExchange.value,
            products: optionSelectedProducts.value,
            start: optionStartDate.value,
            end: optionEndDate.value,
        }, { headers: { 'Authorization': token || '' } })
        optionStatus.value = '下载任务已提交, 等待完成...'
    } catch (err: any) {
        sseService.off('option_data_download', onOptionDownloadDone)
        optionDownloading.value = false
        optionStatus.value = `下载失败: ${err.response?.data?.message || err.message}`
        addOptionLog(`✗ 提交失败: ${err.message}`, 'error')
    }
}

// 期权「按策略下载」：策略通常不使用期权合约，提取不到时给明确提示
const onOptionDownloadByStrategy = async () => {
    if (!isLoggedIn.value) {
        optionStatus.value = '请先登录'
        return
    }
    if (!optionStrategy.value) {
        optionStatus.value = '请先选择策略'
        return
    }
    const securities = optionStrategySecurities.value
    if (securities.length === 0) {
        optionStatus.value = `策略「${optionStrategy.value}」未使用期权合约，请使用上方的交易所+品种下载入口`
        addOptionLog(`✗ 策略「${optionStrategy.value}」未使用期权合约`, 'error')
        return
    }
    // 预留：未来若策略支持期权合约，此处按 securities 关联品种 + 调用后端接口
    optionStatus.value = `策略「${optionStrategy.value}」包含 ${securities.length} 个标的（${securities.map(s => s.code).join(',')}）。期权合约与股票代码维度不同，请改用交易所+品种下载`
    addOptionLog(`ℹ 策略「${optionStrategy.value}」含 ${securities.length} 个股票/ETF 标的，期权合约需按交易所品种下载`)
}

const onLoadOptionContracts = async () => {
    loadingOptionContracts.value = true
    optionPage.value = 1
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    try {
        const params: Record<string, string> = {}
        if (optionFilterExchange.value) params.exchange = optionFilterExchange.value
        if (optionFilterProduct.value)  params.product  = optionFilterProduct.value
        const resp = await axios.get(`https://${server}/v0/option/data`, {
            params,
            headers: { 'Authorization': token || '' }
        })
        optionContracts.value = resp.data.contracts || []
    } catch (err: any) {
        optionStatus.value = `查询失败: ${err.response?.data?.message || err.message}`
        optionContracts.value = []
    } finally {
        loadingOptionContracts.value = false
    }
}

const onOptionDeleteContract = async (contract: any) => {
    const confirmed = await promptDialogRef.value?.confirm({
        title: '确认删除',
        message: `确定要删除合约 ${contract.contract_name} 的所有数据吗？此操作不可恢复！`
    })
    if (!confirmed) return
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    try {
        await axios.delete(`https://${server}/v0/option/data`, {
            params: { contract: String(contract.symbol_id) },
            headers: { 'Authorization': token || '' }
        })
        // 如果删除的是当前选中的合约，清除详情
        if (selectedOptionContract.value?.symbol_id === contract.symbol_id) {
            selectedOptionContract.value = null
            window.dispatchEvent(new CustomEvent('datacenter-option-cleared'))
        }
        await onLoadOptionContracts()
    } catch (err: any) {
        optionStatus.value = `删除失败: ${err.response?.data?.message || err.message}`
    }
}

const onOptionJumpPage = () => {
    const p = Math.max(1, Math.min(optionJumpPage.value, optionTotalPages.value))
    optionPage.value = p
    optionJumpPage.value = p
}

const onOptionSort = (field: 'start_date' | 'end_date') => {
    if (optionSortField.value === field) {
        optionSortAsc.value = !optionSortAsc.value
    } else {
        optionSortField.value = field
        optionSortAsc.value = true
    }
    optionPage.value = 1
}

const onOptionContractClick = (contract: any) => {
    console.log('[OptionClick] contract:', JSON.stringify({ symbol_id: contract.symbol_id, contract_name: contract.contract_name, call_put: contract.call_put, strike_price: contract.strike_price }))
    selectedOptionContract.value = contract
    const detail = {
        contract_code: contract.contract_code || contract.contract_name,
        contract_name: contract.contract_name,
        exchange: contract.exchange,
        product: contract.product,
        symbol_id: contract.symbol_id,
        call_put: contract.call_put,
        strike_price: contract.strike_price,
    }
    console.log('[OptionClick] dispatching event detail:', JSON.stringify(detail))
    window.dispatchEvent(new CustomEvent('datacenter-option-contract-selected', { detail }))
}

const onOptionDeleteAll = async () => {
    const confirmed = await promptDialogRef.value?.confirm({
        title: '确认清空',
        message: '确定要清空所有期权日终数据吗？此操作不可恢复！'
    })
    if (!confirmed) return
    optionDeleting.value = true
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    try {
        await axios.delete(`https://${server}/v0/option/data`, {
            headers: { 'Authorization': token || '' }
        })
        optionStatus.value = '所有期权数据已清空'
        await onLoadOptionContracts()
    } catch (err: any) {
        optionStatus.value = `清空失败: ${err.response?.data?.message || err.message}`
    } finally {
        optionDeleting.value = false
    }
}
</script>

<template>
    <div class="tab-content">
        <div class="section-title">
            <i class="fas fa-stream"></i> 期权日终数据 (官方源: CFFEX / SSE / SZSE)
        </div>

        <!-- 下载区 -->
        <div class="download-card">
            <div class="card-title"><i class="fas fa-download"></i> 下载日终数据</div>
            <div class="input-row">
                <div class="input-group">
                    <label>交易所</label>
                    <select v-model="optionExchange">
                        <option value="CFFEX">中金所 (CFFEX)</option>
                        <option value="SSE">上交所 (SSE)</option>
                        <option value="SZSE">深交所 (SZSE)</option>
                    </select>
                </div>
                <div class="input-group" style="flex: 2;">
                    <label>品种 (可多选, 失败跳过)</label>
                    <div class="checkbox-row">
                        <label v-for="p in optionProductsForExchange" :key="p.value" class="checkbox-item">
                            <input type="checkbox" :value="p.value" v-model="optionSelectedProducts">
                            {{ p.label }}
                        </label>
                    </div>
                </div>
            </div>
            <div class="input-row">
                <div class="input-group">
                    <label>开始日期</label>
                    <input type="date" v-model="optionStartDate" />
                </div>
                <div class="input-group">
                    <label>结束日期</label>
                    <input type="date" v-model="optionEndDate" />
                </div>
                <button class="btn btn-primary" style="align-self: flex-end;"
                        @click="onOptionDownload"
                        :disabled="optionDownloading || !isLoggedIn || optionSelectedProducts.length === 0">
                    <i class="fas fa-download"></i>
                    {{ optionDownloading ? '下载中...' : (!isLoggedIn ? '请先登录' : '开始下载') }}
                </button>
            </div>

            <!-- 按策略下载（期权合约场景几乎用不到） -->
            <div class="sub-section-divider"></div>
            <div class="card-subtitle"><i class="fas fa-project-diagram"></i> 按策略下载</div>
            <div class="input-row">
                <div class="input-group" style="flex: 2;">
                    <label>策略</label>
                    <select v-model="optionStrategy" class="quote-select">
                        <option value="">选择策略…</option>
                        <option v-for="s in historyStore.strategies" :key="s.id" :value="s.name">
                            {{ s.name }}
                        </option>
                    </select>
                </div>
                <button class="btn" style="align-self: flex-end;"
                        @click="onOptionDownloadByStrategy"
                        :disabled="!isLoggedIn || !optionStrategy">
                    <i class="fas fa-search"></i>
                    查询策略标的池
                </button>
            </div>
            <div v-if="optionStrategy && optionStrategySecurities.length === 0" class="status-hint">
                <i class="fas fa-info-circle"></i> 策略「{{ optionStrategy }}」未配置任何 QuoteInputNode 标的
            </div>
            <div v-else-if="optionStrategySecurities.length > 0" class="status-hint">
                <i class="fas fa-info-circle"></i> 该策略含 {{ optionStrategySecurities.length }} 个股票/ETF 标的（期权合约需按上方交易所+品种下载）
            </div>

            <div v-if="optionStatus" class="status-text"
                 :class="{ 'status-error': optionStatus.includes('失败') }">
                {{ optionStatus }}
            </div>
            <div v-if="optionLogs.length" class="quote-log-box">
                <div class="quote-log-title">下载日志</div>
                <div class="quote-log-content" ref="optionLogRef">
                    <div v-for="(log, i) in optionLogs" :key="i" class="quote-log-line"
                         :class="{ 'log-error': log.type === 'error', 'log-success': log.type === 'done' || log.type === 'success' }">
                        <span class="log-time">{{ log.time }}</span> {{ log.text }}
                    </div>
                </div>
            </div>
        </div>

        <!-- 数据管理区 -->
        <div class="manage-section">
            <div class="section-title">
                <i class="fas fa-database"></i> 已下载期权合约
            </div>
            <div class="symbol-search-row">
                <div class="search-item">
                    <span class="search-label">交易所</span>
                    <select v-model="optionFilterExchange" @change="onLoadOptionContracts">
                        <option value="">全部</option>
                        <option value="CFFEX">中金所</option>
                        <option value="SSE">上交所</option>
                        <option value="SZSE">深交所</option>
                    </select>
                </div>
                <div class="search-item">
                    <span class="search-label">品种</span>
                    <input type="text" placeholder="如 IO / 50ETF (留空全部)"
                           v-model="optionFilterProduct" @change="onLoadOptionContracts">
                </div>
                <button class="btn btn-primary btn-sm" @click="onLoadOptionContracts"
                        :disabled="loadingOptionContracts">
                    <i class="fas fa-sync-alt" :class="{ 'fa-spin': loadingOptionContracts }"></i>
                    刷新
                </button>
                <button class="btn-danger btn-sm" @click="onOptionDeleteAll"
                        :disabled="optionDeleting || !isLoggedIn">
                    <i class="fas fa-trash"></i>
                    {{ optionDeleting ? '删除中...' : '清空所有' }}
                </button>
            </div>

            <div v-if="loadingOptionContracts" class="loading-text">
                <i class="fas fa-spinner fa-spin"></i> 加载中...
            </div>
            <div v-else-if="optionContracts.length === 0" class="empty-text">
                <i class="fas fa-info-circle"></i> 暂无已下载期权数据
            </div>
            <div v-else class="quote-data-table">
                <div class="table-scroll">
                    <table class="data-table">
                        <thead class="sticky-header">
                            <tr>
                                <th>交易所</th>
                                <th>品种</th>
                                <th>合约名称</th>
                                <th>类型</th>
                                <th>行权价</th>
                                <th>标的</th>
                                <th>到期月</th>
                                <th class="sortable" @click="onOptionSort('start_date')">
                                    起始日期
                                    <span v-if="optionSortField === 'start_date'" class="sort-icon">
                                        {{ optionSortAsc ? '▲' : '▼' }}
                                    </span>
                                </th>
                                <th class="sortable" @click="onOptionSort('end_date')">
                                    结束日期
                                    <span v-if="optionSortField === 'end_date'" class="sort-icon">
                                        {{ optionSortAsc ? '▲' : '▼' }}
                                    </span>
                                </th>
                                <th>数据量</th>
                                <th>操作</th>
                            </tr>
                        </thead>
                        <tbody>
                            <tr v-for="c in pagedOptionContracts" :key="c.symbol_id"
                                :class="{ 'row-selected': selectedOptionContract?.symbol_id === c.symbol_id }">
                                <td>{{ c.exchange }}</td>
                                <td>{{ c.product }}</td>
                                <td class="symbol-code clickable" @click="onOptionContractClick(c)"
                                    :title="`点击查看 ${c.contract_name} 日线数据`">{{ c.contract_name }}</td>
                                <td>{{ c.call_put || '-' }}</td>
                                <td>{{ c.strike_price ? c.strike_price.toFixed(2) : '-' }}</td>
                                <td>{{ c.underlying || '-' }}</td>
                                <td>{{ c.year && c.month ? `${c.year}-${String(c.month).padStart(2, '0')}` : (c.exercise_date || '-') }}</td>
                                <td>{{ c.start_date || '-' }}</td>
                                <td>{{ c.end_date || '-' }}</td>
                                <td>{{ c.count }}</td>
                                <td>
                                    <button class="btn-danger btn-sm"
                                            @click="onOptionDeleteContract(c)">
                                        <i class="fas fa-trash"></i> 删除
                                    </button>
                                </td>
                            </tr>
                        </tbody>
                    </table>
                </div>
                <div class="pagination-center" v-if="optionContracts.length > 0">
                    <button class="page-btn" :disabled="optionPage === 1" @click="optionPage = 1">
                        <i class="fas fa-angle-double-left"></i>
                    </button>
                    <button class="page-btn" :disabled="optionPage === 1" @click="optionPage--">
                        <i class="fas fa-angle-left"></i>
                    </button>
                    <span class="page-info">第 {{ optionPage }} / {{ optionTotalPages }} 页 (共 {{ optionContracts.length }} 合约)</span>
                    <button class="page-btn" :disabled="optionPage === optionTotalPages" @click="optionPage++">
                        <i class="fas fa-angle-right"></i>
                    </button>
                    <button class="page-btn" :disabled="optionPage === optionTotalPages" @click="optionPage = optionTotalPages">
                        <i class="fas fa-angle-double-right"></i>
                    </button>
                    <span class="page-jump">
                        跳至 <input type="number" class="page-jump-input" min="1" :max="optionTotalPages"
                              v-model.number="optionJumpPage" @keyup.enter="onOptionJumpPage"> 页
                    </span>
                </div>
            </div>
        </div>
        <PromptDialog ref="promptDialogRef" />
    </div>
</template>

<style scoped>
@import './datacenter.scss';
</style>
