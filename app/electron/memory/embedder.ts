/**
 * 记忆模块的嵌入器
 *
 * 与 electron/vectorDB.ts 使用同一个模型与同一个磁盘缓存目录，
 * 但各自持有 extractor 实例（同一进程内会加载两份量化模型，磁盘缓存共享，不会重复下载）。
 *
 * 模型下载源由 env.remoteHost 决定：默认走 hf-mirror.com，
 * 镜像失败时回退 huggingface.co 再试一次。
 */

import { pipeline, env } from '@xenova/transformers'
import { existsSync } from 'fs'
import { join } from 'path'
import { EMBEDDING_MODEL, EMBEDDING_DIM } from './schema'

/** 镜像源 */
export const DEFAULT_REMOTE_HOST = 'https://hf-mirror.com/'

/** 官方源 */
export const OFFICIAL_REMOTE_HOST = 'https://huggingface.co/'

let extractor: any = null
let extractorHost: string | null = null
let extractorCacheDir: string | null = null

/** 默认模型缓存目录 */
export function defaultCacheDir(): string {
  return join(process.cwd(), 'knowledge', '.model-cache')
}

/**
 * 配置 transformers.js 环境（须在首次 embed 之前调用）
 */
export function configureEmbeddingEnv(opts: { cacheDir?: string; remoteHost?: string }): void {
  env.cacheDir = opts.cacheDir ?? defaultCacheDir()
  env.allowLocalModels = true
  env.allowRemoteModels = true
  env.remoteHost = opts.remoteHost ?? DEFAULT_REMOTE_HOST
}

/** 模型是否已缓存 */
export function isModelCached(cacheDir: string): boolean {
  const onnxDir = join(cacheDir, EMBEDDING_MODEL, 'onnx')
  return existsSync(join(onnxDir, 'model_quantized.onnx')) || existsSync(join(onnxDir, 'model.onnx'))
}

/** 加载（或复用）extractor */
async function getExtractor(): Promise<any> {
  if (extractor) return extractor

  const cacheDir = env.cacheDir as string
  const host = (env.remoteHost as string) || DEFAULT_REMOTE_HOST

  try {
    extractor = await pipeline('feature-extraction', EMBEDDING_MODEL, {
      quantized: true,
      cache_dir: cacheDir,
    })
  } catch (err) {
    if (host === OFFICIAL_REMOTE_HOST) throw err
    console.warn(
      `[Memory/Embedder] 镜像源加载失败，回退官方源：${err instanceof Error ? err.message : String(err)}`,
    )
    env.remoteHost = OFFICIAL_REMOTE_HOST
    extractor = await pipeline('feature-extraction', EMBEDDING_MODEL, {
      quantized: true,
      cache_dir: cacheDir,
    })
  }

  extractorHost = env.remoteHost as string
  extractorCacheDir = cacheDir
  return extractor
}

/** 生成单条文本的向量（归一化） */
export async function embedText(text: string): Promise<number[]> {
  const ext = await getExtractor()
  const output = await ext(text, { pooling: 'mean', normalize: true })
  const vec = Array.from(output.data as ArrayLike<number>) as number[]
  if (vec.length !== EMBEDDING_DIM) {
    throw new Error(`嵌入维度不符：期望 ${EMBEDDING_DIM}，实际 ${vec.length}`)
  }
  return vec
}

/** 批量生成向量 */
export async function embedTexts(texts: string[]): Promise<number[][]> {
  const out: number[][] = []
  for (const t of texts) out.push(await embedText(t))
  return out
}

/** 释放 extractor */
export function releaseEmbedder(): void {
  extractor = null
  extractorHost = null
  extractorCacheDir = null
}

/** extractor 状态（诊断用） */
export function embedderInfo(): { host: string | null; cacheDir: string | null; loaded: boolean } {
  return { host: extractorHost, cacheDir: extractorCacheDir, loaded: !!extractor }
}
