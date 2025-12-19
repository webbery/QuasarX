<template>
    <div class="grid-container">
         <!-- Price Trend and Trading Signals - 独占一行 -->
        <div class="chart-row">
            <div class="chart-card full-width">
                <div class="chart-title">
                    <div class="title-icon">💹</div>
                    <span>Price Trend & Trading Signals</span>
                    <div class="chart-controls">
                        <select v-model="selectedSymbol" @change="updatePriceChart">
                            <option value="000001">000001</option>
                        </select>
                    </div>
                </div>
                <div class="chart-container" id="priceTrend"></div>
            </div>
        </div>
        
        <!-- Strategy Performance - 独占一行 -->
        <div class="chart-row">
            <div class="chart-card full-width">
                <div class="chart-title">
                    <div class="title-icon">📈</div>
                    <span>Strategy Performance</span>
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
                                <tr>
                                    <td>年化收益率</td>
                                    <td>15.3%</td>
                                    <td>10.2%</td>
                                    <td class="positive">+5.1%</td>
                                </tr>
                                <tr>
                                    <td>夏普比率</td>
                                    <td>1.45</td>
                                    <td>1.12</td>
                                    <td class="positive">+0.33</td>
                                </tr>
                                <tr>
                                    <td>最大回撤</td>
                                    <td>-12.4%</td>
                                    <td>-15.7%</td>
                                    <td class="positive">+3.3%</td>
                                </tr>
                                <tr>
                                    <td>波动率</td>
                                    <td>14.2%</td>
                                    <td>16.8%</td>
                                    <td class="positive">-2.6%</td>
                                </tr>
                                <tr>
                                    <td>Alpha</td>
                                    <td>4.2%</td>
                                    <td>-</td>
                                    <td class="positive">+4.2%</td>
                                </tr>
                                <tr>
                                    <td>Beta</td>
                                    <td>0.89</td>
                                    <td>1.0</td>
                                    <td class="positive">-0.11</td>
                                </tr>
                                <tr>
                                    <td>胜率</td>
                                    <td>62.3%</td>
                                    <td>55.1%</td>
                                    <td class="positive">+7.2%</td>
                                </tr>
                                <tr>
                                    <td>平均盈亏比</td>
                                    <td>1.8</td>
                                    <td>1.5</td>
                                    <td class="positive">+0.3</td>
                                </tr>
                                <tr>
                                    <td>交易次数</td>
                                    <td>156</td>
                                    <td>128</td>
                                    <td class="positive">+28</td>
                                </tr>
                                <tr>
                                    <td>持仓天数</td>
                                    <td>5.2</td>
                                    <td>4.8</td>
                                    <td class="neutral">+0.4</td>
                                </tr>
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
const selectedSymbol = ref('')
const zoomLevel = ref(100)
const tableScrollPosition = ref(0)
const symbolPrices = ref<any[]>([])
const buySignals = ref<any[]>([])
const sellSignals = ref<any[]>([])

watch(symbolPrices, (newPrices) => {
    if (newPrices.length > 0 && priceChart.value) {
        updatePriceChart();
    }
}, { deep: true });

onMounted(() => {
    console.info('mount report view')
    echarts.registerTheme('dark', darkTheme);
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

function generateSecondData() {
    const data = [];
    const signals = [];
    const buySignals = [];
    const sellSignals = [];
    
    // 生成一天内的秒级数据 (9:30 AM - 4:00 PM, 6.5小时 = 23400秒)
    const startPrice = 100;
    let currentPrice = startPrice;
    
    for (let i = 0; i < 23400; i++) {
        const timeInSeconds = 9.5 * 3600 + i; // 从9:30开始
        const hours = Math.floor(timeInSeconds / 3600);
        const minutes = Math.floor((timeInSeconds % 3600) / 60);
        const seconds = timeInSeconds % 60;
        
        const timestamp = `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
        
        // 随机价格波动
        const change = (Math.random() - 0.5) * 0.1;
        currentPrice += change;
        currentPrice = Math.max(99, Math.min(101, currentPrice)); // 保持在99-101之间
        
        data.push([timestamp, currentPrice]);
        
        // 随机生成交易信号
        if (Math.random() < 0.0005) { // 大约每2000秒一个信号
            if (Math.random() > 0.5) {
                buySignals.push([timestamp, currentPrice]);
            } else {
                sellSignals.push([timestamp, currentPrice]);
            }
        }
    }
    
    return { data, buySignals, sellSignals };
}

function updatePriceChart() {
    if (!priceChart.value) return;
    
    const isSecond = false;
    let chartData, chartBuySignals, chartSellSignals;
    if (isSecond) {
        const secondData = generateSecondData();
        chartData = secondData.data;
        chartBuySignals = secondData.buySignals;
        chartSellSignals = secondData.sellSignals;
    } else {
        // 使用现有的日级数据
        chartData = symbolPrices.value;
        chartBuySignals = buySignals.value;
        chartSellSignals = sellSignals.value;
    }
    const option = getPriceOption(isSecond, chartData, chartSellSignals, chartBuySignals);
    
    priceChart.value.setOption(option, true);
}

async function updatePrice(symbol: string, startDate: string, endDate: string) {
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    const url = 'https://' + server + '/v0/stocks/history'
    const agent = new https.Agent({  
        rejectUnauthorized: false // 忽略证书错误
    });
    let params = {
        id: symbol,
        type: '1d',
        start: Date.parse('2010-01-01')/1000,
        end: Date.parse('2025-01-01')/1000,
        right: 1    // 默认使用后复权
    }
    const response = await axios.get(url, {
        params: params,
        httpsAgent: agent,
        responseType: 'application/json',
        headers: { 'Authorization': token}})
    
    console.info('get price response:', response)
    if (response.status != 200)
        return;

    symbolPrices.value = [];
    buySignals.value = [];
    sellSignals.value = [];
    const data = JSON.parse(response.data)
    for (const oclhv of data) {
        const dt = oclhv['datetime']
        const date = new Date(dt * 1000)
        const Y = date.getFullYear() + '-';
        const M = (date.getMonth()+1 < 10 ? '0'+(date.getMonth()+1) : date.getMonth()+1) + '-';
        const D = date.getDate() ;
        symbolPrices.value.push([Y + M + D, oclhv['close']])
    }
    if (priceChart.value) {
        updatePriceChart();
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
                option = getPriceOption(true, symbolPrices.value, sellSignals.value, buySignals.value)
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
        } else {
            console.warn(`容器 ${config.id} 不可见，延迟初始化`)
            setTimeout(() => {
                if (document.getElementById(config.id)?.offsetWidth > 0) {
                    initializeCharts()
                }
            }, 300)
        }
    })
}

function getPriceOption(isSecond: boolean, chartData: any[], sellSignals: any[], buySignals: any[]) {
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
function getChartOption(type: string) {
    switch (type) {
        case 'strategy':
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
                    data: ['策略收益', '基准收益'],
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
                    data: ['1月', '2月', '3月', '4月', '5月', '6月', '7月', '8月', '9月', '10月', '11月', '12月'],
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
                        data: [2.3, 4.2, 3.7, 7.2, 9.5, 11.2, 9.8, 13.1, 15.3, 18.2, 21.8, 25.0],
                        lineStyle: {
                            width: 3
                        },
                        itemStyle: {
                            color: '#2962ff'
                        },
                        smooth: true
                    },
                    {
                        name: '基准收益',
                        type: 'line',
                        data: [1.5, 2.8, 2.5, 4.1, 5.3, 6.2, 5.1, 6.8, 7.9, 9.1, 10.5, 12.0],
                        lineStyle: {
                            width: 2,
                            type: 'dashed'
                        },
                        itemStyle: {
                            color: '#a0aec0'
                        },
                        smooth: true
                    }
                ]
            }
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

defineExpose({
    updatePrice,
    updatePriceChart,
    resetTableZoom
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
    }
    
    .chart-controls select {
        min-width: 80px;
        font-size: 12px;
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