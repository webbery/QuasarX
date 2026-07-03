<template>
        <!-- Tick 数据下载 -->
        <div class="section-title">
            <i class="fas fa-chart-bar"></i> Tick 数据下载 (DuckDB)
        </div>

        <div class="input-group">
            <label>本地 DuckDB 路径</label>
            <input
                type="text"
                placeholder="如留空，默认在数据文件夹下创建 tick_data.db"
                v-model="tickDbPath"
            >
        </div>

        <div class="input-group">
            <label>标的代码 (留空下载全部)</label>
            <input
                type="text"
                placeholder="如 600000.SH"
                v-model="tickSymbol"
            >
        </div>

        <div class="input-row">
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

        <!-- 行情数据下载 (K线) -->
        <div class="section-divider"></div>
        <div class="section-title">
            <i class="fas fa-chart-line"></i> 行情数据下载 (K线)
        </div>

        <div class="input-row">
            <div class="input-group">
                <label>资产类型</label>
                <select v-model="quoteAssetType" class="quote-select">
                    <option value="etf">ETF</option>
                    <option value="stock">Stock</option>
                </select>
            </div>
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
            <input
                type="text"
                placeholder="510300.SH, 510500.SH"
                v-model="quoteSymbols"
            >
        </div>

        <div class="button-row">
            <button class="btn" @click="onHandleQuoteDownload" :disabled="quoteDownloading">
                <i class="fas fa-download"></i>
                {{ quoteDownloading ? '下载中...' : '开始下载' }}
            </button>
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

        <!-- 服务端数据管理 -->
        <div class="section-divider danger"></div>
        <div class="section-title danger">
            <i class="fas fa-exclamation-triangle"></i> 服务端数据管理
        </div>

        <div class="button-row">
            <button class="btn-danger" @click="onHandleDeleteServerTicks" :disabled="deleting">
                <i class="fas fa-trash"></i>
                {{ deleting ? '删除中...' : '删除服务端 Tick 数据' }}
            </button>
        </div>

        <div v-if="deleteStatus" class="status-text" :class="{ 'status-error': deleteStatus.includes('失败') }">
            {{ deleteStatus }}
        </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted, nextTick } from 'vue'
import { ipcRenderer } from 'electron'
import axios from 'axios'
import sseService from '@/ts/SSEService'

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
const quoteAssetType = ref('etf')
const quoteFreq = ref('5m')
const quoteSymbols = ref('')
const quoteStartDate = ref('')
const quoteEndDate = ref('')
const quoteDownloading = ref(false)
const quoteStatus = ref('')
const quoteLogs = ref([])
const quoteLogRef = ref(null)

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
    const d = msg.data
    switch (d.status) {
        case 'started':
            addQuoteLog(`开始下载: ${d.symbols}`)
            break
        case 'downloaded':
            addQuoteLog('脚本下载完成，正在导入...')
            break
        case 'download_failed':
            addQuoteLog('脚本下载失败', 'error')
            if (d.output) addQuoteLog(d.output, 'error')
            break
        case 'importing':
            addQuoteLog(`导入 ${d.table}: ${d.symbol} (${d.rows} 行)`, 'success')
            break
        case 'done':
            quoteDownloading.value = false
            if (d.success === 'true') {
                addQuoteLog(`完成: 共导入 ${d.total_rows} 行 → ${d.table}`, 'done')
                quoteStatus.value = `下载完成，${d.total_rows} 行已导入 ${d.table}`
            } else {
                addQuoteLog('下载失败', 'error')
                quoteStatus.value = '下载失败'
            }
            break
    }
}

onMounted(() => {
    ipcRenderer.on('tick-download-progress', onTickProgress)
    sseService.on('quote_download', onQuoteDownloadEvent)
})

onUnmounted(() => {
    ipcRenderer.removeListener('tick-download-progress', onTickProgress)
    sseService.off('quote_download', onQuoteDownloadEvent)
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
            asset_type: quoteAssetType.value,
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
    const result = await ipcRenderer.invoke('show-message-box', {
        type: 'warning',
        title: '确认删除',
        message: '确定要删除服务端所有 Tick 数据吗？此操作不可恢复！',
        buttons: ['取消', '确定删除'],
        defaultId: 0,
        cancelId: 0
    })

    if (!result || result.response !== 1) return

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
</script>

<style scoped>
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
.status-text {
    margin-top: 6px;
    color: #999;
    font-size: 12px;
}
.status-text.status-error {
    color: #f87171;
}

/* ── 分区 ── */
.section-divider {
    height: 1px;
    background: rgba(74, 85, 104, 0.3);
    margin: 16px 0;
}
.section-divider.danger {
    background: rgba(239, 68, 68, 0.3);
}
.section-title {
    color: #e0e0e0;
    font-size: 13px;
    font-weight: 600;
    margin-bottom: 8px;
    display: flex;
    align-items: center;
    gap: 6px;
}
.section-title.danger {
    color: #f87171;
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
</style>
