/**
 * 对话持久化（IndexedDB）
 *
 * 为什么不用 localStorage：
 * - 图表产物带完整时间序列，几百个点就能撑爆 ~5MB 配额
 * - localStorage 是同步的，大对象写入会阻塞渲染线程
 *
 * 库结构（quasarx-chat v1）：
 * | store      | keyPath | 内容                        |
 * |------------|---------|-----------------------------|
 * | sessions   | id      | ChatSession（含消息树）      |
 * | artifacts  | id      | ArtifactEnvelope            |
 * | meta       | key     | { key, value } 杂项标记     |
 *
 * 裸 IndexedDB 实现，与 lib/agent/checkpointer.ts 同一风格，不引新依赖。
 */

import type { ArtifactEnvelope } from './artifacts'
import type { ChatSession } from '@/stores/chatStore'

const DB_NAME = 'quasarx-chat'
const DB_VERSION = 1

export const CHAT_STORES = {
  SESSIONS: 'sessions',
  ARTIFACTS: 'artifacts',
  META: 'meta',
} as const

/** meta 键：标记已从 localStorage 导入过，避免重复导入 */
export const META_KEYS = {
  MIGRATED_FROM_LOCAL_STORAGE: 'migratedFromLocalStorage',
} as const

let _db: IDBDatabase | null = null
let _dbPromise: Promise<IDBDatabase> | null = null

/** 打开（并缓存）数据库连接 */
export function openChatDb(): Promise<IDBDatabase> {
  if (_db) return Promise.resolve(_db)
  if (_dbPromise) return _dbPromise

  _dbPromise = new Promise<IDBDatabase>((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, DB_VERSION)

    request.onupgradeneeded = () => {
      const db = request.result
      if (!db.objectStoreNames.contains(CHAT_STORES.SESSIONS)) {
        db.createObjectStore(CHAT_STORES.SESSIONS, { keyPath: 'id' })
      }
      if (!db.objectStoreNames.contains(CHAT_STORES.ARTIFACTS)) {
        db.createObjectStore(CHAT_STORES.ARTIFACTS, { keyPath: 'id' })
      }
      if (!db.objectStoreNames.contains(CHAT_STORES.META)) {
        db.createObjectStore(CHAT_STORES.META, { keyPath: 'key' })
      }
    }

    request.onsuccess = () => {
      _db = request.result
      resolve(request.result)
    }

    request.onerror = () => {
      _dbPromise = null
      reject(request.error)
    }
  })

  return _dbPromise
}

/** 事务写操作 */
async function write<T>(
  storeName: string,
  operation: (store: IDBObjectStore) => IDBRequest,
): Promise<T> {
  const db = await openChatDb()
  return new Promise<T>((resolve, reject) => {
    const tx = db.transaction(storeName, 'readwrite')
    const store = tx.objectStore(storeName)
    let result: T
    const request = operation(store)
    request.onsuccess = () => {
      result = request.result
    }
    request.onerror = () => reject(request.error)
    tx.oncomplete = () => resolve(result)
    tx.onerror = () => reject(tx.error)
    tx.onabort = () => reject(tx.error)
  })
}

/** 事务读操作 */
async function read<T>(
  storeName: string,
  operation: (store: IDBObjectStore) => IDBRequest,
): Promise<T> {
  const db = await openChatDb()
  return new Promise<T>((resolve, reject) => {
    const tx = db.transaction(storeName, 'readonly')
    const store = tx.objectStore(storeName)
    const request = operation(store)
    request.onsuccess = () => resolve(request.result as T)
    request.onerror = () => reject(request.error)
  })
}

/* ============================================================================
 * 会话
 * ========================================================================== */

/** 写入（覆盖）一个会话 */
export async function putSession(session: ChatSession): Promise<void> {
  await write(CHAT_STORES.SESSIONS, (store) => store.put(session))
}

/** 读取全部会话 */
export async function getAllSessions(): Promise<ChatSession[]> {
  return read<ChatSession[]>(CHAT_STORES.SESSIONS, (store) => store.getAll())
}

/** 删除一个会话 */
export async function deleteSession(id: string): Promise<void> {
  await write(CHAT_STORES.SESSIONS, (store) => store.delete(id))
}

/* ============================================================================
 * 产物
 * ========================================================================== */

/** 写入（覆盖）一个产物 */
export async function putArtifact(artifact: ArtifactEnvelope): Promise<void> {
  await write(CHAT_STORES.ARTIFACTS, (store) => store.put(artifact))
}

/** 读取全部产物 */
export async function getAllArtifacts(): Promise<ArtifactEnvelope[]> {
  return read<ArtifactEnvelope[]>(CHAT_STORES.ARTIFACTS, (store) => store.getAll())
}

/** 批量删除产物 */
export async function deleteArtifacts(ids: string[]): Promise<void> {
  if (!ids.length) return
  const db = await openChatDb()
  await new Promise<void>((resolve, reject) => {
    const tx = db.transaction(CHAT_STORES.ARTIFACTS, 'readwrite')
    const store = tx.objectStore(CHAT_STORES.ARTIFACTS)
    for (const id of ids) store.delete(id)
    tx.oncomplete = () => resolve()
    tx.onerror = () => reject(tx.error)
    tx.onabort = () => reject(tx.error)
  })
}

/* ============================================================================
 * meta
 * ========================================================================== */

export async function getMeta<T = unknown>(key: string): Promise<T | undefined> {
  const record = await read<{ key: string; value: T } | undefined>(
    CHAT_STORES.META,
    (store) => store.get(key),
  )
  return record?.value
}

export async function setMeta(key: string, value: unknown): Promise<void> {
  await write(CHAT_STORES.META, (store) => store.put({ key, value }))
}

/** 清空全部对话数据（用于「清空所有会话」之类的显式操作） */
export async function clearChatDb(): Promise<void> {
  const db = await openChatDb()
  await new Promise<void>((resolve, reject) => {
    const tx = db.transaction(
      [CHAT_STORES.SESSIONS, CHAT_STORES.ARTIFACTS],
      'readwrite',
    )
    tx.objectStore(CHAT_STORES.SESSIONS).clear()
    tx.objectStore(CHAT_STORES.ARTIFACTS).clear()
    tx.oncomplete = () => resolve()
    tx.onerror = () => reject(tx.error)
    tx.onabort = () => reject(tx.error)
  })
}
