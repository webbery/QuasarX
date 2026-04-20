<template>
    <div class="panel-section">
        <div class="panel-title">
          <h3><i class="fas fa-chart-bar"></i> 市场概览</h3>
          <span>实时</span>
        </div>

        <div class="market-list">
          <div class="market-item">
            <div class="market-name">
              <i :class="indexIconClass"></i>
              <span>上证指数</span>
            </div>
            <div class="market-value" :class="indexChangeClass">{{ indexDisplay }} ({{ indexPercentDisplay }}%)</div>
          </div>
          <div class="market-item">
            <div class="market-name">
              <i class="fas fa-caret-down negative-change"></i>
              <span>中证500</span>
            </div>
            <div class="market-value negative-change">{{zz500}} ({{zz500Percent}}%)</div>
          </div>
          <!-- <div class="market-item">
            <div class="market-name">
              <i class="fas fa-caret-up positive-change"></i>
              <span>创业板指</span>
            </div>
            <div class="market-value positive-change">2,845.33 (+2.1%)</div>
          </div> -->
          <div class="market-item">
            <div class="market-name">
              <i class="fas fa-caret-up positive-change"></i>
              <span>恒生指数</span>
            </div>
            <div class="market-value positive-change">{{hs}} ({{hsPercent}}%)</div>
          </div>
          <div class="market-item">
            <div class="market-name">
              <i class="fas fa-caret-down negative-change"></i>
              <span>标普500</span>
            </div>
            <div class="market-value negative-change">-- (--)</div>
          </div>
          <div class="market-item">
            <div class="market-name">
              <i class="fas fa-caret-up positive-change"></i>
              <span>黄金</span>
            </div>
            <div class="market-value positive-change">{{ GlodPrice }} (+0.6%)</div>
          </div>
          <div class="market-item">
            <div class="market-name">
              <i class="fas fa-caret-up positive-change"></i>
              <span>USD/CNY</span>
            </div>
            <div class="market-value " :class="rate_status_class">{{ usd_cny_rate }} ({{ up_rate }}%)</div>
          </div>
        </div>
    </div>

    <!-- 无风险利率与货币市场指标 -->
    <div class="panel-section">
        <div class="panel-title">
          <h3><i class="fas fa-landmark"></i> 无风险利率与货币市场</h3>
          <span class="update-time">{{ lastUpdateTime }}</span>
        </div>

        <div class="rate-indicators">
          <!-- Shibor 利率 -->
          <div class="rate-group">
            <div class="rate-group-title">
              <i class="fas fa-yen-sign"></i>
              <span>Shibor 上海银行间同业拆放利率</span>
            </div>
            <div class="rate-grid">
              <div class="rate-item">
                <div class="rate-label">
                  <span>O/N 隔夜</span>
                  <span class="rate-badge" :class="getRateChangeClass(shiborON.change)">
                    {{ shiborON.change >= 0 ? '+' : '' }}{{ shiborON.change.toFixed(2) }}BP
                  </span>
                </div>
                <div class="rate-value">{{ shiborON.value.toFixed(4) }}%</div>
              </div>
              <div class="rate-item">
                <div class="rate-label">
                  <span>1W 1周</span>
                  <span class="rate-badge" :class="getRateChangeClass(shibor1W.change)">
                    {{ shibor1W.change >= 0 ? '+' : '' }}{{ shibor1W.change.toFixed(2) }}BP
                  </span>
                </div>
                <div class="rate-value">{{ shibor1W.value.toFixed(4) }}%</div>
              </div>
              <div class="rate-item">
                <div class="rate-label">
                  <span>2W 2周</span>
                  <span class="rate-badge" :class="getRateChangeClass(shibor2W.change)">
                    {{ shibor2W.change >= 0 ? '+' : '' }}{{ shibor2W.change.toFixed(2) }}BP
                  </span>
                </div>
                <div class="rate-value">{{ shibor2W.value.toFixed(4) }}%</div>
              </div>
              <div class="rate-item">
                <div class="rate-label">
                  <span>1M 1个月</span>
                  <span class="rate-badge" :class="getRateChangeClass(shibor1M.change)">
                    {{ shibor1M.change >= 0 ? '+' : '' }}{{ shibor1M.change.toFixed(2) }}BP
                  </span>
                </div>
                <div class="rate-value">{{ shibor1M.value.toFixed(4) }}%</div>
              </div>
              <div class="rate-item">
                <div class="rate-label">
                  <span>3M 3个月</span>
                  <span class="rate-badge" :class="getRateChangeClass(shibor3M.change)">
                    {{ shibor3M.change >= 0 ? '+' : '' }}{{ shibor3M.change.toFixed(2) }}BP
                  </span>
                </div>
                <div class="rate-value">{{ shibor3M.value.toFixed(4) }}%</div>
              </div>
              <div class="rate-item">
                <div class="rate-label">
                  <span>6M 6个月</span>
                  <span class="rate-badge" :class="getRateChangeClass(shibor6M.change)">
                    {{ shibor6M.change >= 0 ? '+' : '' }}{{ shibor6M.change.toFixed(2) }}BP
                  </span>
                </div>
                <div class="rate-value">{{ shibor6M.value.toFixed(4) }}%</div>
              </div>
              <div class="rate-item">
                <div class="rate-label">
                  <span>9M 9个月</span>
                  <span class="rate-badge" :class="getRateChangeClass(shibor9M.change)">
                    {{ shibor9M.change >= 0 ? '+' : '' }}{{ shibor9M.change.toFixed(2) }}BP
                  </span>
                </div>
                <div class="rate-value">{{ shibor9M.value.toFixed(4) }}%</div>
              </div>
              <div class="rate-item">
                <div class="rate-label">
                  <span>1Y 1年</span>
                  <span class="rate-badge" :class="getRateChangeClass(shibor1Y.change)">
                    {{ shibor1Y.change >= 0 ? '+' : '' }}{{ shibor1Y.change.toFixed(2) }}BP
                  </span>
                </div>
                <div class="rate-value">{{ shibor1Y.value.toFixed(4) }}%</div>
              </div>
            </div>
          </div>

          <!-- DR007 存款类金融机构质押式回购利率 -->
          <div class="rate-group">
            <div class="rate-group-title">
              <i class="fas fa-coins"></i>
              <span>DR007 存款类金融机构7天期回购利率</span>
            </div>
            <div class="dr007-display">
              <div class="dr007-main">
                <div class="dr007-value">{{ dr007.value.toFixed(4) }}%</div>
                <div class="dr007-change" :class="getRateChangeClass(dr007.change)">
                  {{ dr007.change >= 0 ? '↑' : '↓' }} {{ Math.abs(dr007.change).toFixed(2) }}BP
                </div>
              </div>
              <div class="dr007-stats">
                <div class="dr007-stat">
                  <span class="stat-label">今日开盘</span>
                  <span class="stat-value">{{ dr007.open.toFixed(4) }}%</span>
                </div>
                <div class="dr007-stat">
                  <span class="stat-label">今日最高</span>
                  <span class="stat-value">{{ dr007.high.toFixed(4) }}%</span>
                </div>
                <div class="dr007-stat">
                  <span class="stat-label">今日最低</span>
                  <span class="stat-value">{{ dr007.low.toFixed(4) }}%</span>
                </div>
                <div class="dr007-stat">
                  <span class="stat-label">昨日收盘</span>
                  <span class="stat-value">{{ dr007.prevClose.toFixed(4) }}%</span>
                </div>
              </div>
            </div>
          </div>

          <!-- 利率曲线示意 -->
          <div class="rate-group">
            <div class="rate-group-title">
              <i class="fas fa-chart-area"></i>
              <span>Shibor 利率曲线</span>
            </div>
            <div class="rate-curve-placeholder">
              <i class="fas fa-chart-line"></i>
              <span>利率曲线图表（待实现）</span>
              <div class="curve-data-preview">
                <span class="curve-point">O/N: {{ shiborON.value.toFixed(2) }}%</span>
                <span class="curve-point">1W: {{ shibor1W.value.toFixed(2) }}%</span>
                <span class="curve-point">2W: {{ shibor2W.value.toFixed(2) }}%</span>
                <span class="curve-point">1M: {{ shibor1M.value.toFixed(2) }}%</span>
                <span class="curve-point">3M: {{ shibor3M.value.toFixed(2) }}%</span>
                <span class="curve-point">6M: {{ shibor6M.value.toFixed(2) }}%</span>
                <span class="curve-point">9M: {{ shibor9M.value.toFixed(2) }}%</span>
                <span class="curve-point">1Y: {{ shibor1Y.value.toFixed(2) }}%</span>
              </div>
            </div>
          </div>
        </div>
    </div>
</template>

<script setup >
import { onMounted, ref, onBeforeUnmount, computed } from 'vue'
import { useIndexQuotes } from '@/composables/useIndexQuotes'
const axios = require('axios');

// 上证指数实时数据
const { lastPrice, changePct, loading } = useIndexQuotes('SH000001')

const indexDisplay = computed(() => {
  if (loading.value && lastPrice.value === 0) return '--'
  return lastPrice.value > 0 ? lastPrice.value.toFixed(2) : '--'
})

const indexPercentDisplay = computed(() => {
  if (loading.value && lastPrice.value === 0) return '--'
  if (lastPrice.value === 0) return '--'
  const pct = changePct.value ?? 0
  return (pct >= 0 ? '+' : '') + pct.toFixed(2)
})

const indexIconClass = computed(() => {
  const pct = changePct.value ?? 0
  if (pct > 0) return 'fas fa-caret-up positive-change'
  if (pct < 0) return 'fas fa-caret-down negative-change'
  return 'fas fa-minus no-change'
})

const indexChangeClass = computed(() => {
  const pct = changePct.value ?? 0
  if (pct > 0) return 'positive-change'
  if (pct < 0) return 'negative-change'
  return 'no-change'
})

let prev_rate = 0
let usd_cny_rate = ref('0')
let up_rate = ref('0')
let rate_status_class = ref('no-change')
const timer = ref(null)
const isPolling = ref(false);
const interval = ref(10000);
let GlodPrice = ref(0);
let hs300 = ref('')
let hs300Percent = ref('0')
let zz500 = ref('')
let zz500Percent = ref('0')
let hs = ref('')
let hsPercent = ref('0')

// ============ 无风险利率与货币市场指标（模拟数据） ============

// 最后更新时间
const lastUpdateTime = computed(() => {
  const now = new Date()
  const hours = String(now.getHours()).padStart(2, '0')
  const minutes = String(now.getMinutes()).padStart(2, '0')
  return `更新于 ${hours}:${minutes}`
})

// Shibor 各期限利率数据（模拟）
const shiborON = ref({ value: 1.8234, change: -2.15 })   // 隔夜
const shibor1W = ref({ value: 2.0156, change: 3.42 })    // 1周
const shibor2W = ref({ value: 2.1823, change: 5.67 })    // 2周
const shibor1M = ref({ value: 2.2945, change: 1.23 })    // 1个月
const shibor3M = ref({ value: 2.4512, change: -0.89 })   // 3个月
const shibor6M = ref({ value: 2.6234, change: -1.45 })   // 6个月
const shibor9M = ref({ value: 2.7456, change: 0.34 })    // 9个月
const shibor1Y = ref({ value: 2.8923, change: -0.56 })   // 1年

// DR007 数据（模拟）
const dr007 = ref({
  value: 2.0345,      // 当前值
  change: 4.23,       // 变化量（BP）
  open: 2.0123,       // 开盘
  high: 2.0567,       // 最高
  low: 1.9987,        // 最低
  prevClose: 1.9922   // 昨收
})

/**
 * 根据利率变化获取对应的样式类
 * @param change 变化值（BP）
 * @returns 样式类名
 */
function getRateChangeClass(change) {
  if (change > 0) return 'rate-up'
  if (change < 0) return 'rate-down'
  return 'rate-flat'
}

/**
 * 更新 Shibor 和 DR007 数据
 * TODO: 实现真实的数据源接入
 */
function updateRates() {
  // TODO: 从后端 API 或外部数据源获取 Shibor 和 DR007 数据
  // 示例 API 端点：
  // - /v0/rates/shibor
  // - /v0/rates/dr007
  // 
  // 当前使用模拟数据，实际应用中应该：
  // 1. 调用后端接口获取最新数据
  // 2. 解析返回的 JSON 响应
  // 3. 更新 shiborON, shibor1W, ..., dr007 等响应式变量
  console.log('[MarketPanel] 更新利率数据（当前使用模拟数据）')
}

function update_rate() {
  // 更新USD/CNY利率
  const url = "https://www.mxnzp.com/api/exchange_rate/aim?from=USD&to=CNY&app_id=g3xecmg8m8yjmrmy&app_secret=sYj6cRRd497E558UraALJOy6LDo1O61Z";
  axios.get(url)
    .then(response => {
      let data = response['data']['data']
      let value = prev_rate
      if ('price' in data) {
        value = data['price']
      }
      if (prev_rate == 0) {
        prev_rate = parseFloat(usd_cny_rate.value)
        console.info('prev rate:', prev_rate)
      } else {
        let rate_value = (parseFloat(value) - prev_rate)/prev_rate * 100
        let prev_flag = ''
        if (rate_value > 0) {
          prev_flag = '+'
          rate_status_class.value = 'positive-change'
        }
        else if (rate_value < 0) {
          prev_flag = '-'
          rate_status_class.value = 'negative-change'
        } else {
          rate_status_class.value = 'no-change'
        }
        update_rate.value = prev_flag + rate_value.toFixed(2)
        prev_rate = parseFloat(usd_cny_rate.value)
      }
      console.log(value)
      usd_cny_rate.value = value
    })
    .catch(error => {
      console.log(error);
    });
}

function update_gold() {
  // https://api.aa1.cn/doc/trade.html
  const url = "https://free.xwteam.cn/api/gold/trade";
  axios.get(url)
    .then(response => {
      let data = response['data']['data']['LF']
      for (const item of data) {
        if (item['Symbol'] === 'Au') {
          console.info('AU', item)
          GlodPrice.value = item['BP']
          break;
        }
      }
    })
}
// 开始轮询
const startPolling = () => {
  // 如果已经在轮询中，先停止
  if (isPolling.value) {
    clearInterval(timer.value);
  }

  // 立即请求一次
  update_rate();
  update_gold();
  updateRates();  // 更新利率数据

  // 设置定时器
  timer.value = setInterval(() => {
    update_rate();
    update_gold();
    updateRates();  // 定时更新利率数据
  }, interval.value);

  isPolling.value = true;
};

// 停止轮询
const stopPolling = () => {
  if (timer.value) {
    clearInterval(timer.value);
    timer.value = null;
    isPolling.value = false;
  }
};

onMounted(() => {
  startPolling();
})

onBeforeUnmount(() => {
  stopPolling();
});
</script>

<style scoped>
.no-change {
  color: var(--text-secondary);
}

/* 无风险利率与货币市场指标样式 */
.update-time {
  font-size: 0.75rem;
  color: var(--text-secondary, rgba(255, 255, 255, 0.6));
}

.rate-indicators {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.rate-group {
  background-color: var(--darker-bg, #1e1e1e);
  border-radius: 6px;
  padding: 12px;
  border: 1px solid var(--border, #333);
}

.rate-group-title {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 12px;
  font-size: 0.9rem;
  font-weight: 600;
  color: var(--text, rgba(255, 255, 255, 0.87));

  i {
    color: var(--primary, #2962ff);
  }
}

/* Shibor 利率网格 */
.rate-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 10px;
}

.rate-item {
  background-color: var(--panel-bg, #2a2a2a);
  border-radius: 6px;
  padding: 10px;
  border: 1px solid var(--border, #333);
  transition: all 0.2s;

  &:hover {
    border-color: var(--primary, #2962ff);
    box-shadow: 0 2px 8px rgba(41, 98, 255, 0.2);
  }
}

.rate-label {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 6px;
  font-size: 0.8rem;
  color: var(--text-secondary, rgba(255, 255, 255, 0.6));
}

.rate-badge {
  font-size: 0.7rem;
  padding: 2px 6px;
  border-radius: 4px;
  font-weight: 500;

  &.rate-up {
    background-color: rgba(239, 35, 42, 0.2);
    color: #ef232a;
  }

  &.rate-down {
    background-color: rgba(20, 177, 67, 0.2);
    color: #14b143;
  }

  &.rate-flat {
    background-color: rgba(255, 255, 255, 0.1);
    color: var(--text-secondary, rgba(255, 255, 255, 0.6));
  }
}

.rate-value {
  font-size: 1.1rem;
  font-weight: 700;
  color: var(--text, rgba(255, 255, 255, 0.87));
}

/* DR007 显示区域 */
.dr007-display {
  display: flex;
  gap: 20px;
}

.dr007-main {
  flex: 1;
  display: flex;
  flex-direction: column;
  justify-content: center;
  align-items: center;
  background-color: var(--panel-bg, #2a2a2a);
  border-radius: 8px;
  padding: 20px;
  border: 1px solid var(--border, #333);
}

.dr007-value {
  font-size: 2rem;
  font-weight: 700;
  color: var(--text, rgba(255, 255, 255, 0.87));
  margin-bottom: 8px;
}

.dr007-change {
  font-size: 1rem;
  font-weight: 600;

  &.rate-up {
    color: #ef232a;
  }

  &.rate-down {
    color: #14b143;
  }

  &.rate-flat {
    color: var(--text-secondary, rgba(255, 255, 255, 0.6));
  }
}

.dr007-stats {
  flex: 2;
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 10px;
}

.dr007-stat {
  display: flex;
  flex-direction: column;
  background-color: var(--panel-bg, #2a2a2a);
  border-radius: 6px;
  padding: 12px;
  border: 1px solid var(--border, #333);

  .stat-label {
    font-size: 0.75rem;
    color: var(--text-secondary, rgba(255, 255, 255, 0.6));
    margin-bottom: 4px;
  }

  .stat-value {
    font-size: 1rem;
    font-weight: 600;
    color: var(--text, rgba(255, 255, 255, 0.87));
  }
}

/* 利率曲线占位符 */
.rate-curve-placeholder {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  background-color: var(--panel-bg, #2a2a2a);
  border-radius: 8px;
  padding: 30px;
  border: 2px dashed var(--border, #333);
  min-height: 150px;
  color: var(--text-secondary, rgba(255, 255, 255, 0.6));

  i {
    font-size: 2rem;
    margin-bottom: 10px;
    color: var(--primary, #2962ff);
  }

  span {
    font-size: 0.9rem;
  }
}

.curve-data-preview {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-top: 12px;
  justify-content: center;
}

.curve-point {
  font-size: 0.75rem;
  padding: 4px 8px;
  background-color: var(--darker-bg, #1e1e1e);
  border-radius: 4px;
  color: var(--text-secondary, rgba(255, 255, 255, 0.6));
  border: 1px solid var(--border, #333);
}

/* 响应式设计 */
@media (max-width: 1200px) {
  .rate-grid {
    grid-template-columns: repeat(2, 1fr);
  }

  .dr007-display {
    flex-direction: column;
  }
}

@media (max-width: 768px) {
  .rate-grid {
    grid-template-columns: 1fr;
  }

  .dr007-stats {
    grid-template-columns: 1fr;
  }
}
</style>