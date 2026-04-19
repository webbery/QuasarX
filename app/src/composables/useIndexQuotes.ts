import { ref, onMounted, onUnmounted } from 'vue'
import { fetchQuotes } from '@/lib/tickflow'

/**
 * 获取上证指数实时行情
 * 每 10 秒轮询一次 TickFlow /v1/quotes API
 */
export function useIndexQuotes(symbol: string = 'SH000001') {
  const lastPrice = ref<number>(0)
  const changePct = ref<number>(0)
  const change = ref<number>(0)
  const timestamp = ref<number>(0)
  const high = ref<number>(0)
  const low = ref<number>(0)
  const open = ref<number>(0)
  const loading = ref<boolean>(false)
  const error = ref<string>('')

  let timer: ReturnType<typeof setInterval> | null = null

  async function fetch() {
    loading.value = true
    error.value = ''
    try {
      const quotes = await fetchQuotes([symbol])
      if (quotes.length > 0) {
        const q = quotes[0]
        lastPrice.value = q.last_price ?? 0
        changePct.value = 100 * (q.ext?.change_pct ??  0)
        change.value = q.ext?.change_amount ?? 0
        timestamp.value = q.timestamp ?? 0
        high.value = q.high ?? 0
        low.value = q.low ?? 0
        open.value = q.open ?? 0
      } else {
        error.value = '未获取到行情数据'
      }
    } catch (e: any) {
      error.value = e.message || '获取行情失败'
      console.warn('[useIndexQuotes]', error.value)
    } finally {
      loading.value = false
    }
  }

  function start() {
    fetch()
    timer = setInterval(fetch, 10000)
  }

  function stop() {
    if (timer) {
      clearInterval(timer)
      timer = null
    }
  }

  onMounted(() => {
    start()
  })

  onUnmounted(() => {
    stop()
  })

  return {
    lastPrice,
    changePct,
    change,
    timestamp,
    high,
    low,
    open,
    loading,
    error,
    // exposed for manual control
    fetch,
    start,
    stop,
  }
}
