/**
 * 记忆存储适配器的注册与获取
 *
 * 具体适配器由宿主环境注入，记忆模块本身不 import 存储实现。
 */

import type { MemoryStore } from './types'
import { createIpcMemoryStore } from './adapters/ipc'
import { createElectronTransport } from './adapters/electron'

let registered: MemoryStore | null = null

/**
 * 注册记忆存储适配器。由宿主环境在启动时调用。
 */
export function registerMemoryStore(store: MemoryStore): void {
  registered = store
}

/** 取消注册（测试与重置用） */
export function resetMemoryStore(): void {
  registered = null
}

/** 是否运行在 Electron 渲染进程 */
export function isElectronRenderer(): boolean {
  return typeof process !== 'undefined' && !!process.versions?.electron
}

/**
 * 获取记忆存储适配器
 *
 * 优先返回已注册的适配器；Electron 渲染进程下回落到 IPC 适配器；
 * 其他环境未注册时抛错。
 */
export function getMemoryStore(): MemoryStore {
  if (registered) return registered
  if (isElectronRenderer()) {
    registered = createIpcMemoryStore(createElectronTransport())
    return registered
  }
  throw new Error(
    '[Memory] 未注册 MemoryStore：非 Electron 环境需要宿主显式调用 registerMemoryStore()',
  )
}
