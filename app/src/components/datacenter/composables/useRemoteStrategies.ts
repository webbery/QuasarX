import { ref } from 'vue'
import axios from 'axios'
import { useIsLoggedIn } from './useSession'

// 远程 service 策略清单：数据中心多个面板（行情 / 基本面 / 分红）共用同一份，
// 所以状态放在模块级，各面板 import 同一个 composable 即可。

export interface RemoteStrategy {
    name: string
    running: boolean
    failed: boolean
    symbols: string[] | null  // null = 未拉过；[] = 拉过但无 QuoteInputNode
}

const remoteStrategies = ref<RemoteStrategy[]>([])
const remoteFetched = ref(false)   // 至少拉过一次，避免空白闪烁
const remoteReachable = ref(false) // 用于圆点显示
const remoteLoading = ref(false)   // 防止并发刷新

const fetchRemoteStrategies = async (includePools: boolean) => {
    const isLoggedIn = useIsLoggedIn()
    if (!isLoggedIn.value) { remoteFetched.value = true; return }
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    remoteLoading.value = true
    try {
        const url = includePools
            ? `https://${server}/v0/strategy?include=pools`
            : `https://${server}/v0/strategy`
        const r = await axios.get(url, {
            headers: { 'Authorization': token || '' },
            timeout: 3000,
        })
        const data = r.data
        const ok = Array.isArray(data) ? data : (data.strategies || [])
        const fail = Array.isArray(data.failed) ? data.failed : []
        const seen = new Set(ok.map((s: any) => s.name))
        const list: RemoteStrategy[] = ok.map((s: any) => ({
            name: s.name,
            running: !!s.running,
            failed: false,
            symbols: Array.isArray(s.symbols) ? s.symbols : null,
        }))
        for (const f of fail) {
            if (f.name && !seen.has(f.name)) {
                list.push({ name: f.name, running: false, failed: true, symbols: null })
            }
        }
        remoteStrategies.value = list
        remoteReachable.value = true
    } catch (e: any) {
        console.debug('[DataCenter] remote strategies unreachable:', e?.message)
        remoteReachable.value = false
        remoteStrategies.value = []
    } finally {
        remoteLoading.value = false
        remoteFetched.value = true
    }
}

export function useRemoteStrategies() {
    return { remoteStrategies, remoteFetched, remoteReachable, remoteLoading, fetchRemoteStrategies }
}
