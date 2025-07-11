<template>
<div>
    <div>
        <el-divider><span class="title">风险敞口评估</span></el-divider>
        <el-row>
            <el-col :span="11">
                <div>参数选择</div>
                <div class="block" style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;">
                    <span style="flex-shrink: 0;">测试时间范围:</span><el-date-picker
                        type="datetimerange"
                        start-placeholder="Start date"
                        end-placeholder="End date"
                        format="YYYY-MM-DD HH:mm:ss"
                        date-format="YYYY/MM/DD ddd"
                        time-format="A hh:mm:ss"
                        v-model="timeRange"
                    />
                </div>
                <div class="container" style="display: flex; justify-content: space-between; align-items: center;">
                    <span class="label" style="flex-shrink: 0;">代码池：</span>
                    <el-input-tag
                    placeholder="代码池"
                    class="input-tag"
                    />
                    <button @click="selectSymbol">选择</button>
                </div>
                <div>止损选择</div>
                <el-row :gutter="10"  style="display: flex; justify-content: space-between; align-items: center;">
                    <el-col :span="6">
                        <span>止损类型:</span>
                    </el-col>
                    <el-col :span="8">
                    <el-select placeholder="请选择" @change="onStoplossChanged" v-model="selected_stoploss_name">
                        <el-option
                            v-for="(item, k) in stoplossName"
                            :key="k"
                            :label="item"
                            :value="item"
                        />
                    </el-select>
                    </el-col>
                </el-row>
                <keep-alive>
                    <!-- <component :is="stoplossPanels[risk_stoploss]"></component> -->
                </keep-alive>
            </el-col>
            <el-col :span="6">
                <div>策略选择</div>
                <el-table :data="strategyData">
                    <el-table-column type="selection" width="55" />
                    <el-table-column prop="strategy_type" label="策略类型" width="100" />
                    <el-table-column prop="desc" label="简介" />
                </el-table>
            </el-col>
            <el-col :span="6">
                <div>佣金计算</div>
                <el-row :gutter="10"  style="display: flex; justify-content: space-between; align-items: center;">
                    <el-col :span="6">
                        <span>佣金类型:</span>
                    </el-col>
                    <el-col :span="8">
                    <el-select placeholder="请选择" v-model="selected_commission_type" @change="onCommissionTypeChanged">
                        <el-option
                            v-for="item in commission_types"
                            :key="item.value"
                            :label="item.label"
                            :value="item.value"
                        />
                    </el-select>
                    </el-col>
                </el-row>
                <div style="display: flex; justify-content: space-between; align-items: center;">
                    <span>印花税:</span><el-input-number v-model="tax">
                        <template #prefix>
                            <span>￥</span>
                        </template>
                    </el-input-number>
                </div>
                <div style="display: flex; justify-content: space-between; align-items: center;">
                    <span>最低交易费:</span><el-input-number v-model="min_trade_price">
                        <template #prefix>
                            <span>￥</span>
                        </template>
                    </el-input-number>
                </div>
                <div style="display: flex; justify-content: space-between; align-items: center;">
                    <span>交易费率:</span><el-input-number v-model="commission" :step="0.1">
                        <template #suffix>
                            <span>‰</span>
                        </template>
                    </el-input-number>
                </div>
                <div style="display: flex; justify-content: space-between; align-items: center;">
                    <span>滑点:</span><el-input-number v-model="slip">
                        <template #suffix>
                            <span>％</span>
                        </template>
                    </el-input-number>
                </div>
            </el-col>
        </el-row>
        <el-button @click="evaluateRisk">风险评估</el-button>
        <el-button >压力测试</el-button>
    </div>
    <KLineView v-if="showChart" ref="lineview" :chart-data="chartData" :ops="ops"></KLineView>
    <div>
        <el-descriptions class="margin-top" title="风险评估结果" :column="5" border >
            <el-descriptions-item label="夏普率" label-align="right" align="center">
                {{ sharpRatio }}
            </el-descriptions-item>
            <el-descriptions-item label="胜率" label-align="right" align="center">
                {{ winRate }}
            </el-descriptions-item>
            <el-descriptions-item label="撤回率" label-align="right" align="center">
                {{ ES }}
            </el-descriptions-item>
            <el-descriptions-item label="10日VaR" label-align="right" align="center">
                {{ VaR }}
            </el-descriptions-item>
            <el-descriptions-item label="ES" label-align="right" align="center">
                {{ ES }}
            </el-descriptions-item>
            <el-descriptions-item label="卡玛比率" label-align="right" align="center">
                {{ sharpRatio }}
            </el-descriptions-item>
        </el-descriptions>
    </div>
    <div>
        <el-divider><span class="title">止损设置</span></el-divider>
        <el-collapse >
            <el-collapse-item key="percent" title="百分比止损" name="1">
                <StopLossPanel></StopLossPanel>
            </el-collapse-item>
            <el-collapse-item key="move" title="移动止损" name="2">
                <StopLossPanel></StopLossPanel>
            </el-collapse-item>
            <el-collapse-item key="step" title="阶梯止损" name="3">
                <StopLossPanel></StopLossPanel>
            </el-collapse-item>
            <el-collapse-item key="atr" title="ATR止损" name="4">
                <StopLossPanel></StopLossPanel>
            </el-collapse-item>
            <el-collapse-item key="sar" title="SAR止损" name="5">
                <StopLossPanel></StopLossPanel>
            </el-collapse-item>
        </el-collapse >
    </div>
    <SymbolSelectionPanel :is-open="openSelection" @close="handleSelectionClose"></SymbolSelectionPanel>
</div>
</template>

<script setup>
import { onMounted, ref, defineOptions } from 'vue'
import StopLossPanel from './StopLossPanel.vue'
import { ElTable } from 'element-plus'
import PercentPanel from './stoploss/PercentPanel.vue'
import KLineView from './KLineView.vue'
import SymbolSelectionPanel from './SymbolSelectionPanel.vue'
import { formatDate } from '../tool'
import axios from 'axios'

let collapseIndex = ref(1)
const stoplossName = {
  fix: '固定额度止损',
  percent: '百分比止损',
  move: '移动止损',
  ATR: 'ATR止损',
  SAR: 'SAR止损',
  key: '关键点位止损',
  step: '阶梯止损',
  time: '时间止损',
}
const strategy_symbols = ref(['000001', '513101'])
const commission_types = [
  {
    value: 'AStock',
    label: 'A股',
  },
  {
    value: 'ETF',
    label: 'ETF',
  },
  {
    value: 'future',
    label: '期货',
  },
  {
    value: 'option',
    label: '期权',
  },
]

const tax = ref(5)
const min_trade_price = ref(5)
const commission = ref(0.1354)
const slip = ref(0.5)
const lineview = ref(null)
const chartData = ref(null)
const timeRange = ref([
  new Date(2010, 10, 10, 10, 10),
  new Date(2020, 10, 11, 10, 10),
])
const ops = ref(null)
let sharpRatio = ref(0.0)
let VaR = ref(0.0)
let ES = ref(0.0)
let winRate = ref(0.0)
let showChart = ref(false)

let openSelection = ref(false)
let strategyData = [
    {id: 1, strategy_type: 'Random', desc: '随机选股'}
]

const singleTableRef = ref()

let risk_stoploss = 'percent'
let selected_stoploss_name = stoplossName['percent']
let selected_commission_type = commission_types[0].label

const stoplossPanels = {
    fix: PercentPanel,
    percent: PercentPanel,
}
const onStoplossChanged = (value) => {
    risk_stoploss = value
}
const onCommissionTypeChanged = (value) => {
    selected_commission_type = value
}

const evaluateRisk = async () => {
    let symbol = '000001'
    const data = {
        "capital":1000000,
        "start": formatDate(timeRange.value[0]),
        "end": formatDate(timeRange.value[1]),
        "portfolio": {
            "strategy": "random",
            "pool": [
                symbol,
                "000005"
            ],
            "data_type": "day"
        },
        "confidence": 0.99,
        "days": 10,
        "commission": {
            "type": "A股",
            "slip": slip.value/100,
            "min_price": min_trade_price.value,
            "trade_price": commission.value/1000,
            "tax": 0.01
        }
    }
    const result = await axios.post('/v0/risk/var', data)
    console.info(result)
    VaR = result.data['10VaR']
    ES = result.data.ES
    sharpRatio = result.data.sharp
    ops.value = result.data.ops
    // 运行风险测试结果
    // 异步获取历史数据
    let query = 'id=' + symbol + '&type=2&start=' + timeRange.value[0].getTime()/1000 + '&end=' + timeRange.value[1].getTime()/1000
    const history = await axios.get('/v0/stocks/history?' + query)
    chartData.value = history.data
    showChart.value = true
    // 绘制
    lineview.value?.renderChart()
}

onMounted(() => {
    // const kchart = document.getElementById('riskchart')
    // myChart = echarts.init(kchart);
})

const selectSymbol = () => {
    openSelection.value = true
}

const handleSelectionClose = () => {
    openSelection.value = false
}
</script>

<style scoped>
.title {
    font-weight: bold;
    color: brown;
}
.container {
  display: flex;
  align-items: center; /* 垂直居中对齐 */
  gap: 10px; /* 元素之间的间距 */
}

.label {
  white-space: nowrap; /* 防止文本换行 */
}

.input-tag {
  flex: 1; /* 让输入框占据剩余宽度 */
}

.margin-top {
  margin-top: 20px;
}
.cell-item {
    display: flex;
    align-items: center;
}
.el-descriptions {
  margin-top: 20px;
}
</style>