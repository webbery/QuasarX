<script setup lang="ts">
import { computed } from 'vue'

interface LocalStrategy {
    id: string
    name: string
}

interface RemoteStrategy {
    name: string
    running: boolean
    failed: boolean
}

const props = withDefaults(defineProps<{
    modelValue: string
    localStrategies: LocalStrategy[]
    remoteStrategies: RemoteStrategy[]
    remoteFetched: boolean
    selectClass?: string
    emptyLabel?: string
}>(), {
    selectClass: 'strategy-select',
    emptyLabel: '选择策略…',
})

const emit = defineEmits<{
    'update:modelValue': [value: string]
}>()

const selectedStrategy = computed({
    get: () => props.modelValue,
    set: (value: string) => emit('update:modelValue', value),
})
</script>

<template>
    <select v-model="selectedStrategy" :class="selectClass">
        <option value="">{{ emptyLabel }}</option>
        <optgroup label="本地缓存">
            <option v-for="strategy in localStrategies" :key="`local:${strategy.id}`" :value="strategy.name">
                {{ strategy.name }}
            </option>
        </optgroup>
        <optgroup v-if="remoteFetched" label="远程运行">
            <option v-for="strategy in remoteStrategies" :key="`remote:${strategy.name}`" :value="strategy.name">
                {{ strategy.name }}{{ strategy.running ? ' ●运行' : '' }}{{ strategy.failed ? ' ✕失败' : '' }}
            </option>
        </optgroup>
    </select>
</template>
