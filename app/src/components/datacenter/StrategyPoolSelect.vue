<script setup lang="ts">
// 策略标的池选择器
//
// 把「选策略 → 提取标的池」这段逻辑收敛到一处，供数据中心各 Tab 复用：
//   - 行情数据 / 基本面数据 / 分红除权 三个 Tab 的策略下拉行为完全一致
//   - 本地策略：从 IndexedDB 最新版本 flowData 里提取 QuoteInputNode / PortfolioNode 的标的
//   - 远程策略：直接用 service GET /v0/strategy?include=pools 返回的 symbols
//
// 组件只负责「选策略 → 产出标的池」，提交下载、进度展示由各 Tab 自己负责。

import { computed, ref, watch } from 'vue'
import StrategySelect from '../StrategySelect.vue'
import { useHistoryStore } from '@/stores/history'
import { extractSecuritiesFromFlowData } from '@/lib/strategyPool'
import { toSuffixedSymbol } from '@/lib/symbol'

interface RemoteStrategy {
    name: string
    running: boolean
    failed: boolean
    symbols: string[] | null
}

const props = withDefaults(defineProps<{
    /** 选中的策略名 */
    modelValue: string
    /** service 返回的远程策略列表 */
    remoteStrategies: RemoteStrategy[]
    /** 是否至少拉过一次远程策略（避免下拉空白闪烁） */
    remoteFetched: boolean
    /** 远程策略的标的池按需拉取：返回该策略的 symbols，取不到返回 null */
    loadRemotePools?: (name: string) => Promise<string[] | null>
    /** 标的过滤：返回 false 的标的不进入下载列表（如基本面只认 A 股） */
    filter?: (symbol: string) => boolean
    /** 池子摘要的单位文案，如「个 A 股」 */
    unitLabel?: string
    /** 被过滤掉的标的的提示前缀 */
    skippedLabel?: string
    /** 下拉框空选项文案 */
    emptyLabel?: string
    selectClass?: string
}>(), {
    unitLabel: '个',
    skippedLabel: '已跳过',
    emptyLabel: '选择策略…',
    selectClass: 'strategy-select',
})

const emit = defineEmits<{
    'update:modelValue': [value: string]
    /** 过滤后的标的池（逗号分隔，后缀格式） */
    'update:symbols': [value: string]
    /** 需要提示用户的情况（如远程策略初始化失败） */
    'notice': [message: string]
}>()

const historyStore = useHistoryStore()

const symbols = ref('')
const skipped = ref(0)

const count = computed(() =>
    symbols.value ? symbols.value.split(',').filter(s => s.trim()).length : 0
)

// 选策略 → 提取标的池
watch(() => props.modelValue, async (name) => {
    symbols.value = ''
    skipped.value = 0
    emit('update:symbols', '')
    if (!name) return

    const all: string[] = []

    // 1) 本地策略优先：从 IndexedDB 最新版本 flowData 提取标的池
    const local = historyStore.strategies.find(s => s.name === name)
    if (local) {
        const versions = historyStore.getVersionsByStrategy(local.id)
        if (versions.length > 0) {
            const sorted = [...versions].sort((a, b) =>
                new Date(b.saveTime).getTime() - new Date(a.saveTime).getTime())
            const flowData = await historyStore.loadVersionFlowData(sorted[0].id)
            if (flowData) {
                const securities = extractSecuritiesFromFlowData(flowData as any)
                securities.forEach(s => all.push(toSuffixedSymbol(s.code)))
            }
        }
    } else {
        // 2) 远程策略：直接用 service 返回的 symbols 数组
        const remote = props.remoteStrategies.find(s => s.name === name)
        if (remote) {
            if (remote.failed) {
                emit('notice', `远程策略「${name}」初始化失败，无可用标的池`)
                return
            }
            let pool = remote.symbols
            if (pool === null && props.loadRemotePools) {
                pool = await props.loadRemotePools(name)
            }
            ;(pool ?? []).forEach(s => all.push(toSuffixedSymbol(s)))
        }
    }

    // 3) 去重 → 按需过滤
    const unique = Array.from(new Set(all.map(s => s.trim()).filter(Boolean)))
    const kept = props.filter ? unique.filter(props.filter) : unique
    symbols.value = kept.join(',')
    skipped.value = unique.length - kept.length
    emit('update:symbols', symbols.value)
})
</script>

<template>
    <div class="strategy-pool-select">
        <div class="input-group">
            <label>策略</label>
            <StrategySelect
                :model-value="modelValue"
                :local-strategies="historyStore.strategies"
                :remote-strategies="remoteStrategies"
                :remote-fetched="remoteFetched"
                :empty-label="emptyLabel"
                :select-class="selectClass"
                @update:model-value="emit('update:modelValue', $event)"
            />
        </div>
        <div v-if="modelValue" class="pool-summary">
            标的池: <strong>{{ count }}</strong> {{ unitLabel }}
            <span v-if="skipped > 0" class="hint">（{{ skippedLabel }} {{ skipped }} 个）</span>
        </div>
    </div>
</template>

<style scoped>
@import './datacenter.scss';

/* 根节点不产生盒子，让内部的输入组与池摘要直接参与父级（.download-card）的排版 */
.strategy-pool-select {
    display: contents;
}
</style>
