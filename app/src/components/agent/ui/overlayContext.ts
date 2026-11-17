/**
 * 放大浮层宿主
 *
 * UiFrame 最大化时把自己的内容 Teleport 到 AgentPanel 提供的宿主里，
 * 这样能盖住整个 Agent 面板，但不影响主界面其它区域。
 */

import type { InjectionKey, Ref } from 'vue'

export interface UiOverlayContext {
  /** 浮层宿主元素；未就绪时视图退化为面板内放大 */
  host: Ref<HTMLElement | null>
}

export const UI_OVERLAY_KEY: InjectionKey<UiOverlayContext> = Symbol('quasarx-ui-overlay')
