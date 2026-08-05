<template>
  <div class="order-desk">
    <!-- 左侧：决策列表 -->
    <div class="decision-list-panel">
      <div class="panel-header">
        <h3>策略决策列表</h3>
        <div class="filter-btns">
          <button :class="{ active: filter === 'all' }" @click="filter = 'all'">全部</button>
          <button :class="{ active: filter === 'pending' }" @click="filter = 'pending'">待确认</button>
          <button :class="{ active: filter === 'executed' }" @click="filter = 'executed'">已下单</button>
        </div>
      </div>
      <div class="decision-table-wrap">
        <table class="decision-table" v-if="filteredDecisions.length > 0">
          <thead>
            <tr>
              <th>#</th>
              <th>标的</th>
              <th>操作</th>
              <th>数量</th>
              <th>价格</th>
              <th>状态</th>
            </tr>
          </thead>
          <tbody>
            <tr
              v-for="d in filteredDecisions"
              :key="d.id"
              :class="{ selected: selectedId === d.id, executed: d.executed }"
              @click="selectedId = d.id"
            >
              <td>{{ d.id }}</td>
              <td class="symbol-cell">
                <span class="symbol-code">{{ d.symbol }}</span>
              </td>
              <td>
                <span class="action-badge" :style="{ background: actionColors[d.action] + '22', color: actionColors[d.action] }">
                  {{ d.label }}
                </span>
              </td>
              <td>{{ d.quantity }}</td>
              <td>{{ d.price.toFixed(2) }}</td>
              <td>
                <span v-if="d.executed" class="status-executed">
                  已下单 {{ d.executedQuantity }}@{{ d.executedPrice.toFixed(2) }}
                </span>
                <span v-else class="status-pending">待确认</span>
              </td>
            </tr>
          </tbody>
        </table>
        <div v-else class="empty-state">
          <i class="fas fa-inbox"></i>
          <p>暂无决策</p>
          <span>等待策略产生交易决策</span>
        </div>
      </div>
    </div>

    <!-- 右侧：下单操作面板 -->
    <div class="decision-action-panel">
      <div class="panel-header">
        <h3>下单操作</h3>
      </div>
      <div class="action-content" v-if="selectedDecision">
        <template v-if="!selectedDecision.executed">
          <div class="info-row">
            <label>标的</label>
            <span class="info-value">{{ selectedDecision.symbol }}</span>
          </div>
          <div class="info-row">
            <label>操作</label>
            <span class="info-value" :style="{ color: actionColors[selectedDecision.action] }">
              {{ selectedDecision.label }}
            </span>
          </div>
          <div class="info-row">
            <label>策略</label>
            <span class="info-value">{{ selectedDecision.strategy }}</span>
          </div>
          <div class="form-row">
            <label>数量</label>
            <input type="number" v-model.number="editQuantity" :min="100" step="100" class="form-input" />
            <span class="hint">决策: {{ selectedDecision.quantity }}</span>
          </div>
          <div class="form-row">
            <label>价格</label>
            <input type="number" v-model.number="editPrice" :min="0" step="0.01" class="form-input" />
            <span class="hint">参考价: {{ selectedDecision.price.toFixed(2) }}</span>
          </div>
          <button class="execute-btn" @click="handleExecute" :disabled="executing">
            {{ executing ? '提交中...' : '确认下单' }}
          </button>
        </template>
        <template v-else>
          <div class="executed-detail">
            <div class="info-row">
              <label>标的</label>
              <span class="info-value">{{ selectedDecision.symbol }}</span>
            </div>
            <div class="info-row">
              <label>操作</label>
              <span class="info-value" :style="{ color: actionColors[selectedDecision.action] }">
                {{ selectedDecision.label }}
              </span>
            </div>
            <div class="info-row">
              <label>成交数量</label>
              <span class="info-value highlight">{{ selectedDecision.executedQuantity }}</span>
            </div>
            <div class="info-row">
              <label>成交价格</label>
              <span class="info-value highlight">{{ selectedDecision.executedPrice.toFixed(2) }}</span>
            </div>
            <div class="info-row">
              <label>决策数量</label>
              <span class="info-value muted">{{ selectedDecision.quantity }}</span>
            </div>
          </div>
        </template>
      </div>
      <div class="action-content empty-action" v-else>
        <i class="fas fa-hand-pointer"></i>
        <p>请从左侧选择一条决策</p>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue'
import { useDecision, DecisionAction, actionLabels, actionColors } from './composables/useDecision'
import { message } from '@/tool'

const {
  decisions, selectedId, selectedDecision, pendingDecisions,
  fetchDecisions, executeDecision, registerSSE, unregisterSSE
} = useDecision()

const filter = ref<'all' | 'pending' | 'executed'>('all')
const editQuantity = ref(0)
const editPrice = ref(0)
const executing = ref(false)

const filteredDecisions = computed(() => {
  if (filter.value === 'pending') return decisions.value.filter(d => !d.executed)
  if (filter.value === 'executed') return decisions.value.filter(d => d.executed)
  return decisions.value
})

watch(selectedDecision, (d) => {
  if (d) {
    editQuantity.value = d.quantity
    editPrice.value = d.price
  }
})

const handleExecute = async () => {
  if (!selectedDecision.value || executing.value) return
  executing.value = true
  const result = await executeDecision(selectedDecision.value.id, editQuantity.value, editPrice.value)
  executing.value = false
  if (result.success) {
    message.success('下单成功')
  } else {
    message.error(result.error || '下单失败')
  }
}

onMounted(() => {
  fetchDecisions()
  registerSSE()
})

onUnmounted(() => {
  unregisterSSE()
})
</script>

<style scoped>
.order-desk {
  display: flex;
  gap: 1px;
  height: 100%;
  background: rgba(74, 158, 255, 0.1);
}

.decision-list-panel {
  flex: 1.2;
  display: flex;
  flex-direction: column;
  background: rgba(15, 23, 42, 0.6);
  min-width: 0;
}

.decision-action-panel {
  flex: 0.8;
  display: flex;
  flex-direction: column;
  background: rgba(15, 23, 42, 0.6);
  min-width: 280px;
  max-width: 380px;
}

.panel-header {
  padding: 12px 16px;
  border-bottom: 1px solid rgba(74, 158, 255, 0.15);
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.panel-header h3 {
  margin: 0;
  font-size: 14px;
  font-weight: 600;
  color: #e2e8f0;
}

.filter-btns {
  display: flex;
  gap: 4px;
}

.filter-btns button {
  padding: 3px 10px;
  font-size: 12px;
  background: transparent;
  border: 1px solid rgba(74, 158, 255, 0.2);
  border-radius: 4px;
  color: #94a3b8;
  cursor: pointer;
  transition: all 0.2s;
}

.filter-btns button.active,
.filter-btns button:hover {
  background: rgba(74, 158, 255, 0.15);
  color: #60a5fa;
  border-color: rgba(74, 158, 255, 0.4);
}

.decision-table-wrap {
  flex: 1;
  overflow-y: auto;
}

.decision-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 13px;
}

.decision-table th {
  padding: 8px 12px;
  text-align: left;
  color: #64748b;
  font-weight: 500;
  font-size: 12px;
  position: sticky;
  top: 0;
  background: rgba(15, 23, 42, 0.95);
  border-bottom: 1px solid rgba(74, 158, 255, 0.15);
}

.decision-table td {
  padding: 8px 12px;
  color: #cbd5e1;
  border-bottom: 1px solid rgba(255, 255, 255, 0.04);
}

.decision-table tr {
  cursor: pointer;
  transition: background 0.15s;
}

.decision-table tbody tr:hover {
  background: rgba(74, 158, 255, 0.08);
}

.decision-table tbody tr.selected {
  background: rgba(74, 158, 255, 0.15);
}

.decision-table tbody tr.executed {
  opacity: 0.6;
}

.symbol-code {
  font-family: 'SF Mono', 'Fira Code', monospace;
  font-size: 12px;
}

.action-badge {
  display: inline-block;
  padding: 2px 8px;
  border-radius: 4px;
  font-size: 12px;
  font-weight: 500;
  white-space: nowrap;
}

.status-pending {
  color: #fbbf24;
  font-size: 12px;
}

.status-executed {
  color: #64748b;
  font-size: 12px;
}

.empty-state {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100%;
  color: #475569;
  gap: 8px;
}

.empty-state i {
  font-size: 32px;
  opacity: 0.5;
}

.empty-state p {
  margin: 0;
  font-size: 14px;
  color: #94a3b8;
}

.empty-state span {
  font-size: 12px;
}

/* 右侧操作面板 */
.action-content {
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.empty-action {
  align-items: center;
  justify-content: center;
  flex: 1;
  color: #475569;
}

.empty-action i {
  font-size: 28px;
  opacity: 0.4;
}

.empty-action p {
  margin: 0;
  font-size: 13px;
  color: #64748b;
}

.info-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.info-row label {
  color: #64748b;
  font-size: 13px;
  flex-shrink: 0;
}

.info-value {
  color: #e2e8f0;
  font-size: 13px;
  font-weight: 500;
}

.info-value.highlight {
  color: #60a5fa;
  font-size: 15px;
}

.info-value.muted {
  color: #64748b;
  text-decoration: line-through;
}

.form-row {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.form-row label {
  color: #64748b;
  font-size: 12px;
}

.form-input {
  padding: 8px 12px;
  background: rgba(15, 23, 42, 0.8);
  border: 1px solid rgba(74, 158, 255, 0.25);
  border-radius: 6px;
  color: #e2e8f0;
  font-size: 14px;
  outline: none;
  transition: border-color 0.2s;
}

.form-input:focus {
  border-color: rgba(74, 158, 255, 0.5);
}

.hint {
  font-size: 11px;
  color: #475569;
}

.execute-btn {
  margin-top: 8px;
  padding: 10px;
  background: linear-gradient(135deg, #3b82f6, #2563eb);
  border: none;
  border-radius: 8px;
  color: white;
  font-size: 14px;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s;
}

.execute-btn:hover:not(:disabled) {
  background: linear-gradient(135deg, #2563eb, #1d4ed8);
  transform: translateY(-1px);
}

.execute-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.executed-detail {
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 12px;
  background: rgba(34, 197, 94, 0.05);
  border: 1px solid rgba(34, 197, 94, 0.15);
  border-radius: 8px;
}
</style>
