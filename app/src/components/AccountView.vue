<template>
    <div>
<!-- 账户总览 -->
    <div class="card">
        <div class="section-title">
            <h2><i class="fas fa-chart-line"></i> 账户总览</h2>
            <div>
            <select class="control-btn">
                <option>今日</option>
                <option>本周</option>
                <option selected>本月</option>
                <option>本季度</option>
                <option>本年</option>
            </select>
            </div>
        </div>

        <div class="card-grid">
            <div class="metric-card card">
                <div class="metric-title">可用资金</div>
                <div class="metric-value">¥{{ avialableFunds }}元</div>
            </div>
            <div class="metric-card card">
                <div class="metric-title">冻结资金</div>
                <div class="metric-value">¥{{ fronzonFunds }}元</div>
            </div>
            <div class="metric-card card">
                <div class="metric-title">账户净值</div>
                <div class="metric-value">¥{{ totalValue }}元</div>
                <div class="positive">+{{increasePencent}}% <i class="fas fa-arrow-up"></i></div>
            </div>

            <div class="metric-card card">
            <div class="metric-title">今日盈亏</div>
            <div class="metric-value positive">¥{{ todayPL }}元</div>
            <div>+2.04%</div>
            </div>

            <div class="metric-card card">
            <div class="metric-title">最大回撤</div>
            <div class="metric-value negative">-{{ maxDrawDown }}%</div>
            <div>风险等级: 中</div>
            </div>

            <div class="metric-card card">
            <div class="metric-title">夏普比率</div>
            <div class="metric-value">{{ sharpRatio }}</div>
            </div>

            <div class="metric-card card">
            <div class="metric-title">卡玛比率</div>
            <div class="metric-value">{{ kamaRatio }}</div>
            </div>
        </div>
    </div>
    <div class="card">
        <div class="section-title">
            <h2><i class="fas fa-chart-area"></i> 净值曲线</h2>
            <div>
                <select class="control-btn">
                    <option>1个月</option>
                    <option selected>3个月</option>
                    <option>6个月</option>
                    <option>1年</option>
                    <option>全部</option>
                </select>
            </div>
        </div>
        <div class="chart-container" id="overviewChart"></div>
    </div>
    </div>
</template>
<script setup>
import {ref, onMounted, onUnmounted } from 'vue'
import axios from 'axios';

let totalValue = ref(0)
let increasePencent = ref(0)
let todayPL = ref(0)
let sharpRatio = ref(0)
let kamaRatio = ref(0)
let maxDrawDown = ref(0)
let avialableFunds = ref(0)
let fronzonFunds = ref(0)

// 获取账户资金信息
const fetchAccountData = async () => {
  try {
    const response = await axios.get('/v0/user/funds')
    const data = response.data
    
    // 根据实际API返回的数据结构进行赋值
    // 这里需要根据你的实际API响应结构调整
    avialableFunds.value = data.funds.toLocaleString() || 0
    fronzonFunds.value = data.frozenFunds || 0
    totalValue.value = data.totalValue || 0
    increasePencent.value = data.increasePercent || 0
    todayPL.value = data.todayProfitLoss || 0
    sharpRatio.value = data.sharpRatio || 0
    kamaRatio.value = data.kamaRatio || 0
    maxDrawDown.value = data.maxDrawDown || 0
    
    console.log('账户数据获取成功:', data)
  } catch (error) {
    console.error('获取账户数据失败:', error)
    // 可以在这里添加错误处理，比如显示提示信息
  }
}

onMounted(()=> {
    // 监听登录成功事件
    window.addEventListener('loginSuccess', fetchAccountData)
})

onUnmounted(() => {
  window.removeEventListener('loginSuccess', fetchAccountData)
})
</script>