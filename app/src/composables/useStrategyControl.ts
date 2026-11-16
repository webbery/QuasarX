// app/src/composables/useStrategyControl.ts
// 策略生命周期统一入口（查询 / 部署 / 启动 / 停止 / 删除）
//
// 收敛了原先三处各写一遍的实现：
//   - ml/composables/useStrategyDeploy.ts  部署（multipart + 模型文件）
//   - StrategyTracker.vue                   内联的 start / stop / delete
//   - lib/tools/strategy.ts                queryRemoteStrategies 查询
//
// 分层：
//   lib/api/serviceClient.ts   URL 解析 / 鉴权 / 错误提取（共享底层）
//   composables/useStrategyControl.ts   本文件 —— 策略生命周期
//   composables/useBacktestControl.ts   回测任务（另一个领域，见该文件）
//
// 本 composable **不做 UI 副作用**（不弹 ElMessage），消息由调用方决定：
//   - useStrategyDeploy 保留原有的 ElMessage 行为
//   - StrategyTracker 保留 message.success / message.error
//   - Agent 动作把 message 放进 ActionResult，由产物卡渲染

import { ref } from 'vue'
import axios from 'axios'
import { ipcRenderer } from 'electron'
import {
  NOT_LOGGED_IN,
  apiUrl,
  authedClient,
  errorMessage,
} from '@/lib/api/serviceClient'

export interface StrategyOpResult {
  success: boolean
  /** 服务端或本地生成的可读消息 */
  message?: string
  /** 部署冲突：同名策略正在运行 */
  conflict?: boolean
  /** 操作后策略是否处于运行中 */
  running?: boolean
}

/** 远程 service 上的策略（字段按服务端返回，宽松处理） */
export interface RemoteStrategy {
  name: string
  running?: boolean
  failed?: boolean
  executionMode?: string
  epochCount?: number
  allocatedCapital?: number
  usedCapital?: number
  availableCapital?: number
  lastHeartbeat?: number
  symbols?: string[]
  [key: string]: any
}

/** GET /v0/strategy 的响应 */
export interface RemoteStrategyList {
  strategies: RemoteStrategy[]
  failed: Array<{ name?: string; error?: string; failed_node?: any }>
}

/** 部署时的模型文件绑定 */
interface ModelBinding {
  label: string
  version?: string
}

/**
 * 收集策略图里所有 XGBoostNode 的模型文件引用。
 * 注意：StrategyFactory.getStrategyGraph() 返回的 nodes 是顶层字段，不是 graph.nodes。
 */
function collectXgbNodes(strategyJson: any): Array<{ id: string; label: string; modelFile: string }> {
  const out: Array<{ id: string; label: string; modelFile: string }> = []
  const nodes = strategyJson?.nodes ?? strategyJson?.graph?.nodes
  if (!Array.isArray(nodes)) return out
  for (const n of nodes) {
    if (n?.data?.nodeType === 'xgboost') {
      const label = n.data.label
      const modelFile = n.data?.params?.modelFile?.value || n.data?.params?.modelFile || ''
      out.push({ id: n.id, label, modelFile })
    }
  }
  return out
}

/**
 * 策略生命周期控制。
 *
 * @example
 * const control = useStrategyControl()
 * const res = await control.start('my-strategy')
 * if (!res.success) console.warn(res.message)
 */
export function useStrategyControl() {
  /** 正在操作的策略名集合（按名粒度，用于按钮禁用态） */
  const busyNames = ref<Set<string>>(new Set())
  /** 是否有部署正在进行 */
  const deploying = ref(false)
  /** 最近一次错误 */
  const lastError = ref<string | null>(null)

  function isBusy(name: string): boolean {
    return busyNames.value.has(name)
  }

  function setBusy(name: string, busy: boolean) {
    const next = new Set(busyNames.value)
    if (busy) next.add(name)
    else next.delete(name)
    busyNames.value = next
  }

  /* ---------------------------------------------------------------------
   * 查询
   * ------------------------------------------------------------------- */

  /**
   * 列出远程 service 上已部署的策略。
   * @param includePools 是否附带标的池
   */
  async function list(includePools = false): Promise<StrategyOpResult & { data?: RemoteStrategyList }> {
    lastError.value = null
    const client = authedClient()
    if (!client) {
      return { success: false, message: NOT_LOGGED_IN }
    }
    try {
      const response = await client.get(apiUrl('/v0/strategy'), {
        params: includePools ? { include: 'pools' } : undefined,
      })
      const d = response.data
      const strategies: RemoteStrategy[] = Array.isArray(d) ? d : (d?.strategies ?? [])
      const failed = Array.isArray(d?.failed) ? d.failed : []
      return { success: true, data: { strategies, failed } }
    } catch (err: any) {
      const message = errorMessage(err, '查询远程策略失败')
      lastError.value = message
      return { success: false, message }
    }
  }

  /* ---------------------------------------------------------------------
   * 部署
   * ------------------------------------------------------------------- */

  /**
   * 部署策略到 service（multipart + XGBoost 模型文件）。
   *
   * 预检规则：
   *   1. 每个 XGBoostNode 的 modelFile 必须匹配 production/{strategyName}-{label}.json
   *   2. data.models 中每个 label 必须在本地 appData/models/{inst}/ 有对应文件
   */
  async function deploy(
    strategyName: string,
    strategyJson: any,
    options?: { force?: boolean },
  ): Promise<StrategyOpResult> {
    lastError.value = null
    if (!strategyName) {
      return { success: false, message: '部署失败：策略名不能为空' }
    }

    const xgbNodes = collectXgbNodes(strategyJson)

    // 校验 1：modelFile 必须匹配 production/{strategyName}-{label}.json（禁止跨策略）
    for (const xn of xgbNodes) {
      const expected = `production/${strategyName}-${xn.label}.json`
      if (xn.modelFile !== expected) {
        const message = `XGBoostNode「${xn.label}」modelFile='${xn.modelFile}' 与策略「${strategyName}」不匹配，禁止跨策略引用（应为 '${expected}'）`
        lastError.value = message
        return { success: false, message }
      }
    }

    // 校验 2：data.models 里的 label 必须与 XGBoostNode label 一致
    const bindings: ModelBinding[] = Array.isArray(strategyJson?.data?.models)
      ? strategyJson.data.models.filter((m: any) => m?.label)
      : []
    const labels = new Set(xgbNodes.map((x) => x.label))
    for (const b of bindings) {
      if (!labels.has(b.label)) {
        const message = `data.models 中的 label '${b.label}' 与任何 XGBoostNode 不匹配`
        lastError.value = message
        return { success: false, message }
      }
    }

    deploying.value = true
    setBusy(strategyName, true)
    try {
      const form = new FormData()
      form.append('script', JSON.stringify(strategyJson))
      form.append('name', strategyName)
      if (options?.force) {
        form.append('force', 'true')
      }

      // 每个 XGBoostNode 都要附 model_{label} 和 model_{label}_meta parts
      for (const xn of xgbNodes) {
        const readRes = await ipcRenderer.invoke('model-read-for-deploy', {
          strategyName,
          label: xn.label,
        })
        if (!readRes?.success) {
          const message = `模型文件读取失败: ${readRes?.error || xn.label}`
          lastError.value = message
          return { success: false, message }
        }
        form.append(
          `model_${xn.label}`,
          new Blob([new Uint8Array(readRes.modelBytes)], { type: 'application/json' }),
          `${xn.label}.json`,
        )
        if (readRes.metaBytes && readRes.metaBytes.length > 0) {
          form.append(
            `model_${xn.label}_meta`,
            new Blob([new Uint8Array(readRes.metaBytes)], { type: 'application/json' }),
            `${xn.label}.meta.json`,
          )
        }
      }

      const token = localStorage.getItem('token') || ''
      const response = await axios.post(apiUrl('/v0/strategy'), form, {
        headers: {
          Authorization: token,
          'Content-Type': 'multipart/form-data',
        },
      })

      return {
        success: true,
        message: response.data?.message || `策略「${strategyName}」部署成功`,
        running: response.data?.running,
      }
    } catch (err: any) {
      const status = err.response?.status
      const data = err.response?.data
      if (status === 409 && data?.running) {
        return {
          success: false,
          conflict: true,
          message: data?.message || `策略「${strategyName}」正在运行，请先停止`,
          running: true,
        }
      }
      const message = errorMessage(err, '部署失败')
      lastError.value = message
      return { success: false, message }
    } finally {
      deploying.value = false
      setBusy(strategyName, false)
    }
  }

  /* ---------------------------------------------------------------------
   * 启停 / 删除
   * ------------------------------------------------------------------- */

  /** 启动策略（mode: 1） */
  async function start(name: string): Promise<StrategyOpResult> {
    return setMode(name, 1, '启动')
  }

  /** 停止策略（mode: 2） */
  async function stop(name: string): Promise<StrategyOpResult> {
    return setMode(name, 2, '停止')
  }

  async function setMode(name: string, mode: 1 | 2, label: string): Promise<StrategyOpResult> {
    lastError.value = null
    if (!name) {
      return { success: false, message: `${label}失败：策略名不能为空` }
    }
    const client = authedClient()
    if (!client) {
      return { success: false, message: NOT_LOGGED_IN }
    }
    setBusy(name, true)
    try {
      await client.post(apiUrl('/v0/strategy'), { mode, name })
      return {
        success: true,
        message: `策略「${name}」已${label}`,
        running: mode === 1,
      }
    } catch (err: any) {
      const message = errorMessage(err, `${label}失败`)
      lastError.value = message
      return { success: false, message }
    } finally {
      setBusy(name, false)
    }
  }

  /** 删除策略 */
  async function remove(name: string): Promise<StrategyOpResult> {
    lastError.value = null
    if (!name) {
      return { success: false, message: '删除失败：策略名不能为空' }
    }
    const client = authedClient()
    if (!client) {
      return { success: false, message: NOT_LOGGED_IN }
    }
    setBusy(name, true)
    try {
      await client.delete(apiUrl('/v0/strategy'), { data: { name } })
      return { success: true, message: `策略「${name}」已删除` }
    } catch (err: any) {
      const message = errorMessage(err, '删除失败')
      lastError.value = message
      return { success: false, message }
    } finally {
      setBusy(name, false)
    }
  }

  return {
    busyNames,
    deploying,
    lastError,
    isBusy,
    list,
    deploy,
    start,
    stop,
    remove,
  }
}
