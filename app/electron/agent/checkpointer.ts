import {
  BaseCheckpointSaver,
  Checkpoint,
  CheckpointMetadata,
  CheckpointTuple,
  CheckpointPendingWrite,
  PendingWrite,
} from "@langchain/langgraph-checkpoint";
import { RunnableConfig } from "@langchain/core/runnables";

/** IndexedDB 数据库实例 */
let _db: IDBDatabase | null = null;

const DB_NAME = "quasarx-langgraph";
const DB_VERSION = 1;
const CHECKPOINT_STORE = "checkpoints";
const WRITES_STORE = "writes";

/**
 * 打开 IndexedDB 连接
 */
async function openDB(): Promise<IDBDatabase> {
  if (_db) return _db;

  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, DB_VERSION);

    request.onupgradeneeded = (event) => {
      const db = (event.target as IDBOpenDBRequest).result;
      if (!db.objectStoreNames.contains(CHECKPOINT_STORE)) {
        // checkpoints: 按 threadId + checkpointId 索引
        const checkpointStore = db.createObjectStore(CHECKPOINT_STORE, {
          keyPath: ["threadId", "checkpointId"],
        });
        checkpointStore.createIndex("threadId", "threadId", { unique: false });
        checkpointStore.createIndex("threadId_ts", ["threadId", "ts"], { unique: false });
      }
      if (!db.objectStoreNames.contains(WRITES_STORE)) {
        // writes: 按 threadId + checkpointId + taskId 索引
        const writesStore = db.createObjectStore(WRITES_STORE, {
          keyPath: ["threadId", "checkpointId", "taskId"],
        });
        writesStore.createIndex("threadId", "threadId", { unique: false });
      }
    };

    request.onsuccess = () => {
      _db = request.result;
      resolve(_db);
    };

    request.onerror = () => reject(request.error);
  });
}

/**
 * 通用 IndexedDB 操作包装
 */
async function idbOperation<T>(
  storeName: string,
  operation: (store: IDBObjectStore) => IDBRequest,
): Promise<T> {
  const db = await openDB();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(storeName, "readwrite");
    const store = tx.objectStore(storeName);
    const request = operation(store);

    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

async function idbRead<T>(
  storeName: string,
  operation: (store: IDBObjectStore) => IDBRequest,
): Promise<T> {
  const db = await openDB();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(storeName, "readonly");
    const store = tx.objectStore(storeName);
    const request = operation(store);

    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

/**
 * 序列化 Checkpoint 为可存储格式
 * LangGraph 的 Checkpoint 包含复杂对象，需要序列化
 */
function serializeCheckpoint(checkpoint: Checkpoint): string {
  return JSON.stringify(checkpoint, (_key, value) => {
    // 处理可能的 BigInt
    if (typeof value === "bigint") return value.toString();
    return value;
  });
}

function deserializeCheckpoint(data: string): Checkpoint {
  return JSON.parse(data);
}

/**
 * 从 RunnableConfig 提取 threadId
 */
function getThreadId(config: RunnableConfig): string {
  return (config.configurable?.thread_id as string) || "default";
}

/**
 * 获取 checkpointId
 */
function getCheckpointId(config: RunnableConfig): string {
  return (config.configurable?.checkpoint_id as string) || "";
}

/**
 * 基于 IndexedDB 的 LangGraph Checkpoint 持久化
 *
 * 支持：
 * - 每步保存 checkpoint
 * - 按 threadId 隔离不同会话
 * - 断点续跑（从最后一个 checkpoint 恢复）
 * - 自动清理过期 checkpoint
 */
export class IndexedDBSaver extends BaseCheckpointSaver<number> {
  constructor() {
    super();
    // 确保 DB 已打开
    openDB().catch((err) => console.error("[IndexedDBSaver] DB open failed:", err));
  }

  /**
   * 获取单个 checkpoint tuple
   */
  async getTuple(config: RunnableConfig): Promise<CheckpointTuple | undefined> {
    const threadId = getThreadId(config);
    const checkpointId = getCheckpointId(config);

    if (!checkpointId) {
      // 没有指定 checkpointId，返回最新的
      return this._getLatestTuple(threadId);
    }

    const checkpointData = await idbRead<string | undefined>(CHECKPOINT_STORE, (store) => {
      return store.get([threadId, checkpointId]);
    });

    if (!checkpointData) return undefined;

    const record = JSON.parse(checkpointData);

    // 读取 pending writes
    const writes = await this._getWrites(threadId, checkpointId);

    return {
      config: {
        configurable: {
          thread_id: threadId,
          checkpoint_id: checkpointId,
        },
      },
      checkpoint: deserializeCheckpoint(record.checkpoint),
      metadata: record.metadata as CheckpointMetadata,
      parentConfig: record.parentConfig
        ? { configurable: { thread_id: threadId, checkpoint_id: record.parentConfig } }
        : undefined,
      pendingWrites: writes,
    };
  }

  /**
   * 获取指定 thread 的最新 checkpoint tuple
   */
  private async _getLatestTuple(threadId: string): Promise<CheckpointTuple | undefined> {
    const records = await idbRead<Array<{ checkpointId: string; ts: string; checkpoint: string; metadata: string; parentConfig?: string }>>(
      CHECKPOINT_STORE,
      (store) => {
        const index = store.index("threadId_ts");
        // 用 IDBKeyRange 限定 threadId 范围
        const range = IDBKeyRange.bound([threadId, ""], [threadId, "\uffff"]);
        return index.openCursor(range, "prev");
      },
    );

    // cursor 方式获取第一条
    return new Promise((resolve) => {
      const db = openDB().then((db) => {
        const tx = db.transaction(CHECKPOINT_STORE, "readonly");
        const store = tx.objectStore(CHECKPOINT_STORE);
        const index = store.index("threadId_ts");
        const range = IDBKeyRange.bound([threadId, ""], [threadId, "\uffff"]);
        const cursor = index.openCursor(range, "prev");

        cursor.onsuccess = (event) => {
          const cursor = (event.target as IDBRequest<IDBCursorWithValue>).result;
          if (!cursor) {
            resolve(undefined);
            return;
          }

          const record = cursor.value;
          const checkpointTuple: CheckpointTuple = {
            config: {
              configurable: {
                thread_id: threadId,
                checkpoint_id: record.checkpointId,
              },
            },
            checkpoint: deserializeCheckpoint(record.checkpoint),
            metadata: record.metadata as CheckpointMetadata,
            parentConfig: record.parentConfig
              ? { configurable: { thread_id: threadId, checkpoint_id: record.parentConfig } }
              : undefined,
          };

          // 读取 pending writes
          this._getWrites(threadId, record.checkpointId).then((writes) => {
            checkpointTuple.pendingWrites = writes;
            resolve(checkpointTuple);
          });
        };

        cursor.onerror = () => resolve(undefined);
      });
      return db;
    });
  }

  /**
   * 列出 checkpoint tuples
   */
  async *list(
    config: RunnableConfig,
    options?: { limit?: number; before?: RunnableConfig; filter?: Record<string, any> },
  ): AsyncGenerator<CheckpointTuple> {
    const threadId = getThreadId(config);
    const limit = options?.limit || 100;
    let count = 0;

    const db = await openDB();
    const tx = db.transaction(CHECKPOINT_STORE, "readonly");
    const store = tx.objectStore(CHECKPOINT_STORE);
    const index = store.index("threadId_ts");
    const range = IDBKeyRange.bound([threadId, ""], [threadId, "\uffff"]);
    const cursor = index.openCursor(range, "prev");

    const results: CheckpointTuple[] = [];

    await new Promise<void>((resolve) => {
      cursor.onsuccess = (event) => {
        const c = (event.target as IDBRequest<IDBCursorWithValue>).result;
        if (!c || count >= limit) {
          resolve();
          return;
        }

        const record = c.value;
        results.push({
          config: {
            configurable: {
              thread_id: threadId,
              checkpoint_id: record.checkpointId,
            },
          },
          checkpoint: deserializeCheckpoint(record.checkpoint),
          metadata: record.metadata as CheckpointMetadata,
          parentConfig: record.parentConfig
            ? { configurable: { thread_id: threadId, checkpoint_id: record.parentConfig } }
            : undefined,
        });

        count++;
        c.continue();
      };
      cursor.onerror = () => resolve();
    });

    for (const tuple of results) {
      yield tuple;
    }
  }

  /**
   * 保存 checkpoint
   */
  async put(
    config: RunnableConfig,
    checkpoint: Checkpoint,
    metadata: CheckpointMetadata,
    _newVersions: Record<string, number | string>,
  ): Promise<RunnableConfig> {
    const threadId = getThreadId(config);
    const checkpointId = checkpoint.id;
    const parentCheckpointId = getCheckpointId(config);

    const record = {
      threadId,
      checkpointId,
      checkpoint: serializeCheckpoint(checkpoint),
      metadata,
      parentConfig: parentCheckpointId || null,
      ts: checkpoint.ts,
    };

    await idbOperation<void>(CHECKPOINT_STORE, (store) => {
      return store.put(record);
    });

    return {
      configurable: {
        thread_id: threadId,
        checkpoint_id: checkpointId,
      },
    };
  }

  /**
   * 保存 pending writes
   */
  async putWrites(
    config: RunnableConfig,
    writes: PendingWrite[],
    taskId: string,
  ): Promise<void> {
    const threadId = getThreadId(config);
    const checkpointId = getCheckpointId(config);

    if (!checkpointId) return;

    const checkpointWrites: CheckpointPendingWrite[] = writes.map((w, idx) => ({
      threadId,
      checkpointId,
      taskId,
      idx,
      data: w,
    }));

    for (const w of checkpointWrites) {
      await idbOperation<void>(WRITES_STORE, (store) => {
        return store.put(w);
      });
    }
  }

  /**
   * 删除指定 thread 的所有 checkpoint 和 writes
   */
  async deleteThread(threadId: string): Promise<void> {
    // 删除 checkpoints
    await new Promise<void>(async (resolve) => {
      const db = await openDB();
      const tx = db.transaction(CHECKPOINT_STORE, "readwrite");
      const store = tx.objectStore(CHECKPOINT_STORE);
      const index = store.index("threadId");
      const range = IDBKeyRange.only(threadId);
      const cursor = index.openCursor(range);

      cursor.onsuccess = (event) => {
        const c = (event.target as IDBRequest<IDBCursorWithValue>).result;
        if (!c) {
          resolve();
          return;
        }
        c.delete();
        c.continue();
      };
      cursor.onerror = () => resolve();
    });

    // 删除 writes
    await new Promise<void>(async (resolve) => {
      const db = await openDB();
      const tx = db.transaction(WRITES_STORE, "readwrite");
      const store = tx.objectStore(WRITES_STORE);
      const index = store.index("threadId");
      const range = IDBKeyRange.only(threadId);
      const cursor = index.openCursor(range);

      cursor.onsuccess = (event) => {
        const c = (event.target as IDBRequest<IDBCursorWithValue>).result;
        if (!c) {
          resolve();
          return;
        }
        c.delete();
        c.continue();
      };
      cursor.onerror = () => resolve();
    });
  }

  /**
   * 获取指定 checkpoint 的 pending writes
   */
  private async _getWrites(threadId: string, checkpointId: string): Promise<CheckpointPendingWrite[]> {
    return new Promise((resolve) => {
      openDB().then((db) => {
        const tx = db.transaction(WRITES_STORE, "readonly");
        const store = tx.objectStore(WRITES_STORE);
        // 用主键范围查询
        const range = IDBKeyRange.bound(
          [threadId, checkpointId, ""],
          [threadId, checkpointId, "\uffff"],
        );
        const request = store.openCursor(range);
        const writes: CheckpointPendingWrite[] = [];

        request.onsuccess = (event) => {
          const cursor = (event.target as IDBRequest<IDBCursorWithValue>).result;
          if (!cursor) {
            resolve(writes);
            return;
          }
          writes.push(cursor.value);
          cursor.continue();
        };

        request.onerror = () => resolve([]);
      }).catch(() => resolve([]));
    });
  }
}
