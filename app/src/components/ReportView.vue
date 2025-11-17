<template>
    <div class="grid-container">
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
        
        <!-- Price Trend and Trading Signals - 独占一行 -->
        <div class="chart-row">
            <div class="chart-card full-width">
                <div class="chart-title">
                    <div class="title-icon">💹</div>
                    <span>Price Trend & Trading Signals</span>
                </div>
                <div class="chart-container" id="priceTrend"></div>
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
                        </tbody>
                    </table>
                </div>
            </div>
        </div>
    </div>
</template>

<script lang="ts" setup>
import * as echarts from 'echarts'
import { onMounted, nextTick } from 'vue'

// 暗色主题配置保持不变
const darkTheme = {
  // ... 保持原有的暗色主题配置
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
  // ... 其他配置保持不变
};

onMounted(() => {
    // 注册主题
    echarts.registerTheme('dark', darkTheme);

    nextTick(() => {
        // 初始化所有图表
        initializeCharts();
        
        // 窗口大小改变时重新调整图表大小
        window.addEventListener('resize', function() {
            // 重新初始化图表以适应新尺寸
            initializeCharts();
        });
    });
});

function initializeCharts() {
    // Strategy Performance Chart
    const strategyChart = echarts.init(document.getElementById('strategyPerformance'), 'dark');
    strategyChart.setOption({
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
    });

    // Price Trend and Trading Signals
    const priceChart = echarts.init(document.getElementById('priceTrend'), 'dark');
    priceChart.setOption({
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
            data: ['价格', '买入信号', '卖出信号'],
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
        series: [
            {
                name: '价格',
                type: 'line',
                data: [100, 105, 103, 110, 115, 112, 108, 116, 120, 125, 130, 135],
                lineStyle: {
                    width: 3
                },
                itemStyle: {
                    color: '#2962ff'
                },
                smooth: true
            },
            {
                name: '买入信号',
                type: 'scatter',
                data: [null, 105, null, null, null, 112, null, null, 120, null, null, null],
                symbol: 'triangle',
                symbolSize: 16,
                itemStyle: {
                    color: '#00c853'
                }
            },
            {
                name: '卖出信号',
                type: 'scatter',
                data: [null, null, 103, null, 115, null, 108, null, null, null, 130, null],
                symbol: 'triangle',
                symbolSize: 16,
                symbolRotate: 180,
                itemStyle: {
                    color: '#ff6d00'
                }
            }
        ]
    });

    // 其他图表初始化代码...
    // Position Changes
    const positionChart = echarts.init(document.getElementById('positionChanges'), 'dark');
    positionChart.setOption({
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
                name: '仓位',
                type: 'line',
                data: [80, 85, 75, 90, 95, 85, 70, 80, 90, 85, 95, 100],
                lineStyle: {
                    width: 3
                },
                areaStyle: {
                    color: 'rgba(41, 98, 255, 0.3)'
                },
                itemStyle: {
                    color: '#2962ff'
                },
                smooth: true
            }
        ]
    });

    // 其他图表初始化代码...
    // 注意：为了简洁，这里只展示了部分图表的初始化代码
    // 实际应用中需要初始化所有图表
}
</script>

<style scoped>
.grid-container {
    display: flex;
    flex-direction: column;
    gap: 20px;
    width: 100%;
    height: 100%;
    padding: 0;
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
    height: 100%;
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

.chart-container {
    height: 300px;
    width: 100%;
    flex: 1;
}

.chart-card.full-width .chart-container {
    height: 400px;
}

.table-container {
    overflow-x: auto;
    border-radius: 8px;
    border: 1px solid var(--border);
}

table {
    width: 100%;
    border-collapse: collapse;
    margin: 0;
}

th, td {
    padding: 14px 16px;
    text-align: left;
    border-bottom: 1px solid var(--border);
}

th {
    background-color: rgba(42, 52, 77, 0.5);
    font-weight: 600;
    color: var(--text);
    font-size: 14px;
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
}
</style>