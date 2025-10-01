<template>
    <div class="panel results-panel">
        <div class="results-section">
            <div class="full-width-chart chart-container">
                <h2 class="panel-title"><i class="fas fa-chart-bar"></i> 价格走势与交易信号</h2>
                <div id="priceChart"></div>
            </div>
            
            <div class="half-width-charts">
                <div class="chart-container">
                    <h2 class="panel-title"><i class="fas fa-wallet"></i> 资金曲线</h2>
                    <div id="equityChart"></div>
                </div>
                
                <div class="chart-container">
                    <h2 class="panel-title"><i class="fas fa-chart-pie"></i> 资产分布</h2>
                    <div id="pieChart"></div>
                </div>
            </div>
        </div>
        
        <div class="metrics-section">
            <h2 class="panel-title"><i class="fas fa-chart-line"></i> 关键指标</h2>
            <div class="metrics">
                <div class="metric-card">
                    <div class="metric-label">总收益率</div>
                    <div class="metric-value">{{ (results.totalReturn * 100).toFixed(2) }}%</div>
                </div>
                <div class="metric-card">
                    <div class="metric-label">年化收益率</div>
                    <div class="metric-value">{{ (results.annualReturn * 100).toFixed(2) }}%</div>
                </div>
                <div class="metric-card">
                    <div class="metric-label">最大回撤</div>
                    <div class="metric-value">{{ (results.maxDrawdown * 100).toFixed(2) }}%</div>
                </div>
                <div class="metric-card">
                    <div class="metric-label">夏普比率</div>
                    <div class="metric-value">{{ results.sharpeRatio.toFixed(2) }}</div>
                </div>
                <div class="metric-card">
                    <div class="metric-label">交易次数</div>
                    <div class="metric-value">{{ results.tradeCount }}</div>
                </div>
                <div class="metric-card">
                    <div class="metric-label">胜率</div>
                    <div class="metric-value">{{ (results.winRate * 100).toFixed(2) }}%</div>
                </div>
            </div>
        </div>
    </div>
</template>

<script setup>
import {ref, onMounted, defineExpose} from 'vue'
import axios from 'axios'
import https from 'https'
import Plotly from 'plotly.js-dist'

defineExpose({runBacktest})

let results = {
                    totalReturn: 0.2845,
                    annualReturn: 0.152,
                    maxDrawdown: -0.184,
                    sharpeRatio: 1.62,
                    tradeCount: 42,
                    winRate: 0.654
                }
let priceChart = ref()
let equityChart = ref()
let pieChart = ref()
let startDate = '2022-01-01'
let endDate = '2023-12-31'
let maPeriod = 5
let selectedAsset = '某股'
let selectSymbol = '000001'
let initialCapital = 100000

async function runBacktest() {
    console.info('get stock history data')
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    const url = 'https://' + server + '/v0/stocks/history'
    const agent = new https.Agent({  
        rejectUnauthorized: false // 忽略证书错误
    });
    let params = {
        id: selectSymbol,
        type: '1d',
        start: Date.parse('2010-01-01')/1000,
        end: Date.parse('2025-01-01')/1000,
        right: 0
    }
    const response = await axios.get(url, {
        params: params,
        httpsAgent: agent,
        responseType: 'application/json',
        headers: { 'Authorization': token}})
    console.info(response)
}

function getPriceChartData() {
    // 生成模拟价格数据
    const dates = [];
    const prices = [];
    const sma = [];
    const signals = [];
    
    const start = new Date(startDate);
    const end = new Date(endDate);
    
    let currentDate = new Date(start);
    let currentPrice = 150;
    
    while (currentDate <= end) {
        dates.push(new Date(currentDate));
        prices.push(currentPrice);
        
        // 模拟价格波动
        currentPrice += (Math.random() - 0.48) * 5;
        if (currentPrice < 50) currentPrice = 50 + Math.random() * 10;
        
        currentDate.setDate(currentDate.getDate() + 1);
    }
    
    // 计算简单移动平均
    for (let i = 0; i < prices.length; i++) {
        if (i < maPeriod) {
            sma.push(null);
        } else {
            let sum = 0;
            for (let j = i - maPeriod; j < i; j++) {
                sum += prices[j];
            }
            sma.push(sum / maPeriod);
        }
    }
    
    // 生成交易信号
    for (let i = 1; i < prices.length; i++) {
        if (prices[i] > sma[i] && prices[i-1] <= sma[i-1]) {
            signals.push({x: dates[i], y: prices[i], type: 'buy'});
        } else if (prices[i] < sma[i] && prices[i-1] >= sma[i-1]) {
            signals.push({x: dates[i], y: prices[i], type: 'sell'});
        }
    }
    
    const buySignals = signals.filter(s => s.type === 'buy');
    const sellSignals = signals.filter(s => s.type === 'sell');
    
    return [
        {
            x: dates,
            y: prices,
            type: 'scatter',
            mode: 'lines',
            name: '价格',
            line: {color: '#4a9eff', width: 2}
        },
        {
            x: dates,
            y: sma,
            type: 'scatter',
            mode: 'lines',
            name: `${maPeriod}日均线`,
            line: {color: '#f59e0b', width: 2, dash: 'dot'}
        },
        {
            x: buySignals.map(s => s.x),
            y: buySignals.map(s => s.y),
            type: 'scatter',
            mode: 'markers',
            name: '买入信号',
            marker: {color: '#10b981', size: 10, symbol: 'triangle-up'}
        },
        {
            x: sellSignals.map(s => s.x),
            y: sellSignals.map(s => s.y),
            type: 'scatter',
            mode: 'markers',
            name: '卖出信号',
            marker: {color: '#ef4444', size: 10, symbol: 'triangle-down'}
        }
    ];
}

function getPriceChartLayout() {
    return {
        title: `${selectedAsset} 价格走势`,
        height: 400,
        margin: { t: 40, l: 50, r: 30, b: 50 },
        plot_bgcolor: 'rgba(0,0,0,0)',
        paper_bgcolor: 'rgba(0,0,0,0)',
        font: { color: '#e2e8f0' },
        xaxis: {
            gridcolor: 'rgba(255,255,255,0.1)',
            showgrid: true,
            zeroline: false
        },
        yaxis: {
            gridcolor: 'rgba(255,255,255,0.1)',
            showgrid: true,
            zeroline: false
        },
        legend: {
            orientation: 'h',
            y: -0.2
        }
    };
}

function getEquityChartData() {
    // 生成模拟资金曲线
    const dates = [];
    const equity = [];
    
    const start = new Date(startDate);
    const end = new Date(endDate);
    
    let currentDate = new Date(start);
    let currentEquity = initialCapital;
    
    while (currentDate <= end) {
        dates.push(new Date(currentDate));
        
        // 模拟资金波动
        currentEquity *= (1 + (Math.random() - 0.49) * 0.008);
        equity.push(currentEquity);
        
        currentDate.setDate(currentDate.getDate() + 1);
    }
    
    return [{
        x: dates,
        y: equity,
        type: 'scatter',
        mode: 'lines',
        name: '资金曲线',
        line: {color: '#10b981', width: 3},
        fill: 'tozeroy',
        fillcolor: 'rgba(16, 185, 129, 0.2)'
    }];
}
                
function getEquityChartLayout() {
    return {
        title: '资金曲线',
        height: 300,
        margin: { t: 40, l: 50, r: 30, b: 50 },
        plot_bgcolor: 'rgba(0,0,0,0)',
        paper_bgcolor: 'rgba(0,0,0,0)',
        font: { color: '#e2e8f0' },
        xaxis: {
            gridcolor: 'rgba(255,255,255,0.1)',
            showgrid: true,
            zeroline: false
        },
        yaxis: {
            gridcolor: 'rgba(255,255,255,0.1)',
            showgrid: true,
            zeroline: false,
            tickprefix: '$'
        }
    };
}
function initCharts() {
    // 初始化价格图表
    priceChart.value = Plotly.newPlot('priceChart', getPriceChartData(), getPriceChartLayout());
    
    // 初始化资金曲线图表
    equityChart.value = Plotly.newPlot('equityChart', getEquityChartData(), getEquityChartLayout());
    
    // // 初始化饼图
    // pieChart.value = Plotly.newPlot('pieChart', this.getPieChartData(), this.getPieChartLayout());
}

onMounted(() => {
    initCharts();
})

function onBacktest() {
    // 模拟回测执行
    results = {
        totalReturn: 0.25 + Math.random() * 0.1,
        annualReturn: 0.12 + Math.random() * 0.05,
        maxDrawdown: -0.15 - Math.random() * 0.05,
        sharpeRatio: 1.5 + Math.random() * 0.3,
        tradeCount: 35 + Math.floor(Math.random() * 15),
        winRate: 0.6 + Math.random() * 0.1
    };
    
    updateCharts();
}

function updateCharts() {
    Plotly.react('priceChart', getPriceChartData(), getPriceChartLayout());
    Plotly.react('equityChart', getEquityChartData(), getEquityChartLayout());
    // Plotly.react('pieChart', this.getPieChartData(), this.getPieChartLayout());
}
</script>
<style scoped>
.panel {
    padding: 15px;
    background: rgba(15, 23, 42, 0.8);
    border-radius: 8px;
    box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
}

.results-section {
    display: flex;
    flex-direction: column;
    gap: 15px;
    margin-bottom: 20px;
}

.full-width-chart {
    width: 100%;
    background: rgba(15, 23, 42, 0.5);
    border-radius: 8px;
    padding: 12px;
}

.half-width-charts {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 15px;
}

.chart-container {
    background: rgba(15, 23, 42, 0.5);
    border-radius: 8px;
    padding: 12px;
    display: flex;
    flex-direction: column;
}

.panel-title {
    font-size: 1.1rem;
    margin-bottom: 10px;
    padding-bottom: 8px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.1);
    color: #4a9eff;
    display: flex;
    align-items: center;
    gap: 8px;
}

.panel-title i {
    font-size: 1rem;
}

.metrics-section {
    background: rgba(15, 23, 42, 0.5);
    border-radius: 8px;
    padding: 12px;
}

.metrics {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 10px;
}

.metric-card {
    background: rgba(30, 41, 59, 0.7);
    border-radius: 6px;
    padding: 10px;
    text-align: center;
    transition: all 0.2s ease;
}

.metric-card:hover {
    transform: translateY(-2px);
    box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
}

.metric-label {
    font-size: 0.85rem;
    color: #94a3b8;
    margin-bottom: 5px;
}

.metric-value {
    font-size: 1.2rem;
    font-weight: bold;
    color: #e2e8f0;
}

/* 响应式设计 */
@media (max-width: 1024px) {
    .half-width-charts {
        grid-template-columns: 1fr;
    }
}

@media (max-width: 768px) {
    .metrics {
        grid-template-columns: repeat(2, 1fr);
    }
}

@media (max-width: 480px) {
    .metrics {
        grid-template-columns: 1fr;
    }
    
    .panel {
        padding: 10px;
    }
}
</style>