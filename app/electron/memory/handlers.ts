/**
 * 记忆 IPC handler —— 纯函数，不依赖 electron
 *
 * electron/main.ts 用 ipcMain.handle 包一层注册；
 * Node bench 直接调用同一批函数。
 * 返回值统一是 { success, ... } 形状。
 */

import type { ChannelHits, MemoryStore, MemoryUnit, SearchOptions } from '../../src/lib/memory/types'
export { MEMORY_CHANNELS } from '../../src/lib/memory/channels'

/** 统一响应形状 */
export interface HandlerResult<T = unknown> {
  success: boolean
  data?: T
  error?: string
}

/** handler 集合 */
export interface MemoryHandlers {
  init(): Promise<HandlerResult>
  upsert(args: { units: MemoryUnit[] }): Promise<HandlerResult>
  searchChannels(args: SearchOptions): Promise<HandlerResult<ChannelHits[]>>
  listByBank(args: { bankId: string }): Promise<HandlerResult<MemoryUnit[]>>
  getByIds(args: { ids: string[] }): Promise<HandlerResult<MemoryUnit[]>>
  deleteBySource(args: { sourceTurnId: string; bankId?: string }): Promise<HandlerResult<{ deleted: number }>>
  deleteByBank(args: { bankId: string }): Promise<HandlerResult<{ deleted: number }>>
}

function ok<T>(data: T): HandlerResult<T> {
  return { success: true, data }
}

function fail<T>(err: unknown): HandlerResult<T> {
  return { success: false, error: err instanceof Error ? err.message : String(err) }
}

/**
 * 用一个 store 实例构造 handler 集合
 */
export function createMemoryHandlers(store: MemoryStore): MemoryHandlers {
  return {
    async init() {
      try {
        await store.init()
        return ok(undefined)
      } catch (e) {
        return fail(e)
      }
    },

    async upsert({ units }) {
      try {
        await store.upsert(units ?? [])
        return ok(undefined)
      } catch (e) {
        return fail(e)
      }
    },

    async searchChannels(opts) {
      try {
        const hits = await store.searchChannels(opts)
        return ok(hits)
      } catch (e) {
        return fail(e)
      }
    },

    async listByBank({ bankId }) {
      try {
        const units = await store.listByBank(bankId)
        return ok(units)
      } catch (e) {
        return fail(e)
      }
    },

    async getByIds({ ids }) {
      try {
        const units = await store.getByIds(ids ?? [])
        return ok(units)
      } catch (e) {
        return fail(e)
      }
    },

    async deleteBySource({ sourceTurnId, bankId }) {
      try {
        const deleted = await store.deleteBySource(sourceTurnId, bankId)
        return ok({ deleted })
      } catch (e) {
        return fail(e)
      }
    },

    async deleteByBank({ bankId }) {
      try {
        const deleted = await store.deleteByBank(bankId)
        return ok({ deleted })
      } catch (e) {
        return fail(e)
      }
    },
  }
}
