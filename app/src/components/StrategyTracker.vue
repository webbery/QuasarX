<template>
    <div class="strategy-tracker">
        <!-- 顶部策略选择器 -->
        <div class="strategy-selector">
            <label>追踪策略：</label>
            <select v-model="selectedStrategy">
                <option value="">-- 选择策略 --</option>
                <option v-for="s in strategyList" :key="s.name" :value="s.name">
                    {{ s.name }}
                    <span v-if="s.running" style="color: #4ade80;">● 运行中</span>
                    <span v-else style="color: #f87171;">○ 已停止</span>
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
            <!-- 未选择策略时的提示 -->
            <div v-if="!selectedStrategy" class="empty-state">
                <i class="fas fa-chart-line"></i>
                <p>请选择策略</p>
                <span>选择策略后可查看完整复盘分析</span>
            </div>

            <!-- 复盘面板 -->
            <ReviewPanel
                v-else
                ref="reviewPanelRef"
                :strategy-id="selectedStrategy"
            />
        </div>
    </div>
</template>

<script setup>
import { ref, onMounted, watch, nextTick } from 'vue'
import axios from 'axios'
import * as echarts from 'echarts'
import PriceTrendChart from './report/charts/PriceTrendChart.vue'
import ReviewPanel from './review/ReviewPanel.vue'

const activeTab = ref('realtime')
const selectedStrategy = ref('')
const strategyList = ref([])

// ReviewPanel 引用
const reviewPanelRef = ref(null)

onMounted(async () => {
    try {
        const res = await axios.get('/v0/strategy')
        const data = Array.isArray(res.data) ? res.data : (res.data.strategies || [])
        strategyList.value = data
    } catch (e) {
        console.warn('获取策略列表失败', e)
        strategyList.value = []
    }
})

// 监听策略选择和 Tab 切换，自动加载复盘数据
watch([selectedStrategy, activeTab], ([newStrategy, newTab]) => {
    if (newStrategy && newTab === 'review' && reviewPanelRef.value) {
        reviewPanelRef.value.loadData()
    }
})
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
</style>
