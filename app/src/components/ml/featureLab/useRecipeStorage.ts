// 配方集 / 收集快照 / 门禁运行记录的本地存储
// 独立数据库，避免改动现有 QuasarX（stores/history.ts）的版本号。

import { ref } from 'vue'
import type { FeatureSnapshot, GateRun, RecipeSet } from './types'

const DB_NAME = 'QuasarXFeatureLab'
const DB_VERSION = 1
const STORES = {
  SETS: 'recipe_sets',
  SNAPSHOTS: 'feature_snapshots',
  RUNS: 'gate_runs',
} as const

const MAX_RECORDS = 200

let dbPromise: Promise<IDBDatabase> | null = null

function getDB(): Promise<IDBDatabase> {
  if (dbPromise) return dbPromise
  dbPromise = new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, DB_VERSION)
    req.onerror = () => reject(req.error)
    req.onsuccess = () => resolve(req.result)
    req.onupgradeneeded = () => {
      const db = req.result
      if (!db.objectStoreNames.contains(STORES.SETS)) {
        const store = db.createObjectStore(STORES.SETS, { keyPath: 'id' })
        store.createIndex('updatedAt', 'updatedAt', { unique: false })
      }
      if (!db.objectStoreNames.contains(STORES.SNAPSHOTS)) {
        const store = db.createObjectStore(STORES.SNAPSHOTS, { keyPath: 'id' })
        store.createIndex('createdAt', 'createdAt', { unique: false })
      }
      if (!db.objectStoreNames.contains(STORES.RUNS)) {
        const store = db.createObjectStore(STORES.RUNS, { keyPath: 'id' })
        store.createIndex('createdAt', 'createdAt', { unique: false })
      }
    }
  })
  return dbPromise
}

async function dbGetAll<T>(storeName: string): Promise<T[]> {
  const db = await getDB()
  return new Promise((resolve, reject) => {
    const req = db.transaction(storeName, 'readonly').objectStore(storeName).getAll()
    req.onerror = () => reject(req.error)
    req.onsuccess = () => resolve(req.result as T[])
  })
}

async function dbPut(storeName: string, value: any): Promise<void> {
  const db = await getDB()
  return new Promise((resolve, reject) => {
    const tx = db.transaction(storeName, 'readwrite')
    tx.objectStore(storeName).put(value)
    tx.onerror = () => reject(tx.error)
    tx.oncomplete = () => resolve()
  })
}

async function dbDelete(storeName: string, id: string): Promise<void> {
  const db = await getDB()
  return new Promise((resolve, reject) => {
    const tx = db.transaction(storeName, 'readwrite')
    tx.objectStore(storeName).delete(id)
    tx.onerror = () => reject(tx.error)
    tx.oncomplete = () => resolve()
  })
}

function sortNewest<T extends { createdAt?: string; updatedAt?: string }>(rows: T[]): T[] {
  return [...rows].sort((a, b) => {
    const ta = a.updatedAt || a.createdAt || ''
    const tb = b.updatedAt || b.createdAt || ''
    return tb.localeCompare(ta)
  })
}

async function prune(storeName: string): Promise<void> {
  const rows = sortNewest(await dbGetAll<any>(storeName))
  const extra = rows.slice(MAX_RECORDS)
  for (const row of extra) await dbDelete(storeName, row.id)
}

export function useRecipeStorage() {
  const sets = ref<RecipeSet[]>([])
  const snapshots = ref<FeatureSnapshot[]>([])
  const runs = ref<GateRun[]>([])
  const ready = ref(false)

  async function refreshSets() {
    sets.value = sortNewest(await dbGetAll<RecipeSet>(STORES.SETS))
  }
  async function refreshSnapshots() {
    snapshots.value = sortNewest(await dbGetAll<FeatureSnapshot>(STORES.SNAPSHOTS))
  }
  async function refreshRuns() {
    runs.value = sortNewest(await dbGetAll<GateRun>(STORES.RUNS))
  }

  async function refresh() {
    try {
      await Promise.all([refreshSets(), refreshSnapshots(), refreshRuns()])
    } finally {
      ready.value = true
    }
  }

  async function saveSet(set: RecipeSet): Promise<void> {
    set.updatedAt = new Date().toISOString()
    await dbPut(STORES.SETS, JSON.parse(JSON.stringify(set)))
    await prune(STORES.SETS)
    await refreshSets()
  }

  async function deleteSet(id: string): Promise<void> {
    await dbDelete(STORES.SETS, id)
    await refreshSets()
  }

  async function addSnapshot(snapshot: FeatureSnapshot): Promise<void> {
    await dbPut(STORES.SNAPSHOTS, JSON.parse(JSON.stringify(snapshot)))
    await prune(STORES.SNAPSHOTS)
    await refreshSnapshots()
  }

  async function addRun(run: GateRun): Promise<void> {
    await dbPut(STORES.RUNS, JSON.parse(JSON.stringify(run)))
    await prune(STORES.RUNS)
    await refreshRuns()
  }

  async function deleteRun(id: string): Promise<void> {
    await dbDelete(STORES.RUNS, id)
    await refreshRuns()
  }

  function snapshotById(id: string): FeatureSnapshot | undefined {
    return snapshots.value.find(s => s.id === id)
  }

  return {
    sets,
    snapshots,
    runs,
    ready,
    refresh,
    refreshSets,
    refreshSnapshots,
    refreshRuns,
    saveSet,
    deleteSet,
    addSnapshot,
    addRun,
    deleteRun,
    snapshotById,
  }
}
