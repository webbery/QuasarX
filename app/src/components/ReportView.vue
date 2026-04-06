<template>
    <div class="grid-container">
         <!-- Price Trend and Trading Signals - 独占一行 -->
        <div class="chart-row">
            <div class="chart-card full-width">
                <div class="chart-title">
                    <div class="title-icon">💹</div>
                    <span>Price Trend & Trading Signals</span>
                    <div class="chart-controls">
                        <select v-model="selectedSymbol[0]" @change="updatePriceChart">
                            <option v-for="symbol in selectedSymbol" :key="symbol">{{ symbol }}</option>
                        </select>
                    </div>
                </div>
                <div class="chart-container" id="priceTrend"></div>
            </div>
        </div>

        <!-- 策略 vs 基准对比图（新增，仅当选择基准时显示） -->
        <div class="chart-row" v-if="selectedBenchmark && benchmarkData.length > 0">
            <div class="chart-card full-width">
                <div class="chart-title">
                    <div class="title-icon">📈</div>
                    <span>策略 vs 基准对比</span>
                    <span class="benchmark-label">{{ benchmarkName }}</span>
                </div>
                <div class="chart-container" id="benchmarkCompare"></div>
            </div>
        </div>

        <!-- Strategy Performance - 独占一行 -->
        <div class="chart-row">
            <div class="chart-card full-width">
                <div class="chart-title">
                    <div class="title-icon">📈</div>
                    <span>Strategy Performance</span>
                    <div class="chart-controls">
                        <label>基准</label>
                        <select v-model="selectedBenchmark" @change="onBenchmarkChange" class="benchmark-select">
                            <option value="">-- 无 --</option>
                            <option v-for="idx in benchmarkIndices" :key="idx.code" :value="idx.code">
                                {{ idx.name }} ({{ idx.code }})
                            </option>
                        </select>
                    </div>
                </div>
                <div class="chart-container" id="strategyPerformance"></div>
            </div>
        </div>
        
       
        <!-- 双列布局 -->
        <div class="chart-row">
            <div class="chart-card">
                <div class="chart-title">
                    <div class="title-icon">📊</div>
                    <span>Position Changes</span>
                </div>
                <div class="chart-container" id="positionChanges"></div>
            </div>
            <div class="chart-card">
                <div class="chart-title">
                    <div class="title-icon">📊</div>
                    <span>Monthly Return</span>
                </div>
                <div class="chart-container" id="monthlyReturn"></div>
            </div>
        </div>
        
        <div class="chart-row">
            <div class="chart-card">
                <div class="chart-title">
                    <div class="title-icon">📅</div>
                    <span>Yearly Return</span>
                </div>
                <div class="chart-container" id="yearlyReturn"></div>
            </div>
            
            <div class="chart-card">
                <div class="chart-title">
                    <div class="title-icon">📋</div>
                    <span>Distribution of Monthly Return</span>
                </div>
                <div class="chart-container" id="distributionReturn"></div>
            </div>
        </div>
        
        <div class="chart-row">
            <div class="chart-card">
                <div class="chart-title">
                    <div class="title-icon">📐</div>
                    <span>Normal Distribution Q-Q</span>
                </div>
                <div class="chart-container" id="qqPlot"></div>
            </div>

            <div class="chart-card">
                <div class="chart-title">
                    <div class="title-icon">🎯</div>
                    <span>Performance vs. Expectation</span>
                </div>
                <div class="chart-container" id="performanceVsExpectation"></div>
            </div>
        </div>
        
        <!-- Rolling Statistics - 独占一行 -->
        <div class="chart-row">
            <div class="chart-card full-width">
                <div class="chart-title">
                    <div class="title-icon">🔄</div>
                    <span>Rolling Statistics (6 month)</span>
                </div>
                <div class="chart-container" id="rollingStats"></div>
            </div>
        </div>
        
        <div class="chart-row">
            <div class="chart-card">
                <div class="chart-title">
                    <div class="title-icon">📊</div>
                    <span>Return Quantiles</span>
                </div>
                <div class="chart-container" id="returnQuantiles"></div>
            </div>
            
            <div class="chart-card">
                <div class="chart-title">
                    <div class="title-icon">📉</div>
                    <span>Drawdown</span>
                </div>
                <div class="chart-container" id="drawdown"></div>
            </div>
        </div>
        
        <div class="chart-row">
            <div class="chart-card">
                <div class="chart-title">
                    <div class="title-icon">⚖️</div>
                    <span>Skewness</span>
                </div>
                <div class="chart-container" id="skewness"></div>
                <div class="stats-highlight">
                    <div class="stat-item">
                        <div class="stat-value positive">0.35</div>
                        <div class="stat-label">Skewness</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value neutral">3.12</div>
                        <div class="stat-label">Kurtosis</div>
                    </div>
                </div>
            </div>
            
            <!-- 占位卡片保持布局平衡 -->
            <div class="chart-card placeholder"></div>
        </div>
        
        <!-- Strategy Static Table -->
        <div class="chart-row">
            <div class="chart-card full-width">
                <div class="chart-title">
                    <div class="title-icon">📋</div>
                    <span>Strategy Static Table</span>
                </div>
                <div class="table-container">
                    <div class="table-wrapper" id="scrollableTable">
                        <table>
                            <thead>
                                <tr>
                                    <th>指标</th>
                                    <th>数值</th>
                                    <th>基准</th>
                                    <th>对比</th>
                                </tr>
                            </thead>
                            <tbody>
                                <!-- 动态数据将通过 updateMetricsTable 填充 -->
                            </tbody>
                        </table>
                    </div>
                </div>
            </div>
        </div>
    </div>
</template>

<script lang="ts" setup>
import * as echarts from 'echarts'
import { onMounted, nextTick, defineExpose, watch, ref, onUnmounted } from 'vue'
import axios from 'axios';
import https from 'https';
import {
  BENCHMARK_INDICES,
  getBenchmark,
  calculateMetrics,
  BenchmarkMetrics,
  KlineData,
  clearExpiredCache,
} from '../lib/tickflow';
import { useHistoryStore, type BacktestResult } from '@/stores/history'

// 暗色主题配置保持不变
const darkTheme = {
  backgroundColor: 'transparent',
  textStyle: {
    color: '#e0e0e0'
  },
  title: {
    textStyle: {
      color: '#e0e0e0'
    }
  },
  line: {
    itemStyle: {
      borderWidth: 1
    },
    lineStyle: {
      width: 2
    },
    symbolSize: 4,
    symbol: 'emptyCircle',
    smooth: false
  },
};

const chartInstances = ref<echarts.ECharts[]>([])
const priceChart = ref<echarts.ECharts | null>(null)
const performanceChart = ref<echarts.ECharts | null>(null)
const selectedSymbol = ref<string[]>([])
const zoomLevel = ref(100)
const tableScrollPosition = ref(0)
const symbolPrices = ref<any[]>([])
const buySignals = ref<any[]>([])
const sellSignals = ref<any[]>([])
// 新增：回测指标数据
const metricsData = ref<Record<string, number>>({})
// 策略性能图表的日期标签和数据
const strategyPerformanceDates = ref<string[]>([])
const strategyPerformanceData = ref<number[]>([])

// 新增：pending 状态，用于存储图表未初始化时的数据请求
const pendingPriceRequest = ref<{
  symbol: string
  startDate?: string
  endDate?: string
} | null>(null)

// 新增：标记价格数据已加载但图表未初始化
const needsPriceChartUpdate = ref(false)

// 基准对比相关状态
const selectedBenchmark = ref(localStorage.getItem('benchmark_symbol') || '')
const benchmarkMetrics = ref<BenchmarkMetrics | null>(null)
const benchmarkData = ref<KlineData[]>([])
const benchmarkName = ref('')
const loading = ref(false)
// 回测日期范围（用于获取基准数据）
const backtestStartDate = ref<Date | null>(null)
const backtestEndDate = ref<Date | null>(null)

// 监听 symbolPrices 变化：更新图表、提取日期范围、加载基准数据
watch(symbolPrices, (newPrices) => {
    if (newPrices.length > 0 && priceChart.value) {
        updatePriceChart();
    }
    // 提取日期范围用于基准对比
    if (newPrices.length > 0 && selectedBenchmark.value) {
        const firstDate = new Date(newPrices[0][0]);
        const lastDate = new Date(newPrices[newPrices.length - 1][0]);
        backtestStartDate.value = firstDate;
        backtestEndDate.value = lastDate;
        loadBenchmark(firstDate, lastDate);
    }
    // 更新策略性能图表的日期标签
    if (newPrices.length > 0) {
        updateStrategyPerformanceDates(newPrices);
    }
}, { deep: true });

// 监听 priceChart 初始化完成，处理延迟更新标记
watch(priceChart, (newChart) => {
    if (newChart && needsPriceChartUpdate.value) {
        console.info('[ReportView] priceChart 初始化完成，处理延迟更新标记');
        nextTick(() => {
            updatePriceChart();
            needsPriceChartUpdate.value = false;
        });
    }
}, { immediate: false });

// 监听基准数据变化，更新策略性能图表中的基准对比
watch(benchmarkData, (newData) => {
    if (newData.length > 0) {
        nextTick(() => {
            updateStrategyPerformanceChart();
        });
    }
}, { deep: true });

// 监听回测日期范围变化，更新策略性能图表的日期标签和数据（当没有价格数据时）
watch([backtestStartDate, backtestEndDate], ([start, end]) => {
    if (start && end) {
        console.info('[ReportView] 回测日期范围已更新:', formatDateTime(start), 'to', formatDateTime(end));
        // 更新策略性能图表的日期标签
        updateStrategyPerformanceDates();
        // 更新策略性能数据
        updateStrategyPerformanceData();
        // 更新策略性能图表
        nextTick(() => {
            updateStrategyPerformanceChart();
        });
    }
}, { immediate: false });

// 监听价格数据和交易信号
watch([symbolPrices, buySignals, sellSignals], ([prices, buys, sells]) => {
    if (prices.length > 0 && (buys.length > 0 || sells.length > 0)) {
        // 累计收益曲线已移除
    }
}, { deep: true });

onMounted(() => {
    console.info('mount report view')
    echarts.registerTheme('dark', darkTheme);

    // 使用 ResizeObserver 监听容器可见性，当容器从隐藏变为可见时初始化图表
    const gridContainer = document.querySelector('.grid-container');
    if (gridContainer) {
        let isInitialized = false;
        const resizeObserver = new ResizeObserver((entries) => {
            for (const entry of entries) {
                // 当容器可见且图表未完全初始化时
                if (entry.contentRect.width > 0 && entry.contentRect.height > 0 && !isInitialized) {
                    // 检查是否有图表需要初始化
                    const chartElements = document.querySelectorAll('#priceTrend, #strategyPerformance, #positionChanges, #monthlyReturn, #yearlyReturn, #distributionReturn, #qqPlot, #performanceVsExpectation, #rollingStats, #returnQuantiles, #drawdown, #skewness');
                    let needsInit = false;

                    chartElements.forEach(el => {
                        const htmlEl = el as HTMLElement;
                        if (htmlEl && htmlEl.offsetWidth > 0) {
                            const chart = echarts.getInstanceByDom(htmlEl);
                            if (!chart) {
                                needsInit = true;
                            }
                        }
                    });

                    if (needsInit) {
                        console.info('[ReportView] 容器可见，检测到未初始化的图表，开始初始化');
                        isInitialized = true;
                        initializeCharts();
                        resizeObserver.disconnect();
                    }
                }
            }
        });

        resizeObserver.observe(gridContainer);

        onUnmounted(() => {
            resizeObserver.disconnect();
        });
    }

    nextTick(() => {
        initializeCharts();
        initTableInteractions();
    })
});

function initTableInteractions() {
    const tableWrapper = document.getElementById('scrollableTable');
    if (tableWrapper) {
        // 添加拖拽滚动
        let isDragging = false;
        let startX = 0;
        let scrollLeft = 0;

        tableWrapper.addEventListener('mousedown', (e) => {
            isDragging = true;
            startX = e.pageX - tableWrapper.offsetLeft;
            scrollLeft = tableWrapper.scrollLeft;
            tableWrapper.style.cursor = 'grabbing';
        });

        tableWrapper.addEventListener('mousemove', (e) => {
            if (!isDragging) return;
            e.preventDefault();
            const x = e.pageX - tableWrapper.offsetLeft;
            const walk = (x - startX) * 2;
            tableWrapper.scrollLeft = scrollLeft - walk;
        });

        tableWrapper.addEventListener('mouseup', () => {
            isDragging = false;
            tableWrapper.style.cursor = 'grab';
        });

        tableWrapper.addEventListener('mouseleave', () => {
            isDragging = false;
            tableWrapper.style.cursor = 'grab';
        });
    }
}

function resetTableZoom() {
    zoomLevel.value = 100;
    const tableWrapper = document.getElementById('scrollableTable');
    if (tableWrapper) {
        tableWrapper.style.transform = 'scale(1)';
        tableWrapper.scrollLeft = 0;
        tableScrollPosition.value = 0;
    }
}

function updatePriceChart() {
    if (!priceChart.value) return;

    const option = getPriceOption(symbolPrices.value, sellSignals.value, buySignals.value);
    priceChart.value.setOption(option, true);
}

async function updatePrice(symbol: string, startDate?: string, endDate?: string) {
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    const url = 'https://' + server + '/v0/stocks/history'
    const agent = new https.Agent({
        rejectUnauthorized: false // 忽略证书错误
    });

    // 如果没有传入日期参数，使用默认范围
    let startTimestamp: number
    let endTimestamp: number

    if (startDate && endDate) {
        // 使用传入的日期范围（格式：YYYY-MM-DD）
        startTimestamp = Math.floor(new Date(startDate).getTime() / 1000)
        endTimestamp = Math.floor(new Date(endDate).getTime() / 1000)
    } else {
        // 默认范围
        startTimestamp = Date.parse('2010-01-01') / 1000
        endTimestamp = Date.parse('2025-01-01') / 1000
    }

    let params = {
        id: symbol,
        type: '1d',
        start: startTimestamp,
        end: endTimestamp,
        right: 1    // 默认使用后复权
    }

    const response = await axios.get(url, {
        params: params,
        httpsAgent: agent,
        headers: { 'Authorization': token}
    })

    console.info('get price response:', response)
    if (response.status != 200)
        return;

    // 检查图表是否已初始化
    if (!priceChart.value) {
        // 图表未初始化，存储请求参数等待后续处理
        console.info(`[updatePrice] 图表未初始化，存储 pending 请求：${symbol}`)
        pendingPriceRequest.value = { symbol, startDate, endDate }
    }

    symbolPrices.value = [];
    for (const oclhv of response.data) {
        const dt = oclhv['datetime']
        const date = new Date(dt * 1000)
        const Y = date.getFullYear() + '-';
        const M = (date.getMonth()+1 < 10 ? '0'+(date.getMonth()+1) : date.getMonth()+1) + '-';
        const D = date.getDate() ;
        symbolPrices.value.push([Y + M + D, oclhv['close']])
    }
    // 数据已存储，设置标记等待图表初始化后更新
    needsPriceChartUpdate.value = true;
    console.info('[updatePrice] 价格数据已加载，设置 needsPriceChartUpdate 标记');

    // 即使没有交易信号，也要更新图表显示收盘价
    console.info('len', symbolPrices.value.length)
    if (priceChart.value && symbolPrices.value.length > 0) {
        updatePriceChart();
    }
    if (performanceChart.value && symbolPrices.value.length > 0) {
        updateStrategyPerformanceChart();
    }
}

function initializeCharts() {
    console.info('initializeCharts')

    const chartsToInitialize = [
        { id: 'strategyPerformance', type: 'strategy' },
        { id: 'priceTrend', type: 'price' },
        { id: 'positionChanges', type: 'position' },
        { id: 'monthlyReturn', type: 'monthly' },
        { id: 'yearlyReturn', type: 'yearly' },
        { id: 'distributionReturn', type: 'distribution' },
        { id: 'qqPlot', type: 'qq' },
        { id: 'performanceVsExpectation', type: 'performance' },
        { id: 'rollingStats', type: 'rolling' },
        { id: 'returnQuantiles', type: 'quantiles' },
        { id: 'drawdown', type: 'drawdown' },
        { id: 'skewness', type: 'skewness' }
    ]

    chartsToInitialize.forEach(config => {
        const element = document.getElementById(config.id)
        if (element && element.offsetWidth > 0) {
            const chart = echarts.init(element, 'dark')

            let option;
            if (config.id === 'priceTrend') {
                priceChart.value = chart;
                option = getPriceOption(symbolPrices.value, sellSignals.value, buySignals.value)
            } else if (config.id === 'benchmarkCompare') {
                // 基准对比图表，通过 updateBenchmarkChart 单独初始化
                return;
            } else if (config.id === 'strategyPerformance') {
                performanceChart.value = chart;
                // 策略性能图表，等待数据更新后由 updateStrategyPerformanceChart 处理
                chartInstances.value.push(chart);
                const dates = symbolPrices.value.length > 0
                    ? symbolPrices.value.map((item: any) => item[0])
                    : strategyPerformanceDates.value;
                option = getPerformanceOption(dates)
            } else {
                option = getChartOption(config.type)
            }
            chart.setOption(option)
            chartInstances.value.push(chart)

            // 监听容器大小变化
            const resizeObserver = new ResizeObserver(() => {
                chart.resize()
            })
            resizeObserver.observe(element)

            onUnmounted(() => {
                resizeObserver.disconnect()
            })

            // 新增：如果是价格图表初始化完成，检查是否有 pending 请求或延迟更新标记
            if (config.id === 'priceTrend') {
                // 检查是否需要更新价格图表（数据已加载但图表刚初始化）
                if (needsPriceChartUpdate.value) {
                    console.info('[initializeCharts] 检测到 needsPriceChartUpdate 标记，更新价格图表');
                    nextTick(() => {
                        updatePriceChart();
                        needsPriceChartUpdate.value = false;
                    });
                }
                // 检查是否有 pending 请求
                if (pendingPriceRequest.value) {
                    console.info('[initializeCharts] 处理 pending 价格请求:', pendingPriceRequest.value)
                    const { symbol, startDate, endDate } = pendingPriceRequest.value
                    updatePrice(symbol, startDate, endDate).then(() => {
                        pendingPriceRequest.value = null
                    })
                }
            }
        } else {
            console.warn(`容器 ${config.id} 不可见，延迟初始化`)
            setTimeout(() => {
                const value = document.getElementById(config.id)?.offsetWidth
                if (value && value > 0) {
                    initializeCharts()
                }
            }, 300)
        }
    })
}

function getPriceOption(chartData: any[], sellSignals: any[], buySignals: any[]) {
    return {
        tooltip: {
            trigger: 'axis',
            axisPointer: {
                type: 'cross',
                label: {
                    backgroundColor: '#6a7985'
                }
            },
            backgroundColor: 'rgba(26, 34, 54, 0.9)',
            borderColor: '#2a3449',
            textStyle: {
                color: '#e0e0e0'
            },
            formatter: function(params: any) {
                let result = `<div style="margin: 0 0 5px 0; font-weight: bold;">${params[0].axisValue}</div>`;
                params.forEach((item: any) => {
                    if (item.seriesName === '价格') {
                        result += `<div>${item.marker} ${item.seriesName}: <span style="color: #2962ff; font-weight: bold;">${item.value[1].toFixed(2)}</span></div>`;
                    } else if (item.seriesName === '买入信号') {
                        result += `<div style="color: #00c853;">${item.marker} ${item.seriesName}</div>`;
                    } else if (item.seriesName === '卖出信号') {
                        result += `<div style="color: #ff6d00;">${item.marker} ${item.seriesName}</div>`;
                    }
                });
                return result;
            }
        },
        legend: {
            data: ['价格', '买入信号', '卖出信号'],
            textStyle: {
                color: '#e0e0e0'
            },
            top: 'top',
            right: '10%'
        },
        grid: {
            left: '3%',
            right: '4%',
            bottom: '15%', // 为dataZoom留出空间
            containLabel: true
        },
        dataZoom: [
            {
                type: 'inside',
                xAxisIndex: 0,
                filterMode: 'filter',
                zoomOnMouseWheel: true,
                moveOnMouseMove: true,
                moveOnMouseWheel: false
            },
            {
                type: 'slider',
                xAxisIndex: 0,
                filterMode: 'filter',
                bottom: '3%',
                height: 20,
                borderColor: '#2a3449',
                fillerColor: 'rgba(41, 98, 255, 0.2)',
                handleStyle: {
                    color: '#2962ff'
                },
                textStyle: {
                    color: '#a0aec0'
                }
            }
        ],
        brush: {
            toolbox: ['lineX', 'clear'],
            xAxisIndex: 0
        },
        toolbox: {
            feature: {
                dataZoom: {
                    yAxisIndex: false
                },
                restore: {},
                saveAsImage: {
                    pixelRatio: 2
                }
            },
            right: 10,
            top: 10
        },
        xAxis: {
            type: 'category',
            data: chartData.map((item: any) => item[0]),
            axisLine: {
                lineStyle: {
                    color: '#6E7079'
                }
            },
            axisLabel: {
                color: '#a0aec0',
                rotate: 0,
                formatter: function(value: string) {
                    return value;
                }
            },
            splitLine: {
                show: false
            }
        },
        yAxis: {
            type: 'value',
            scale: true,
            axisLine: {
                lineStyle: {
                    color: '#6E7079'
                }
            },
            axisLabel: {
                color: '#a0aec0'
            },
            splitLine: {
                lineStyle: {
                    color: '#2a3449',
                    type: 'dashed'
                }
            }
        },
        series: [
            {
                name: '价格',
                type: 'line',
                data: chartData.map((item: any) => item[1]),
                lineStyle: {
                    width: 2
                },
                itemStyle: {
                    color: '#2962ff'
                },
                smooth: true,
                showSymbol: false,
                animationDuration: 2000,
                animationEasing: 'cubicOut'
            },
            {
                name: '买入信号',
                type: 'scatter',
                data: buySignals,
                symbol: 'triangle',
                symbolSize: 16,
                itemStyle: {
                    color: '#00c853'
                },
                emphasis: {
                    scale: 1.5
                }
            },
            {
                name: '卖出信号',
                type: 'scatter',
                data: sellSignals,
                symbol: 'triangle',
                symbolSize: 16,
                symbolRotate: 180,
                itemStyle: {
                    color: '#ff6d00'
                },
                emphasis: {
                    scale: 1.5
                }
            }
        ]
    };
}

function getPerformanceOption(dates: string[]) {
    // 生成基准收益数据（如果有基准数据）
    let benchmarkSeries: any[] = [];
    if (benchmarkData.value.length > 0) {
        const benchmarkCumulative = benchmarkData.value.map((d) => {
            const firstClose = benchmarkData.value[0].close;
            return Number((((d.close - firstClose) / firstClose) * 100).toFixed(2));
        });
        benchmarkSeries = [{
            name: '基准收益',
            type: 'line',
            data: benchmarkCumulative,
            smooth: true,
            lineStyle: { width: 2, type: 'dashed', color: '#a0aec0' }
        }];
    }

    return {
        tooltip: {
            trigger: 'axis',
            axisPointer: {
                type: 'cross'
            },
            backgroundColor: 'rgba(26, 34, 54, 0.9)',
            borderColor: '#2a3449',
            textStyle: {
                color: '#e0e0e0'
            }
        },
        legend: {
            data: benchmarkSeries.length > 0 ? ['策略收益', '基准收益'] : ['策略收益'],
            textStyle: {
                color: '#e0e0e0'
            }
        },
        grid: {
            left: '3%',
            right: '4%',
            bottom: '3%',
            containLabel: true
        },
        xAxis: {
            type: 'category',
            data: dates,
            axisLine: {
                lineStyle: {
                    color: '#6E7079'
                }
            },
            axisLabel: {
                color: '#a0aec0'
            }
        },
        yAxis: {
            type: 'value',
            axisLabel: {
                formatter: '{value}%',
                color: '#a0aec0'
            },
            axisLine: {
                lineStyle: {
                    color: '#6E7079'
                }
            },
            splitLine: {
                lineStyle: {
                    color: '#2a3449'
                }
            }
        },
        series: [
            {
                name: '策略收益',
                type: 'line',
                data: strategyPerformanceData.value,
                lineStyle: {
                    width: 3
                },
                itemStyle: {
                    color: '#2962ff'
                },
                smooth: true
            },
            ...benchmarkSeries
        ]
    }
}

function getChartOption(type: string) {
    switch (type) {
        default:
            return {
                title: {
                    text: `${type} Chart`,
                    left: 'center',
                    textStyle: {
                        color: '#e0e0e0'
                    }
                },
                tooltip: {
                    trigger: 'axis'
                },
                xAxis: {
                    type: 'category',
                    data: ['1', '2', '3', '4', '5', '6', '7', '8', '9', '10'],
                    axisLine: {
                        lineStyle: {
                            color: '#6E7079'
                        }
                    },
                    axisLabel: {
                        color: '#a0aec0'
                    }
                },
                yAxis: {
                    type: 'value',
                    axisLine: {
                        lineStyle: {
                            color: '#6E7079'
                        }
                    },
                    axisLabel: {
                        color: '#a0aec0'
                    },
                    splitLine: {
                        lineStyle: {
                            color: '#2a3449'
                        }
                    }
                },
                series: [{
                    data: [820, 932, 901, 934, 1290, 1330, 1320, 801, 102, 230],
                    type: 'line',
                    smooth: true,
                    itemStyle: {
                        color: '#2962ff'
                    }
                }]
            }
    }
}

// 基准对比相关方法
const benchmarkIndices = BENCHMARK_INDICES;

// 加载基准数据
async function loadBenchmark(start: Date, end: Date) {
  if (!selectedBenchmark.value) {
    benchmarkMetrics.value = null;
    benchmarkData.value = [];
    benchmarkName.value = '';
    return;
  }

  loading.value = true;
  try {
    const result = await getBenchmark(selectedBenchmark.value, start, end);
    benchmarkMetrics.value = result.metrics;
    benchmarkData.value = result.data;
    benchmarkName.value = result.name;
    console.info(`[ReportView] 基准数据已加载：${result.name}`);
    updateBenchmarkChart();
    updateMetricsTable();
  } catch (e) {
    console.error('[ReportView] 获取基准数据失败:', e);
  } finally {
    loading.value = false;
  }
}

// 更新基准对比图表
function updateBenchmarkChart() {
  const chartEl = document.getElementById('benchmarkCompare');
  if (!chartEl || !benchmarkData.value.length || !metricsData.value.total_return) {
    return;
  }

  let chart = chartInstances.value.find(c => (c as any)._dom === chartEl);

  if (!chart) {
    chart = echarts.init(chartEl, 'dark');
    chartInstances.value.push(chart);
  }

  // 转换 K 线数据为累计收益曲线
  const benchmarkCloses = benchmarkData.value.map(d => d.close);
  const baseValue = benchmarkCloses[0];
  const strategyBase = 100000; // 基准本金

  // 基准累计收益
  const benchmarkCumulative = benchmarkCloses.map(c => ((c - baseValue) / baseValue + 1) * strategyBase);

  // 策略累计收益（简化：用总收益线性插值，实际应该使用每日净值）
  const totalReturn = metricsData.value.total_return || 0;
  const strategyCumulative = benchmarkCumulative.map((v, i) => {
    const progress = i / (benchmarkCumulative.length - 1);
    return strategyBase * (1 + totalReturn * progress);
  });

  // 日期标签
  const dates = benchmarkData.value.map(d => {
    const date = new Date(d.time * 1000);
    return `${date.getMonth() + 1}/${date.getDate()}`;
  });

  chart.setOption({
    tooltip: {
      trigger: 'axis',
      backgroundColor: 'rgba(26, 34, 54, 0.9)',
      borderColor: '#2a3449',
      textStyle: { color: '#e0e0e0' }
    },
    legend: {
      data: ['策略收益', benchmarkName.value],
      textStyle: { color: '#e0e0e0' },
      top: 10
    },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: {
      type: 'category',
      data: dates,
      axisLabel: { color: '#a0aec0' }
    },
    yAxis: {
      type: 'value',
      axisLabel: {
        formatter: (v: number) => ((v / strategyBase - 1) * 100).toFixed(0) + '%',
        color: '#a0aec0'
      },
      splitLine: { lineStyle: { color: '#2a3449', type: 'dashed' } }
    },
    series: [
      {
        name: '策略收益',
        type: 'line',
        data: strategyCumulative,
        smooth: true,
        lineStyle: { width: 3, color: '#2962ff' },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: 'rgba(41, 98, 255, 0.3)' },
            { offset: 1, color: 'rgba(41, 98, 255, 0.05)' }
          ])
        }
      },
      {
        name: benchmarkName.value,
        type: 'line',
        data: benchmarkCumulative,
        smooth: true,
        lineStyle: { width: 2, color: '#ff9800', type: 'dashed' }
      }
    ]
  });

  chart.resize();
}

// 基准变更
function onBenchmarkChange() {
  localStorage.setItem('benchmark_symbol', selectedBenchmark.value);
  if (backtestStartDate.value && backtestEndDate.value) {
    loadBenchmark(backtestStartDate.value, backtestEndDate.value);
  }
}

// 刷新基准数据
function refreshBenchmark() {
  // 清除缓存后重新加载
  clearExpiredCache().then(() => {
    if (backtestStartDate.value && backtestEndDate.value) {
      loadBenchmark(backtestStartDate.value, backtestEndDate.value);
    }
  });
}

// 新增：更新交易信号数据的方法
function updateTradeSignals(buySignalsData: any[], sellSignalsData: any[]) {
  try {
    // 验证输入数据
    if (!Array.isArray(buySignalsData) || !Array.isArray(sellSignalsData)) {
      console.error('交易信号数据必须是数组', { buySignalsData, sellSignalsData })
      return
    }

    // 更新响应式变量
    buySignals.value = buySignalsData
    sellSignals.value = sellSignalsData

    console.info(`更新交易信号数据：买入信号 ${buySignalsData.length} 条，卖出信号 ${sellSignalsData.length} 条`)

    // 触发图表更新
    if (priceChart.value) {
      updatePriceChart()
      console.info('价格图表已更新交易信号')
    } else {
      console.warn('价格图表实例未初始化，无法更新')
    }
  } catch (error) {
    console.error('更新交易信号数据时出错:', error)
  }
}

// 更新策略性能图表的日期标签（从价格数据或回测日期范围生成）
function updateStrategyPerformanceDates(prices?: any[]) {
  // 如果有价格数据，优先使用价格数据生成日期标签
  if (prices && prices.length > 0) {
    const monthMap = new Map<string, number>();
    prices.forEach((item: any) => {
      const date = new Date(item[0]);
      const monthKey = `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}`;
      if (!monthMap.has(monthKey)) {
        monthMap.set(monthKey, monthMap.size);
      }
    });

    // 生成显示标签（如 "1 月"、"2 月"）
    strategyPerformanceDates.value = Array.from(monthMap.keys()).map(key => {
      const [, month] = key.split('-');
      return `${parseInt(month)}月`;
    });
    console.info(`[StrategyPerformance] 从价格数据生成日期标签：${strategyPerformanceDates.value.length} 个月份`);
    return;
  }

  // 如果没有价格数据，使用 backtestStartDate 和 backtestEndDate 生成日期标签
  if (backtestStartDate.value && backtestEndDate.value) {
    const start = backtestStartDate.value;
    const end = backtestEndDate.value;
    const dates: string[] = [];

    const current = new Date(start);
    while (current <= end) {
      const month = current.getMonth() + 1;
      dates.push(`${month}月`);
      current.setMonth(current.getMonth() + 1);
    }

    strategyPerformanceDates.value = dates;
    console.info(`[StrategyPerformance] 从回测日期生成标签：${strategyPerformanceDates.value.length} 个月份`);
  }
}

// 更新策略性能数据（使用 metricsData 生成月度收益）
function updateStrategyPerformanceData() {
  const totalReturn = metricsData.value.total_return || 0;
  const numDates = strategyPerformanceDates.value.length || 12;

  // 简化：用总收益线性插值生成月度收益曲线
  // TODO: 如果后端能提供月度收益数据，应该直接使用
  strategyPerformanceData.value = strategyPerformanceDates.value.map((_, i) => {
    const progress = i / (numDates - 1);
    return Number((totalReturn * progress * 100).toFixed(2));
  });
  console.info(`[StrategyPerformance] 数据已更新：${strategyPerformanceData.value.length} 个点，总收益=${totalReturn}`);
}

// 更新 Strategy Performance 图表
function updateStrategyPerformanceChart() {
  if (!performanceChart.value) {
    return
  }

  // 生成基准收益数据（如果有基准数据）
  let benchmarkSeries = [];
  if (benchmarkData.value.length > 0) {
    const benchmarkCumulative = benchmarkData.value.map((d) => {
      const firstClose = benchmarkData.value[0].close;
      const dateStr = new Date(d.time * 1000).toISOString().split('T')[0];
      return [dateStr, Number((((d.close - firstClose) / firstClose) * 100).toFixed(2))];
    });
    benchmarkSeries = [{
      name: '基准收益',
      type: 'line',
      data: benchmarkCumulative,
      smooth: true,
      lineStyle: { width: 2, type: 'dashed', color: '#a0aec0' }
    }];
  }

  // 构建策略收益数据（带日期格式）
  let strategySeriesData = [];
  if (strategyPerformanceData.value.length > 0 && backtestStartDate.value && backtestEndDate.value) {
    const start = backtestStartDate.value.getTime();
    const end = backtestEndDate.value.getTime();
    const numPoints = strategyPerformanceData.value.length;
    const interval = numPoints > 1 ? (end - start) / (numPoints - 1) : 0;
    strategySeriesData = strategyPerformanceData.value.map((value, i) => {
      const date = new Date(start + (numPoints === 1 ? 0 : i * interval)).toISOString().split('T')[0];
      return [date, value];
    });
  }

  const dates = symbolPrices.value.length > 0
    ? symbolPrices.value.map((item: any) => item[0])
    : strategyPerformanceDates.value;

  const option = getPerformanceOption(dates)
  performanceChart.value.setOption(option);
  console.info('[StrategyPerformance] 图表已更新');
}

// 新增：指标名称映射（英文 -> 中文）
const metricNameMap: Record<string, string> = {
  total_return: '总收益率',
  annual_return: '年化收益率',
  max_drawdown: '最大回撤',
  sharp: '夏普比率',
  calmar_ratio: '卡玛比率',
  win_rate: '胜率',
  num_trades: '交易次数',
  VAR: '在险价值 (VaR)',
  ES: '预期短缺 (ES)',
  annual_sharp: '年化夏普比率',
  information_ratio: '信息比率',
  volatility: '波动率',
  alpha: 'Alpha',
  beta: 'Beta',
  avg_holding_days: '平均持仓天数',
  profit_loss_ratio: '盈亏比',
}

// 新增：格式化指标值
function formatMetricValue(key: string, value: number): string {
  // 比率类指标（名称包含 ratio, rate, return, drawdown, volatility, alpha, beta）
  const ratioKeys = ['ratio', 'rate', 'return', 'drawdown', 'volatility', 'alpha', 'beta', 'sharp']
  const isRatio = ratioKeys.some(k => key.toLowerCase().includes(k))

  if (isRatio) {
    // 如果是小数（绝对值小于 1），转换为百分比
    if (Math.abs(value) < 1) {
      return `${(value * 100).toFixed(2)}%`
    }
    return value.toFixed(4)
  }

  // 次数类指标（num, count, days）
  const countKeys = ['num', 'count', 'days', 'trades']
  const isCount = countKeys.some(k => key.toLowerCase().includes(k))
  if (isCount) {
    return Math.round(value).toString()
  }

  // 默认保留 4 位小数
  return value.toFixed(4)
}

// 新增：获取基准值（用于对比）
function getBenchmarkValue(key: string): string {
  // 优先使用动态基准（TickFlow 获取的实际指数数据）
  if (benchmarkMetrics.value && (benchmarkMetrics.value as any)[key] !== undefined) {
    return formatMetricValue(key, (benchmarkMetrics.value as any)[key]);
  }

  // Fallback 到默认基准
  const benchmarks: Record<string, number> = {
    total_return: 0.10,      // 10%
    annual_return: 0.08,     // 8%
    max_drawdown: -0.20,     // -20%
    sharp: 1.0,
    win_rate: 0.50,          // 50%
    num_trades: 50,
    volatility: 0.20,        // 20%
    alpha: 0,
    beta: 1.0,
  };
  const benchmark = benchmarks[key];
  if (benchmark !== undefined) {
    return formatMetricValue(key, benchmark);
  }
  return '-';
}

// 新增：计算对比值
function getComparison(key: string, actualValue: number): { value: string, type: 'positive' | 'neutral' | 'negative' } {
  // 优先使用动态基准
  let benchmark: number | undefined;
  if (benchmarkMetrics.value && (benchmarkMetrics.value as any)[key] !== undefined) {
    benchmark = (benchmarkMetrics.value as any)[key];
  }

  // 没有动态基准，使用默认
  if (benchmark === undefined) {
    const defaults: Record<string, number> = {
      total_return: 0.10,
      annual_return: 0.08,
      max_drawdown: -0.20,
      sharp: 1.0,
      win_rate: 0.50,
      num_trades: 50,
      volatility: 0.20,
    };
    benchmark = defaults[key];
  }

  if (benchmark === undefined) {
    return { value: '-', type: 'neutral' };
  }

  const diff = actualValue - benchmark;
  const isPositive = diff > 0;
  // 对于回撤和波动率，越低越好
  const isInverted = key === 'max_drawdown' || key === 'volatility';

  let displayValue: string;
  if (isRatioKeys(key)) {
    displayValue = `${diff > 0 ? '+' : ''}${(diff * 100).toFixed(2)}%`;
  } else {
    displayValue = `${diff > 0 ? '+' : ''}${diff.toFixed(2)}`;
  }

  const type = isInverted ? (diff < 0 ? 'positive' : 'negative') : (isPositive ? 'positive' : 'neutral');
  return { value: displayValue, type };
}

function isRatioKeys(key: string): boolean {
  const ratioKeys = ['ratio', 'rate', 'return', 'drawdown', 'volatility', 'alpha', 'beta', 'sharp']
  return ratioKeys.some(k => key.toLowerCase().includes(k))
}

// 新增：更新策略指标数据
function updateMetrics(features: Record<string, number>) {
  try {
    if (!features || typeof features !== 'object') {
      console.error('指标数据必须是对象', features)
      return
    }

    metricsData.value = features
    console.info('策略指标数据已更新:', features)

    // 更新策略性能数据
    updateStrategyPerformanceData();

    // 更新表格数据
    nextTick(() => {
      updateMetricsTable()
      // 更新策略性能图表
      updateStrategyPerformanceChart();
    })
  } catch (error) {
    console.error('更新策略指标数据时出错:', error)
  }
}

// 新增：更新基准数据（外部调用）
function updateBenchmark(data: { symbol: string; name: string; startDate: Date; endDate: Date }) {
  backtestStartDate.value = data.startDate;
  backtestEndDate.value = data.endDate;
  if (data.symbol) {
    selectedBenchmark.value = data.symbol;
    loadBenchmark(data.startDate, data.endDate);
  }
}

// 新增：从版本加载回测结果
async function loadBacktestResultFromVersion(versionId: string, startDate?: string, endDate?: string, symbol?: string) {
  const historyStore = useHistoryStore()
  const backtestResult = await historyStore.loadBacktestResult(versionId)

  if (backtestResult) {
    console.info(`[ReportView] 加载版本 ${versionId} 的回测结果`)

    // 1. 恢复策略指标
    updateMetrics(backtestResult.features || {})

    // 2. 恢复交易信号
    const formatSignals = (signals: any[]) => {
      return signals.map(signal => {
        const timestamp = signal[1]
        const price = signal[3]
        const date = new Date(timestamp * 1000)
        const Y = date.getFullYear() + '-'
        const M = (date.getMonth() + 1 < 10 ? '0' + (date.getMonth() + 1) : date.getMonth() + 1) + '-'
        const D = date.getDate()
        return [Y + M + D, price]
      })
    }

    updateTradeSignals(
      formatSignals(backtestResult.buy || []),
      formatSignals(backtestResult.sell || [])
    )

    // 3. 提取标的代码和日期范围
    const allSignals = [...(backtestResult.buy || []), ...(backtestResult.sell || [])]
    let signalStartDate: Date | null = null
    let signalEndDate: Date | null = null
    let signalSymbol = ''

    if (allSignals.length > 0) {
      // 从交易信号中提取
      const timestamps = allSignals.map(s => s[1])
      const minTime = Math.min(...timestamps)
      const maxTime = Math.max(...timestamps)
      signalStartDate = new Date(minTime * 1000)
      signalEndDate = new Date(maxTime * 1000)
      signalSymbol = backtestResult.buy?.[0]?.[0] || backtestResult.sell?.[0]?.[0] || ''
    }

    // 优先使用传入的参数，其次使用从信号中提取的值
    const useStartDate = startDate || (signalStartDate ? formatDateTime(signalStartDate) : null)
    const useEndDate = endDate || (signalEndDate ? formatDateTime(signalEndDate) : null)
    const useSymbol = symbol || signalSymbol

    // 设置 selectedSymbol 数组（用于下拉框显示）
    if (useSymbol) {
        // 支持多个标的（逗号分隔）
        const symbols = useSymbol.split(',').map(s => s.trim()).filter(s => s.length > 0)
        if (symbols.length > 0) {
            selectedSymbol.value = symbols
            console.info(`[ReportView] 设置标的代码列表：${selectedSymbol.value}`)
        }
    }

    // 4. 获取历史价格数据（即使没有交易信号）
    if (useSymbol && useStartDate && useEndDate) {
      console.info(`[ReportView] 获取历史价格：${useSymbol}, ${useStartDate} - ${useEndDate}`)
      await updatePrice(useSymbol, useStartDate, useEndDate)
    } else if (!useSymbol) {
      console.warn(`[ReportView] 无法获取标的代码，请检查流程图输入节点是否配置了代码参数`)
    }

    // 5. 加载基准数据
    if (signalStartDate && signalEndDate) {
      const benchmarkSymbol = localStorage.getItem('benchmark_symbol') || 'SH000300'
      updateBenchmark({
        symbol: benchmarkSymbol,
        name: '',
        startDate: signalStartDate,
        endDate: signalEndDate
      })
    }

    console.info(`[ReportView] 回测结果已恢复：${backtestResult.buy?.length || 0} 笔买入，${backtestResult.sell?.length || 0} 笔卖出`)
  } else {
    console.info(`[ReportView] 版本 ${versionId} 没有回测结果`)
  }
}

// 格式化日期为 YYYY-MM-DD 格式
function formatDateTime(date: Date): string {
  const Y = date.getFullYear() + '-'
  const M = (date.getMonth() + 1 < 10 ? '0' + (date.getMonth() + 1) : date.getMonth() + 1) + '-'
  const D = (date.getDate() < 10 ? '0' + date.getDate() : date.getDate())
  return Y + M + D
}

// 新增：更新指标表格
function updateMetricsTable() {
  const tbody = document.querySelector('#scrollableTable tbody')
  if (!tbody) return

  const features = metricsData.value
  const rows: string[] = []

  // 定义指标显示顺序
  const metricOrder = [
    'total_return', 'annual_return', 'max_drawdown', 'sharp', 'calmar_ratio',
    'win_rate', 'num_trades', 'VAR', 'ES', 'volatility', 'alpha', 'beta',
    'annual_sharp', 'information_ratio', 'avg_holding_days', 'profit_loss_ratio'
  ]

  // 按照定义的顺序遍历，如果指标存在则添加行
  metricOrder.forEach(key => {
    if (features.hasOwnProperty(key)) {
      const value = features[key]
      const chineseName = metricNameMap[key] || key
      const formattedValue = formatMetricValue(key, value)
      const benchmark = getBenchmarkValue(key)
      const comparison = getComparison(key, value)

      rows.push(`
        <tr>
          <td>${chineseName}</td>
          <td>${formattedValue}</td>
          <td>${benchmark}</td>
          <td class="${comparison.type}">${comparison.value}</td>
        </tr>
      `)
    }
  })

  // 如果还有未排序的指标，追加到后面
  const addedKeys = new Set(metricOrder)
  Object.keys(features).forEach(key => {
    if (!addedKeys.has(key)) {
      const value = features[key]
      const chineseName = metricNameMap[key] || key
      const formattedValue = formatMetricValue(key, value)
      rows.push(`
        <tr>
          <td>${chineseName}</td>
          <td>${formattedValue}</td>
          <td>-</td>
          <td class="neutral">-</td>
        </tr>
      `)
    }
  })

  tbody.innerHTML = rows.join('')
}

defineExpose({
    updatePrice,
    updatePriceChart,
    updateTradeSignals,  // 新增
    updateMetrics,       // 新增：更新策略指标
    updateBenchmark,     // 新增：更新基准数据
    loadBacktestResultFromVersion,  // 新增：从版本加载回测结果
    resetTableZoom,
    initializeCharts   // 新增：暴露初始化方法
})
</script>

<style scoped>
.grid-container {
    display: flex;
    flex-direction: column;
    flex: 1;
    gap: 20px;
    width: 100%;
    padding: 0;
    min-height: 0;
}


.benchmark-label {
    margin-left: 12px;
    font-size: 12px;
    color: #ff9800;
    padding: 2px 8px;
    background: rgba(255, 152, 0, 0.1);
    border-radius: 4px;
}

.chart-row {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 20px;
    width: 100%;
}

.chart-row:has(.full-width) {
    grid-template-columns: 1fr;
}

.chart-card {
    background: var(--panel-bg);
    border-radius: 12px;
    padding: 20px;
    border: 1px solid var(--border);
    transition: all 0.3s ease;
    box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
    display: flex;
    flex-direction: column;
    min-height: 0;
}

.chart-card:hover {
    box-shadow: 0 8px 25px rgba(0, 0, 0, 0.2);
    transform: translateY(-2px);
    border-color: #2962ff;
}

.chart-card.full-width {
    grid-column: 1 / -1;
}

.chart-card.placeholder {
    visibility: hidden;
    pointer-events: none;
}

.chart-title {
    font-size: 16px;
    font-weight: 600;
    margin-bottom: 16px;
    color: var(--text);
    display: flex;
    align-items: center;
    gap: 12px;
    padding-bottom: 12px;
    border-bottom: 1px solid var(--border);
}

.title-icon {
    font-size: 20px;
    width: 32px;
    height: 32px;
    display: flex;
    align-items: center;
    justify-content: center;
    background: rgba(41, 98, 255, 0.1);
    border-radius: 8px;
}

.chart-controls {
    margin-left: auto;
    display: flex;
    align-items: center;
    gap: 10px;
}

.chart-controls label {
    font-size: 14px;
    color: var(--text);
    font-weight: 500;
    white-space: nowrap;
}

.chart-controls .benchmark-select {
    padding: 8px 12px;
    background: rgba(42, 52, 77, 0.5);
    border: 1px solid var(--border);
    border-radius: 6px;
    color: var(--text);
    font-size: 14px;
    cursor: pointer;
    transition: all 0.2s;
    min-width: 150px;
}

.chart-controls .benchmark-select:hover {
    border-color: #2962ff;
}

.chart-controls .btn-refresh {
    padding: 8px 12px;
    background: rgba(41, 98, 255, 0.2);
    border: 1px solid #2962ff;
    border-radius: 6px;
    color: #2962ff;
    cursor: pointer;
    font-size: 16px;
    transition: all 0.2s;
}

.chart-controls .btn-refresh:hover:not(:disabled) {
    background: rgba(41, 98, 255, 0.3);
}

.chart-controls .btn-refresh:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}

.chart-controls select {
    background: rgba(42, 52, 77, 0.5);
    border: 1px solid var(--border);
    color: var(--text);
    padding: 6px 12px;
    border-radius: 6px;
    font-size: 14px;
    cursor: pointer;
    transition: all 0.2s;
    min-width: 100px;
}

.chart-controls select:hover {
    border-color: #2962ff;
    background: rgba(41, 98, 255, 0.1);
}

.chart-controls select:focus {
    outline: none;
    border-color: #2962ff;
    box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2);
}

.chart-container {
    height: 300px;
    min-height: 300px;
    width: 100%;
    flex-shrink: 0;
}

.chart-card.full-width .chart-container {
    height: 300px;
    min-height: 300px;
}

.table-container {
    display: flex;
    flex-direction: column;
    gap: 15px;
}

.table-wrapper {
    overflow-x: auto;
    overflow-y: hidden;
    border-radius: 8px;
    border: 1px solid var(--border);
    cursor: grab;
    transition: transform 0.3s ease;
}

.table-wrapper:active {
    cursor: grabbing;
}

.table-wrapper::-webkit-scrollbar {
    height: 8px;
}

.table-wrapper::-webkit-scrollbar-track {
    background: rgba(42, 52, 77, 0.3);
    border-radius: 4px;
}

.table-wrapper::-webkit-scrollbar-thumb {
    background: rgba(41, 98, 255, 0.5);
    border-radius: 4px;
}

.table-wrapper::-webkit-scrollbar-thumb:hover {
    background: rgba(41, 98, 255, 0.7);
}

table {
    width: 100%;
    border-collapse: collapse;
    margin: 0;
    min-width: 800px;
}

th, td {
    padding: 14px 16px;
    text-align: left;
    border-bottom: 1px solid var(--border);
    white-space: nowrap;
}

th {
    background-color: rgba(42, 52, 77, 0.5);
    font-weight: 600;
    color: var(--text);
    font-size: 14px;
    position: sticky;
    top: 0;
    z-index: 10;
}

td {
    color: var(--text);
    font-size: 14px;
}

tbody tr:hover {
    background-color: rgba(42, 52, 77, 0.3);
}

.positive {
    color: #00c853;
    font-weight: 600;
}

.neutral {
    color: #ff6d00;
    font-weight: 600;
}

/* 基准值列样式 */
.benchmark-value {
    color: #ff9800;
    font-weight: 500;
}

.table-controls {
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 10px;
    padding: 10px;
    background: rgba(42, 52, 77, 0.3);
    border-radius: 8px;
    border: 1px solid var(--border);
}

.table-btn {
    background: rgba(42, 52, 77, 0.5);
    border: 1px solid var(--border);
    color: var(--text);
    width: 36px;
    height: 36px;
    border-radius: 6px;
    display: flex;
    align-items: center;
    justify-content: center;
    cursor: pointer;
    transition: all 0.2s;
    font-size: 16px;
}

.table-btn:hover {
    background: rgba(41, 98, 255, 0.1);
    border-color: #2962ff;
    transform: translateY(-1px);
}

.table-btn:active {
    transform: translateY(0);
}

.zoom-level {
    color: var(--text);
    font-size: 14px;
    font-weight: 600;
    min-width: 50px;
    text-align: center;
}

.stats-highlight {
    display: flex;
    gap: 20px;
    margin-top: 20px;
    padding-top: 20px;
    border-top: 1px solid var(--border);
}

.stat-item {
    text-align: center;
    flex: 1;
}

.stat-value {
    font-size: 28px;
    font-weight: bold;
    margin-bottom: 8px;
}

.stat-label {
    font-size: 13px;
    color: var(--text-secondary);
    text-transform: uppercase;
    letter-spacing: 0.5px;
}

/* 响应式设计 */
@media (max-width: 1200px) {
    .chart-row {
        grid-template-columns: 1fr;
    }
    
    .chart-card.full-width .chart-container {
        height: 350px;
    }
}

@media (max-width: 768px) {
    .chart-card {
        padding: 16px;
    }
    
    .chart-container {
        height: 250px;
    }
    
    .chart-card.full-width .chart-container {
        height: 300px;
    }
    
    .stats-highlight {
        flex-direction: column;
        gap: 15px;
    }
    
    .chart-controls {
        flex-direction: column;
        align-items: flex-end;
        gap: 8px;
    }

    .chart-controls label {
        font-size: 12px;
    }

    .chart-controls select,
    .chart-controls .benchmark-select {
        min-width: 80px;
        font-size: 12px;
        padding: 4px 8px;
    }

    .chart-controls .btn-refresh {
        font-size: 14px;
        padding: 4px 8px;
    }
    
    .table-controls {
        flex-wrap: wrap;
    }
    
    .table-btn {
        width: 32px;
        height: 32px;
        font-size: 14px;
    }
}
</style>