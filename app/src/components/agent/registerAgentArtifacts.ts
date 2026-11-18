/**
 * 产物渲染器注册
 *
 * 新增一种产物只需在这里加一行 registerArtifact，不用改 AgentArtifactCard。
 */

import { registerArtifact, listArtifactKinds } from '@/lib/agent/artifactRegistry'
import AgentPatchArtifact from './AgentPatchArtifact.vue'
import AgentActionArtifact from './AgentActionArtifact.vue'
import UiFrame from './ui/UiFrame.vue'

let registered = false

export function registerAgentArtifacts(): void {
  if (registered) return
  registered = true

  // 通用 UI 产物：自带标题 / 最大化 / 参数控件条，不再套通用卡片外壳
  registerArtifact({
    kind: 'ui',
    label: '可视化',
    icon: 'chart-line',
    render: UiFrame,
    compact: true,
    exportable: true,
  })

  registerArtifact({
    kind: 'patch',
    label: '策略修改提案',
    icon: 'code-branch',
    render: AgentPatchArtifact,
  })

  registerArtifact({
    kind: 'action',
    label: '操作',
    icon: 'bolt',
    render: AgentActionArtifact,
  })

  console.info('[AgentArtifacts] 已注册产物类型:', listArtifactKinds().join(', '))
}
