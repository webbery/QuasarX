<template>
  <span class="symbol-code-inline">
    <span v-if="exchange" class="ex-badge" :class="`ex-badge--${exchange}`">{{ label }}</span>
    <span>{{ code || fallback }}</span>
  </span>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import { EXCHANGE_LABEL, splitSymbol } from '@/lib/symbol'

const props = withDefaults(defineProps<{
  symbol?: string
  fallback?: string
}>(), {
  symbol: '',
  fallback: '-'
})

const parts = computed(() => splitSymbol(props.symbol))
const exchange = computed(() => parts.value.exchange)
const code = computed(() => parts.value.code)
const label = computed(() => (exchange.value ? EXCHANGE_LABEL[exchange.value] : ''))
</script>
