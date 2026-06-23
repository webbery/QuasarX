<!-- src/components/flow/params/ButtonParam.vue -->
<!-- 按钮参数组件 -->

<template>
  <div class="button-param-wrapper">
    <button
      class="param-button"
      :class="buttonClass"
      @click="handleClick"
    >
      <i v-if="iconClass" :class="iconClass"></i>
      {{ buttonText }}
    </button>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'

const props = defineProps<{
  paramKey: string
  paramConfig: Record<string, any>
}>()

const emit = defineEmits(['click'])

const buttonText = computed(() => {
  const label = props.paramConfig.label || '按钮'
  return label
})

const iconClass = computed(() => {
  const key = props.paramKey
  if (key === 'visualize') return 'fas fa-chart-line'
  return ''
})

const buttonClass = computed(() => {
  const key = props.paramKey
  if (key === 'visualize') return 'btn-visualize'
  return 'btn-default'
})

const handleClick = () => {
  emit('click')
}
</script>

<style scoped>
.button-param-wrapper {
  width: 100%;
}

.param-button {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  width: 100%;
  padding: 6px 14px;
  border: none;
  border-radius: 6px;
  font-size: 0.8rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s ease;
  color: white;
}

.param-button i {
  font-size: 0.85rem;
}

.btn-visualize {
  background: linear-gradient(135deg, #10b981, #059669);
}

.btn-visualize:hover {
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(16, 185, 129, 0.3);
}

.btn-visualize:active {
  transform: translateY(0);
}

.btn-default {
  background: linear-gradient(135deg, var(--primary), #1d4ed8);
}

.btn-default:hover {
  transform: translateY(-1px);
  box-shadow: 0 4px 12px rgba(41, 98, 255, 0.3);
}
</style>
