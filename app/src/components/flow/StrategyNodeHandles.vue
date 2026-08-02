<!-- src/components/flow/StrategyNodeHandles.vue -->
<!-- 节点连接点组件 -->

<template>
  <div class="node-connection-row">
    <!-- 普通节点 / 单输入指标：单一输入连接点 -->
    <Handle
      v-if="(nodeType !== 'input' && nodeType !== 'function' && nodeType !== 'breakout') || (nodeType === 'function' && functionSlots.length <= 1)"
      type="target"
      :position="Position.Left"
      id="input"
      class="connection-handle left-handle input-handle"
    />

    <!-- HMM 特殊处理：多个独立输出 handles -->
    <template v-if="nodeType === 'hmm'">
      <div class="hmm-outputs">
        <Handle
          type="source"
          :position="Position.Right"
          id="hmm_state"
          class="connection-handle right-handle output-handle hmm-handle"
        />
        <span class="hmm-handle-label">状态</span>
      </div>
      <div class="hmm-outputs">
        <Handle
          type="source"
          :position="Position.Right"
          id="hmm_probs"
          class="connection-handle right-handle output-handle hmm-handle"
        />
        <span class="hmm-handle-label">概率</span>
      </div>
      <div class="hmm-outputs">
        <Handle
          type="source"
          :position="Position.Right"
          id="hmm_transition"
          class="connection-handle right-handle output-handle hmm-handle"
        />
        <span class="hmm-handle-label">转移</span>
      </div>
      <div class="hmm-outputs">
        <Handle
          type="source"
          :position="Position.Right"
          id="hmm_duration"
          class="connection-handle right-handle output-handle hmm-handle"
        />
        <span class="hmm-handle-label">持续</span>
      </div>
    </template>

    <!-- EMD 节点：无额外 handles，IMF 和衍生特征的 handles 在参数面板中渲染 -->

    <!-- 其他节点：单一输出连接点 -->
    <Handle
      v-if="shouldShowOutput && nodeType !== 'hmm' && nodeType !== 'emd'"
      type="source"
      :position="Position.Right"
      id="output"
      class="connection-handle right-handle output-handle"
    />
  </div>
</template>

<script setup lang="ts">
import { Handle, Position } from '@vue-flow/core'
import { computed } from 'vue'
import { functionInputSlots } from '@/lib/nodes/configs/function'

const props = defineProps<{
  nodeType: string
  params?: Record<string, any>
}>()

const shouldShowOutput = computed(() => {
  return !['output', 'input', 'execution', 'emd', 'hmm'].includes(props.nodeType)
})

const emdImfCount = computed(() => {
  if (props.nodeType !== 'emd') return 0
  const numParam = props.params?.['IMF 数量'] || props.params?.['numIMFs']
  return numParam?.value || 5
})

const functionSlots = computed(() => {
  if (props.nodeType !== 'function' || !props.params) return []
  // params key 是中文 label（如 "方法"），不是英文 schema.key（如 "method"）
  const params = props.params
  const method = params['方法']?.value || params.method?.value
    || Object.values(params).find((c: any) => c?.type === 'select' && Array.isArray(c.options))?.value
    || 'MA'
  return functionInputSlots[method] || []
})
</script>

<style scoped>
.named-input-row {
  display: flex;
  align-items: center;
  gap: 6px;
  margin: 3px 0;
  position: relative;
}
.named-input-label {
  font-size: 11px;
  color: var(--text-secondary, #a0aec0);
  user-select: none;
}
.named-input-field {
  font-size: 10px;
  color: var(--text-tertiary, #718096);
  font-family: 'SF Mono', 'Consolas', monospace;
  user-select: none;
}
.named-input-handle {
  position: absolute;
  left: -12px;
}

.hmm-outputs, .emd-output {
  display: flex;
  align-items: center;
  gap: 4px;
  margin: 2px 0;
}
.hmm-handle-label, .emd-handle-label {
  font-size: 10px;
  color: var(--text-secondary, #a0aec0);
  user-select: none;
}
</style>
