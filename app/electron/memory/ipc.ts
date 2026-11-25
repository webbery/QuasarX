/**
 * 记忆 IPC handler 注册（主进程）
 *
 * 把 electron/memory/handlers.ts 的纯函数注册到 ipcMain。
 */

import { ipcMain } from 'electron'
import { MEMORY_CHANNELS } from '../../src/lib/memory/channels'
import type { MemoryHandlers } from './handlers'

/**
 * 注册记忆相关的 IPC channel
 */
export function registerMemoryIpcHandlers(handlers: MemoryHandlers): void {
  ipcMain.handle(MEMORY_CHANNELS.init, async () => handlers.init())

  ipcMain.handle(MEMORY_CHANNELS.upsert, async (_, args) => handlers.upsert(args))

  ipcMain.handle(MEMORY_CHANNELS.searchChannels, async (_, args) => handlers.searchChannels(args))

  ipcMain.handle(MEMORY_CHANNELS.listByBank, async (_, args) => handlers.listByBank(args))

  ipcMain.handle(MEMORY_CHANNELS.getByIds, async (_, args) => handlers.getByIds(args))

  ipcMain.handle(MEMORY_CHANNELS.deleteBySource, async (_, args) => handlers.deleteBySource(args))

  ipcMain.handle(MEMORY_CHANNELS.deleteByBank, async (_, args) => handlers.deleteByBank(args))
}
