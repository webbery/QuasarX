import { computed } from 'vue'

/** 登录状态：各面板共用 */
export function useIsLoggedIn() {
    return computed(() => {
        const token = localStorage.getItem('token')
        return !!(token && token.length > 0)
    })
}
