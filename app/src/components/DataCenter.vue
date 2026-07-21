<template>
    <div class="data-center-root">
        <!-- Tab 导航栏 -->
        <div class="tab-bar">
            <button
                class="tab-btn"
                :class="{ active: activeTab === 'quote' }"
                @click="activeTab = 'quote'"
            >
                <i class="fas fa-chart-line"></i> 行情数据
            </button>
            <button
                class="tab-btn"
                :class="{ active: activeTab === 'finance' }"
                @click="activeTab = 'finance'"
            >
                <i class="fas fa-file-invoice-dollar"></i> 基本面数据
            </button>
            <button
                class="tab-btn"
                :class="{ active: activeTab === 'dividend' }"
                @click="activeTab = 'dividend'"
            >
                <i class="fas fa-coins"></i> 分红除权
            </button>
        </div>

        <!-- ═══════════ Tab: 行情数据 ═══════════ -->
        <div v-show="activeTab === 'quote'" class="tab-content">
            <!-- 下载区 -->
            <div class="download-section">
                <div class="section-header" @click="downloadExpanded = !downloadExpanded">
                    <div class="section-title">
                        <i class="fas fa-download"></i> 数据下载
                    </div>
                    <i class="fas chevron-icon" :class="downloadExpanded ? 'fa-chevron-up' : 'fa-chevron-down'"></i>
                </div>
                <div v-show="downloadExpanded" class="download-body">
                    <!-- K线下载 -->
                    <div class="download-card">
                        <div class="card-title"><i class="fas fa-chart-line"></i> K线数据</div>
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
                            <span v-if="!isLoggedIn" class="login-hint">💡 需要先登录</span>
                        </div>
                        <div v-if="quoteStatus" class="status-text" :class="{ 'status-error': quoteStatus.includes('失败') }">
                            {{ quoteStatus }}
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
                                <tr v-for="(item, idx) in pagedSymbols" :key="`${item.table}-${item.symbol}`">
                                    <td class="col-checkbox">
                                        <input type="checkbox" :checked="isSelected(item)" @change="toggleSelect(item)">
                                    </td>
                                    <td class="asset-type">{{ item.assetType }}</td>
                                    <td class="freq">{{ item.freq }}</td>
                                    <td class="symbol-code">{{ item.symbol }}</td>
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
                <div v-if="allFlatSymbols.length === 0 && !loadingQuoteData && quoteDataList.length > 0 && (symbolFilter || assetTypeFilter)" class="empty-text">
                    <i class="fas fa-search"></i> 无匹配的标的，请调整筛选条件
                </div>

                <div v-if="deleteStatus" class="status-text" :class="{ 'status-error': deleteStatus.includes('失败') }">
                    {{ deleteStatus }}
                </div>
            </div>
        </div>

        <!-- ═══════════ Tab: 基本面数据 ═══════════ -->
        <div v-show="activeTab === 'finance'" class="tab-content">
            <div class="section-title">
                <i class="fas fa-file-invoice-dollar"></i> 基本面数据管理
            </div>

            <div class="input-row">
                <div class="input-group">
                    <label>标的代码</label>
                    <input type="text" placeholder="如 600519.SH（留空列出全部）" v-model="financeCode" />
                </div>
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

            <div class="button-row">
                <button class="btn" @click="onLoadFinanceData">
                    <i class="fas fa-search"></i> 查询
                </button>
                <button class="btn btn-primary" @click="onDownloadFinance" :disabled="financeDownloading">
                    <i class="fas fa-download"></i>
                    {{ financeDownloading ? '下载中...' : '下载财务数据' }}
                </button>
                <button class="btn-danger btn-sm" @click="onDeleteAllFinance" :disabled="financeDeleting">
                    <i class="fas fa-trash"></i> 清空所有
                </button>
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
                            <tr v-for="item in pagedFinanceItems" :key="`${item.category}-${item.symbol}`">
                                <td class="col-checkbox">
                                    <input type="checkbox" :checked="isFinanceSelected(item)" @change="toggleFinanceSelect(item)" />
                                </td>
                                <td>{{ categoryNameMap[item.category] || item.category }}</td>
                                <td class="symbol-code">{{ item.symbol }}</td>
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
        </div>

        <!-- ═══════════ Tab: 分红除权 ═══════════ -->
        <div v-show="activeTab === 'dividend'" class="tab-content">
            <div class="section-title">
                <i class="fas fa-coins"></i> 分红除权数据管理
            </div>

            <div class="input-row">
                <div class="input-group">
                    <label>查询日期</label>
                    <input type="date" v-model="dividendQueryDate" />
                </div>
                <div class="input-group">
                    <label>查询标的</label>
                    <input type="text" placeholder="如 600519.SH（留空列出全部）" v-model="dividendQueryCode" />
                </div>
            </div>

            <div class="button-row">
                <button class="btn" @click="onLoadDividendData" :disabled="dividendLoading">
                    <i class="fas fa-search"></i> 查询
                </button>
                <button class="btn btn-primary" @click="onDownloadDividend" :disabled="dividendDownloading">
                    <i class="fas fa-download"></i>
                    {{ dividendDownloading ? '下载中...' : '更新分红数据' }}
                </button>
                <button class="btn-danger btn-sm" @click="onDeleteAllDividend" :disabled="dividendDeleting">
                    <i class="fas fa-trash"></i> 清空所有
                </button>
            </div>

            <div v-if="dividendStatus" class="status-text" :class="{ 'status-error': dividendStatus.includes('失败') }">
                {{ dividendStatus }}
            </div>

            <div v-if="dividendResults.length > 0" class="quote-data-table">
                <div class="table-scroll">
                    <table class="data-table">
                        <thead class="sticky-header">
                            <tr>
                                <th>标的代码</th>
                                <th>除权除息日</th>
                                <th>登记日</th>
                                <th>类型</th>
                                <th>送股(10股)</th>
                                <th>转增(10股)</th>
                                <th>派息(10股)</th>
                            </tr>
                        </thead>
                        <tbody>
                            <tr v-for="(item, idx) in dividendResults" :key="idx">
                                <td class="symbol-code">{{ item.symbol || item.code || '-' }}</td>
                                <td>{{ item.ex_dividend_date || '-' }}</td>
                                <td>{{ item.record_date || '-' }}</td>
                                <td>{{ dividendActionTypeName[item.action_type] || item.action_type }}</td>
                                <td>{{ item.bonus_per_10 || 0 }}</td>
                                <td>{{ item.transfer_per_10 || 0 }}</td>
                                <td>{{ item.cash_per_10 || 0 }}</td>
                            </tr>
                        </tbody>
                    </table>
                </div>
                <div v-if="dividendCount > 0" class="pagination-center" style="padding: 8px;">
                    <span class="page-info">共 {{ dividendCount }} 条记录</span>
                </div>
            </div>

            <div v-if="dividendResults.length === 0 && !dividendLoading && dividendLoaded" class="empty-text">
                <i class="fas fa-info-circle"></i> 暂无分红数据
            </div>
        </div>

        <PromptDialog ref="promptDialogRef" />
    </div>
</template>

<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, onActivated, onDeactivated, nextTick } from 'vue'
import { ipcRenderer } from 'electron'
import axios from 'axios'
import sseService from '@/ts/SSEService'
import PromptDialog from './PromptDialog.vue'

// 声明接收父组件传递的事件（避免 Vue fragment 警告）
defineEmits(['load-version', 'strategy-click'])

// PromptDialog 引用
const promptDialogRef = ref(null)

// Tab 状态
const activeTab = ref<'quote' | 'finance' | 'dividend'>('quote')
const downloadExpanded = ref(true)

// 登录状态
const isLoggedIn = computed(() => {
    const token = localStorage.getItem('token')
    return token && token.length > 0
})

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
const quoteLogs = ref([])
const quoteLogRef = ref(null)

// 服务端数据列表状态
const quoteDataList = ref([])
const loadingQuoteData = ref(false)
const deletingQuote = ref(false)
const deletingSymbol = ref(false)
const updatingSymbol = ref(false)

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

// ── 基本面数据状态 ──
const financeCode = ref('')
const financeCategory = ref('')
const financeTables = ref([])
const financeStatus = ref('')
const financeDownloading = ref(false)
const financeDeleting = ref(false)
const financeLoading = ref(false)
const financeLoaded = ref(false)
const financePage = ref(1)
const selectedFinance = ref(new Set())
const categoryNameMap = {
    profit: '盈利能力', operation: '营运能力', growth: '成长能力',
    balance: '偿债能力', cashflow: '现金流量', dupont: '杜邦分析'
}
// 详情弹窗
const financeDetailVisible = ref(false)
const financeDetailTitle = ref('')
const financeDetailData = ref([])
const financeDetailKeys = ref([])

// 排序
const sortKey = ref('')
const sortOrder = ref('asc') // 'asc' or 'desc'

// ── 分红除权数据状态 ──
const dividendQueryDate = ref('')
const dividendQueryCode = ref('')
const dividendResults = ref([])
const dividendStatus = ref('')
const dividendLoading = ref(false)
const dividendLoaded = ref(false)
const dividendDownloading = ref(false)
const dividendDeleting = ref(false)
const dividendCount = ref(0)
const dividendActionTypeName = {
    0: '未知', 1: '分红', 2: '送股', 3: '转增',
    4: '送转', 5: '分红送转', 6: '配股', 7: '混合'
}

const handleSort = (key) => {
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
    const freqMap = {
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
            result.push({ table: table.table, assetType, freq, ...sym })
        }
    }
    return result
})

// 应用筛选和排序后的标的列表
const flatSymbols = computed(() => {
    let result = allFlatSymbols.value

    // 按标的代码筛选
    if (symbolFilter.value.trim()) {
        const filter = symbolFilter.value.trim().toUpperCase()
        result = result.filter(item => item.symbol.toUpperCase().includes(filter))
    }

    // 按资产类型筛选
    if (assetTypeFilter.value) {
        result = result.filter(item => item.assetType === assetTypeFilter.value)
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
const selectedSymbols = ref(new Set())  // key: "table|symbol"

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

const itemKey = (item) => `${item.table}|${item.symbol}`

const isSelected = (item) => selectedSymbols.value.has(itemKey(item))

const toggleSelect = (item) => {
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
const onTickProgress = (_, data) => {
    tickCount.value = data.count
}

// 行情下载 SSE 事件处理
const addQuoteLog = (text, type = 'info') => {
    const now = new Date()
    const time = `${String(now.getHours()).padStart(2,'0')}:${String(now.getMinutes()).padStart(2,'0')}:${String(now.getSeconds()).padStart(2,'0')}`
    quoteLogs.value.push({ time, text, type })
    nextTick(() => {
        if (quoteLogRef.value) quoteLogRef.value.scrollTop = quoteLogRef.value.scrollHeight
    })
}

const onQuoteDownloadEvent = (msg) => {
    console.log('[QuoteDownload] 收到 SSE 事件, msg:', msg)
    const d = msg.data
    console.log('[QuoteDownload] msg.data:', d)
    console.log('[QuoteDownload] status 字段值:', d.status)
    console.log('[QuoteDownload] 当前 quoteLogs 数量:', quoteLogs.value.length)
    switch (d.status) {
        case 'started':
            addQuoteLog(`📥 开始下载: ${d.total} 只标的 (${d.asset_type})`)
            break
        case 'symbol_downloaded':
            addQuoteLog(`✅ ${d.symbol}: ${d.rows} 行 (${d.downloaded}/${d.total})`, 'success')
            break
        case 'symbol_failed':
            addQuoteLog(`❌ ${d.symbol}: 下载失败 (${d.error})`, 'error')
            break
        case 'downloaded':
            addQuoteLog(`📥 下载完成: ${d.downloaded} 成功，${d.failed} 失败，开始导入...`)
            break
        case 'download_failed':
            addQuoteLog('脚本执行失败', 'error')
            if (d.output) {
                const lines = d.output.split(/\r?\n/).filter(l => l.trim())
                lines.forEach(line => addQuoteLog(line, 'error'))
            }
            break
        case 'importing':
            addQuoteLog(`导入 ${d.table}: ${d.symbol} (${d.rows} 行)`, 'success')
            break
        case 'done':
            quoteDownloading.value = false
            if (d.success === 'true') {
                addQuoteLog(`✅ 完成: 共导入 ${d.total_rows} 行 → ${d.table}`, 'done')
                quoteStatus.value = `下载完成，${d.total_rows} 行已导入 ${d.table}`
            } else {
                addQuoteLog('下载失败', 'error')
                quoteStatus.value = '下载失败'
            }
            break
    }
}

onMounted(() => {
    // 只在首次挂载时注册 SSE handler（KeepAlive 不会触发重复注册）
    ipcRenderer.on('tick-download-progress', onTickProgress)
    sseService.on('quote_download', onQuoteDownloadEvent)
    
    // 加载已下载的行情数据列表
    if (isLoggedIn.value) {
        loadQuoteData()
        onLoadFinanceData()
    }
})

onUnmounted(() => {
    // 组件真正销毁时才移除 handler
    ipcRenderer.removeListener('tick-download-progress', onTickProgress)
    sseService.off('quote_download', onQuoteDownloadEvent)
})

// KeepAlive 缓存期间不需要重复注册
onActivated(() => {
    // 组件被 KeepAlive 缓存后重新显示
    console.log('[DataCenter] onActivated')
})

onDeactivated(() => {
    // 组件被 KeepAlive 缓存后隐藏
    console.log('[DataCenter] onDeactivated')
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
    } catch (err) {
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
    quoteStatus.value = ''
    quoteLogs.value = []

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    try {
        await axios.post(`https://${server}/v0/quote`, {
            symbols: quoteSymbols.value.trim(),
            freq: quoteFreq.value,
            start: quoteStartDate.value || undefined,
            end: quoteEndDate.value || undefined,
        }, {
            headers: { 'Authorization': token || '' }
        })
        // POST 立即返回，进度通过 SSE 推送
    } catch (err) {
        quoteDownloading.value = false
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
    } catch (err) {
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
    } catch (err) {
        quoteStatus.value = `加载数据列表失败: ${err.response?.data?.message || err.message}`
    } finally {
        loadingQuoteData.value = false
    }
}

// 删除单个表
const onDeleteTable = async (tableName) => {
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
    } catch (err) {
        quoteStatus.value = `删除失败: ${err.response?.data?.message || err.message}`
    } finally {
        deletingQuote.value = false
    }
}

// 更新单个标的（重新下载）
const onUpdateSymbol = async (tableName, symbol) => {
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
        const freqMap = {
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
    } catch (err) {
        quoteStatus.value = `更新失败: ${err.response?.data?.message || err.message}`
    } finally {
        updatingSymbol.value = false
    }
}

// 删除单个标的
const onDeleteSymbol = async (tableName, symbol) => {
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
    } catch (err) {
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
        } catch (err) {
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
    const freqMap = {
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
            await axios.post(`https://${server}/v0/quote`, params, {
                headers: { 'Authorization': token || '' }
            })
            successCount += symbols.length  // 该组所有标的都算成功
        } catch (err) {
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
        } catch (err) {
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
        } catch (err) {
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
    } catch (err) {
        quoteStatus.value = `删除失败: ${err.response?.data?.message || err.message}`
    } finally {
        deletingQuote.value = false
    }
}

// ═══════════════════════════════════════════════════════════
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

const financeItemKey = (item) => `${item.category}|${item.symbol}`
const isFinanceSelected = (item) => selectedFinance.value.has(financeItemKey(item))

const isFinanceAllSelected = computed(() => {
    return pagedFinanceItems.value.length > 0 && pagedFinanceItems.value.every(item => isFinanceSelected(item))
})

const toggleFinanceSelect = (item) => {
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

const onLoadFinanceData = async () => {
    if (!isLoggedIn.value) return
    financeLoading.value = true
    financeStatus.value = ''
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    try {
        const params = {}
        if (financeCategory.value) params.category = financeCategory.value
        if (financeCode.value.trim()) params.code = financeCode.value.trim()
        const resp = await axios.get(`https://${server}/v0/finance`, {
            params, headers: { 'Authorization': token || '' }
        })
        financeTables.value = resp.data.tables || []
        financeStatus.value = `查询完成，共 ${financeFlatItems.value.length} 条记录`
    } catch (err) {
        financeStatus.value = `查询失败: ${err.response?.data?.message || err.message}`
    } finally {
        financeLoading.value = false
        financeLoaded.value = true
    }
}

const onDownloadFinance = async () => {
    if (!financeCode.value.trim()) {
        financeStatus.value = '请输入标的代码'
        return
    }
    financeDownloading.value = true
    financeStatus.value = '正在下载...'
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    try {
        await axios.post(`https://${server}/v0/finance`, {
            code: financeCode.value.trim(),
            category: financeCategory.value || 'all'
        }, { headers: { 'Authorization': token || '' } })
        financeStatus.value = '下载任务已提交'
        setTimeout(onLoadFinanceData, 3000)
    } catch (err) {
        financeStatus.value = `下载失败: ${err.response?.data?.message || err.message}`
    } finally {
        financeDownloading.value = false
    }
}

const onViewFinanceDetail = async (item) => {
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
    } catch (err) {
        financeStatus.value = `查询详情失败: ${err.response?.data?.message || err.message}`
    }
}

const onDeleteFinanceSymbol = async (item) => {
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
    } catch (err) {
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
    } catch (err) {
        financeStatus.value = `删除失败: ${err.response?.data?.message || err.message}`
    } finally {
        financeDeleting.value = false
    }
}

// ── 分红除权数据方法 ──

const onLoadDividendData = async () => {
    if (!isLoggedIn.value) return
    dividendLoading.value = true
    dividendStatus.value = ''
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    try {
        const params = {}
        if (dividendQueryDate.value) params.date = dividendQueryDate.value
        if (dividendQueryCode.value.trim()) params.code = dividendQueryCode.value.trim()
        const resp = await axios.get(`https://${server}/v0/dividend`, {
            params, headers: { 'Authorization': token || '' }
        })
        if (params.date) {
            dividendResults.value = resp.data.data || []
        } else if (params.code) {
            dividendResults.value = resp.data.data || []
        } else {
            // 无参数：返回所有标的列表
            dividendResults.value = []
            dividendStatus.value = `共 ${resp.data.count} 个标的已导入分红数据，输入日期或代码查询详情`
        }
        dividendCount.value = dividendResults.value.length
        if (dividendResults.value.length > 0) {
            dividendStatus.value = `查询完成，共 ${dividendCount.value} 条记录`
        }
    } catch (err) {
        dividendStatus.value = `查询失败: ${err.response?.data?.message || err.message}`
    } finally {
        dividendLoading.value = false
        dividendLoaded.value = true
    }
}

const onDownloadDividend = async () => {
    dividendDownloading.value = true
    dividendStatus.value = '正在从活跃策略收集标的并下载分红数据...'
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    try {
        const resp = await axios.post(`https://${server}/v0/dividend`, {
            from_strategies: true
        }, { headers: { 'Authorization': token || '' } })
        dividendStatus.value = `已启动下载: ${resp.data.symbol_count} 个标的，后台执行中...`
        // 10秒后自动刷新查询结果
        setTimeout(onLoadDividendData, 10000)
    } catch (err) {
        dividendStatus.value = `启动失败: ${err.response?.data?.message || err.message}`
    } finally {
        dividendDownloading.value = false
    }
}

const onDeleteAllDividend = async () => {
    const confirmed = await promptDialogRef.value?.confirm({
        title: '确认删除',
        message: '确定要清空所有分红除权数据吗？此操作不可恢复！'
    })
    if (!confirmed) return
    dividendDeleting.value = true
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    try {
        await axios.delete(`https://${server}/v0/dividend`, {
            headers: { 'Authorization': token || '' }
        })
        dividendStatus.value = '所有分红数据已清空'
        dividendResults.value = []
        dividendCount.value = 0
    } catch (err) {
        dividendStatus.value = `删除失败: ${err.response?.data?.message || err.message}`
    } finally {
        dividendDeleting.value = false
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
</script>

<style scoped>
/* ── 根容器 ── */
.data-center-root {
    display: flex;
    flex-direction: column;
    max-height: calc(100vh - 60px);
    overflow: hidden;
}

/* ── Tab 导航栏 ── */
.tab-bar {
    display: flex;
    gap: 2px;
    padding: 0 4px;
    border-bottom: 1px solid rgba(74, 85, 104, 0.3);
    background: rgba(15, 20, 35, 0.4);
    flex-shrink: 0;
}
.tab-btn {
    padding: 8px 20px;
    border: none;
    border-bottom: 2px solid transparent;
    background: transparent;
    color: #999;
    font-size: 12px;
    font-weight: 500;
    cursor: pointer;
    transition: all 0.2s;
    display: flex;
    align-items: center;
    gap: 6px;
}
.tab-btn:hover {
    color: #e0e0e0;
    background: rgba(41, 98, 255, 0.05);
}
.tab-btn.active {
    color: #2962ff;
    border-bottom-color: #2962ff;
    font-weight: 600;
}

/* ── Tab 内容区 ── */
.tab-content {
    flex: 1;
    overflow-y: auto;
    padding: 12px 4px 12px 0;
}
.tab-content::-webkit-scrollbar {
    width: 6px;
}
.tab-content::-webkit-scrollbar-track {
    background: rgba(15, 20, 35, 0.5);
}
.tab-content::-webkit-scrollbar-thumb {
    background: rgba(74, 85, 104, 0.5);
    border-radius: 3px;
}
.tab-content::-webkit-scrollbar-thumb:hover {
    background: rgba(74, 85, 104, 0.8);
}

/* ── 下载区 ── */
.download-section {
    border: 1px solid rgba(74, 85, 104, 0.25);
    border-radius: 6px;
    margin-bottom: 12px;
    overflow: hidden;
}
.section-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 8px 12px;
    background: rgba(26, 34, 54, 0.6);
    cursor: pointer;
    user-select: none;
    transition: background 0.2s;
}
.section-header:hover {
    background: rgba(26, 34, 54, 0.9);
}
.chevron-icon {
    color: #999;
    font-size: 11px;
}
.download-body {
    display: flex;
    gap: 12px;
    padding: 12px;
}
.download-card {
    flex: 1;
    border: 1px solid rgba(74, 85, 104, 0.2);
    border-radius: 4px;
    padding: 10px 12px;
    background: rgba(15, 20, 35, 0.3);
}
.card-title {
    color: #e0e0e0;
    font-size: 12px;
    font-weight: 600;
    margin-bottom: 8px;
    display: flex;
    align-items: center;
    gap: 6px;
}

/* ── 管理区 ── */
.manage-section {
    margin-top: 4px;
}

/* ── 基础控件（与 AnalysisControlBar 统一） ── */
input, select {
    width: 100%;
    padding: 4px 8px;
    background: rgba(26, 34, 54, 0.8);
    border: 1px solid rgba(74, 85, 104, 0.3);
    border-radius: 4px;
    color: #e0e0e0;
    font-size: 12px;
    outline: none;
}
input:focus, select:focus {
    border-color: rgba(41, 98, 255, 0.5);
}
input::-webkit-calendar-picker-indicator {
    filter: invert(1);
    cursor: pointer;
}
select option {
    background: #1a2236;
    color: #e0e0e0;
}

/* ── 按钮 ── */
.btn {
    padding: 6px 16px;
    border: 1px solid rgba(74, 85, 104, 0.3);
    border-radius: 4px;
    background: rgba(26, 34, 54, 0.8);
    color: #e0e0e0;
    font-size: 12px;
    cursor: pointer;
    transition: all 0.2s;
}
.btn:hover:not(:disabled) {
    border-color: rgba(41, 98, 255, 0.5);
}
.btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}
.btn-primary {
    background: #2962ff;
    border-color: #2962ff;
    color: #fff;
    font-weight: 600;
}
.btn-primary:hover:not(:disabled) {
    background: #1e54e6;
    border-color: #1e54e6;
}
.btn-danger {
    padding: 6px 16px;
    border: 1px solid rgba(239, 68, 68, 0.4);
    border-radius: 4px;
    background: rgba(239, 68, 68, 0.15);
    color: #f87171;
    font-size: 12px;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.2s;
}
.btn-danger:hover:not(:disabled) {
    background: rgba(239, 68, 68, 0.25);
    border-color: rgba(239, 68, 68, 0.6);
}
.btn-danger:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}
.btn-sm {
    padding: 4px 12px;
    font-size: 11px;
}
.btn-xs {
    padding: 3px 8px;
    font-size: 10px;
}

/* ── 布局 ── */
.input-row {
    display: flex;
    gap: 12px;
    margin-top: 8px;
}
.input-row .input-group {
    flex: 1;
}
.button-group {
    display: flex;
    gap: 8px;
    margin-top: 8px;
}
.button-row {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-top: 8px;
}

/* ── 标签 & 状态 ── */
.input-group {
    margin-top: 8px;
}
.input-group label {
    display: block;
    color: #999;
    font-size: 12px;
    margin-bottom: 4px;
}
.progress-text {
    color: #999;
    font-size: 12px;
}
.login-hint {
    color: #fbbf24;
    font-size: 11px;
    font-style: italic;
}
.status-text {
    margin-top: 6px;
    color: #999;
    font-size: 12px;
}
.status-text.status-error {
    color: #f87171;
}

/* ── 分区标题 ── */
.section-title {
    color: #e0e0e0;
    font-size: 13px;
    font-weight: 600;
    margin-bottom: 8px;
    display: flex;
    align-items: center;
    gap: 6px;
}

/* ── 标的筛选行 ── */
.symbol-search-row {
    display: flex;
    gap: 12px;
    margin-top: 8px;
    margin-bottom: 8px;
}
.search-item {
    display: flex;
    align-items: center;
    gap: 8px;
    flex: 1;
}
.search-label {
    color: #999;
    font-size: 12px;
    white-space: nowrap;
    min-width: 65px;
}
.search-item input,
.search-item select {
    flex: 1;
    margin: 0;
}

/* ── 数据列表表格 ── */
.quote-data-table {
    margin-top: 12px;
    border: 1px solid rgba(74, 85, 104, 0.3);
    border-radius: 4px;
    overflow: hidden;
}
.table-scroll {
    max-height: 420px;
    overflow-y: auto;
}
.table-scroll::-webkit-scrollbar {
    width: 6px;
}
.table-scroll::-webkit-scrollbar-track {
    background: rgba(15, 20, 35, 0.5);
}
.table-scroll::-webkit-scrollbar-thumb {
    background: rgba(74, 85, 104, 0.5);
    border-radius: 3px;
}
.table-scroll::-webkit-scrollbar-thumb:hover {
    background: rgba(74, 85, 104, 0.8);
}
.btn-warning {
    padding: 4px 12px;
    border: 1px solid rgba(251, 191, 36, 0.4);
    border-radius: 4px;
    background: rgba(251, 191, 36, 0.15);
    color: #fbbf24;
    font-size: 11px;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.2s;
}
.btn-warning:hover:not(:disabled) {
    background: rgba(251, 191, 36, 0.25);
    border-color: rgba(251, 191, 36, 0.6);
}
.btn-warning:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}
.btn-info {
    padding: 3px 8px;
    border: 1px solid rgba(96, 165, 250, 0.4);
    border-radius: 3px;
    background: rgba(96, 165, 250, 0.15);
    color: #60a5fa;
    font-size: 10px;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.2s;
}
.btn-info:hover:not(:disabled) {
    background: rgba(96, 165, 250, 0.25);
    border-color: rgba(96, 165, 250, 0.6);
}
.btn-info:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}
.btn-success {
    padding: 4px 12px;
    border: 1px solid rgba(74, 222, 128, 0.4);
    border-radius: 4px;
    background: rgba(74, 222, 128, 0.15);
    color: #4ade80;
    font-size: 11px;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.2s;
}
.btn-success:hover:not(:disabled) {
    background: rgba(74, 222, 128, 0.25);
    border-color: rgba(74, 222, 128, 0.6);
}
.btn-success:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}
.btn-icon {
    padding: 4px 8px;
    border: 1px solid rgba(74, 85, 104, 0.4);
    border-radius: 4px;
    background: rgba(74, 85, 104, 0.15);
    color: #94a3b8;
    font-size: 11px;
    cursor: pointer;
    transition: all 0.2s;
}
.btn-icon:hover {
    background: rgba(74, 85, 104, 0.3);
    border-color: rgba(74, 85, 104, 0.6);
    color: #e0e0e0;
}
.export-dir-hint {
    font-size: 10px;
    color: #4ade80;
    max-width: 200px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    opacity: 0.8;
}
.data-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 11px;
}
.data-table thead {
    background: rgba(26, 34, 54, 0.8);
}
.data-table th {
    padding: 8px 10px;
    text-align: left;
    color: #999;
    font-weight: 600;
    border-bottom: 1px solid rgba(74, 85, 104, 0.3);
}
.data-table th.sortable {
    cursor: pointer;
    user-select: none;
    transition: color 0.2s;
}
.data-table th.sortable:hover {
    color: #e0e0e0;
}
.data-table th.sortable .sort-icon {
    margin-left: 4px;
    font-size: 10px;
    opacity: 0.8;
}
.data-table thead.sticky-header th {
    position: sticky;
    top: 0;
    z-index: 10;
    background: #1a2236;
}
.data-table td {
    padding: 8px 10px;
    color: #e0e0e0;
    border-bottom: 1px solid rgba(74, 85, 104, 0.2);
}
.data-table tbody tr:hover {
    background: rgba(41, 98, 255, 0.05);
}
.col-checkbox {
    width: 40px;
    text-align: center;
}
.col-checkbox input[type="checkbox"] {
    cursor: pointer;
    accent-color: #2962ff;
    width: 14px;
    height: 14px;
}
.asset-type {
    font-weight: 600;
    text-align: center;
    white-space: nowrap;
}
.asset-type:after {
    content: '';
}
.freq {
    color: #fbbf24;
    font-family: 'Courier New', monospace;
    font-weight: 600;
    text-align: center;
    white-space: nowrap;
}
.table-name {
    font-family: 'Courier New', monospace;
    color: #2962ff;
    font-weight: 600;
    vertical-align: middle;
}
.symbol-code {
    font-family: 'Courier New', monospace;
    color: #60a5fa;
    font-weight: 600;
}
.time-range {
    color: #999;
    font-size: 11px;
    font-family: 'Courier New', monospace;
}
.symbol-count {
    color: #4ade80;
    font-weight: 600;
    text-align: right;
}
.symbols-list {
    max-width: 400px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}
.symbol-tag {
    display: inline-block;
    padding: 2px 6px;
    margin: 2px 4px 2px 0;
    background: rgba(41, 98, 255, 0.1);
    border: 1px solid rgba(41, 98, 255, 0.3);
    border-radius: 3px;
    color: #60a5fa;
    font-size: 10px;
    font-family: 'Courier New', monospace;
}
.more-symbols {
    color: #999;
    font-size: 10px;
    font-style: italic;
}
.actions {
    white-space: nowrap;
}
.loading-text,
.empty-text {
    margin-top: 12px;
    padding: 12px;
    text-align: center;
    color: #999;
    font-size: 12px;
}
.loading-text i,
.empty-text i {
    margin-right: 6px;
}

/* ── 行情下载日志 ── */
.quote-select {
    appearance: auto;
}
.quote-log-box {
    margin-top: 8px;
    border: 1px solid rgba(74, 85, 104, 0.3);
    border-radius: 4px;
    overflow: hidden;
}
.quote-log-title {
    padding: 4px 10px;
    background: rgba(26, 34, 54, 0.6);
    color: #999;
    font-size: 11px;
    font-weight: 600;
    border-bottom: 1px solid rgba(74, 85, 104, 0.2);
}
.quote-log-content {
    max-height: 180px;
    overflow-y: auto;
    padding: 6px 10px;
    background: rgba(15, 20, 35, 0.5);
    font-family: 'Courier New', monospace;
    font-size: 11px;
}
.quote-log-content::-webkit-scrollbar {
    width: 6px;
}
.quote-log-content::-webkit-scrollbar-track {
    background: rgba(15, 20, 35, 0.5);
}
.quote-log-content::-webkit-scrollbar-thumb {
    background: rgba(74, 85, 104, 0.5);
    border-radius: 3px;
}
.quote-log-content::-webkit-scrollbar-thumb:hover {
    background: rgba(74, 85, 104, 0.8);
}
.quote-log-line {
    color: #999;
    line-height: 1.5;
}
.quote-log-line .log-time {
    color: #666;
    margin-right: 6px;
}
.quote-log-line.log-error {
    color: #f87171;
}
.quote-log-line.log-success {
    color: #4ade80;
}
.quote-log-line.log-done {
    color: #2962ff;
    font-weight: 600;
}

/* ── 分页控件 ── */
.pagination {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    justify-content: space-between;
    gap: 8px;
    padding: 10px 12px;
    border-top: 1px solid rgba(74, 85, 104, 0.3);
    background: rgba(26, 34, 54, 0.6);
}
.pagination-left {
    display: flex;
    align-items: center;
    gap: 8px;
}
.pagination-center {
    display: flex;
    align-items: center;
    gap: 4px;
}
.page-btn {
    padding: 3px 6px;
    border: 1px solid rgba(74, 85, 104, 0.3);
    border-radius: 3px;
    background: rgba(26, 34, 54, 0.8);
    color: #e0e0e0;
    font-size: 11px;
    cursor: pointer;
    transition: all 0.2s;
}
.page-btn:hover:not(:disabled) {
    border-color: rgba(41, 98, 255, 0.5);
    background: rgba(41, 98, 255, 0.15);
}
.page-btn:disabled {
    opacity: 0.4;
    cursor: not-allowed;
}
.page-info {
    color: #999;
    font-size: 11px;
    min-width: 200px;
    text-align: center;
    margin: 0 4px;
}
.page-size-select {
    padding: 2px 4px;
    border: 1px solid rgba(74, 85, 104, 0.3);
    border-radius: 3px;
    background: rgba(26, 34, 54, 0.8);
    color: #e0e0e0;
    font-size: 10px;
    outline: none;
    max-width: 80px;
}
.page-size-select option {
    background: #1a2236;
    color: #e0e0e0;
}

/* ── 财务数据详情弹窗 ── */
.finance-detail-overlay {
    position: fixed;
    top: 0; left: 0; right: 0; bottom: 0;
    background: rgba(0, 0, 0, 0.6);
    z-index: 1000;
    display: flex;
    align-items: center;
    justify-content: center;
}
.finance-detail-dialog {
    background: #1a2236;
    border: 1px solid rgba(74, 85, 104, 0.5);
    border-radius: 8px;
    width: 90%;
    max-width: 1000px;
    max-height: 80vh;
    display: flex;
    flex-direction: column;
}
.finance-detail-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 12px 16px;
    border-bottom: 1px solid rgba(74, 85, 104, 0.3);
    color: #e0e0e0;
    font-size: 14px;
    font-weight: 600;
}
.finance-detail-close {
    background: none;
    border: none;
    color: #999;
    font-size: 20px;
    cursor: pointer;
    padding: 0 4px;
}
.finance-detail-close:hover {
    color: #f87171;
}
.finance-detail-body {
    padding: 12px;
    overflow: auto;
}
</style>
