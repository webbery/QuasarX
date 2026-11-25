/**
 * IPC 记忆存储适配器
 *
 * 通过可注入的 transport 与主进程通信；本文件不 import electron。
 */

import { MEMORY_CHANNELS } from '../channels'
import type { ChannelHits, MemoryStore, MemoryUnit, SearchOptions } from '../types'

/**
 * IPC 传输层
 *
 * 生产：ipcRenderer.invoke 包装；bench：直连主进程 handler 函数。
 */
export interface MemoryTransport {
  invoke(channel: string, args: unknown): Promise<any>
}

/** 主进程 handler 的返回形状 */
interface HandlerResponse<T> {
  success: boolean
  data?: T
  error?: string
}

function unwrap<T>(resp: HandlerResponse<T>): T {
  if (!resp || !resp.success) {
    throw new Error(resp?.error ?? '[Memory] IPC 调用失败')
  }
  return resp.data as T
}

/**
 * 构造走 IPC 的记忆存储适配器
 */
export function createIpcMemoryStore(transport: MemoryTransport): MemoryStore {
  return {
    async init(): Promise<void> {
      unwrap(await transport.invoke(MEMORY_CHANNELS.init, {}))
    },

    async upsert(units: MemoryUnit[]): Promise<void> {
      unwrap(await transport.invoke(MEMORY_CHANNELS.upsert, { units }))
    },

    async deleteBySource(sourceTurnId: string, bankId?: string): Promise<number> {
      const r = unwrap<{ deleted: number }>(
        await transport.invoke(MEMORY_CHANNELS.deleteBySource, { sourceTurnId, bankId }),
      )
      return r?.deleted ?? 0
    },

    async deleteByBank(bankId: string): Promise<number> {
      const r = unwrap<{ deleted: number }>(
        await transport.invoke(MEMORY_CHANNELS.deleteByBank, { bankId }),
      )
      return r?.deleted ?? 0
    },

    async searchChannels(opts: SearchOptions): Promise<ChannelHits[]> {
      return (unwrap(await transport.invoke(MEMORY_CHANNELS.searchChannels, opts)) ?? []) as ChannelHits[]
    },

    async listByBank(bankId: string): Promise<MemoryUnit[]> {
      return (unwrap(await transport.invoke(MEMORY_CHANNELS.listByBank, { bankId })) ?? []) as MemoryUnit[]
    },

    async getByIds(ids: string[]): Promise<MemoryUnit[]> {
      return (unwrap(await transport.invoke(MEMORY_CHANNELS.getByIds, { ids })) ?? []) as MemoryUnit[]
    },

    async close(): Promise<void> {
      // 主进程持有连接，渲染进程不负责关闭
    },
  }
}
