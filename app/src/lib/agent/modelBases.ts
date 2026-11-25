/**
 * Agent 模型基底
 *
 * 一套「基底」= 一个服务商的完整连接信息（URL / 协议 / API Key / 模型名）。
 * 预设固定为 DeepSeek / Qwen / MiniMax / 自定义，用户只需分别填写各自的 Key，
 * 在聊天输入框旁一键切换。
 *
 * 存储与 AgentConfig 分开：
 * - 旧的单配置 `quasarx_agent_config` 首次读取时自动迁移成最匹配的基底；
 * - `getAgentConfig()` 仍返回当前 active 基底，兼容知识库等旧调用方。
 */

export type ModelProtocol = 'openai' | 'anthropic' | 'custom'
export type ModelBaseId = 'deepseek' | 'qwen' | 'minimax' | 'custom'

export interface ModelBasePreset {
  id: ModelBaseId
  label: string
  protocol: ModelProtocol
  defaultUrl: string
  defaultModel: string
  /** 该基底常用模型名（设置页下拉提示用） */
  models: string[]
}

export interface ModelBaseConfig {
  presetId: ModelBaseId
  url: string
  protocol: ModelProtocol
  model: string
  apiKey: string
}

export interface ModelBaseState {
  bases: ModelBaseConfig[]
  activeId: ModelBaseId
}

interface StoredModelBaseState {
  version?: number
  bases?: Array<Partial<ModelBaseConfig> & { presetId?: string }>
  activeId?: string
}

interface LegacyAgentConfig {
  url?: string
  protocol?: string
  apiKey?: string
  model?: string
}

const STORAGE_KEY = 'quasarx_agent_model_bases_v1'
const LEGACY_STORAGE_KEY = 'quasarx_agent_config'

export const MODEL_BASE_PRESETS: ModelBasePreset[] = [
  {
    id: 'deepseek',
    label: 'DeepSeek',
    protocol: 'openai',
    defaultUrl: 'https://api.deepseek.com',
    defaultModel: 'deepseek-chat',
    models: ['deepseek-chat', 'deepseek-reasoner'],
  },
  {
    id: 'qwen',
    label: 'Qwen',
    protocol: 'openai',
    defaultUrl: 'https://dashscope.aliyuncs.com/compatible-mode/v1',
    defaultModel: 'qwen-plus',
    models: ['qwen-max', 'qwen-plus', 'qwen-turbo'],
  },
  {
    id: 'minimax',
    label: 'MiniMax',
    protocol: 'openai',
    defaultUrl: 'https://api.minimax.chat/v1',
    defaultModel: 'MiniMax-Text-01',
    models: ['MiniMax-Text-01', 'abab6.5s-chat'],
  },
  {
    id: 'custom',
    label: '自定义',
    protocol: 'openai',
    defaultUrl: '',
    defaultModel: '',
    models: [],
  },
]

const PRESET_BY_ID = new Map(MODEL_BASE_PRESETS.map((preset) => [preset.id, preset]))

export function getModelBasePreset(id: ModelBaseId): ModelBasePreset {
  return PRESET_BY_ID.get(id) ?? PRESET_BY_ID.get('custom')!
}

/** 基底是否已可用于对话 */
export function isBaseReady(base: ModelBaseConfig): boolean {
  return !!(base.url.trim() && base.model.trim() && base.apiKey.trim())
}

function normalizeProtocol(value: unknown): ModelProtocol {
  return value === 'anthropic' || value === 'custom' ? value : 'openai'
}

function normalizeBase(
  preset: ModelBasePreset,
  saved?: Partial<ModelBaseConfig>,
): ModelBaseConfig {
  return {
    presetId: preset.id,
    url: typeof saved?.url === 'string' ? saved.url : preset.defaultUrl,
    protocol: normalizeProtocol(saved?.protocol ?? preset.protocol),
    model: typeof saved?.model === 'string' ? saved.model : preset.defaultModel,
    apiKey: typeof saved?.apiKey === 'string' ? saved.apiKey : '',
  }
}

function readJson<T>(key: string): T | null {
  try {
    const raw = localStorage.getItem(key)
    return raw ? (JSON.parse(raw) as T) : null
  } catch (e) {
    console.warn(`[ModelBases] 读取 ${key} 失败`, e)
    return null
  }
}

function guessPresetId(legacy: LegacyAgentConfig): ModelBaseId {
  const url = (legacy.url ?? '').toLowerCase()
  const model = (legacy.model ?? '').toLowerCase()
  if (url.includes('deepseek') || model.includes('deepseek')) return 'deepseek'
  if (
    url.includes('dashscope') ||
    url.includes('aliyun') ||
    model.startsWith('qwen')
  ) {
    return 'qwen'
  }
  if (
    url.includes('minimax') ||
    model.includes('minimax') ||
    model.startsWith('abab')
  ) {
    return 'minimax'
  }
  return 'custom'
}

function pickDefaultActive(bases: ModelBaseConfig[]): ModelBaseId {
  return bases.find((base) => isBaseReady(base))?.presetId ?? MODEL_BASE_PRESETS[0].id
}

/** 读取完整基底状态（首次读取时迁移旧的单配置） */
export function loadModelBaseState(): ModelBaseState {
  const stored = readJson<StoredModelBaseState>(STORAGE_KEY)
  const savedBases = stored && Array.isArray(stored.bases) ? stored.bases : []

  const bases = MODEL_BASE_PRESETS.map((preset) => {
    const saved = savedBases.find((b) => b?.presetId === preset.id) as
      | Partial<ModelBaseConfig>
      | undefined
    return normalizeBase(preset, saved)
  })

  if (!stored) {
    const legacy = readJson<LegacyAgentConfig>(LEGACY_STORAGE_KEY)
    if (legacy) {
      const presetId = guessPresetId(legacy)
      const preset = getModelBasePreset(presetId)
      const index = bases.findIndex((base) => base.presetId === presetId)
      if (index >= 0) {
        bases[index] = {
          presetId,
          url: legacy.url || preset.defaultUrl,
          protocol: normalizeProtocol(legacy.protocol ?? preset.protocol),
          model: legacy.model || preset.defaultModel,
          apiKey: legacy.apiKey || '',
        }
      }
      return { bases, activeId: presetId }
    }
  }

  const activeId =
    stored?.activeId && bases.some((base) => base.presetId === stored.activeId)
      ? (stored.activeId as ModelBaseId)
      : pickDefaultActive(bases)

  return { bases, activeId }
}

export function saveModelBaseState(state: ModelBaseState): void {
  try {
    localStorage.setItem(
      STORAGE_KEY,
      JSON.stringify({
        version: 1,
        bases: state.bases,
        activeId: state.activeId,
      }),
    )
  } catch (e) {
    console.warn('[ModelBases] 保存模型基底失败', e)
  }
}

/** 当前选中的基底 */
export function getActiveModelBase(): ModelBaseConfig | null {
  const state = loadModelBaseState()
  return state.bases.find((base) => base.presetId === state.activeId) ?? state.bases[0] ?? null
}

/** 切换 active 基底 */
export function setActiveModelBase(id: ModelBaseId): ModelBaseState {
  const state = loadModelBaseState()
  if (state.bases.some((base) => base.presetId === id)) {
    state.activeId = id
    saveModelBaseState(state)
  }
  return state
}

/** 新增或更新一套基底配置 */
export function updateModelBase(config: ModelBaseConfig): ModelBaseState {
  const state = loadModelBaseState()
  const index = state.bases.findIndex((base) => base.presetId === config.presetId)
  if (index >= 0) state.bases[index] = { ...config }
  else state.bases.push({ ...config })
  saveModelBaseState(state)
  return state
}

/** 恢复某个基底的预设默认值（清空 Key） */
export function resetModelBase(id: ModelBaseId): ModelBaseState {
  const state = loadModelBaseState()
  const preset = getModelBasePreset(id)
  const index = state.bases.findIndex((base) => base.presetId === id)
  if (index >= 0) state.bases[index] = normalizeBase(preset)
  saveModelBaseState(state)
  return state
}
