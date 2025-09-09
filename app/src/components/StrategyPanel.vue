<template>
    <div class="panel-section">
        <h2 class="panel-title"><i class="fas fa-sliders-h"></i> 回测配置</h2>
        
        <div class="form-group">
            <label>资产类型</label>
            <div class="tabs">
                <div class="tab" :class="{active: assetType === 'stock'}" @click="assetType = 'stock'">股票</div>
                <div class="tab" :class="{active: assetType === 'etf'}" @click="assetType = 'etf'">ETF</div>
                <div class="tab" :class="{active: assetType === 'futures'}" @click="assetType = 'futures'">期货</div>
                <div class="tab" :class="{active: assetType === 'options'}" @click="assetType = 'options'">期权</div>
            </div>
        </div>
        
        <div class="form-group">
            <label>选择资产</label>
            <select v-model="selectedAsset">
                <option v-for="asset in assets[assetType]" :value="asset.id">{{ asset.name }} ({{ asset.symbol }})</option>
            </select>
        </div>
        
        <div class="form-group">
            <label>时间范围</label>
            <div style="display: flex; gap: 10px;">
                <input type="date" v-model="startDate"/>
                <input type="date" v-model="endDate"/>
            </div>
        </div>
        
        <div class="form-group">
            <label>初始资金 ({{ currency }})</label>
            <input type="number" v-model="initialCapital" min="1000" step="1000"/>
        </div>
        
        <div class="form-group">
            <label>交易策略</label>
            <select v-model="selectedStrategy">
                <option v-for="strategy in strategies" :value="strategy.id">{{ strategy.name }}</option>
            </select>
        </div>
        
        <div class="form-group">
            <label>技术指标</label>
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
                <div>
                    <select v-model="indicator1">
                        <option v-for="indicator in indicators" :value="indicator.id">{{ indicator.name }}</option>
                    </select>
                </div>
                <div>
                    <!-- <select v-model="indicator2">
                        <option v-for="indicator in indicators" :value="indicator.id">{{ indicator.name }}</option>
                    </select> -->
                </div>
            </div>
        </div>
        
        <div class="form-group">
            <label>参数设置</label>
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
                <div>
                    <label>移动平均周期</label>
                    <input type="number" v-model="maPeriod" min="5" max="200"/>
                </div>
                <div>
                    <label>RSI周期</label>
                    <input type="number" v-model="rsiPeriod" min="5" max="50"/>
                </div>
            </div>
        </div>
    </div>
</template>
<script setup>
import {ref} from 'vue'

let currency = ref(1000000)
let assetType = ref('stock')
let startDate = ref('2022-01-01')
let endDate = ref('2023-12-31')
let selectedStrategy = ref('')
let maPeriod = ref(20)
let rsiPeriod = ref(14)
let strategies = ref([
                    { id: 'ma_crossover', name: '双均线策略' },
                    { id: 'rsi_divergence', name: 'RSI背离策略' },
                    { id: 'bollinger_bands', name: '布林带策略' },
                    { id: 'macd', name: 'MACD策略' }
                ])
let indicators = ref([
                    { id: 'sma', name: '简单移动平均线' },
                    { id: 'ema', name: '指数移动平均线' },
                    { id: 'rsi', name: '相对强弱指数' },
                    { id: 'macd', name: 'MACD' },
                    { id: 'bollinger', name: '布林带' },
                    { id: 'stochastic', name: '随机指标' }
                ])
let assets = ref({
                    stock: [
                        { id: 'AAPL', name: '苹果公司', symbol: 'AAPL' },
                        { id: 'MSFT', name: '微软公司', symbol: 'MSFT' },
                        { id: 'GOOGL', name: '谷歌', symbol: 'GOOGL' },
                        { id: 'TSLA', name: '特斯拉', symbol: 'TSLA' }
                    ],
                    etf: [
                        { id: 'SPY', name: '标普500 ETF', symbol: 'SPY' },
                        { id: 'QQQ', name: '纳斯达克100 ETF', symbol: 'QQQ' },
                        { id: 'GLD', name: '黄金ETF', symbol: 'GLD' }
                    ],
                    futures: [
                        { id: 'ES', name: '标普500期货', symbol: 'ES' },
                        { id: 'CL', name: '原油期货', symbol: 'CL' },
                        { id: 'GC', name: '黄金期货', symbol: 'GC' }
                    ],
                    options: [
                        { id: 'AAPL_OPT', name: '苹果期权', symbol: 'AAPL' },
                        { id: 'TSLA_OPT', name: '特斯拉期权', symbol: 'TSLA' }
                    ]
                })
</script>
<style scoped>
.form-group {
    margin-bottom: 20px;
}
.stock { background: rgba(59, 130, 246, 0.2); color: #3b82f6; }
.etf { background: rgba(16, 185, 129, 0.2); color: #10b981; }
.futures { background: rgba(245, 158, 11, 0.2); color: #f59e0b; }
.options { background: rgba(139, 92, 246, 0.2); color: #8b5cf6; }

select, input {
    width: 100%;
    padding: 6px 8px;
    background: rgba(15, 23, 42, 0.7);
    border: 1px solid rgba(74, 158, 255, 0.3);
    border-radius: 8px;
    color: #e2e8f0;
    font-size: 1rem;
}
</style>