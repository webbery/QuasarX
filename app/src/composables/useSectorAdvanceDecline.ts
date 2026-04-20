import { ref, onMounted, onUnmounted } from 'vue'
import { fetchSectorQuotes, calculateTotalAdvanceDecline } from '@/lib/sectorApi'

/**
 * 获取行业板块涨跌家数
 * 每 30 秒轮询一次 /stocks/sector/quote API，计算总的上涨家数和下跌家数
 */
export function useSectorAdvanceDecline() {
  const advanceCount = ref<number>(0)  // 总上涨家数
  const declineCount = ref<number>(0)  // 总下跌家数
  const loading = ref<boolean>(false)
  const error = ref<string>('')

  let timer: ReturnType<typeof setInterval> | null = null

  async function fetch() {
    loading.value = true
    error.value = ''
    try {
      const sectors = await fetchSectorQuotes()
      const { totalUp, totalDown } = calculateTotalAdvanceDecline(sectors)
      advanceCount.value = totalUp
      declineCount.value = totalDown
    } catch (e: any) {
      error.value = e.message || '获取板块涨跌数据失败'
      console.warn('[useSectorAdvanceDecline]', error.value)
    } finally {
      loading.value = false
    }
  }

  function start() {
    fetch()
    timer = setInterval(fetch, 30000) // 每30秒刷新
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
    advanceCount,
    declineCount,
    loading,
    error,
    // exposed for manual control
    fetch,
    start,
    stop,
  }
}
