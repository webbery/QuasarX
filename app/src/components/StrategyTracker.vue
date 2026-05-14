<template>
    <div class="strategy-tracker">
        <!-- 顶部策略选择器 -->
        <div class="strategy-selector">
            <label>追踪策略：</label>
            <select v-model="selectedStrategy">
                <option value="">-- 选择策略 --</option>
                <option v-for="s in strategyList" :key="s.id" :value="s.id">
                    {{ s.name }}
                </option>
            </select>
        </div>

        <!-- Tab 切换 -->
        <div class="tabs">
            <button
                :class="{ active: activeTab === 'realtime' }"
                @click="activeTab = 'realtime'"
            >
                <i class="fas fa-bolt"></i> 实时监控
            </button>
            <button
                :class="{ active: activeTab === 'review' }"
                @click="activeTab = 'review'"
            >
                <i class="fas fa-history"></i> 历史复盘
            </button>
        </div>

        <!-- 实时监控面板 -->
        <div v-if="activeTab === 'realtime'" class="tab-content">
            <div class="empty-state">
                <i class="fas fa-bolt"></i>
                <p>实时监控</p>
                <span>选择运行中的策略以查看实时持仓、信号和净值曲线</span>
            </div>
        </div>

        <!-- 历史复盘面板 -->
        <div v-if="activeTab === 'review'" class="tab-content">
            <!-- 查询控制区 -->
            <div class="review-controls">
                <div class="control-group">
                    <label>标的：</label>
                    <input type="text" v-model="reviewSymbol" placeholder="600000.SH" />
                </div>
                <div class="control-group">
                    <label>开始时间：</label>
                    <input type="datetime-local" v-model="reviewStart" />
                </div>
                <div class="control-group">
                    <label>结束时间：</label>
                    <input type="datetime-local" v-model="reviewEnd" />
                </div>
                <button class="btn-query" @click="onQueryTicks" :disabled="queryLoading">
                    <i :class="queryLoading ? 'fa-spinner fa-spin' : 'fa-search'"></i>
                    {{ queryLoading ? '查询中...' : '查询' }}
                </button>
                <button class="btn-export" @click="onExportCSV" :disabled="rawTicks.length === 0">
                    <i class="fas fa-download"></i> 导出CSV
                </button>
            </div>

            <!-- 价格趋势图表 -->
            <PriceTrendChart
                v-if="tickChartData.length > 0"
                :prices="{ [reviewSymbol]: tickChartData }"
                :buy-signals="tickBuySignals"
                :sell-signals="tickSellSignals"
            />

            <div v-else class="empty-state">
                <i class="fas fa-chart-line"></i>
                <p>暂无数据</p>
                <span>设置查询条件后点击"查询"</span>
            </div>
        </div>
    </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import PriceTrendChart from './report/charts/PriceTrendChart.vue'

const activeTab = ref('realtime')
const selectedStrategy = ref('')
const strategyList = ref([])

// 复盘查询
const reviewSymbol = ref('')
const reviewStart = ref('')
const reviewEnd = ref('')
const rawTicks = ref([])  // 原始 CBOR tick 数据（用于导出）
const tickChartData = ref([])
const tickBuySignals = ref([])
const tickSellSignals = ref([])
const queryLoading = ref(false)

onMounted(() => {
    // TODO: 从后端获取运行中的策略列表
    strategyList.value = []
})

async function onQueryTicks() {
    if (!reviewSymbol.value) return

    queryLoading.value = true
    rawTicks.value = []
    tickChartData.value = []
    tickBuySignals.value = []
    tickSellSignals.value = []

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    const startTs = reviewStart.value ? Math.floor(new Date(reviewStart.value).getTime() / 1000) : 0
    const endTs = reviewEnd.value ? Math.floor(new Date(reviewEnd.value).getTime() / 1000) : 0

    try {
        // 1. 查询 CBOR Tick 数据
        const tickUrl = `https://${server}/v0/record?symbol=${reviewSymbol.value}&start=${startTs}&end=${endTs}`
        const tickRes = await fetch(tickUrl, {
            method: 'POST',
            headers: { 'Authorization': token }
        })
        if (tickRes.ok) {
            const ticks = await tickRes.json()
            rawTicks.value = ticks  // 保存原始数据用于导出
            tickChartData.value = ticks.map(t => {
                const d = new Date(t.time * 1000)
                const Y = d.getFullYear()
                const M = String(d.getMonth() + 1).padStart(2, '0')
                const D = String(d.getDate()).padStart(2, '0')
                const h = String(d.getHours()).padStart(2, '0')
                const m = String(d.getMinutes()).padStart(2, '0')
                return [`${Y}-${M}-${D} ${h}:${m}`, t.close]
            })
        }

        // 2. 查询历史交易记录
        const tradeUrl = `https://${server}/v0/trade/history?symbol=${reviewSymbol.value}&start=${startTs}&end=${endTs}&page_size=1000`
        const tradeRes = await fetch(tradeUrl, {
            headers: { 'Authorization': token }
        })
        if (tradeRes.ok) {
            const tradeData = await tradeRes.json()
            if (tradeData.trades) {
                // 提取买卖信号：[symbol, timestamp, quantity, price]
                for (const t of tradeData.trades) {
                    const symbol = t.order?.symbol || reviewSymbol.value
                    const side = t.order?.side
                    const price = t.average_price || t.trades?.[0]?.price || 0
                    const quantity = t.total_quantity || t.trades?.[0]?.quantity || 0
                    const time = t.order?.time || t.trades?.[0]?.time || 0

                    const signal = [symbol, time, quantity, price]
                    if (side === 0) {
                        tickBuySignals.value.push(signal)
                    } else if (side === 1) {
                        tickSellSignals.value.push(signal)
                    }
                }
            }
        }
    } catch (e) {
        console.error('查询数据失败:', e)
    } finally {
        queryLoading.value = false
    }
}

function onExportCSV() {
    if (rawTicks.value.length === 0) return

    const lines = []
    // Tick 行情数据表头
    lines.push('时间,开,高,低,收,成交量,成交额')
    // Tick 数据行
    for (const t of rawTicks.value) {
        const d = new Date(t.time * 1000)
        const ts = d.toLocaleString('zh-CN')
        lines.push(`${ts},${t.open},${t.high},${t.low},${t.close},${t.volume},${t.turnover}`)
    }

    // 分隔线 + 交易记录表头
    lines.push('--- 交易记录 ---')
    lines.push('时间,标的,方向,价格,数量,金额')
    // 买入信号
    for (const s of tickBuySignals.value) {
        const [sym, time, qty, price] = s
        const d = new Date(time * 1000)
        const ts = d.toLocaleString('zh-CN')
        lines.push(`${ts},${sym},买入,${price},${qty},${(price * qty).toFixed(2)}`)
    }
    // 卖出信号
    for (const s of tickSellSignals.value) {
        const [sym, time, qty, price] = s
        const d = new Date(time * 1000)
        const ts = d.toLocaleString('zh-CN')
        lines.push(`${ts},${sym},卖出,${price},${qty},${(price * qty).toFixed(2)}`)
    }

    // 生成 CSV 内容
    const csvContent = lines.join('\n')
    // BOM 防止 Excel 中文乱码
    const blob = new Blob(['\uFEFF' + csvContent], { type: 'text/csv;charset=utf-8;' })
    const url = URL.createObjectURL(blob)

    // 触发下载
    const a = document.createElement('a')
    const startStr = reviewStart.value || 'start'
    const endStr = reviewEnd.value || 'end'
    a.href = url
    a.download = `${reviewSymbol.value}_${startStr}_${endStr}.csv`
    a.click()
    URL.revokeObjectURL(url)
}
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
    overflow: auto;
    display: flex;
    flex-direction: column;
    gap: 16px;
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

/* 复盘查询控制区 */
.review-controls {
    display: flex;
    flex-wrap: wrap;
    gap: 12px;
    align-items: flex-end;
}

.control-group {
    display: flex;
    flex-direction: column;
    gap: 4px;
}

.control-group label {
    font-size: 12px;
    color: #64748b;
}

.control-group input {
    padding: 6px 10px;
    background: rgba(15, 23, 42, 0.7);
    border: 1px solid rgba(74, 158, 255, 0.3);
    border-radius: 6px;
    color: #e2e8f0;
    font-size: 13px;
}

.btn-query {
    padding: 6px 20px;
    background: linear-gradient(90deg, #2563eb, #1d4ed8);
    color: white;
    border: none;
    border-radius: 6px;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.2s;
}

.btn-query:hover:not(:disabled) {
    background: linear-gradient(90deg, #1d4ed8, #1e40af);
}

.btn-query:disabled {
    opacity: 0.6;
    cursor: not-allowed;
}

.btn-export {
    padding: 6px 20px;
    background: linear-gradient(90deg, #059669, #047857);
    color: white;
    border: none;
    border-radius: 6px;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.2s;
}

.btn-export:hover:not(:disabled) {
    background: linear-gradient(90deg, #047857, #065f46);
}

.btn-export:disabled {
    opacity: 0.4;
    cursor: not-allowed;
}
</style>
