<template>
  <div class="tab-pane" :class="{ active: active }">
    <!-- 持仓表格 -->
    <table class="position-table">
      <thead>
        <tr>
          <th>代码</th>
          <th>交易所</th>
          <th>名称</th>
          <th>持仓数量</th>
          <th>可用数量</th>
          <th>成本价</th>
          <th>当前价</th>
          <th>浮动盈亏</th>
          <th>操作</th>
        </tr>
      </thead>
      <tbody>
        <tr v-for="position in positions" :key="position.code">
          <td>{{ position.code }}</td>
          <td>{{ position.exchange }}</td>
          <td>{{ position.name }}</td>
          <td>{{ position.quantity }}</td>
          <td>{{ position.available }}</td>
          <td>{{ position.costPrice }}</td>
          <td>{{ position.currentPrice }}</td>
          <td :class="position.profit > 0 ? 'profit-positive' : 'profit-negative'">
            {{ position.profit > 0 ? '+' : '' }}{{ position.profit.toFixed(4) }}
          </td>
          <td>
            <button class="btn btn-mini" @click="handleTrade(position, 'buy')">买入</button>
            <button class="btn btn-mini btn-danger" @click="handleTrade(position, 'sell')">卖出</button>
          </td>
        </tr>
      </tbody>
    </table>

    <div class="empty-state" v-if="positions.length === 0">
      暂无持仓
    </div>
  </div>
</template>

<script setup lang="ts">
defineProps({
  active: { type: Boolean, default: true },
  positions: { 
    type: Array as () => Array<{
      code: string
      exchange: string
      name: string
      quantity: number
      available: number
      costPrice: number
      currentPrice: number
      profit: number
    }>, 
    required: true 
  }
})

const emit = defineEmits<{
  trade: [position: any, operationType: string]
}>()

const handleTrade = (position: any, operationType: string) => {
  emit('trade', position, operationType)
}
</script>

<style scoped>
.tab-pane {
  display: none;
}

.tab-pane.active {
  display: block;
}

.position-table {
  width: 100%;
  border-collapse: collapse;
  margin-bottom: 10px;
}

.position-table th,
.position-table td {
  padding: 12px 15px;
  text-align: left;
  border-bottom: 1px solid var(--border);
}

.position-table th {
  color: var(--text-secondary);
  font-weight: 500;
  font-size: 0.85rem;
}

.position-table tbody tr:hover {
  background-color: rgba(255, 255, 255, 0.03);
}

.btn {
  padding: 4px 8px;
  font-size: 0.75rem;
  margin-right: 5px;
  border-radius: 4px;
  cursor: pointer;
  border: none;
  background: linear-gradient(90deg, #2563eb, #1d4ed8);
  color: white;
  transition: all 0.2s;
}

.btn-mini {
  padding: 4px 8px;
  font-size: 0.75rem;
}

.btn:hover:not(:disabled) {
  background: linear-gradient(90deg, #1d4ed8, #1e40af);
  transform: translateY(-1px);
}

.btn-danger {
  background: linear-gradient(90deg, #f44336, #d32f2f);
}

.btn-danger:hover {
  background: linear-gradient(90deg, #d32f2f, #b71c1c);
}

.profit-positive {
  color: var(--secondary);
}

.profit-negative {
  color: #f44336;
}

.empty-state {
  text-align: center;
  padding: 20px;
  color: var(--text-secondary);
}
</style>
