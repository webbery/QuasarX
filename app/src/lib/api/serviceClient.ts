/**
 * service API 共享底层：URL 解析 / 鉴权 / 错误提取
 *
 * 这三件事此前散落在多处，各写一遍，导致登录态、remote 配置、错误文案经常对不上：
 *   - composables/useStrategyControl.ts
 *   - components/strategy/stability/stabilityApi.ts
 *   - components/strategy/composables/useBacktest.js
 *   - lib/tools/strategy.ts
 *
 * 上层 composable（useStrategyControl / useBacktestControl）只管自己的业务方法，
 * 不再各写一遍 URL 拼接与 Authorization。
 */

import axios from 'axios'
import { applyAuthHeader } from '@/lib/tools/common/auth'

/** 未登录时的统一提示 */
export const NOT_LOGGED_IN = '未登录，无法访问 service（本地没有 token）'

/**
 * API base 解析，按优先级：
 *   1. axios.defaults.baseURL —— 登录后由 LoginForm 设置（形如 https://host:port）
 *   2. localStorage.remote     —— 配置了远程 service 但当前未登录
 *   3. 空串                   —— dev 环境走 vite 代理的相对路径
 */
export function apiBase(): string {
  const configured = axios.defaults.baseURL
  if (configured) return String(configured).replace(/\/+$/, '')
  const remote = localStorage.getItem('remote')
  if (remote) return `https://${remote}`
  return ''
}

/** 拼出完整 URL */
export function apiUrl(path: string): string {
  const suffix = path.startsWith('/') ? path : `/${path}`
  return `${apiBase()}${suffix}`
}

/**
 * 带鉴权的 axios 实例。
 * 缺 token 时返回 null —— 调用方应直接返回「请先登录」，不要发请求。
 */
export function authedClient(): typeof axios | null {
  if (!applyAuthHeader(axios)) return null
  return axios
}

/** 提取服务端错误消息（统一各处错误文案） */
export function errorMessage(err: any, fallback: string): string {
  return err?.response?.data?.message || err?.message || fallback
}

/** 是否已登录（本地有 token） */
export function hasToken(): boolean {
  return !!localStorage.getItem('token')
}
