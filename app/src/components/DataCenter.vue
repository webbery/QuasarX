<template>
    <div class="data-center-root">
        <div class="tab-bar">
            <button
                class="tab-btn"
                :class="{ active: activeTab === 'quote' }"
                @click="activeTab = 'quote'"
            >
                <i class="fas fa-chart-line"></i> 行情数据
            </button>
            <button
                class="tab-btn"
                :class="{ active: activeTab === 'finance' }"
                @click="activeTab = 'finance'"
            >
                <i class="fas fa-file-invoice-dollar"></i> 基本面数据
            </button>
            <button
                class="tab-btn"
                :class="{ active: activeTab === 'dividend' }"
                @click="activeTab = 'dividend'"
            >
                <i class="fas fa-coins"></i> 分红除权
            </button>
            <button
                class="tab-btn"
                :class="{ active: activeTab === 'option' }"
                @click="activeTab = 'option'"
            >
                <i class="fas fa-stream"></i> 期权数据
            </button>
            <button
                class="tab-btn"
                :class="{ active: activeTab === 'macro' }"
                @click="activeTab = 'macro'"
            >
                <i class="fas fa-globe"></i> 宏观数据
            </button>
        </div>

        <!-- 各 Tab 独立面板组件 -->
        <QuotePanel v-show="activeTab === 'quote'" />
        <FinancePanel v-show="activeTab === 'finance'" />
        <DividendPanel v-show="activeTab === 'dividend'" :active="activeTab === 'dividend'" />
        <OptionPanel v-show="activeTab === 'option'" />
        <MacroPanel v-show="activeTab === 'macro'" @macro-data-click="(payload: any) => emit('macro-data-click', payload)" />
    </div>
</template>

<script setup lang="ts">
import { ref, watch, onMounted, onActivated, onDeactivated } from 'vue'
import { useHistoryStore } from '@/stores/history'
import { useRemoteStrategies } from './datacenter/composables/useRemoteStrategies'
import QuotePanel from './datacenter/QuotePanel.vue'
import FinancePanel from './datacenter/FinancePanel.vue'
import DividendPanel from './datacenter/DividendPanel.vue'
import OptionPanel from './datacenter/OptionPanel.vue'
import MacroPanel from './datacenter/MacroPanel.vue'

const emit = defineEmits(['load-version', 'strategy-click', 'macro-data-click'])

// Tab 状态
const activeTab = ref<'quote' | 'finance' | 'dividend' | 'option' | 'macro'>('quote')

// 远程 service 策略清单：各面板共用同一份（模块级状态）
const historyStore = useHistoryStore()
const { fetchRemoteStrategies } = useRemoteStrategies()

onMounted(() => {
    // 首次挂载拉一次远程策略清单（不含 pools，按需再补）
    fetchRemoteStrategies(false)
})

// 切换 service（重新登录后 localStorage.remote 变化）→ 重拉
watch(() => localStorage.getItem('remote'), () => {
    fetchRemoteStrategies(false)
})

// 本地保存策略后 → service 端可能也部署了同名策略，重拉以保持一致
watch(() => historyStore.strategies.length, () => {
    fetchRemoteStrategies(false)
})

// KeepAlive 缓存期间不需要重复注册
onActivated(() => {
    // 组件被 KeepAlive 缓存后重新显示
    console.log('[DataCenter] onActivated')
})

onDeactivated(() => {
    // 组件被 KeepAlive 缓存后隐藏
    console.log('[DataCenter] onDeactivated')
})
</script>

<style scoped>
@import './datacenter/datacenter.scss';
</style>
