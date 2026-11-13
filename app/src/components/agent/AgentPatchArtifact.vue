<template>
  <div class="agent-patch">
    <!-- 风险提示 -->
    <div v-if="payload.riskNote" class="patch-warn">
      <i class="fas fa-exclamation-triangle"></i>
      <span>{{ payload.riskNote }}</span>
    </div>

    <!-- 校验失败 -->
    <div v-if="!payload.validation.valid" class="patch-invalid">
      <div class="invalid-title">结构校验未通过，提案不可应用</div>
      <ul>
        <li v-for="(err, i) in payload.validation.errors" :key="i">{{ err }}</li>
      </ul>
    </div>

    <!-- 修改明细 -->
    <div v-if="payload.ops.length" class="patch-ops">
      <div
        v-for="(op, i) in payload.ops"
        :key="i"
        class="patch-op"
        :class="{ 'is-done': applied }"
      >
        <span class="op-badge" :class="'op-' + op.op">{{ opSymbol(op.op) }}</span>
        <span class="op-label">{{ op.label }}</span>

        <template v-if="op.op === 'update'">
          <span class="op-before">{{ formatValue(op.before) }}</span>
          <span class="op-arrow">→</span>
          <span class="op-after">{{ formatValue(op.after) }}</span>
        </template>
        <template v-else-if="op.op === 'add'">
          <span class="op-after">新增</span>
        </template>
        <template v-else-if="op.op === 'remove'">
          <span class="op-before">移除</span>
        </template>

        <span class="op-node">{{ op.path === 'node' ? op.nodeId : `${op.nodeId} · ${op.path}` }}</span>
      </div>
    </div>

    <div v-else class="patch-empty">与当前版本无实质差异</div>

    <!-- 策略规模 -->
    <div v-if="payload.graph" class="patch-meta">
      应用后：{{ payload.graph.nodes.length }} 个节点 / {{ payload.graph.edges.length }} 条连线
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed } from 'vue'
import type { PatchOpKind, PatchPayload } from '@/lib/agent/artifacts'

const props = defineProps<{ payload: PatchPayload; applied?: boolean }>()

const applied = computed(() => props.applied === true)

function opSymbol(op: PatchOpKind): string {
  switch (op) {
    case 'add':
      return '+'
    case 'remove':
      return '−'
    case 'move':
      return '↕'
    default:
      return '±'
  }
}

function formatValue(value: unknown): string {
  if (value === undefined || value === null) return '—'
  if (typeof value === 'number') return Number.isInteger(value) ? String(value) : value.toFixed(4)
  if (typeof value === 'string') return value
  if (typeof value === 'boolean') return value ? 'true' : 'false'
  try {
    const s = JSON.stringify(value)
    return s.length > 40 ? `${s.slice(0, 40)}…` : s
  } catch {
    return String(value)
  }
}
</script>

<style scoped>
.agent-patch {
  padding: 4px 0;
}

.patch-warn {
  display: flex;
  gap: 9px;
  align-items: flex-start;
  margin: 8px 13px;
  padding: 9px 12px;
  border-radius: 9px;
  font-size: 12.5px;
  line-height: 1.6;
  background: rgba(255, 152, 0, 0.1);
  border: 1px solid rgba(255, 152, 0, 0.32);
  color: #e0e0e0;
}

.patch-warn i {
  color: #ff9800;
  margin-top: 3px;
}

.patch-invalid {
  margin: 8px 13px;
  padding: 9px 12px;
  border-radius: 9px;
  background: rgba(239, 35, 42, 0.1);
  border: 1px solid rgba(239, 35, 42, 0.35);
  font-size: 12px;
}

.invalid-title {
  color: #ef5350;
  margin-bottom: 4px;
}

.patch-invalid ul {
  margin: 0;
  padding-left: 18px;
  color: #a0aec0;
}

.patch-op {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 9px 13px;
  border-bottom: 1px solid rgba(74, 85, 104, 0.16);
  font-size: 12px;
}

.patch-op:last-child {
  border-bottom: none;
}

.patch-op.is-done {
  opacity: 0.55;
}

.op-badge {
  width: 18px;
  height: 18px;
  border-radius: 4px;
  flex: 0 0 18px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  font-size: 11px;
  font-weight: 600;
}

.op-badge.op-update {
  background: rgba(255, 152, 0, 0.16);
  color: #ff9800;
}

.op-badge.op-add {
  background: rgba(0, 200, 83, 0.16);
  color: #00c853;
}

.op-badge.op-remove {
  background: rgba(239, 35, 42, 0.16);
  color: #ef5350;
}

.op-badge.op-move {
  background: rgba(41, 98, 255, 0.16);
  color: #2962ff;
}

.op-label {
  color: #a0aec0;
  min-width: 128px;
}

.op-before {
  color: #ef5350;
  font-family: "SF Mono", Menlo, Consolas, monospace;
  text-decoration: line-through;
}

.op-after {
  color: #00c853;
  font-family: "SF Mono", Menlo, Consolas, monospace;
}

.op-arrow {
  color: #6f7b8a;
}

.op-node {
  margin-left: auto;
  font-family: "SF Mono", Menlo, Consolas, monospace;
  font-size: 11px;
  color: #6f7b8a;
}

.patch-empty {
  padding: 12px 13px;
  font-size: 12px;
  color: #6f7b8a;
}

.patch-meta {
  padding: 8px 13px 10px;
  font-size: 11px;
  color: #6f7b8a;
}
</style>
