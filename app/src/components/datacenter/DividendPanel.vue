<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import axios from 'axios'
import sseService from '@/ts/SSEService'
import PromptDialog from '../PromptDialog.vue'
import StrategyPoolSelect from './StrategyPoolSelect.vue'
import SymbolCode from '../SymbolCode.vue'
import { useRemoteStrategies } from './composables/useRemoteStrategies'
import { useIsLoggedIn } from './composables/useSession'

const { remoteStrategies, remoteFetched, remoteReachable, fetchRemoteStrategies } = useRemoteStrategies()
const isLoggedIn = useIsLoggedIn()

// PromptDialog 引用
const promptDialogRef = ref<{ confirm: (opts: { title: string; message: string }) => Promise<boolean> } | null>(null)

const props = defineProps<{ active?: boolean }>()

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

const dividendStrategy = ref('')  // 空字符串 = 全部策略
const dividendStrategySymbols = ref('')  // 选中策略的标的列表（逗号分隔）
const dividendSymbolCount = computed(() => {
    if (!dividendStrategySymbols.value) return 0
    return dividendStrategySymbols.value.split(',').filter(s => s.trim()).length
})

// 选择策略后从本地或远程策略提取标的池

// 行情「按策略下载」：选策略后从本地流程图提取标的池
const dividendQueryDate = ref('')
const dividendQueryCode = ref('')
const dividendDownloadSymbols = ref('')
const dividendResults = ref<any[]>([])
const dividendStatus = ref('')
const dividendLoading = ref(false)
const dividendLoaded = ref(false)
const dividendDownloading = ref(false)
const dividendDeleting = ref(false)
const dividendCount = ref(0)
const isUpdatingDividend = ref(false)

// 分红分页
const dividendPage = ref(1)
const dividendPageSize = ref(20)
const dividendJumpPage = ref(1)

const dividendTotalPages = computed(() => Math.max(1, Math.ceil(dividendResults.value.length / dividendPageSize.value)))
const pagedDividendResults = computed(() => {
    const start = (dividendPage.value - 1) * dividendPageSize.value
    return dividendResults.value.slice(start, start + dividendPageSize.value)
})
const onDividendJumpPage = () => {
    dividendPage.value = Math.max(1, Math.min(dividendJumpPage.value, dividendTotalPages.value))
    dividendJumpPage.value = dividendPage.value
}

// SSE 完成事件 handler（模块级 const 保证 on/off 同引用）
const onDividendDownloadDone = (msg: any) => {
    if (msg.data.status !== 'completed' && msg.data.status !== 'aborted') return
    sseService.off('dividend_download', onDividendDownloadDone)
    if (msg.data.status === 'completed') {
        dividendStatus.value = `下载完成：导入 ${msg.data.imported_rows || 0} 行`
    } else {
        dividendStatus.value = `下载失败: ${msg.data.reason || '未知错误'}`
    }
    onLoadDividendData()
    dividendDownloading.value = false
    isUpdatingDividend.value = false
}

const dividendActionTypeName: Record<string, string> = {
    0: '未知', 1: '分红', 2: '送股', 3: '转增',
    4: '送转', 5: '分红送转', 6: '配股', 7: '混合'
}

// ── 分红除权数据方法 ──

// 切换到分红 tab 时自动加载
// 切到分红 Tab 时自动加载
watch(() => props.active, (active) => {
    if (active && !dividendLoaded.value && isLoggedIn.value) {
        onLoadDividendData()
    }
})

const onLoadDividendData = async () => {
    if (!isLoggedIn.value) return
    dividendLoading.value = true
    dividendStatus.value = ''
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    try {
        const params: Record<string, string> = {}
        if (dividendQueryDate.value) params.date = dividendQueryDate.value
        if (dividendQueryCode.value.trim()) params.code = dividendQueryCode.value.trim()
        const resp = await axios.get(`https://${server}/v0/dividend`, {
            params, headers: { 'Authorization': token || '' }
        })
        if (params.date || params.code) {
            dividendResults.value = resp.data.data || []
        } else {
            // 无参数：返回全部记录（前端分页）
            dividendResults.value = resp.data.data || []
        }
        dividendCount.value = dividendResults.value.length
        dividendPage.value = 1
        if (dividendResults.value.length > 0) {
            dividendStatus.value = `共 ${dividendCount.value} 条记录`
        }
    } catch (err: any) {
        dividendStatus.value = `查询失败: ${err.response?.data?.message || err.message}`
    } finally {
        dividendLoading.value = false
        dividendLoaded.value = true
    }
}

const onDownloadDividend = async () => {
    dividendDownloading.value = true
    const strategyLabel = dividendStrategy.value || '全部策略'
    dividendStatus.value = `正在从「${strategyLabel}」收集标的并下载分红数据...`
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    // 订阅 SSE 完成事件
    sseService.on('dividend_download', onDividendDownloadDone)

    try {
        const body: any = {}
        if (dividendStrategy.value && dividendStrategySymbols.value) {
            body.symbols = dividendStrategySymbols.value
        } else if (dividendStrategy.value) {
            body.strategy_name = dividendStrategy.value
        } else {
            body.from_strategies = true
        }
        const resp = await axios.post(`https://${server}/v0/dividend`, body, {
            headers: { 'Authorization': token || '' }
        })
        dividendStatus.value = `已启动下载: ${resp.data.symbol_count} 个标的，后台执行中...`
    } catch (err: any) {
        sseService.off('dividend_download', onDividendDownloadDone)
        dividendStatus.value = `启动失败: ${err.response?.data?.message || err.message}`
        dividendDownloading.value = false
    }
}

const onDownloadDividendBySymbol = async () => {
    const syms = dividendDownloadSymbols.value.trim()
    if (!syms) return
    dividendDownloading.value = true
    dividendStatus.value = `正在下载指定标的分红数据: ${syms}`
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    sseService.on('dividend_download', onDividendDownloadDone)

    try {
        const resp = await axios.post(`https://${server}/v0/dividend`, {
            symbols: syms
        }, { headers: { 'Authorization': token || '' } })
        dividendStatus.value = `已启动下载: ${resp.data.symbol_count} 个标的，后台执行中...`
    } catch (err: any) {
        sseService.off('dividend_download', onDividendDownloadDone)
        dividendStatus.value = `启动失败: ${err.response?.data?.message || err.message}`
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
    } catch (err: any) {
        dividendStatus.value = `删除失败: ${err.response?.data?.message || err.message}`
    } finally {
        dividendDeleting.value = false
    }
}

// 更新当前查询结果中的标的分红数据到最新
const onUpdateDividendLatest = async () => {
    if (isUpdatingDividend.value || !isLoggedIn.value) return
    if (dividendResults.value.length === 0) return

    // 从查询结果中提取去重 symbol
    const uniqueSymbols = new Set<string>()
    for (const item of dividendResults.value) {
        const sym = item.symbol || item.code
        if (sym) uniqueSymbols.add(sym)
    }
    if (uniqueSymbols.size === 0) return

    isUpdatingDividend.value = true
    dividendStatus.value = `正在为 ${uniqueSymbols.size} 个标的下载最新分红数据...`

    // 订阅 SSE completed/aborted 事件
    sseService.on('dividend_download', onDividendDownloadDone)

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    const symbolsStr = Array.from(uniqueSymbols).join(',')

    try {
        await axios.post(`https://${server}/v0/dividend`, {
            symbols: symbolsStr
        }, { headers: { 'Authorization': token || '' } })
    } catch (err: any) {
        sseService.off('dividend_download', onDividendDownloadDone)
        dividendStatus.value = `启动失败: ${err.response?.data?.message || err.message}`
        isUpdatingDividend.value = false
    }
}
</script>

<template>
    <div class="tab-content">
        <div class="section-title">
            <i class="fas fa-coins"></i> 分红除权数据管理
        </div>

        <!-- 第一行：左操作 + 右状态 -->
        <div class="dividend-top">
            <div class="dividend-actions">
                <!-- 查询 -->
                <div class="action-card">
                    <div class="card-label"><i class="fas fa-search"></i> 查询</div>
                    <div class="input-group">
                        <label>日期</label>
                        <input type="date" v-model="dividendQueryDate" />
                    </div>
                    <div class="input-group">
                        <label>标的</label>
                        <input type="text" placeholder="如 600519.SH（留空全部）" v-model="dividendQueryCode" />
                    </div>
                    <div class="action-btn-row">
                        <button class="btn btn-sm" @click="onLoadDividendData" :disabled="dividendLoading">
                            <i class="fas fa-search"></i> 查询
                        </button>
                        <button class="btn btn-primary btn-sm"
                                @click="onUpdateDividendLatest"
                                :disabled="isUpdatingDividend || dividendResults.length === 0 || !isLoggedIn"
                                title="对当前查询结果中的标的下载最新分红数据">
                            <i class="fas fa-cloud-download-alt"></i>
                            {{ isUpdatingDividend ? '更新中...' : '更新到最新' }}
                        </button>
                    </div>
                </div>

                <!-- 从策略下载 -->
                <div class="action-card">
                    <div class="card-label"><i class="fas fa-download"></i> 从策略下载</div>
                    <StrategyPoolSelect
                        v-model="dividendStrategy"
                        v-model:symbols="dividendStrategySymbols"
                        :remote-strategies="remoteStrategies"
                        :remote-fetched="remoteFetched"
                        :load-remote-pools="loadRemotePoolSymbols"
                        empty-label="全部策略"
                    />
                    <div class="action-btn-row">
                        <button class="btn btn-primary btn-sm" @click="onDownloadDividend" :disabled="dividendDownloading">
                            <i class="fas fa-download"></i>
                            {{ dividendDownloading ? '下载中...' : '下载' }}
                        </button>
                        <button class="btn-danger btn-sm" @click="onDeleteAllDividend" :disabled="dividendDeleting">
                            <i class="fas fa-trash"></i> 清空所有
                        </button>
                    </div>
                </div>

                <!-- 按标的下载 -->
                <div class="action-card">
                    <div class="card-label"><i class="fas fa-crosshairs"></i> 按标的下载</div>
                    <div class="input-group">
                        <label>标的代码</label>
                        <input type="text" placeholder="多个用逗号分隔，如 600519.SH,000858.SZ" v-model="dividendDownloadSymbols" />
                    </div>
                    <button class="btn btn-primary btn-sm" @click="onDownloadDividendBySymbol" :disabled="dividendDownloading || !dividendDownloadSymbols.trim()">
                        <i class="fas fa-download"></i>
                        {{ dividendDownloading ? '下载中...' : '下载' }}
                    </button>
                </div>
            </div>

            <!-- 右列：状态信息 -->
            <div class="dividend-status-panel">
                <div v-if="dividendStatus" class="status-text" :class="{ 'status-error': dividendStatus.includes('失败') }">
                    {{ dividendStatus }}
                </div>
                <div v-if="!dividendStatus" class="status-placeholder">
                    <i class="fas fa-info-circle"></i> 执行操作后此处显示状态
                </div>
                <div v-if="dividendStrategy && dividendSymbolCount > 0" class="status-summary">
                    <span>策略「{{ dividendStrategy }}」标的池: <strong>{{ dividendSymbolCount }}</strong> 个</span>
                </div>
                <div v-else-if="dividendCount > 0" class="status-summary">
                    <span>共 <strong>{{ dividendCount }}</strong> 条记录</span>
                </div>
            </div>
        </div>

        <!-- 第二行：数据表格 -->
        <div class="dividend-table-area">
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
                            <tr v-for="(item, idx) in pagedDividendResults" :key="idx">
                                <td class="symbol-code">
                                    <SymbolCode :symbol="item.symbol || item.code" />
                                </td>
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
            </div>

            <!-- 分页 -->
            <div class="pagination-center" v-if="dividendResults.length > 0">
                <button class="page-btn" @click="onLoadDividendData" :disabled="dividendLoading" title="刷新数据">
                    <i class="fas fa-sync-alt" :class="{ 'fa-spin': dividendLoading }"></i>
                </button>
                <button class="page-btn" :disabled="dividendPage === 1" @click="dividendPage = 1">
                    <i class="fas fa-angle-double-left"></i>
                </button>
                <button class="page-btn" :disabled="dividendPage === 1" @click="dividendPage--">
                    <i class="fas fa-angle-left"></i>
                </button>
                <span class="page-info">第 {{ dividendPage }} / {{ dividendTotalPages }} 页 (共 {{ dividendResults.length }} 条)</span>
                <button class="page-btn" :disabled="dividendPage === dividendTotalPages" @click="dividendPage++">
                    <i class="fas fa-angle-right"></i>
                </button>
                <button class="page-btn" :disabled="dividendPage === dividendTotalPages" @click="dividendPage = dividendTotalPages">
                    <i class="fas fa-angle-double-right"></i>
                </button>
                <select class="page-size-select" v-model.number="dividendPageSize" @change="dividendPage = 1">
                    <option :value="10">10 条/页</option>
                    <option :value="20">20 条/页</option>
                    <option :value="50">50 条/页</option>
                    <option :value="100">100 条/页</option>
                </select>
                <span class="page-jump">
                    跳至 <input type="number" class="page-jump-input" min="1" :max="dividendTotalPages"
                          v-model.number="dividendJumpPage" @keyup.enter="onDividendJumpPage"> 页
                </span>
            </div>

            <div v-if="dividendResults.length === 0 && !dividendLoading && dividendLoaded" class="empty-text">
                <i class="fas fa-info-circle"></i> 暂无分红数据
            </div>
        </div>
        <PromptDialog ref="promptDialogRef" />
    </div>
</template>

<style scoped>
@import './datacenter.scss';
</style>
