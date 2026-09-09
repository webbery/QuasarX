/**
 * Token 辅助函数（方案 B：显式读取）
 *
 * 每个 Tool 在调 axios 前显式从 localStorage 读取 token
 * 并设置到 axios.defaults.headers.common['Authorization']，
 * 不依赖全局 LoginForm 登录流程设置的默认值。
 */

const TOKEN_KEY = "token"

export function getAuthToken(): string | null {
  if (typeof localStorage === "undefined") return null
  return localStorage.getItem(TOKEN_KEY)
}

/**
 * 给一个 axios 实例（或 axios.default）显式设置 Authorization header。
 * 缺失 token 时返回 false（Tool 应返回"请先登录"错误）。
 */
export function applyAuthHeader(axiosInstance: any): boolean {
  const token = getAuthToken()
  if (!token) return false
  if (axiosInstance?.defaults?.headers?.common) {
    axiosInstance.defaults.headers.common["Authorization"] = token
  }
  return true
}
