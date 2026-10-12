<template>
  <div class="global-risk-config">
    <div class="config-header" @click="expanded = !expanded">
      <h3><i class="fas fa-cog"></i> 全局风控配置</h3>
      <i :class="expanded ? 'fas fa-chevron-up' : 'fas fa-chevron-down'" class="toggle-icon"></i>
    </div>

    <div v-show="expanded" class="config-body">
      <div class="config-grid">
        <!-- 总资金止损 -->
        <div class="config-section">
          <h4>总资金止损</h4>
          <div class="field">
            <label>止损线（占初始本金）</label>
            <input
              type="number"
              v-model="capitalConfig.totalStopLossPercent"
              :disabled="!capitalConfig.enableTotalStopLoss"
              min="0.01"
              max="1"
              step="0.01"
              class="num-input"
            />
          </div>
          <div class="field toggle-field">
            <label class="toggle-label">
              <input type="checkbox" v-model="capitalConfig.enableTotalStopLoss" />
              <span class="toggle-text">启用总资金止损</span>
            </label>
          </div>
          <div class="field toggle-field">
            <label class="toggle-label">
              <input type="checkbox" v-model="capitalConfig.autoClosePosition" />
              <span class="toggle-text">触发后自动平仓</span>
            </label>
          </div>
        </div>

        <!-- 单日亏损 -->
        <div class="config-section">
          <h4>单日亏损限额</h4>
          <div class="field">
            <label>单日最大亏损（占昨日权益）</label>
            <input
              type="number"
              v-model="dailyConfig.dailyMaxLossPercent"
              :disabled="!dailyConfig.enableDailyLossLimit"
              min="0.01"
              max="1"
              step="0.01"
              class="num-input"
            />
          </div>
          <div class="field toggle-field">
            <label class="toggle-label">
              <input type="checkbox" v-model="dailyConfig.enableDailyLossLimit" />
              <span class="toggle-text">启用单日亏损限额</span>
            </label>
          </div>
          <div class="field">
            <label>昨日权益（重置用）</label>
            <input
              type="number"
              v-model="dailyConfig.lastDayEquity"
              min="0"
              step="10000"
              class="num-input"
            />
          </div>
        </div>
      </div>

      <div class="config-actions">
        <button @click="loadConfig" :disabled="loading" class="btn btn-ghost">
          {{ loading ? '加载中...' : '刷新' }}
        </button>
        <button @click="saveConfig" :disabled="saving" class="btn btn-primary">
          {{ saving ? '保存中...' : '保存' }}
        </button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import axios from 'axios'

const expanded = ref(true)
const loading = ref(false)
const saving = ref(false)

const capitalConfig = ref({
  totalStopLossPercent: 0.85,
  enableTotalStopLoss: false,
  autoClosePosition: false,
  initialCapital: 0,
})

const dailyConfig = ref({
  dailyMaxLossPercent: 0.03,
  enableDailyLossLimit: false,
  lastDayEquity: 0,
})

async function loadConfig() {
  loading.value = true
  try {
    const [capitalRes, dailyRes] = await Promise.all([
      axios.get('/v0/risk/capital'),
      axios.get('/v0/risk/daily'),
    ])

    const c = capitalRes.data
    capitalConfig.value = {
      totalStopLossPercent: c.totalStopLossPercent ?? 0.85,
      enableTotalStopLoss: c.enableTotalStopLoss ?? false,
      autoClosePosition: c.autoClosePosition ?? false,
      initialCapital: c.initialCapital ?? 0,
    }

    const d = dailyRes.data
    dailyConfig.value = {
      dailyMaxLossPercent: d.dailyMaxLossPercent ?? 0.03,
      enableDailyLossLimit: d.enableDailyLossLimit ?? false,
      lastDayEquity: d.lastDayEquity ?? 0,
    }
  } catch (err) {
    console.error('[GlobalRiskConfig] Load error:', err)
  } finally {
    loading.value = false
  }
}

async function saveConfig() {
  saving.value = true
  try {
    await Promise.all([
      axios.post('/v0/risk/capital', {
        totalStopLossPercent: capitalConfig.value.totalStopLossPercent,
        enableTotalStopLoss: capitalConfig.value.enableTotalStopLoss,
        autoClosePosition: capitalConfig.value.autoClosePosition,
      }),
      axios.post('/v0/risk/daily', {
        dailyMaxLossPercent: dailyConfig.value.dailyMaxLossPercent,
        enableDailyLossLimit: dailyConfig.value.enableDailyLossLimit,
        lastDayEquity: dailyConfig.value.lastDayEquity,
      }),
    ])
    alert('配置已保存')
  } catch (err) {
    console.error('[GlobalRiskConfig] Save error:', err)
    alert('保存失败')
  } finally {
    saving.value = false
  }
}

onMounted(() => {
  loadConfig()
})
</script>

<style scoped lang="scss">
.global-risk-config {
  background: var(--panel-bg);
  border: 1px solid var(--border);
  border-radius: 10px;
  margin-bottom: 20px;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
}

.config-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 20px;
  cursor: pointer;
  user-select: none;
  transition: background 0.2s;

  &:hover {
    background: rgba(41, 98, 255, 0.05);
  }

  h3 {
    margin: 0;
    font-size: 16px;
    color: var(--text);
    font-weight: 600;

    i {
      margin-right: 8px;
      color: var(--primary);
    }
  }

  .toggle-icon {
    color: var(--text-secondary);
    font-size: 14px;
  }
}

.config-body {
  padding: 0 20px 20px;
  border-top: 1px solid var(--border);
}

.config-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 24px;
  margin-top: 16px;
}

.config-section {
  background: var(--darker-bg);
  border: 1px solid var(--border);
  border-radius: 8px;
  padding: 16px;

  h4 {
    margin: 0 0 12px;
    font-size: 14px;
    color: var(--text);
    font-weight: 600;
  }
}

.field {
  margin-bottom: 12px;

  &:last-child {
    margin-bottom: 0;
  }

  label {
    display: block;
    font-size: 12px;
    color: var(--text-secondary);
    margin-bottom: 6px;
  }
}

.num-input {
  width: 100%;
  padding: 8px 12px;
  background: var(--panel-bg);
  border: 1px solid var(--border);
  border-radius: 6px;
  color: var(--text);
  font-size: 14px;
  font-family: inherit;
  transition: border-color 0.2s;

  &:focus {
    outline: none;
    border-color: var(--primary);
  }

  &:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
}

.toggle-field {
  margin-top: 8px;
}

.toggle-label {
  display: flex;
  align-items: center;
  gap: 8px;
  cursor: pointer;
  user-select: none;

  input[type="checkbox"] {
    width: 16px;
    height: 16px;
    cursor: pointer;
    accent-color: var(--primary);
  }

  .toggle-text {
    font-size: 13px;
    color: var(--text);
  }
}

.config-actions {
  display: flex;
  justify-content: flex-end;
  gap: 12px;
  margin-top: 20px;
  padding-top: 16px;
  border-top: 1px solid var(--border);
}

.btn {
  padding: 8px 20px;
  border-radius: 6px;
  font-size: 14px;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s;
  border: none;

  &:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
}

.btn-ghost {
  background: transparent;
  border: 1px solid var(--border);
  color: var(--text-secondary);

  &:hover:not(:disabled) {
    background: rgba(255, 255, 255, 0.05);
    color: var(--text);
  }
}

.btn-primary {
  background: var(--primary);
  color: #fff;

  &:hover:not(:disabled) {
    background: color-mix(in srgb, var(--primary) 85%, black);
  }
}
</style>
