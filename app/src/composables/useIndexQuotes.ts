import { computed, onMounted, onUnmounted } from 'vue'
import { useQuoteStore } from '@/stores/quoteStore'

/**
 * 获取指定指数的实时行情
 * 底层复用 quoteStore 的统一定时器，不再单独创建 setInterval
 */
export function useIndexQuotes(symbol: string = 'SH000001') {
  const store = useQuoteStore()
  const quote = computed(() => store.getQuote(symbol))

  const lastPrice = computed(() => quote.value.lastPrice)
  const changePct = computed(() => quote.value.changePct)
  const change = computed(() => quote.value.changeAmount)
  const timestamp = computed(() => quote.value.timestamp)
  const high = computed(() => quote.value.high)
  const low = computed(() => quote.value.low)
  const open = computed(() => quote.value.open)
  const loading = computed(() => store.loading)
  const error = computed(() => '')

  function start() {
    store.subscribe(symbol)
  }

  function stop() {
    store.unsubscribe(symbol)
  }

  function fetch() {
    store.fetchAll()
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
    fetch,
    start,
    stop,
  }
}
