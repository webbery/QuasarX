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
                <div class="metric-value">¥{{ accountStore.availableFunds.toLocaleString() }}元</div>
            </div>
            <div class="metric-card card">
                <div class="metric-title">冻结资金</div>
                <div class="metric-value">¥{{ accountStore.frozenFunds.toLocaleString() }}元</div>
            </div>
            <div class="metric-card card">
                <div class="metric-title">账户净值</div>
                <div class="metric-value">¥{{ accountStore.totalValue }}元</div>
                <div class="positive">+{{accountStore.increasePencent}}% <i class="fas fa-arrow-up"></i></div>
            </div>

            <div class="metric-card card">
            <div class="metric-title">今日盈亏</div>
            <div class="metric-value positive">¥{{ accountStore.todayPL }}元</div>
            <div>+2.04%</div>
            </div>

            <div class="metric-card card">
            <div class="metric-title">最大回撤</div>
            <div class="metric-value negative">-{{accountStore. maxDrawDown }}%</div>
            <div>风险等级: 中</div>
            </div>

            <div class="metric-card card">
            <div class="metric-title">夏普比率</div>
            <div class="metric-value">{{ accountStore.sharpRatio }}</div>
            </div>

            <div class="metric-card card">
            <div class="metric-title">卡玛比率</div>
            <div class="metric-value">{{ accountStore.kamaRatio }}</div>
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
import { useAccountStore } from '@/stores/account'

const accountStore = useAccountStore()

// 获取账户资金信息
onMounted(()=> {
    accountStore.fetchAccountData()
})

onUnmounted(() => {
//   window.removeEventListener('loginSuccess', fetchAccountData)
})
</script>